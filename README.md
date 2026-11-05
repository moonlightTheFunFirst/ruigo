# Ruigo — オフライン日本語類語辞典

C++17 / Qt 6 Widgets / SQLite。検索は完全一致で、意味ごとに類語候補を表示します。
WordNetの同一意味に属する語でも、文中でそのまま置換できるとは限りません。

## 構成

splitFilerの配置に合わせ、共通コードとOS別のビルド・配布物を分離しています。
CMakeのソース一覧はルートで一元管理します。

```text
CMakeLists.txt          共通ビルド定義
src/                    Windows / macOS共通のQtコード
tools/                  辞書変換（開発時だけPythonを使用）
tests/                  Qt Testによる検索・履歴・画面操作テスト
data/dictionary.sqlite  生成した実辞書（Git管理対象外）
licenses/               同梱ライセンス
win/scripts/            Windowsビルド・起動・配布
win/build/              Windows生成物（Git管理対象外）
win/output/             Windows配布フォルダ（Git管理対象外）
win/vs2022/             任意のVisual Studio用CMake入口
mac/scripts/            macOSビルド・起動・配布
mac/build/              macOS生成物（Git管理対象外）
mac/output/             macOS配布用.app（Git管理対象外）
```

## 辞書の生成

必要環境: Python 3.10以上。アプリを使うだけならPythonは不要です。
提供された日本語WordNet 2.1を使用します。
公式配布: https://bond-lab.github.io/wnja/jpn/downloads.html

Windows（リポジトリのルートで実行）:

```powershell
C:\Python10\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/requirements.txt
.\.venv\Scripts\python.exe tools/build_dictionary.py --download-dependencies
.\.venv\Scripts\python.exe tools/build_dictionary.py --verify-only
```

macOS:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements.txt
.venv/bin/python tools/build_dictionary.py --download-dependencies
.venv/bin/python tools/build_dictionary.py --verify-only
```

初回のみパッケージと`omw-en:2.0`をネットワーク経由で取得します。
`wn`の作業領域はプロジェクト内の`.wn/`です。準備後は
`--download-dependencies`を外してオフラインで再生成できます。
辞書XMLの既定位置は`wnja-2.1/wnja/wnja-2.1.xml`。
別の場所なら`--source PATH`、出力先は`--output PATH`で指定します。

`wn.lmf.load`で元データを解析し、見出しID・語義ID・品詞・読みを保持して
専用SQLiteへ変換します。読みは検索対象にしません。今回のXMLは自身のsynsetを
収録していますが、配布元の手順に従い`omw-en:2.0`を生成環境に用意します。
英語の見出しと定義はアプリDBに出力しません。

変換後はSQLite整合性・外部キー・件数を検証し、成功時だけ出力DBを置き換えます。
期待値: 見出し90,706 / synset 67,509 / 語義148,703 / 日本語定義67,064。
変換に失敗した場合、既存の完成DBは保持します。アプリ使用中のDBの置き換えは避けてください。

## Windowsビルド・起動

Qt 6のMinGW版（Widgets / Sql / Test、QSQLITEプラグイン）、対応するMinGW、
CMake 3.21以上、Ninjaが必要です。既定のQtは`F:\Qt\6.8.3\mingw_64\bin`、
MinGWは`F:\Qt\Tools\mingw1310_64\bin`です。

```powershell
# 必要なときだけ上書き
$env:QT_BIN = 'F:\Qt\6.8.3\mingw_64\bin'
$env:MINGW_BIN = 'F:\Qt\Tools\mingw1310_64\bin'
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -Run
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -Deploy
```

splitFiler同様の`build_and_run.bat`、`rebuild_and_run.bat`、
`rebuild_and_deploy.bat`も使用できます。ビルド後にテストを実施します。
失敗時の詳細は`win/build/mingw-release/test-results.txt`。
配布は`win/output/`全体をコピーしてください。Qt DLL、QSQLITE、辞書、ライセンスを含みます。
実行中のアプリを強制終了しないため、再ビルド時は自分でアプリを閉じてください。

Visual StudioではMSVC版Qtを指定し、MinGW版と混ぜないでください:

```powershell
cmake -S win/vs2022 -B win/build/vs2022 -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=F:/Qt/6.8.3/msvc2022_64
cmake --build win/build/vs2022 --config Release
```

MSVCビルドとmacOSビルドはこのWindows/MinGW環境では未検証です。

## macOSビルド・起動

Xcode Command Line Tools、CMake、対象CPUに対応するQt 6 macOS版が必要です。
Windowsで生成した`data/dictionary.sqlite`はそのまま使用できます。

```sh
export QT_PREFIX="$HOME/Qt/6.8.3/macos"
sh mac/scripts/build.sh
sh mac/scripts/build.sh --run
sh mac/scripts/build.sh --deploy
```

辞書・ライセンスは`.app/Contents/Resources/`へ配置し、`macdeployqt`で
Qtフレームワークとプラグインを同梱します。公開配布用の署名・公証は未実施です。
ソースは共通で、OS別コードはバンドル内のデータ位置の判定だけです。

## 使い方と保存

「設定 → 表示設定」からフォントとサイズ（8〜32pt）を選べます。
日本語と選択行のプレビューを確認し、OKで画面全体へ適用、キャンセルで変更を破棄します。
設定は再起動後も保持します。「標準に戻す」→OKでOS標準フォントへ戻ります。
既定サイズはOS標準サイズと11ptの大きい方です。指定フォントが利用できない場合も標準へ戻ります。
文字と選択背景はOSの明暗に合わせた控えめな色とし、候補行には上下の余白を設けています。

- 左の検索欄でEnter。前後の空白を除き、空入力は無視します。
- 入力履歴は検索欄でEnterを押した語だけを最大100件保持します。新しい語は先頭に追加し、登録済みの語は位置を変えません。
- 入力履歴の選択・クリック・Enterでは再検索だけを行い、履歴の順序を保持します。結果の「この単語で検索」も履歴には追加しません。
- 入力履歴はダブルクリック・右クリックの「編集」・F2で該当行を直接編集できます。単語は全選択で表示され、Enterで確定、Escまたは別の場所へのフォーカス移動で取り消します。右クリックの「削除」またはDeleteで1件ずつ確認なしで削除できます。
- 編集は元の位置で保存し、Enterで確定すると変更後の単語ですぐに再検索します。前後の空白は除去し、空文字・別項目との重複は理由を表示して保存しません。編集のキャンセルや削除では自動検索しません。
- 既存の検索履歴は現在の順序のまま入力履歴として引き継ぎます。編集・削除も再起動後に保持されます。
- 候補をダブルクリック、または右クリックの「コピー」で単語だけをコピーします。
- 右クリックの「この単語で検索」で再検索します。
- 件数は意味グループ内の重複除去後の表示行数。同じ語が別の意味にあれば別々に数えます。
- 日本語定義がない意味は「説明なし」。検索語自身のみの意味は結果に含めません。
- 履歴・ウィンドウ位置とサイズ・左右幅はQSettingsに保存します。
  Windowsは`HKEY_CURRENT_USER\Software\Keishin\Ruigo`、macOSはQt標準のPreferencesを使用します。

DBは実行ファイル横の`data/dictionary.sqlite`（macOSではResources内）を
読み取り専用で開きます。別のDBを使う場合は`--dictionary PATH`を指定できます。
DB欠落・不正・非対応スキーマは画面にエラーを表示し、空のDBを自動生成しません。

## 設計と検証

`MainWindow`はSQLを持たず、`DictionaryRepository::search(word, language)`を使います。
見出しと定義は言語別、synset IDは辞書IDと版を含む名前空間付き。
履歴も言語別の文字列リストなので、後の英語追加で画面とDBを作り直す必要はありません。
現時点では日本語完全一致のみで、あいまい検索・読み検索・英語UIはありません。

Qt Testでは意味分離、言語分離、重複、自分自身の除外、SQL文字列入力、長文、
DB欠落・破損、履歴100件と永続化、Enter検索、コピー、0件、左右幅の復元を検証します。
実DBがある場合は実辞書検索101回の中央値をログに記録し、画面を`preview.png`に保存します。
テスト用履歴・設定は一時フォルダへ隔離します。

配布元のライセンスと出典は`THIRD_PARTY_NOTICES.md`および`licenses/`を参照してください。
