#include "DictionaryRepository.h"
#include "MainWindow.h"
#include "SearchHistory.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <QAction>
#include <QDialogButtonBox>
#include <QDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QSplitter>
#include <algorithm>

class Tests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_{QStringLiteral("ruigo-tests-XXXXXX")};
    QString fixture_;
private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName("RuigoTests");
        QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        fixture_ = temporary_.filePath("fixture.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "fixture");
            db.setDatabaseName(fixture_);
            QVERIFY2(db.open(), qPrintable(db.lastError().text()));
            QSqlQuery q(db);
            const QStringList sql = {
                "PRAGMA user_version=1",
                "CREATE TABLE lemmas(id INTEGER PRIMARY KEY,word TEXT,language TEXT)",
                "CREATE TABLE synsets(id TEXT PRIMARY KEY,source TEXT)",
                "CREATE TABLE synset_definitions(synset_id TEXT,language TEXT,definition TEXT)",
                "CREATE TABLE senses(synset_id TEXT,lemma_id INTEGER)",
                "INSERT INTO lemmas VALUES (1,'美しい','ja'),(2,'綺麗','ja'),(3,'綺麗','ja'),(4,'麗しい','ja'),(5,'beautiful','en'),(6,'pretty','en')",
                "INSERT INTO synsets VALUES ('s1','test'),('s2','test')",
                "INSERT INTO senses VALUES ('s1',1),('s1',2),('s1',3),('s1',5),('s1',6),('s2',1),('s2',4)",
                "INSERT INTO synset_definitions VALUES ('s1','ja','テスト用の意味'),('s1','en','English test definition')"
            };
            for (const auto &statement : sql) QVERIFY(q.exec(statement));
            q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase("fixture");
    }
    void repository() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        auto result = repository.search(" 美しい ", "ja");
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.groups.size(), 2);
        QCOMPARE(result.candidateCount(), 2);
        QCOMPARE(result.groups[0].words, QStringList{"綺麗"});
        QCOMPARE(result.groups[0].definition, QString("テスト用の意味"));
        QVERIFY(result.groups[1].definition.isEmpty());
        auto english = repository.search("beautiful", "en");
        QCOMPARE(english.candidateCount(), 1);
        QCOMPARE(english.groups[0].words[0], QString("pretty"));
        QVERIFY(repository.search("美しい", "en").groups.isEmpty());
        QVERIFY(repository.search("' OR 1=1 --", "ja").groups.isEmpty());
        QVERIFY(repository.search(QString(100000, 'x'), "ja").groups.isEmpty());
        QVERIFY(repository.search("　 ", "ja").groups.isEmpty());
        QVERIFY(!repository.open(temporary_.filePath("missing.sqlite")));
        QVERIFY(!QFile::exists(temporary_.filePath("missing.sqlite")));
        QVERIFY(!repository.search("美しい", "ja").error.isEmpty());
        QFile corrupt(temporary_.filePath("corrupt.sqlite"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not sqlite"); corrupt.close();
        QVERIFY(!repository.open(corrupt.fileName()));
        QVERIFY(repository.open(fixture_));
    }
    void history() {
        const auto file = temporary_.filePath("history.ini");
        {
            QSettings settings(file, QSettings::IniFormat);
            SearchHistory history(settings);
            for (int i = 0; i < 110; ++i) history.add(QString::number(i), "ja");
            history.add(" 50 ", "ja");
            history.add("   ", "ja");
            history.add("50", "en");
            QCOMPARE(history.words("ja").size(), 100);
            QCOMPARE(history.words("ja").first(), QString("109"));
            QCOMPARE(history.words("ja").indexOf("50"), 59);
            QCOMPARE(history.words("ja").count("50"), 1);
            QCOMPARE(history.words("en"), QStringList{"50"});
            const auto before = history.words("ja");
            QVERIFY(!history.edit("50", "  ", "ja").isEmpty());
            QVERIFY(!history.edit("50", " 109 ", "ja").isEmpty());
            QCOMPARE(history.words("ja"), before);
            QVERIFY(history.edit("50", " 編集済み ", "ja").isEmpty());
            QCOMPARE(history.words("ja").at(59), QString("編集済み"));
            history.remove("108", "ja");
            QCOMPARE(history.words("ja").at(58), QString("編集済み"));
            QCOMPARE(history.words("en"), QStringList{"50"});
        }
        QSettings restored(file, QSettings::IniFormat);
        SearchHistory history(restored);
        QCOMPARE(history.words("ja").first(), QString("109"));
        QCOMPARE(history.words("ja").at(58), QString("編集済み"));
        QVERIFY(!history.words("ja").contains("108"));
        QCOMPARE(history.words("ja").size(), 99);
    }
    void ui() {
        MainWindow window(fixture_);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        auto *history = window.findChild<QListWidget *>("historyList");
        QVERIFY(input && tree && history);
        input->setText(" 美しい ");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(tree->topLevelItemCount(), 2);
        auto *word = tree->topLevelItem(0)->child(0);
        tree->scrollToItem(word);
        QApplication::processEvents();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(word).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(word).center());
        QCOMPARE(QApplication::clipboard()->text(), QString("綺麗"));
        input->setText("存在しないテスト語");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(window.findChild<QLabel *>("resultHeading")->text().contains("類語が見つかりませんでした"));
        QCOMPARE(history->count(), 2);
        history->setCurrentRow(1);
        QCOMPARE(input->text(), QString("美しい"));
        QCOMPARE(history->item(0)->text(), QString("存在しないテスト語"));
        QCOMPARE(history->currentRow(), 1);
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(history->item(0)->text(), QString("存在しないテスト語"));
        QCOMPARE(history->count(), 2);
        history->setFocus();
        QTest::keyClick(history, Qt::Key_F2);
        auto *editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor && editor->isVisible());
        QCOMPARE(editor->selectedText(), QString("美しい"));
        editor->setText("  ");
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(history->item(1)->text(), QString("美しい"));
        QVERIFY(editor->isVisible());
        QCOMPARE(input->text(), QString("美しい"));
        editor->setText(history->item(0)->text());
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(history->item(1)->text(), QString("美しい"));
        QVERIFY(editor->isVisible());
        editor->setText(" 綺麗 ");
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(1)->text(), QString("綺麗"));
        QCOMPARE(input->text(), QString("綺麗")); // Committing immediately refreshes results.
        QCOMPARE(history->currentRow(), 1);
        QCOMPARE(history->item(0)->text(), QString("存在しないテスト語"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("美しい"));
        QTest::keyClick(history, Qt::Key_F2);
        editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor && editor->isVisible());
        editor->setText("キャンセルされる語");
        QTest::keyClick(editor, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(1)->text(), QString("綺麗"));
        QCOMPARE(input->text(), QString("綺麗"));
        QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier,
            history->visualItemRect(history->item(1)).center());
        QCOMPARE(input->text(), QString("綺麗"));
        QTest::mouseDClick(history->viewport(), Qt::LeftButton, Qt::NoModifier,
            history->visualItemRect(history->item(1)).center());
        editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor && editor->isVisible());
        QCOMPARE(editor->selectedText(), QString("綺麗"));
        editor->setText("フォーカス移動で取り消す語");
        input->setFocus();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(1)->text(), QString("綺麗"));
        QVERIFY(!history->findChild<QLineEdit *>("historyEditor"));
        history->setFocus();
        QTest::keyClick(history, Qt::Key_Delete);
        QCOMPARE(history->count(), 1);
        QCOMPARE(input->text(), QString("綺麗")); // Deletion does not search either.
        window.findChild<QSplitter *>("splitter")->setSizes({350, 600});
        const auto state = window.findChild<QSplitter *>("splitter")->saveState();
        window.close();
        MainWindow restored(fixture_);
        QCOMPARE(restored.findChild<QListWidget *>("historyList")->item(0)->text(), QString("存在しないテスト語"));
        QCOMPARE(restored.findChild<QListWidget *>("historyList")->count(), 1);
        QCOMPARE(restored.findChild<QSplitter *>("splitter")->saveState(), state);
        MainWindow missing(temporary_.filePath("missing.sqlite"));
        QVERIFY(missing.findChild<QLabel *>("resultHeading")->text().contains("見つかりません"));
    }
    void displaySettings() {
        QSettings settings;
        settings.remove("display");
        MainWindow window(fixture_);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto original = window.font();
        auto *action = window.findChild<QAction *>("displaySettingsAction");
        QVERIFY(action);
        bool dialogSeen = false;
        QTimer::singleShot(100, [&] {
            auto *dialog = window.findChild<QDialog *>("displaySettingsDialog");
            if (!dialog) return;
            dialogSeen = true;
            dialog->findChild<QSpinBox *>("displayFontSize")->setValue(18);
            QCOMPARE(dialog->findChild<QTreeWidget *>("displayPreview")->font().pointSize(), 18);
            dialog->reject();
        });
        action->trigger();
        QVERIFY(dialogSeen);
        QCOMPARE(window.font(), original);
        QVERIFY(!settings.contains("display/fontSize"));
        QString chosenFamily;
        QTimer::singleShot(100, [&] {
            auto *dialog = window.findChild<QDialog *>("displaySettingsDialog");
            if (!dialog) return;
            auto *family = dialog->findChild<QFontComboBox *>("displayFontFamily");
            family->setCurrentFont(QFont(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
            chosenFamily = family->currentFont().family();
            dialog->findChild<QSpinBox *>("displayFontSize")->setValue(18);
            dialog->accept();
        });
        action->trigger();
        QCOMPARE(window.font().pointSize(), 18);
        QCOMPARE(window.font().family(), chosenFamily);
        QCOMPARE(window.findChild<QLineEdit *>("searchInput")->font().pointSize(), 18);
        QCOMPARE(window.findChild<QListWidget *>("historyList")->font().pointSize(), 18);
        QCOMPARE(window.findChild<QTreeWidget *>("results")->font().pointSize(), 18);
        MainWindow restored(fixture_);
        QCOMPARE(restored.font(), window.font());
        QTimer::singleShot(100, [&] {
            auto *dialog = window.findChild<QDialog *>("displaySettingsDialog");
            if (!dialog) return;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
            dialog->accept();
        });
        action->trigger();
        QCOMPARE(window.font(), original);
        QVERIFY(!settings.contains("display/fontSize"));
        settings.setValue("display/fontFamily", "Ruigo nonexistent font 123456789");
        settings.setValue("display/fontSize", 24);
        MainWindow fallback(fixture_);
        QCOMPARE(fallback.font(), original);
        settings.remove("display");
        window.close();
    }
    void realDictionary() {
        const auto path = QStringLiteral(RUIGO_SOURCE_DIR "/data/dictionary.sqlite");
        if (!QFile::exists(path)) QSKIP("Generate the real dictionary to run integration tests");
        DictionaryRepository repository;
        QVERIFY(repository.open(path));
        QVector<qint64> durations;
        for (int i = 0; i < 101; ++i) {
            QElapsedTimer timer; timer.start();
            auto result = repository.search(QStringLiteral("美しい"), "ja");
            durations.append(timer.nsecsElapsed());
            QVERIFY(result.error.isEmpty());
            QVERIFY(result.candidateCount() > 0);
            for (const auto &group : result.groups) QVERIFY(!group.words.contains("美しい"));
        }
        std::sort(durations.begin(), durations.end());
        qInfo("Real dictionary search median: %.3f ms", durations[50] / 1000000.0);
        QVERIFY(repository.search("beautiful", "en").groups.isEmpty());
        MainWindow window(path);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        input->setText(QStringLiteral("美しい"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(window.grab().save("preview.png"));
        window.close();
    }
};
QTEST_MAIN(Tests)
#include "Tests.moc"
