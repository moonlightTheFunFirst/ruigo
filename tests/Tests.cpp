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
#include <QToolTip>
#include <QHelpEvent>
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
                "CREATE TABLE lemmas(id INTEGER PRIMARY KEY,word TEXT,language TEXT,forms_json TEXT DEFAULT '[]')",
                "CREATE TABLE synsets(id TEXT PRIMARY KEY,source TEXT)",
                "CREATE TABLE synset_definitions(synset_id TEXT,language TEXT,definition TEXT)",
                "CREATE TABLE senses(synset_id TEXT,lemma_id INTEGER)",
                "INSERT INTO lemmas(id,word,language) VALUES (1,'美しい','ja'),(2,'綺麗','ja'),(3,'綺麗','ja'),(4,'麗しい','ja'),(5,'beautiful','en'),(6,'pretty','en')",
                "INSERT INTO synsets VALUES ('s1','test'),('s2','test')",
                "INSERT INTO senses VALUES ('s1',1),('s1',2),('s1',3),('s1',5),('s1',6),('s2',1),('s2',4)",
                "INSERT INTO synset_definitions VALUES ('s1','ja','テスト用の意味'),('s1','en','English test definition')",
                R"(UPDATE lemmas SET forms_json='[{"writtenForm":"キレイ","script":"kana"},{"writtenForm":"きれい","script":"hira"}]' WHERE id IN (2,3))",
                "INSERT INTO synsets VALUES ('s3','test'),('s4','test'),('s5','test'),('s6','test')",
                "INSERT INTO senses VALUES ('s3',2),('s3',3),('s4',2),('s6',2)",
                "INSERT INTO synset_definitions VALUES ('s3','ja','別の意味 <テスト> & 確認'),('s3','en','Do not display this')",
                // Unrelated homograph must not leak its reading/meaning into s1.
                R"(INSERT INTO lemmas VALUES (7,'綺麗','ja','[{"writtenForm":"べつよみ","script":"hira"}]'))",
                "INSERT INTO senses VALUES ('s5',7)",
                "INSERT INTO synset_definitions VALUES ('s5','ja','別の同形異義語')"
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
    void wordDetails() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        const auto details = repository.wordDetails("綺麗", "ja", "s1");
        QVERIFY(details.error.isEmpty());
        QCOMPARE(details.readings, QStringList{"きれい"});
        QCOMPARE(details.otherMeanings.size(), 3);
        QCOMPARE(details.otherMeanings[0].definition, QString("別の意味 <テスト> & 確認"));
        QCOMPARE(details.otherMeanings[1].id, QString("s4"));
        QCOMPARE(details.otherMeanings[2].id, QString("s6"));
        QVERIFY(details.otherMeanings[1].definition.isEmpty());
        QVERIFY(details.otherMeanings[2].definition.isEmpty());
        const auto missing = repository.wordDetails("麗しい", "ja", "s2");
        QVERIFY(missing.readings.isEmpty());
        QVERIFY(missing.otherMeanings.isEmpty());
        QVERIFY(repository.wordDetails("綺麗", "en", "s1").readings.isEmpty());
        QVERIFY(repository.wordDetails("' OR 1=1 --", "ja", "s1").otherMeanings.isEmpty());
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
        QVERIFY(word->toolTip(0).isEmpty()); // Details are loaded only on hover.
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
    void tooltips() {
        if (QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal")
            QSKIP("Tooltip window visibility requires a desktop");
        QSettings settings;
        settings.setValue("display/fontFamily", QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        settings.setValue("display/fontSize", 18);
        MainWindow window(fixture_);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        input->setText("美しい");
        QTest::keyClick(input, Qt::Key_Return);
        auto *word = tree->topLevelItem(0)->child(0);
        QVERIFY(word->toolTip(0).isEmpty());
        const auto hover = [&](QTreeWidgetItem *item) {
            tree->scrollToItem(item);
            QApplication::processEvents();
            const auto point = tree->visualItemRect(item).center();
            QHelpEvent event(QEvent::ToolTip, point, tree->viewport()->mapToGlobal(point));
            QApplication::sendEvent(tree->viewport(), &event);
        };
        hover(word);
        QTRY_VERIFY(QToolTip::isVisible());
        const auto text = QToolTip::text();
        QVERIFY(text.contains("読み：きれい"));
        QVERIFY(text.contains("現在の意味</b><br>テスト用の意味"));
        QVERIFY(text.contains("別の意味 &lt;テスト&gt; &amp; 確認"));
        QVERIFY(text.contains("2. 説明なし"));
        QVERIFY(text.contains("3. 説明なし"));
        QVERIFY(!text.contains("べつよみ"));
        QVERIFY(!text.contains("Do not display"));
        hover(word);
        QCOMPARE(QToolTip::text(), text);
        hover(tree->topLevelItem(1)->child(0));
        QVERIFY(QToolTip::text().contains("読み：未収録"));
        QVERIFY(QToolTip::text().contains("現在の意味</b><br>説明なし"));
        QVERIFY(QToolTip::text().contains("ほかに収録されている意味</b><br>なし"));
        // A long definition must wrap within the supported fixed-width table.
        word->setData(0, Qt::UserRole + 2, QStringLiteral("これは長い定義の折り返しを確認するテストです。").repeated(6));
        hover(word);
        QTRY_VERIFY(QToolTip::isVisible());
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget->windowType() == Qt::ToolTip && widget->isVisible()) {
                QVERIFY(widget->width() < 500);
                QCOMPARE(QToolTip::font().pointSize(), 18);
                QVERIFY(widget->grab().save("tooltip-long.png"));
            }
        }
        QToolTip::hideText();
        window.close();
        settings.remove("display");
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
        QCOMPARE(QToolTip::font().pointSize(), 18);
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
        const auto shurei = repository.wordDetails("秀麗", "ja", "wnja:2.1:wnja-00217728-a");
        QCOMPARE(shurei.readings, QStringList{"しゅうれい"});
        QVERIFY(shurei.otherMeanings.isEmpty());
        const auto tanrei = repository.wordDetails("端麗", "ja", "wnja:2.1:wnja-00217728-a");
        QCOMPARE(tanrei.readings, QStringList{"たんれい"});
        QCOMPARE(tanrei.otherMeanings.size(), 2);
        MainWindow window(path);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        input->setText(QStringLiteral("美しい"));
        QElapsedTimer displayTimer;
        displayTimer.start();
        QTest::keyClick(input, Qt::Key_Return);
        qInfo("Search and result display (no tooltip queries): %.3f ms", displayTimer.nsecsElapsed() / 1000000.0);
        QVERIFY(window.grab().save("preview.png"));
        if (QGuiApplication::platformName() != "offscreen" && QGuiApplication::platformName() != "minimal") {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            auto *tree = window.findChild<QTreeWidget *>("results");
            const auto checkTooltip = [&](const QString &search, const QString &candidate,
                                          const QString &filename, bool missingDefinition) {
                input->setText(search);
                QTest::keyClick(input, Qt::Key_Return);
                QTreeWidgetItem *target = nullptr;
                for (int i = 0; i < tree->topLevelItemCount() && !target; ++i) {
                    auto *group = tree->topLevelItem(i);
                    for (int j = 0; j < group->childCount(); ++j) {
                        auto *item = group->child(j);
                        if ((!candidate.isEmpty() && item->text(0) == candidate) ||
                            (missingDefinition && item->data(0, Qt::UserRole + 2).toString().isEmpty())) {
                            target = item;
                            break;
                        }
                    }
                }
                QVERIFY(target);
                QVERIFY(target->toolTip(0).isEmpty());
                tree->scrollToItem(target);
                QApplication::processEvents();
                const auto point = tree->visualItemRect(target).center();
                QHelpEvent event(QEvent::ToolTip, point, tree->viewport()->mapToGlobal(point));
                QApplication::sendEvent(tree->viewport(), &event);
                QTRY_VERIFY(QToolTip::isVisible());
                if (candidate == "ビューティフル") QVERIFY(!QToolTip::text().contains("読み："));
                if (missingDefinition) QVERIFY(QToolTip::text().contains("現在の意味</b><br>説明なし"));
                bool captured = false;
                for (auto *widget : QApplication::topLevelWidgets()) {
                    if (widget->windowType() == Qt::ToolTip && widget->isVisible()) {
                        QVERIFY(widget->width() < 500);
                        captured = widget->grab().save(filename);
                    }
                }
                QVERIFY(captured);
                QToolTip::hideText();
            };
            checkTooltip("美しい", "端麗", "tooltip-tanrei.png", false);
            checkTooltip("美しい", "ビューティフル", "tooltip-kana.png", false);
            checkTooltip("あいつ", "", "tooltip-missing.png", true);
        }
        window.close();
    }
};
QTEST_MAIN(Tests)
#include "Tests.moc"
