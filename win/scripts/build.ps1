param([switch]$Run, [switch]$Deploy, [switch]$Rebuild)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Find-Qmake([string]$Bin) {
    foreach ($name in @('qmake6.exe', 'qmake.exe')) {
        $path = Join-Path $Bin $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
}
function Test-Qt6MinGW([string]$Qmake) {
    $version = & $Qmake -query QT_VERSION
    if ($LASTEXITCODE -ne 0 -or $version -notmatch '^6\.') { return $false }
    $spec = & $Qmake -query QMAKE_XSPEC
    return ($LASTEXITCODE -eq 0 -and $spec -eq 'win32-g++')
}
$qmake = $null
if ($env:QT_BIN) {
    $qmake = Find-Qmake $env:QT_BIN
    if (!$qmake -or !(Test-Qt6MinGW $qmake)) {
        throw "QT_BIN must contain qmake for Qt 6 MinGW: $env:QT_BIN"
    }
} else {
    $candidates = @(
        $roots = @("$env:USERPROFILE/Qt") + @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { Join-Path $_.Root 'Qt' })
        $versions = foreach ($root in ($roots | Select-Object -Unique)) {
            Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^6\.\d+\.\d+$' }
        }
        $versions | Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Find-Qmake (Join-Path $_.FullName 'mingw_64/bin') }
        foreach ($name in @('qmake6.exe', 'qmake.exe')) {
            Get-Command $name -CommandType Application -All -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
        }
    )
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Qt6MinGW $candidate) { $qmake = $candidate; break }
    }
    if (!$qmake) { throw 'Qt 6 MinGW not found. Add its bin directory to PATH or set QT_BIN.' }
}
$qtBin = Split-Path -Parent $qmake
$qtPrefix = & $qmake -query QT_INSTALL_PREFIX
if ($LASTEXITCODE -ne 0) { throw 'Cannot query QT_INSTALL_PREFIX.' }
$mingwBin = $env:MINGW_BIN
if (!$mingwBin) {
    # MSYS2 installs Qt and its matching compiler in the same prefix.
    if (Test-Path -LiteralPath "$qtBin/g++.exe") { $mingwBin = $qtBin }
    else {
        $qtRoot = Split-Path -Parent (Split-Path -Parent $qtPrefix)
        $compilers = @(Get-ChildItem -Path "$qtRoot/Tools/mingw*_64/bin/g++.exe" -ErrorAction SilentlyContinue)
        if ($compilers.Count -eq 1) { $mingwBin = $compilers[0].DirectoryName }
        else { throw 'Cannot uniquely select the MinGW compiler matching Qt. Set MINGW_BIN to its bin directory.' }
    }
}
if (!(Test-Path -LiteralPath "$mingwBin/g++.exe" -PathType Leaf)) {
    throw "MinGW compiler not found: $mingwBin/g++.exe. Correct MINGW_BIN."
}
Write-Host "Qt: $(& $qmake -query QT_VERSION) ($qtBin)"
Write-Host "MinGW: $(& "$mingwBin/g++.exe" -dumpfullversion) ($mingwBin)"
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
$buildDir = Join-Path $projectRoot 'win/build/mingw-release'
$outputDir = Join-Path $projectRoot 'win/output'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        if ($Program -eq 'ctest' -and (Test-Path "$buildDir/test-results.txt")) { Get-Content "$buildDir/test-results.txt" }
        throw "$Program failed ($LASTEXITCODE)"
    }
}
function Assert-FileWritable([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        try {
            $stream = [IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
            $stream.Dispose()
        } catch {
            throw "File is in use or not writable: $Path. Close Ruigo before building/deploying, then retry. This is not a dictionary DB error."
        }
    }
}
function Remove-GeneratedDirectory([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $allowed = @([IO.Path]::GetFullPath("$projectRoot/win/build/mingw-release"),
                 [IO.Path]::GetFullPath("$projectRoot/win/output"))
    if ($fullPath -notin $allowed) { throw "Refusing to remove unexpected directory: $fullPath" }
    if (Test-Path -LiteralPath $fullPath) {
        if ((Get-Item -LiteralPath $fullPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to remove linked directory: $fullPath"
        }
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}
function Test-DeployedApplication {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = "$outputDir/ruigo.exe"
    $startInfo.WorkingDirectory = $outputDir
    $startInfo.UseShellExecute = $false
    $startInfo.WindowStyle = 'Hidden'
    $startInfo.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $startInfo.EnvironmentVariables.Remove('QT_PLUGIN_PATH')
    $startInfo.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH')
    $startInfo.Arguments = '--self-check'
    $check = [Diagnostics.Process]::Start($startInfo)
    try {
        if (!$check.WaitForExit(15000)) { throw 'Deployed dictionary self-check timed out.' }
        if ($check.ExitCode -ne 0) { throw "Deployed dictionary self-check failed ($($check.ExitCode)). Check DB, DLLs and plugins." }
    } finally {
        if (!$check.HasExited) { $check.Kill(); $check.WaitForExit() }
        $check.Dispose()
    }
    $startInfo.Arguments = ''
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        if ($process.WaitForExit(5000)) {
            $exitHex = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$process.ExitCode), 0).ToString('X8')
            throw "Deployed Ruigo exited during startup: 0x$exitHex. Check deployed DLLs/plugins."
        }
        Write-Host 'Deployment startup check passed (Windows-only PATH).'
    } finally {
        # Only stop the process created by this check, never existing Ruigo instances.
        if (!$process.HasExited) {
            $null = $process.CloseMainWindow()
            if (!$process.WaitForExit(2000)) { $process.Kill(); $process.WaitForExit() }
        }
        $process.Dispose()
    }
}
Assert-FileWritable "$buildDir/ruigo.exe"
if ($Deploy) {
    Assert-FileWritable "$outputDir/ruigo.exe"
    $python = "$projectRoot/.venv/Scripts/python.exe"
    if (!(Test-Path -LiteralPath $python)) {
        $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
        if ($launcher) { Invoke-Checked $launcher.Source @('-3', '-m', 'venv', "$projectRoot/.venv") }
        else {
            $launcher = Get-Command python.exe -ErrorAction SilentlyContinue
            if (!$launcher) { throw 'Install Python 3.10 or newer to generate the dictionary DB.' }
            Invoke-Checked $launcher.Source @('-m', 'venv', "$projectRoot/.venv")
        }
    }
    Invoke-Checked $python @('-c', 'import sys; assert sys.version_info >= (3, 10)')
    & $python -c 'import wn'
    if ($LASTEXITCODE -ne 0) {
        Invoke-Checked $python @('-m', 'pip', 'install', '-r', "$projectRoot/tools/requirements.txt")
    }
    if (!(Test-Path -LiteralPath "$projectRoot/data/dictionary.sqlite")) {
        if (!(Test-Path -LiteralPath "$projectRoot/wnja-2.1/wnja/wnja-2.1.xml")) {
            throw 'Japanese WordNet source missing: wnja-2.1/wnja/wnja-2.1.xml. See README.md.'
        }
        Write-Host 'Generating dictionary DB (first setup requires network access)...'
        Invoke-Checked $python @('-m', 'pip', 'install', '-r', "$projectRoot/tools/requirements.txt")
        Invoke-Checked $python @("$projectRoot/tools/build_dictionary.py", '--download-dependencies')
    }
    & $python "$projectRoot/tools/build_dictionary.py" --verify-only
    if ($LASTEXITCODE -ne 0) {
        throw '辞書DBが古いか不正なので再生成が必要です。README.md の「辞書の生成」を参照してください。既存DBは置き換えていません。'
    }
}
if ($Rebuild) { Remove-GeneratedDirectory $buildDir }
Invoke-Checked cmake @('-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCMAKE_CXX_COMPILER=$mingwBin/g++.exe")
$buildArgs = @('--build', $buildDir, '--parallel')
if ($Rebuild) { $buildArgs += '--clean-first' }
Invoke-Checked cmake $buildArgs
Invoke-Checked ctest @('--test-dir', $buildDir, '--output-on-failure')
if ($Deploy) {
    if (!(Test-Path "$projectRoot/data/dictionary.sqlite")) { throw 'Generate data/dictionary.sqlite before deployment.' }
    Assert-FileWritable "$outputDir/ruigo.exe"
    Remove-GeneratedDirectory $outputDir
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    Copy-Item -LiteralPath "$buildDir/ruigo.exe" -Destination $outputDir
    Copy-Item -LiteralPath "$projectRoot/data" -Destination $outputDir -Recurse -Force
    Copy-Item -LiteralPath "$projectRoot/licenses" -Destination $outputDir -Recurse -Force
    Copy-Item -LiteralPath "$projectRoot/THIRD_PARTY_NOTICES.md" -Destination $outputDir -Force
    Invoke-Checked "$qtBin/windeployqt.exe" @('--qtpaths', "$qtBin/qtpaths.exe", '--release', '--compiler-runtime', '--exclude-plugins', 'qsqlmysql,qsqlpsql,qsqlodbc,qsqlibase,qsqlmimer', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw', "$outputDir/ruigo.exe")
    if (!(Test-Path "$outputDir/sqldrivers/qsqlite.dll")) { throw 'QSQLITE plugin missing from deployment.' }
    Test-DeployedApplication
}
if ($Run) { Start-Process -FilePath "$buildDir/ruigo.exe" -WorkingDirectory $buildDir -WindowStyle Hidden }
