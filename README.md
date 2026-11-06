# Ruigo — オフライン日英類語辞典

C++17 / Qt 6 Widgets / SQLite。見出しと収録済みの別表記を完全一致で検索し、意味ごとに類語候補を表示します。
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
提供された日本語WordNet 2.1とOpen English Wordnet 2025通常版を使用します。
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

初回のみパッケージ、生成依存の`omw-en:2.0`、OEWN 2025の公式XMLをネットワーク経由で取得します。
英語の公式配布: https://en-word.net/downloads
`wn`の作業領域はプロジェクト内の`.wn/`です。準備後は
`--download-dependencies`を外してオフラインで再生成できます。
辞書XMLの既定位置は`wnja-2.1/wnja/wnja-2.1.xml`。
別の場所なら`--source PATH`、英語XMLは`--english-source PATH`（既定`.wn/english-wordnet-2025.xml`）、出力先は`--output PATH`で指定します。

`wn.lmf.load`で元データを解析し、見出しID・語義ID・品詞・読みを保持して
専用SQLiteへ変換します。日本語のscript指定なしのformsを別表記、hira/kanaのformsを読み、英語のformsを不規則活用形として索引へ収録します。今回のXMLは自身のsynsetを
収録していますが、配布元の手順に従い`omw-en:2.0`を生成環境に用意します。
英語はOEWN 2025から同じDBへ収録し、日英の意味ID・定義・候補は分離します。

変換後はSQLite整合性・外部キー・件数を検証し、成功時だけ出力DBを置き換えます。
期待値: 日本語は見出し90,706 / synset 67,509 / 語義148,703 / 定義67,064、
英語は見出し135,969 / synset 107,519 / 語義185,129 / 定義107,519。
生成DBは約162 MiB（スキーマ5）。旧版DBは自動変換せず再生成を案内します。
既存DBがある場合も、英語対応・別表記検索への更新時には上記の生成コマンドを実行してください。
生成前に両XMLのSHA-256を検証します。`--source` / `--english-source`指定にも同じ検証を適用します。
公式配布ページではSHA-256公開値を確認できなかったため、次の値はこの環境の同梱日本語XML・取得済み英語XMLを基準に固定しています（公式値との照合済みではありません）。

- wnja 2.1: `0e08760cd3fd82ac583b846d210a7db25282378af28e8d6c1612aaac51139919`
- OEWN 2025: `6f49adeec174ab3092169fb25cf4a925226b63975a5d29a691a5dff88f0673b2`

変換に失敗した場合、既存の完成DBは保持します。アプリ使用中のDBの置き換えは避けてください。

## Windowsビルド・起動

Windowsの実行ファイル・ウィンドウアイコンは`assets/ruigo.ico`を埋め込みます。
採用した元画像は`assets/ruigo.png`です。ICOを再生成する場合は
`powershell -ExecutionPolicy Bypass -File tools/build_icon.ps1`を実行します。
16～256pxの9サイズを収録し、通常のビルドには画像変換ツールは不要です。

推奨環境はQt公式インストーラーのQt 6 MinGW 64-bit版です。
Qt（Widgets / Sql / Test、QSQLITEプラグイン）、対応するMinGW、
CMake 3.21以上、Ninjaが必要です（CMake / CTest / NinjaはPATHに配置）。
Qtは`QT_BIN`の指定を優先し、未指定ならユーザーフォルダおよび各ドライブ直下の
`Qt/6.x.x/mingw_64/bin`から新しいバージョン順に検出します。
PATH上の`qmake6.exe` / `qmake.exe`は最後の候補です。
MinGWは`MINGW_BIN`を優先し、未指定ならQtと同じbin（MSYS2）、
またはそのQtインストール先の`Tools/mingw*_64/bin`から検出します。
Tools内に複数のMinGWがある場合は、Qtに対応するものを`MINGW_BIN`で指定してください。
検出したQtのバージョンと両方のパスは実行時に表示します。
MSYS2のQtはビルド・テストには使えますが、windeployqtだけでは依存DLLが揃わないため、
このスクリプトによる配布には使えません。配布には公式版を選択してください。

```powershell
# 必要なときだけ上書き（実際のインストール先に合わせる）
$env:QT_BIN = 'D:\Qt\6.8.2\mingw_64\bin'
$env:MINGW_BIN = 'D:\Qt\Tools\mingw1310_64\bin'
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -Run
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -Deploy
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -Rebuild -Deploy
powershell -ExecutionPolicy Bypass -File win/scripts/build.ps1 -RebuildDictionary -Rebuild -Deploy
```

splitFiler同様の`build_and_run.bat`、`rebuild_and_run.bat`、
`rebuild_and_deploy.bat`も使用できます。ビルド後にテストを実施します。
辞書DBの形式が変わったときや辞書を作り直したいときは`full_rebuild_and_deploy.bat`
（`-RebuildDictionary -Rebuild -Deploy`）を使います。既存の`data/dictionary.sqlite`があっても
辞書DBを再生成・検証してから、ビルドフォルダを作り直してビルド・テストし、`win/output/`へ配布します。
新しいDBは生成と検証に成功したときだけ既存DBと置き換わるので、途中で失敗しても元のDBは残ります。
依存辞書とOEWNのXMLが取得済みならネット接続は不要です。実行前にRuigoを閉じてください（DBが開かれていると置き換えできません）。
失敗時の詳細は`win/build/mingw-release/test-results.txt`。
QtやMinGWを切り替えたときは、古いQtのパスがCMakeキャッシュに残るため、
`-Rebuild`を付けるか`win/build/mingw-release`を削除してください。
`-Rebuild`はこのビルドフォルダをキャッシュごと作り直します。
配布は`win/output/`全体をコピーしてください。Qt DLL、QSQLITE、辞書、ライセンスを含みます。
`-Deploy`は古いDLLが残らないよう`win/output/`を作り直します。手作業のファイルは置かないでください。
SQLドライバは不要なものを除外し、MinGWランタイムをwindeployqtで同梱します。
配布後はPATHをWindowsのSystem32とWindowsフォルダだけにした子プロセスで5秒間の
起動確認を行います。その前に隠しオプション`--self-check`を実行してDBのオープンと日英検索のSQL実行を確認し、非0終了・タイムアウトなら停止します。通常起動のエラー画面は成功扱いにしません。起動確認の途中終了はエラーとし、確認後は自分で起動したプロセスだけを終了します。
実行中のアプリを強制終了しないため、再ビルド時は自分でアプリを閉じてください。
ビルド・配布先の実行ファイルが使用中なら、ビルド開始前に終了を促すエラーを表示します。
`-Deploy`（`rebuild_and_deploy.bat`を含む）は辞書DBがない場合、CMake実行前に
`.venv`を作成し、必要パッケージと依存辞書を取得してDBを生成・検証します。
初回はPython 3.10以上（`py`または`python`）とネット接続、
`wnja-2.1/wnja/wnja-2.1.xml`が必要です。既存DBがある場合は再生成しません。配布のたびに`tools/build_dictionary.py --verify-only`で検証し、古い形式や不正なDBなら配布前に停止します。READMEの「辞書の生成」の手順で明示的に再生成してください。
検証にもPython環境が必要です。`.venv`がなければ生成時と同じ手順で作成し、`wn`がなければ依存パッケージをインストールします（初回はネット接続が必要）。

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

- 日本語WordNetに別表記として収録されている語も検索できます。例：「悲しい」から「哀しい」に属する意味・類語を検索します。
  見出しの完全一致を優先し、未一致の場合だけ別表記を確認します。完全一致、全角英字を半角化した照合の各段階でこの優先順を使います。
  別表記に紐づいた見出しIDだけを対象とし、同じ表記の無関係な別見出しの意味は混ぜません。候補は辞書の見出し表記、履歴は入力した別表記のまま保存します。
- 日本語は読み（ひらがな・カタカナ）でも検索できます。照合順は見出し→別表記→読みです。
  読みが1語の見出しだけに対応する場合はそのまま検索し、「「かなしい」（「哀しい」の読み）」と示します。
  同じ読みの見出しが複数ある場合（例:「きかい」→奇怪・機械・機会）は意味を混ぜず、各見出しと最初の定義を一覧に出して選んでもらいます。
  辞書に読みが収録されていない語（例: 見る）や見出しにない語（例: 速い）は読みでは見つかりません。
- 英語は活用形からも元の見出しを検索できます。辞書に収録された不規則活用（went→go、children→child、happier→happy）を使い、
  なければWordNet標準の規則（-s、-es、-ies、-ed、-ing、-er、-est など）で元の形を求めます。規則で得た形は、
  同じ品詞の見出しとして辞書にある場合だけ採用します（例: cats→cat、walked→walk、studies→study）。「「went」（「go」の活用形）」と示します。
  複数の語の活用形になる場合（例: leaves→leaf/leave）は一覧から選びます。
- 入力自体が見出しで、ほかの見出しの読み・活用形でもある場合（例: saw→see）は見出しを優先し、「「see」の活用形としても検索」ボタンを表示します。
- 候補一覧は検索欄で↓を押すと選択でき、Enterでその見出しを検索します。一覧から選んだ見出しは履歴に追加します。
- 左の検索欄でEnter。前後の空白を除き、空入力は無視します。
- 日本語文字があれば日本語、ラテン文字だけなら英語を優先します。数字・記号だけは日本語優先です。優先言語に見出しがない場合だけ、もう一方を検索します。候補0件でも登録見出しがあれば言語は変えません。
- 両言語に見出しがあるときは、結果付近の「別言語で検索」ボタンで選べます。全角英字は日英とも完全一致で見つからないときだけ半角英字にして確認します（かな・漢字は変換しません）。
- `AFGHAN`など複数の綴りに一致したときは、一覧からクリックまたはEnterで綴りを選びます。選んだ語と言語は別言語ボタンと同じく履歴に登録します。
- 未収録語は「見出しが見つかりませんでした」、登録済みで候補0件なら「見出しはありますが、同じ意味の類語は収録されていません」と表示します。
- 英語は完全一致優先。なければ全角英字を半角化・NFC正規化し、さらに大小文字を無視した照合をします。複数の綴りに一致した場合は綴りの選択肢を表示し、`US`と`us`などを混ぜません。語中の空白・記号は変換しません。和訳はありません。
- 入力履歴は検索欄のEnterと別言語ボタンで検索した語・言語の組を日英合計100件保持します。新しい語は先頭に追加し、登録済みの語は位置を変えません。
- 入力履歴の選択・クリック・Enterでは再検索だけを行い、保存済み言語と履歴の順序を保持します。結果の「この単語で検索」も履歴には追加しません。
- 入力履歴はダブルクリック・右クリックの「編集」・F2で該当行を直接編集できます。単語は全選択で表示され、Enterで確定、Escまたは別の場所へのフォーカス移動で取り消します。右クリックの「削除」またはDeleteで1件ずつ確認なしで削除できます。
- 編集は元の位置で保存し、Enterで確定すると変更後の単語ですぐに再検索します。語が変わった場合だけ言語を再判定し、同じ語なら元の言語を保ちます。判定は類語検索から独立しており、DBを開けないときも文字種から言語を決めて編集を保存できます。前後の空白は除去し、空文字・別項目との重複は理由を表示して保存しません。編集のキャンセルや削除では自動検索しません。
- 履歴は通常、語だけを表示します（言語は語から明らかなため）。同じ表記が日英両方の履歴にある場合（例: 「別言語で検索」を使った`AI`）だけ、英語の項目に`[英語]`を付けて区別します。ラベルは編集・検索・コピーの文字列に含めません。
- 履歴は`history/entries`に`ja:語` / `en:word`で保存します。旧日本語履歴の後ろへ旧英語履歴を連結して一度だけ移行します。旧キーは残しますが、新形式が空でも再取り込みしません。編集・削除も再起動後に保持されます。
- 候補をダブルクリック、Enter、Ctrl+C、または右クリックの「コピー」で単語だけをコピーします。
- 検索欄で↓を押すと最初の候補へ移り、矢印キーで選んでEnterでコピーできます。最初の候補で↑を押すと検索欄へ戻ります（入力は全選択）。
- 見出しは辞書の「似ている意味」（similar）関係で結ばれた意味を「関連（似た意味）」として、同じ意味の類語の後に別枠で表示します。類語の件数には含めず、見出しに「関連語：N件」と表示します。例えば英語の`beautiful`は同じ意味の類語がありませんが、`pretty`、`gorgeous`などを関連語として表示します。日本語WordNetの同じ関係も同様に使います。
  結果欄右上の「似た意味も表示」チェックボックスで関連語の表示を切り替えられます（既定はオン、設定は再起動後も保持）。切り替えは表示中の結果をそのまま描き直し、再検索や履歴の追加はしません。オフのときは見出しに「似た意味の関連語N件は非表示」と件数だけを示します。
- 別表記や大文字・小文字、全角英字の変換で見出しに一致した場合は、「「悲しい」（「哀しい」の別表記）」「「Beautiful」（「beautiful」として検索）」のように、実際に検索した見出しを結果の見出しに示します。
- 候補にマウスを重ねると、読み・現在の意味・ほかに収録されている意味を表示します。
  詳細はホバー時にその項目だけ取得し、検索時には作成しません。
  英語では読みの行を表示しません。日本語の読みはひらがなを優先し、なければカタカナを表示します。読みがない場合は「未収録」、定義がない場合は「説明なし」です。
  ほかの意味がない場合は「なし」、定義のない別の意味が複数ある場合は意味ごとに番号付きで表示します。
  ほかの意味は同じ見出し語（同じ品詞の見出し）に属する別の意味だけで、同じ表記でも品詞の違う見出しや現在の意味に属さない別見出しは混ぜません。
  かなだけの表記と読みが同じ場合は読みの行を省略します。ポップアップにも表示設定のフォントを使います。
  意味はWordNetの意味グループ共通の定義であり、単語固有の使い分けを生成するものではありません。
- 右クリックの「この単語で検索」で再検索します。
- 件数は意味グループ内の重複除去後の表示行数。同じ語が別の意味にあれば別々に数えます。
- 検索対象言語の定義がない意味は「説明なし」。検索語自身のみの意味は結果に含めません。
- 履歴・ウィンドウ位置とサイズ・左右幅はQSettingsに保存します。
  Windowsは`HKEY_CURRENT_USER\Software\Keishin\Ruigo`、macOSはQt標準のPreferencesを使用します。

DBは実行ファイル横の`data/dictionary.sqlite`（macOSではResources内）を
読み取り専用で開きます。別のDBを使う場合は`--dictionary PATH`を指定できます。
DB欠落・不正・非対応スキーマは画面にエラーを表示し、空のDBを自動生成しません。

## 設計と検証

`MainWindow`はSQLを持たず、`DictionaryRepository::search(word, language)`を使います。
見出しと定義は言語別、synset IDは辞書IDと版を含む名前空間付き。
言語引数を省略するとリポジトリが検索先を自動判定します。履歴は語・言語の統合リストです。
画面操作は日本語のまま、英語候補・英語定義も表示します。あいまい検索・前方一致検索はありません。

Qt Testでは意味分離、言語分離、重複、自分自身の除外、SQL文字列入力、長文、
DB欠落・破損、履歴100件と永続化、Enter検索、コピー、0件、左右幅の復元を検証します。
実DBがある場合は実辞書検索101回の中央値をログに記録し、画面を`preview.png`に保存します。
テスト用履歴・設定は一時フォルダへ隔離します。

配布元のライセンスと出典は`THIRD_PARTY_NOTICES.md`および`licenses/`を参照してください。

別表記・読み・活用形の索引化ルールのPythonテスト:

```powershell
.venv/Scripts/python.exe -m unittest discover -s tests -p test_dictionary_aliases.py
```
## Kaikki の補足表示とDBの生成

日本語版 Wiktionary の Kaikki 抽出データを
独立した `data/kaikki.sqlite` に変換できます。Python 標準ライブラリだけを使います。
集計で比較するため、先に上記の手順で WordNet の `data/dictionary.sqlite` を用意してください。

採用した入力は [Kaikki の日本語版生データ](https://kaikki.org/jawiktionary/rawdata.html) の
2026-11-01 ダンプ（2026-11-02 抽出、公開ページ記載のコミット `19f9a30` / `6f96ddf`）です。
通常の変換は、展開後の JSONL の SHA-256
`491dbaf5fbf7c34cf4d033cdac59776627287eb5072799e7581ecef495717edd`
を照合します。配布URLのファイルは更新されるため、取得した入力を日付付きで保存してください。

```powershell
python tools/import_kaikki.py .wn/kaikki-ja-2026-11-01.jsonl.gz
```

通常の変換では、SQLite の整合性検査後に出力を置き換えます。集計は標準出力に JSON で出ます。
更新候補は下記の手順で別途評価できます。この手順で作る一時 DB は処理後に破棄されます。
集計と出典を確認して採用を決めた後、スクリプト内の固定値と出典情報を更新します。

```powershell
python tools/import_kaikki.py .wn/kaikki-candidate.jsonl.gz --evaluate-candidate --source-url https://kaikki.org/jawiktionary/raw-wiktextract-data.jsonl.gz --dump-date YYYY-MM-DD
```

採用版での集計結果です。見出し数は言語と表記の組で数え、同じ表記の辞書項目はDB内で区別しています。

| 言語 | 見出し | 語義 | WordNet に同表記なし | 翻訳関係（見出しの収録率） |
|---|---:|---:|---:|---:|
| 日本語 | 128,380 | 201,379 | 94,380 | 32,237（12.24%） |
| 英語 | 58,509 | 107,054 | 34,072 | 442（0.29%） |

形式2のDBは約153 MBです。収録できた翻訳はすべて見出し側に付き、語義ラベルがある場合は原文を保持します。
このスナップショットでは、類語・反意語・関連語の関係先言語が抽出結果から確認できず、収録件数はいずれも0件でした。
[日本語版の抽出コード](https://github.com/tatuylonen/wiktextract/blob/19f9a30/src/wiktextract/extractor/ja/linkage.py)
では異言語へのリンクも同じ形式にまとめられるため、見出しの言語を一律には引き継げません。
語義の補足に使える一方、類語の補足に使うには関係先言語を確認できるデータが必要です。

アプリで補足を表示するには、生成した形式2の `kaikki.sqlite` を、アプリが使う
`dictionary.sqlite` と同じフォルダに置いて再起動します。Windows の開発ビルドでは、
プロジェクトの `data/kaikki.sqlite` があればビルド時に `win/build/mingw-release/data/` へコピーします
（`-Rebuild`・`full_rebuild_and_deploy.bat` でビルドフォルダを作り直した後も同様）。
DBを生成・更新した後は `build.ps1` をもう一度実行してください。配布版では `ruigo.exe` と同じ階層の `data/` に置きます。
標準の配布物は WordNet のDBを同梱します。Kaikki DB は利用者が手元で生成して追加します。
形式1を使っていた場合は上のコマンドで再生成してください。

検索結果に「Wiktionary / Kaikki」欄が出ると、見出しの語義を確認できます。
WordNetで見出しを選んだ場合も、元の入力語の項目があれば併せて表示します。
形式2の実データでは語義単位で参照先が1項目のものが8,905件、複数項目が3,413件、
未解決が2,240件でした。未解決のうち67件は説明と表記候補が一致しませんでした。
