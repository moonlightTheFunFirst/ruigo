#include "DictionaryRepository.h"
#include "MainWindow.h"
#include "SearchHistory.h"
#include <QApplication>
#include <QAccessible>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QAction>
#include <QDialogButtonBox>
#include <QDialog>
#include <QPushButton>
#include <QCheckBox>
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
                "PRAGMA user_version=4",
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
                "INSERT INTO synset_definitions VALUES ('s5','ja','別の同形異義語')",
                "CREATE TABLE lemma_aliases(lemma_id INTEGER,language TEXT,word TEXT,PRIMARY KEY(lemma_id,word))",
                "CREATE INDEX idx_alias ON lemma_aliases(language,word)",
                "CREATE TABLE sources(id TEXT PRIMARY KEY, reading_mode TEXT)",
                "INSERT INTO sources VALUES ('ja-source','kana'),('en-source','none')",
                "ALTER TABLE lemmas ADD COLUMN source TEXT DEFAULT 'ja-source'",
                "ALTER TABLE lemmas ADD COLUMN search_key TEXT DEFAULT ''",
                "UPDATE lemmas SET source='en-source' WHERE language='en'",
                "UPDATE lemmas SET search_key=lower(word)",
                "CREATE INDEX idx_word ON lemmas(language,word)",
                "CREATE INDEX idx_key ON lemmas(language,search_key)",
                "INSERT INTO lemmas(id,word,language,source,search_key) VALUES (8,'AI','ja','ja-source','AI'),(9,'人工知能','ja','ja-source','人工知能'),(10,'AI','en','en-source','ai'),(11,'artificial intelligence','en','en-source','artificial intelligence'),(12,'solo','en','en-source','solo'),(13,'solo','ja','ja-source','solo'),(14,'US','en','en-source','us'),(15,'us','en','en-source','us'),(16,'Wi-Fi','ja','ja-source','Wi-Fi'),(17,'café','en','en-source','café')",
                "INSERT INTO synsets VALUES ('sj','ja-source'),('se','en-source'),('single','en-source')",
                "INSERT INTO senses VALUES ('sj',8),('sj',9),('sj',13),('se',10),('se',11),('single',12)",
                "INSERT INTO lemmas(id,word,language,source,search_key) VALUES (18,'哀しい','ja','ja-source','哀しい'),(19,'悲しげ','ja','ja-source','悲しげ'),(20,'哀しい','ja','ja-source','哀しい'),(21,'無関係な語','ja','ja-source','無関係な語')",
                "INSERT INTO synsets VALUES ('alias1','ja-source'),('alias2','ja-source')",
                "INSERT INTO senses VALUES ('alias1',18),('alias1',19),('alias2',20),('alias2',21)",
                "INSERT INTO lemma_aliases VALUES (18,'ja','悲しい'),(18,'ja','美しい'),(18,'ja','SAD-JA'),(18,'ja','別''表記')",
                // "similar" relations: s1 -> similar1 (English words only, so Japanese searches on s1 gain nothing),
                // alias1 -> similar2 (Japanese), a self-relation on alias1, and a non-"similar" relation that must be ignored.
                "CREATE TABLE synset_relations(synset_id TEXT,target_id TEXT,rel_type TEXT,PRIMARY KEY(synset_id,rel_type,target_id))",
                "INSERT INTO lemmas(id,word,language,source,search_key) VALUES (22,'gorgeous','en','en-source','gorgeous'),(23,'lovely','en','en-source','lovely'),(24,'物悲しい','ja','ja-source','物悲しい'),(25,'淋しい','ja','ja-source','淋しい')",
                "INSERT INTO synsets VALUES ('similar1','en-source'),('similar2','ja-source')",
                "INSERT INTO senses VALUES ('similar1',22),('similar1',23),('similar2',24),('similar2',25)",
                "INSERT INTO synset_definitions VALUES ('similar1','en','very pleasing'),('similar2','ja','似た意味のテスト')",
                "INSERT INTO synset_relations VALUES ('s1','similar1','similar'),('alias1','similar2','similar'),('alias1','alias1','similar'),('alias1','s3','hypernym')"
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
        const auto oldPath = temporary_.filePath("old.sqlite");
        QVERIFY(QFile::copy(fixture_, oldPath));
        {
            auto old = QSqlDatabase::addDatabase("QSQLITE", "old");
            old.setDatabaseName(oldPath);
            QVERIFY(old.open());
            QSqlQuery query(old);
            QVERIFY(query.exec("PRAGMA user_version=1"));
        }
        QSqlDatabase::removeDatabase("old");
        QVERIFY(!repository.open(oldPath));
        QVERIFY(repository.error().contains("再生成"));
        const auto old2Path = temporary_.filePath("old2.sqlite");
        QVERIFY(QFile::copy(fixture_, old2Path));
        {
            auto old = QSqlDatabase::addDatabase("QSQLITE", "old2");
            old.setDatabaseName(old2Path);
            QVERIFY(old.open());
            QSqlQuery query(old);
            QVERIFY(query.exec("PRAGMA user_version=2"));
        }
        QSqlDatabase::removeDatabase("old2");
        QVERIFY(!repository.open(old2Path));
        QVERIFY(repository.error().contains("再生成"));
        const auto old3Path = temporary_.filePath("old3.sqlite");
        QVERIFY(QFile::copy(fixture_, old3Path));
        {
            auto old = QSqlDatabase::addDatabase("QSQLITE", "old3");
            old.setDatabaseName(old3Path);
            QVERIFY(old.open());
            QSqlQuery query(old);
            QVERIFY(query.exec("PRAGMA user_version=3"));
        }
        QSqlDatabase::removeDatabase("old3");
        QVERIFY(!repository.open(old3Path));
        QVERIFY(repository.error().contains("再生成"));
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
            QCOMPARE(history.words("ja").size(), 99);
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
        QCOMPARE(history.words("ja").size(), 98);
    }
    void autoLanguage() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        for (const auto &word : {QStringLiteral("USBメモリ"), QStringLiteral("3日"), QStringLiteral("ー"), QStringLiteral("ｰ"), QStringLiteral("々"), QStringLiteral("〆"), QStringLiteral("〇"), QStringLiteral("𠮷")})
            QCOMPARE(DictionaryRepository::preferredLanguage(word), QString("ja"));
        for (const auto &word : {QStringLiteral("beautiful"), QStringLiteral("U．S．"), QStringLiteral("ＢＥＡＵＴＩＦＵＬ"), QStringLiteral("café"), QStringLiteral("naïve")})
            QCOMPARE(DictionaryRepository::preferredLanguage(word), QString("en"));
        QCOMPARE(DictionaryRepository::preferredLanguage("100%"), QString("ja"));
        const auto english = repository.search("Beautiful");
        QVERIFY(english.error.isEmpty());
        QCOMPARE(english.language, QString("en"));
        QCOMPARE(english.candidateCount(), 1);
        QCOMPARE(english.groups[0].words, QStringList{"pretty"});
        QCOMPARE(repository.search("ＢＥＡＵＴＩＦＵＬ").candidateCount(), 1);
        QCOMPARE(repository.search("CAFÉ").language, QString("en"));
        QVERIFY(repository.search("CAFÉ").found);
        const auto ai = repository.search("AI");
        QCOMPARE(ai.language, QString("en"));
        QCOMPARE(ai.alternativeLanguage, QString("ja"));
        QVERIFY(!ai.groups[0].words.contains("人工知能"));
        QCOMPARE(repository.search("AI", "ja").alternativeLanguage, QString("en"));
        QCOMPARE(repository.search("Wi-Fi").language, QString("ja"));
        QVERIFY(!repository.search("Wi-Fi", "en").found); // Saved language never falls back.
        const auto solo = repository.search("solo");
        QVERIFY(solo.found);
        QVERIFY(solo.groups.isEmpty());
        QCOMPARE(solo.language, QString("en"));
        QCOMPARE(solo.alternativeLanguage, QString("ja"));
        QVERIFY(repository.search("US").error.isEmpty());
        QVERIFY(repository.search("us").error.isEmpty());
        const auto ambiguous = repository.search("Us");
        QVERIFY(ambiguous.error.isEmpty());
        QCOMPARE(ambiguous.spellings, (QStringList{"US", "us"}));
        QVERIFY(ambiguous.found && ambiguous.groups.isEmpty());
        QCOMPARE(repository.resolveLanguage("Us"), QString("en"));
        QCOMPARE(repository.resolveLanguage("Wi-Fi"), QString("ja"));
        QCOMPARE(repository.search("ＡＩ").alternativeLanguage, QString("ja"));
        QVERIFY(repository.search("ＡＩ", "ja").found);
        QVERIFY(!repository.search("ｷﾚｲ", "ja").found); // Kana normalization is unchanged.
        DictionaryRepository closed;
        QCOMPARE(closed.resolveLanguage("Us"), QString("en"));
        QCOMPARE(closed.resolveLanguage("美しい"), QString("ja"));
        QVERIFY(repository.search("ice  cream").groups.isEmpty());
        QVERIFY(repository.search("artificial intelligence").found);
        QVERIFY(!repository.search("artificial  intelligence").found);
        QVERIFY(!repository.wordDetails("pretty", "en", "s1").showReadings);
    }
    void relatedWords() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        // English: s1 is "similar" to similar1. Synonyms stay as before; related words are separate.
        const auto english = repository.search("beautiful", "en");
        QCOMPARE(english.candidateCount(), 1);
        QCOMPARE(english.relatedGroups.size(), 1);
        QCOMPARE(english.relatedGroups[0].id, QString("similar1"));
        QCOMPARE(english.relatedGroups[0].definition, QString("very pleasing"));
        QCOMPARE(english.relatedGroups[0].words, (QStringList{"gorgeous", "lovely"})); // The input itself is excluded.
        QCOMPARE(english.relatedCount(), 2);
        // Japanese searches through s1 never pick up the English related words.
        QVERIFY(repository.search("美しい", "ja").relatedGroups.isEmpty());
        // Only "similar" is used, a self-relation is dropped as a direct meaning, and the
        // matched headword (哀しい, reached through the alias) is excluded from related words.
        const auto alias = repository.search("悲しい");
        QCOMPARE(alias.relatedGroups.size(), 1);
        QCOMPARE(alias.relatedGroups[0].id, QString("similar2"));
        QCOMPARE(alias.relatedGroups[0].words, (QStringList{"淋しい", "物悲しい"}));
        // UI: related groups follow the synonym groups with their own prefix.
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        input->setText("beautiful");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(tree->topLevelItem(0)->text(0).startsWith("意味："));
        QCOMPARE(tree->topLevelItem(1)->text(0), QString("関連（似た意味）：very pleasing"));
        QCOMPARE(tree->topLevelItem(1)->child(0)->data(0, Qt::UserRole).toString(), QString("gorgeous"));
        QVERIFY(heading->text().contains("関連語：2件"));
        settings.setValue("history/entries", QStringList{});
    }
    void relatedToggle() {
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        settings.remove("display/showRelated");
        {
            MainWindow window(fixture_);
            auto *input = window.findChild<QLineEdit *>("searchInput");
            auto *tree = window.findChild<QTreeWidget *>("results");
            auto *heading = window.findChild<QLabel *>("resultHeading");
            auto *history = window.findChild<QListWidget *>("historyList");
            auto *check = window.findChild<QCheckBox *>("showRelatedCheck");
            QVERIFY(check);
            QVERIFY(check->isChecked()); // Shown by default.
            input->setText("beautiful");
            QTest::keyClick(input, Qt::Key_Return);
            QCOMPARE(tree->topLevelItemCount(), 2);
            QCOMPARE(history->count(), 1);
            input->setText("編集中の入力"); // The toggle redraws the last result, not the edited text.
            check->setChecked(false);
            QCOMPARE(tree->topLevelItemCount(), 1);
            QVERIFY(tree->topLevelItem(0)->text(0).startsWith("意味："));
            QVERIFY(heading->text().contains("「beautiful」の類語候補：1件"));
            QVERIFY(heading->text().contains("似た意味の関連語2件は非表示"));
            QCOMPARE(history->count(), 1); // No new history entry.
            QCOMPARE(input->text(), QString("編集中の入力"));
            QVERIFY(!settings.value("display/showRelated").toBool());
            check->setChecked(true);
            QCOMPARE(tree->topLevelItemCount(), 2);
            QVERIFY(heading->text().contains("関連語：2件"));
            check->setChecked(false);
            // A word with no synonyms and hidden related words gets the plain message.
            settings.setValue("history/entries", QStringList{});
        }
        {
            MainWindow restored(fixture_); // The choice survives a restart.
            auto *check = restored.findChild<QCheckBox *>("showRelatedCheck");
            QVERIFY(!check->isChecked());
            auto *input = restored.findChild<QLineEdit *>("searchInput");
            auto *heading = restored.findChild<QLabel *>("resultHeading");
            input->setText("悲しい");
            QTest::keyClick(input, Qt::Key_Return);
            QCOMPARE(restored.findChild<QTreeWidget *>("results")->topLevelItemCount(), 1);
            QVERIFY(heading->text().contains("似た意味の関連語2件は非表示"));
        }
        settings.remove("display/showRelated");
        settings.setValue("history/entries", QStringList{});
    }
    void matchedHeadword() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        const auto alias = repository.search("悲しい");
        QVERIFY(alias.viaAlias);
        QCOMPARE(alias.matchedWords, QStringList{"哀しい"});
        const auto exact = repository.search("哀しい");
        QVERIFY(!exact.viaAlias);
        QVERIFY(exact.matchedWords.isEmpty());
        const auto folded = repository.search("Beautiful");
        QVERIFY(!folded.viaAlias);
        QCOMPARE(folded.matchedWords, QStringList{"beautiful"});
        QCOMPARE(repository.search("ＡＩ").matchedWords, QStringList{"AI"});
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        input->setText("悲しい");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「悲しい」（「哀しい」の別表記）の類語候補"));
        input->setText("Beautiful");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「Beautiful」（「beautiful」として検索）"));
        input->setText("哀しい");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("— 「哀しい」の類語候補"));
        settings.setValue("history/entries", QStringList{});
    }
    void keyboardNavigation() {
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        if (QGuiApplication::platformName() != "offscreen") QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        input->setFocus();
        input->setText("美しい");
        QTest::keyClick(input, Qt::Key_Return);
        QApplication::clipboard()->clear();
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(tree));
        QCOMPARE(tree->currentItem(), tree->topLevelItem(0)->child(0));
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(QApplication::clipboard()->text(), QString("綺麗"));
        QTest::keyClick(tree, Qt::Key_Down); // Next row is the second meaning's heading.
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(tree->currentItem(), tree->topLevelItem(1)->child(0));
        QApplication::clipboard()->clear();
        QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("麗しい"));
        // Enter and Ctrl+C on a meaning heading copy nothing.
        tree->setCurrentItem(tree->topLevelItem(0));
        QApplication::clipboard()->clear();
        QTest::keyClick(tree, Qt::Key_Return);
        QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(QApplication::clipboard()->text().isEmpty());
        // Up from the first word returns to the search box with the text selected.
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Up);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(input));
        QCOMPARE(input->selectedText(), QString("美しい"));
        // Down with no candidates stays in the search box.
        input->setText("存在しない語");
        QTest::keyClick(input, Qt::Key_Return);
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(input));
        window.close();
        settings.setValue("history/entries", QStringList{});
    }
    void spellingAliases() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        const auto alias = repository.search("悲しい");
        QVERIFY(alias.error.isEmpty());
        QVERIFY(alias.found);
        QCOMPARE(alias.language, QString("ja"));
        QCOMPARE(alias.groups.size(), 1);
        QCOMPARE(alias.groups[0].id, QString("alias1"));
        QCOMPARE(alias.groups[0].words, QStringList{"悲しげ"});
        // The alias belongs to entry 18, not unrelated homograph entry 20.
        QCOMPARE(repository.search("哀しい").groups.size(), 2);
        QCOMPARE(repository.search("美しい").groups.size(), 2); // Exact headword wins.
        QCOMPARE(repository.search("別'表記").groups[0].words, alias.groups[0].words);
        QCOMPARE(repository.search("SAD-JA").language, QString("ja"));
        QCOMPARE(repository.resolveLanguage("ＳＡＤ-ＪＡ"), QString("ja"));
        QCOMPARE(repository.search("ＳＡＤ-ＪＡ").groups[0].words, alias.groups[0].words);
        QVERIFY(!repository.search("悲しい", "en").found);
        QVERIFY(!repository.search("かなしい").found);
        QVERIFY(!repository.search("カナシイ").found);
        QVERIFY(!repository.search("きれい").found);
        QVERIFY(!repository.search("キレイ").found);
        QVERIFY(!repository.search("sad-ja").found); // Japanese case rules unchanged.
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        input->setText("悲しい");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("悲しげ"));
        QCOMPARE(settings.value("history/entries").toStringList(), QStringList{"ja:悲しい"});
        settings.setValue("history/entries", QStringList{});
    }
    void historyMigration() {
        QSettings settings(temporary_.filePath("migration.ini"), QSettings::IniFormat);
        settings.setValue("history/ja", QStringList{"AI", "美しい", "AI"});
        settings.setValue("history/en", QStringList{"AI", "a:b"});
        SearchHistory history(settings);
        QCOMPARE(history.entries().size(), 4);
        QCOMPARE(settings.value("history/entries").toStringList(), (QStringList{"ja:AI", "ja:美しい", "en:AI", "en:a:b"}));
        QVERIFY(settings.contains("history/ja"));
        history.remove("AI", "ja");
        SearchHistory restored(settings);
        QCOMPARE(restored.entries().size(), 3); // No reimport.
        QVERIFY(!history.edit("美しい", "AI", "ja", "en").isEmpty());
        QVERIFY(history.edit("美しい", "beautiful", "ja", "en").isEmpty());
        QCOMPARE(history.entries()[0].language, QString("en"));
        settings.setValue("history/entries", QStringList{"bad", "xx:test", "en:", "en:a:b", "en:a:b"});
        QCOMPARE(history.entries().size(), 1);
        QCOMPARE(history.entries()[0].word, QString("a:b"));
        settings.setValue("history/entries", QStringList{});
        SearchHistory empty(settings);
        QVERIFY(empty.entries().isEmpty());
    }
    void bilingualUi() {
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *history = window.findChild<QListWidget *>("historyList");
        auto *button = window.findChild<QPushButton *>("alternativeLanguageButton");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        input->setText("AI");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("英語で検索"));
        QVERIFY(button->isVisible());
        button->click();
        QVERIFY(heading->text().contains("日本語で検索"));
        QCOMPARE(history->count(), 2);
        QCOMPARE(history->item(0)->text(), QString("AI"));
        QCOMPARE(history->item(1)->text(), QString("AI  [英語]"));
        QCOMPARE(history->item(0)->data(Qt::UserRole).toString(), QString("ja"));
        history->setCurrentRow(0);
        history->setFocus();
        QTest::keyClick(history, Qt::Key_F2);
        auto *editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor);
        QCOMPARE(editor->text(), QString("AI"));
        editor->setText(" AI ");
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(0)->data(Qt::UserRole).toString(), QString("ja"));
        history->setCurrentRow(1);
        QVERIFY(heading->text().contains("英語で検索"));
        QCOMPARE(history->count(), 2);
        history->setCurrentRow(0);
        QTest::keyClick(history, Qt::Key_F2);
        editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor);
        editor->setText("beautiful");
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(0)->data(Qt::UserRole).toString(), QString("en"));
        QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), QString("beautiful"));
        QVERIFY(!button->isVisible());
        // English entries carry no label unless the same spelling is also saved in Japanese.
        QCOMPARE(history->item(0)->text(), QString("beautiful"));
        QCOMPARE(history->item(1)->text(), QString("AI"));
        QCOMPARE(history->item(1)->data(Qt::UserRole).toString(), QString("en"));
        input->setText("AI");
        QTest::keyClick(input, Qt::Key_Return);
        button->click(); // Saves ja:AI again, so the English row needs its label.
        QCOMPARE(history->item(0)->text(), QString("AI"));
        QCOMPARE(history->item(2)->text(), QString("AI  [英語]"));
        history->setCurrentRow(0);
        history->setFocus();
        QTest::keyClick(history, Qt::Key_Delete);
        QCOMPARE(history->count(), 2);
        QCOMPARE(history->item(1)->text(), QString("AI")); // Label removed with the pair.
        window.close();
        settings.setValue("history/entries", QStringList{});
    }
    void finishingUi() {
        QSettings settings;
        settings.setValue("history/entries", QStringList{"ja:編集前"});
        for (const auto &path : {fixture_, temporary_.filePath("missing.sqlite")}) {
            MainWindow window(path);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.activateWindow();
            if (QGuiApplication::platformName() != "offscreen") QVERIFY(QTest::qWaitForWindowActive(&window));
            auto *history = window.findChild<QListWidget *>("historyList");
            history->setCurrentRow(0);
            history->setFocus();
            QTest::keyClick(history, Qt::Key_F2);
            auto *editor = history->findChild<QLineEdit *>("historyEditor");
            QVERIFY(editor);
            editor->setText(path == fixture_ ? "Us" : "AFGHAN");
            QTest::keyClick(editor, Qt::Key_Return);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(history->item(0)->data(Qt::UserRole).toString(), QString("en"));
            QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), path == fixture_ ? QString("Us") : QString("AFGHAN"));
            if (path == fixture_) {
                auto *choices = window.findChild<QListWidget *>("spellingChoices");
                QCOMPARE(choices->count(), 2);
                QTest::mouseClick(choices->viewport(), Qt::LeftButton, Qt::NoModifier,
                                 choices->visualItemRect(choices->item(0)).center());
                QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), QString("US"));
                QVERIFY(!choices->isVisible());
                auto *input = window.findChild<QLineEdit *>("searchInput");
                auto *heading = window.findChild<QLabel *>("resultHeading");
                input->setText("solo"); QTest::keyClick(input, Qt::Key_Return);
                QVERIFY(heading->text().contains("見出しはありますが"));
                input->setText("zzzzmissing"); QTest::keyClick(input, Qt::Key_Return);
                QVERIFY(heading->text().contains("見出しが見つかりませんでした"));
                input->setText("ＡＩ"); QTest::keyClick(input, Qt::Key_Return);
                QVERIFY(window.findChild<QPushButton *>("alternativeLanguageButton")->isVisible());
            }
            window.close();
        }
        settings.setValue("history/entries", QStringList{});
    }
    void selfCheck() {
#ifdef Q_OS_WIN
        const auto executable = QCoreApplication::applicationDirPath() + "/ruigo.exe";
        for (const auto &path : {fixture_, temporary_.filePath("missing.sqlite"), temporary_.filePath("old.sqlite"), temporary_.filePath("old2.sqlite"), temporary_.filePath("old3.sqlite")}) {
            QProcess process;
            process.start(executable, {"--self-check", "--dictionary", QFileInfo(path).absoluteFilePath()});
            QVERIFY(process.waitForFinished(15000));
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(process.exitCode(), path == fixture_ ? 0 : 2);
        }
#else
        QSKIP("Windows deployment executable check");
#endif
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
        QVERIFY(window.findChild<QLabel *>("resultHeading")->text().contains("見出しが見つかりませんでした"));
        QCOMPARE(history->count(), 2);
        history->setCurrentRow(1);
        QCOMPARE(input->text(), QString("美しい"));
        QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), QString("存在しないテスト語"));
        QCOMPARE(history->currentRow(), 1);
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), QString("存在しないテスト語"));
        QCOMPARE(history->count(), 2);
        history->setFocus();
        QTest::keyClick(history, Qt::Key_F2);
        auto *editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor && editor->isVisible());
        QCOMPARE(editor->selectedText(), QString("美しい"));
        editor->setText("  ");
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(history->item(1)->data(Qt::UserRole + 1).toString(), QString("美しい"));
        QVERIFY(editor->isVisible());
        QCOMPARE(input->text(), QString("美しい"));
        editor->setText(history->item(0)->data(Qt::UserRole + 1).toString());
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(history->item(1)->data(Qt::UserRole + 1).toString(), QString("美しい"));
        QVERIFY(editor->isVisible());
        editor->setText(" 綺麗 ");
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(1)->data(Qt::UserRole + 1).toString(), QString("綺麗"));
        QCOMPARE(input->text(), QString("綺麗")); // Committing immediately refreshes results.
        QCOMPARE(history->currentRow(), 1);
        QCOMPARE(history->item(0)->data(Qt::UserRole + 1).toString(), QString("存在しないテスト語"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("美しい"));
        QTest::keyClick(history, Qt::Key_F2);
        editor = history->findChild<QLineEdit *>("historyEditor");
        QVERIFY(editor && editor->isVisible());
        editor->setText("キャンセルされる語");
        QTest::keyClick(editor, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(history->item(1)->data(Qt::UserRole + 1).toString(), QString("綺麗"));
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
        QCOMPARE(history->item(1)->data(Qt::UserRole + 1).toString(), QString("綺麗"));
        QVERIFY(!history->findChild<QLineEdit *>("historyEditor"));
        history->setFocus();
        QTest::keyClick(history, Qt::Key_Delete);
        QCOMPARE(history->count(), 1);
        QCOMPARE(input->text(), QString("綺麗")); // Deletion does not search either.
        window.findChild<QSplitter *>("splitter")->setSizes({350, 600});
        const auto state = window.findChild<QSplitter *>("splitter")->saveState();
        window.close();
        MainWindow restored(fixture_);
        QCOMPARE(restored.findChild<QListWidget *>("historyList")->item(0)->data(Qt::UserRole + 1).toString(), QString("存在しないテスト語"));
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
    void collapseExpand() {
        const auto path = QStringLiteral(RUIGO_SOURCE_DIR "/data/dictionary.sqlite");
        if (!QFile::exists(path)) QSKIP("Generate the real dictionary to run integration tests");
        MainWindow window(path);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
#ifdef Q_OS_WIN
        // Request the UIA root just as a Windows accessibility client does.
        SendMessageW(reinterpret_cast<HWND>(window.winId()), WM_GETOBJECT, 0, -25);
#endif
        if (!QAccessible::isActive()) QSKIP("Requires an active accessibility backend");
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *tree = window.findChild<QTreeWidget *>("results");
        for (const auto &word : {"花", "美しい", "bank", "good", "凡そ"}) {
            input->setText(QString::fromUtf8(word));
            QTest::keyClick(input, Qt::Key_Return);
            QVERIFY(tree->topLevelItemCount() > 0);
            QApplication::processEvents();
            auto *accessible = QAccessible::queryAccessibleInterface(tree);
            QVERIFY(accessible);
            const auto checkAccessible = [&] {
                for (int row = 0; row < accessible->childCount(); ++row) {
                    auto *child = accessible->child(row);
                    QVERIFY(child);
                    QCOMPARE(accessible->indexOfChild(child), row);
                }
            };
            checkAccessible();
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *group = tree->topLevelItem(i);
                tree->scrollToItem(group);
                QApplication::processEvents();
                for (int n = 0; n < 3; ++n) {
                    auto rect = tree->visualItemRect(group);
                    const QPoint arrow(rect.left() - tree->indentation() / 2, rect.center().y());
                    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
                    QVERIFY(!group->isExpanded());
                    checkAccessible();
                    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
                    QVERIFY(group->isExpanded());
                    QApplication::processEvents();
                    checkAccessible();
                }
            }
        }
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
        for (const auto &word : {"beautiful", "bank", "happy"}) {
            QElapsedTimer timer; timer.start();
            const auto english = repository.search(word);
            qInfo("English %s: %.3f ms, %d candidates", word, timer.nsecsElapsed() / 1000000.0, english.candidateCount());
            QVERIFY(english.error.isEmpty());
            QCOMPARE(english.language, QString("en"));
            QVERIFY(english.found);
            if (QString(word) != "beautiful") QVERIFY(english.candidateCount() > 0);
            else QVERIFY(english.relatedCount() > 0); // No same-meaning synonyms, but "similar" meanings exist.
            qInfo("English %s related: %d words in %d meanings", word, english.relatedCount(), int(english.relatedGroups.size()));
            for (const auto &group : english.groups) {
                QVERIFY(group.id.startsWith("oewn:2025:"));
                QVERIFY(!group.definition.isEmpty());
            }
        }
        const auto sad = repository.search("悲しい");
        const auto sadHeadword = repository.search("哀しい");
        QVERIFY(sad.found && sad.error.isEmpty());
        QCOMPARE(sad.groups.size(), sadHeadword.groups.size());
        QCOMPARE(sad.candidateCount(), sadHeadword.candidateCount());
        for (int i = 0; i < sad.groups.size(); ++i) {
            QCOMPARE(sad.groups[i].id, sadHeadword.groups[i].id);
            QCOMPARE(sad.groups[i].words, sadHeadword.groups[i].words);
        }
        QVERIFY(!repository.search("かなしい").found);
        QVERIFY(!repository.search("カナシイ").found);
        qInfo("Sad spelling alias: %d candidates in %d meanings", sad.candidateCount(), int(sad.groups.size()));
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
            checkTooltip("happy", "felicitous", "tooltip-english.png", false);
            QVERIFY(!QToolTip::text().contains("読み："));
            QVERIFY(window.grab().save("preview-english.png"));
        }
        for (const auto &word : {"AFGHAN", "beautiful", "存在しないzzzz", "ＡＩ"}) {
            input->setText(QString::fromUtf8(word));
            QTest::keyClick(input, Qt::Key_Return);
            QApplication::processEvents();
            const auto name = QString::fromUtf8(word) == "ＡＩ" ? QString("fullwidth") :
                QString::fromUtf8(word).startsWith("存在") ? QString("missing") : QString::fromUtf8(word);
            QVERIFY(window.grab().save("preview-" + name + ".png"));
        }
        window.close();
    }
};
QTEST_MAIN(Tests)
#include "Tests.moc"
