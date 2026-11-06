#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/ruigo.ico")));
#endif
    QCoreApplication::setOrganizationName("Keishin");
    QCoreApplication::setOrganizationDomain("keishin.jp");
    QCoreApplication::setApplicationName("Ruigo");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("オフライン日英類語辞典"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"dictionary", QStringLiteral("辞書DBのパス"), "path"});
    QCommandLineOption selfCheck("self-check", "Verify the deployed dictionary and exit");
    selfCheck.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(selfCheck);
    parser.process(app);
    QString base = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    base += "/../Resources";
#endif
    const auto path = parser.isSet("dictionary") ? parser.value("dictionary")
        : QDir(base).filePath("data/dictionary.sqlite");
    if (parser.isSet(selfCheck)) {
        DictionaryRepository repository;
        if (!repository.open(path)) return 2;
        return repository.search("美しい", "ja").error.isEmpty()
            && repository.search("happy", "en").error.isEmpty() ? 0 : 3;
    }
    MainWindow window(path);
    window.show();
    return app.exec();
}
