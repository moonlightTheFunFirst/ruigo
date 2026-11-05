# Third-party notices

## Japanese WordNet 2.1

Japanese WordNet (2.1), https://bond-lab.github.io/wnja/index.ja.html

Copyright 2009–2011 NICT; 2012–2015 Francis Bond;
2016–2017 Francis Bond, Takayuki Kuribayashi; 2018–2020 Francis Bond;
2021–2026 Francis Bond, Takayuki Kuribayashi.

The application database is a conversion of the supplied WN-LMF 1.4 package.
Its original license, including disclaimer, is in `licenses/wnja-LICENSE`.
Dictionary readings are retained but are not searched. Examples and semantic
relations are not included in the application database. No definitions are generated.

Build-time dependency: OMW English WordNet `omw-en:2.0`, downloaded through `wn`.
English entries are not exported into this Japanese release.

## Qt 6

The application dynamically links Qt Core, Gui, Widgets and Sql, including
the QSQLITE plugin. Qt is available under commercial and open-source licenses;
the open-source LGPLv3/GPLv3 texts are included in `licenses/`.
https://www.qt.io/licensing/ and https://download.qt.io/archive/qt/

Keep Qt DLLs/frameworks replaceable when distributing the LGPL build. Include
the corresponding Qt source or a compliant source offer for the actual shipped
Qt version, and review the Qt installation's third-party notices before public release.

## Development tools

Windows packages also contain GCC's libgcc/libstdc++ and MinGW winpthreads.
Their runtime license notices are included under `licenses/mingw/`.

Python and `wn` (MIT, https://github.com/goodmami/wn) are used only to generate
the SQLite dictionary. They are not runtime dependencies and are not bundled.
