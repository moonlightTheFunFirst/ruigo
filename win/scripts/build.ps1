param([switch]$Run, [switch]$Deploy, [switch]$Rebuild)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$qtBin = if ($env:QT_BIN) { $env:QT_BIN } else { 'F:\Qt\6.8.3\mingw_64\bin' }
$mingwBin = if ($env:MINGW_BIN) { $env:MINGW_BIN } else { 'F:\Qt\Tools\mingw1310_64\bin' }
foreach ($tool in @("$qtBin/qmake.exe", "$mingwBin/g++.exe")) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Tool not found: $tool. Set QT_BIN / MINGW_BIN." }
}
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
Invoke-Checked cmake @('-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$qtBin/..", "-DCMAKE_CXX_COMPILER=$mingwBin/g++.exe")
$buildArgs = @('--build', $buildDir, '--parallel')
if ($Rebuild) { $buildArgs += '--clean-first' }
Invoke-Checked cmake $buildArgs
Invoke-Checked ctest @('--test-dir', $buildDir, '--output-on-failure')
if ($Deploy) {
    if (!(Test-Path "$projectRoot/data/dictionary.sqlite")) { throw 'Generate data/dictionary.sqlite before deployment.' }
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    Copy-Item -LiteralPath "$buildDir/ruigo.exe" -Destination $outputDir
    Copy-Item -LiteralPath "$projectRoot/data" -Destination $outputDir -Recurse -Force
    Copy-Item -LiteralPath "$projectRoot/licenses" -Destination $outputDir -Recurse -Force
    Copy-Item -LiteralPath "$projectRoot/THIRD_PARTY_NOTICES.md" -Destination $outputDir -Force
    Invoke-Checked "$qtBin/windeployqt.exe" @('--qtpaths', "$qtBin/qtpaths.exe", '--release', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw', "$outputDir/ruigo.exe")
    if (!(Test-Path "$outputDir/sqldrivers/qsqlite.dll")) { throw 'QSQLITE plugin missing from deployment.' }
}
if ($Run) { Start-Process -FilePath "$buildDir/ruigo.exe" -WorkingDirectory $buildDir -WindowStyle Hidden }
