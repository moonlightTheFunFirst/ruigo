#include "DictionaryRepository.h"
#include "MainWindow.h"
#include <QPlainTextEdit>
#include <QDir>
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
                "PRAGMA user_version=5",
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
                "CREATE TABLE lemma_aliases(lemma_id INTEGER,language TEXT,word TEXT,kind TEXT DEFAULT 'spelling',PRIMARY KEY(lemma_id,kind,word))",
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
                "INSERT INTO lemma_aliases(lemma_id,language,word) VALUES (18,'ja','悲しい'),(18,'ja','美しい'),(18,'ja','SAD-JA'),(18,'ja','別''表記')",
                // "similar" relations: s1 -> similar1 (English words only, so Japanese searches on s1 gain nothing),
                // alias1 -> similar2 (Japanese), a self-relation on alias1, and a non-"similar" relation that must be ignored.
                "CREATE TABLE synset_relations(synset_id TEXT,target_id TEXT,rel_type TEXT,PRIMARY KEY(synset_id,rel_type,target_id))",
                "INSERT INTO lemmas(id,word,language,source,search_key) VALUES (22,'gorgeous','en','en-source','gorgeous'),(23,'lovely','en','en-source','lovely'),(24,'物悲しい','ja','ja-source','物悲しい'),(25,'淋しい','ja','ja-source','淋しい')",
                "INSERT INTO synsets VALUES ('similar1','en-source'),('similar2','ja-source')",
                "INSERT INTO senses VALUES ('similar1',22),('similar1',23),('similar2',24),('similar2',25)",
                "INSERT INTO synset_definitions VALUES ('similar1','en','very pleasing'),('similar2','ja','似た意味のテスト')",
                "INSERT INTO synset_relations VALUES ('s1','similar1','similar'),('alias1','similar2','similar'),('alias1','alias1','similar'),('alias1','s3','hypernym')",
                // Readings (ja). 綺麗 has two entries with the same reading: one headword, searched by entry.
                // きかい is shared by two different headwords, so it becomes a choice. べつよみ belongs only to
                // the unrelated 綺麗 homograph (entry 7).
                "INSERT INTO lemmas(id,word,language,source,search_key) VALUES (26,'機械','ja','ja-source','機械'),(27,'機会','ja','ja-source','機会'),(28,'装置','ja','ja-source','装置'),(29,'好機','ja','ja-source','好機')",
                "INSERT INTO synsets VALUES ('machine','ja-source'),('chance','ja-source')",
                "INSERT INTO senses VALUES ('machine',26),('machine',28),('chance',27),('chance',29)",
                "INSERT INTO synset_definitions VALUES ('machine','ja','動力で動く仕掛け'),('chance','ja','ちょうどよい時')",
                "INSERT INTO lemma_aliases VALUES (2,'ja','きれい','reading'),(2,'ja','キレイ','reading'),(3,'ja','きれい','reading'),(3,'ja','キレイ','reading'),(7,'ja','べつよみ','reading'),(18,'ja','かなしい','reading'),(18,'ja','カナシイ','reading'),(26,'ja','きかい','reading'),(27,'ja','きかい','reading')",
                // English: recorded irregular forms (went -> go; leaves -> leaf/leave), a regular form resolved by
                // rule (cats -> cat, noun only), and a headword that is also another word's form (saw -> see).
                "ALTER TABLE lemmas ADD COLUMN pos TEXT DEFAULT ''",
                "INSERT INTO lemmas(id,word,language,source,search_key,pos) VALUES (40,'go','en','en-source','go','v'),(41,'travel','en','en-source','travel','v'),(42,'cat','en','en-source','cat','n'),(43,'true cat','en','en-source','true cat','n'),(44,'saw','en','en-source','saw','n'),(45,'see','en','en-source','see','v'),(46,'understand','en','en-source','understand','v'),(47,'leaf','en','en-source','leaf','n'),(48,'leave','en','en-source','leave','n'),(49,'walk','en','en-source','walk','v'),(50,'stroll','en','en-source','stroll','v'),(51,'cat','en','en-source','cat','v')",
                "INSERT INTO synsets VALUES ('go1','en-source'),('cat1','en-source'),('saw1','en-source'),('see1','en-source'),('leaf1','en-source'),('leave1','en-source'),('walk1','en-source'),('vomit1','en-source')",
                "INSERT INTO senses VALUES ('go1',40),('go1',41),('cat1',42),('cat1',43),('saw1',44),('see1',45),('see1',46),('leaf1',47),('leave1',48),('walk1',49),('walk1',50),('vomit1',51)",
                "INSERT INTO synset_definitions VALUES ('go1','en','change location'),('cat1','en','feline mammal'),('see1','en','perceive by sight'),('leaf1','en','green part of a plant'),('leave1','en','permission to be absent')",
                "INSERT INTO lemma_aliases VALUES (40,'en','went','inflection'),(45,'en','saw','inflection'),(47,'en','leaves','inflection'),(48,'en','leaves','inflection')",
                // Same spelling, other part of speech, no forms registered: the noun "see" (39) and the verb
                // "leave" (38). Lower ids than the registered entries, so a lookup by word alone would pick
                // them first. seeN also has a "similar" relation that must not appear after choosing "see".
                "INSERT INTO lemmas(id,word,language,source,search_key,pos) VALUES (38,'leave','en','en-source','leave','v'),(39,'see','en','en-source','see','n'),(52,'depart','en','en-source','depart','v'),(53,'bishopric','en','en-source','bishopric','n'),(54,'diocese','en','en-source','diocese','n')",
                "INSERT INTO synsets VALUES ('leaveV','en-source'),('seeN','en-source'),('dioceseS','en-source')",
                "INSERT INTO senses VALUES ('leaveV',38),('leaveV',52),('seeN',39),('seeN',53),('dioceseS',54)",
                "INSERT INTO synset_definitions VALUES ('leaveV','en','go away'),('seeN','en','seat of a bishop'),('dioceseS','en','district of a bishop')",
                "INSERT INTO synset_relations VALUES ('seeN','dioceseS','similar')"
            };
            for (const auto &statement : sql)
                QVERIFY2(q.exec(statement), qPrintable(statement + "\n" + q.lastError().text()));
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
    void readings() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        // One headword: searched directly, by entry, and reported as a reading.
        const auto sad = repository.search("かなしい");
        QVERIFY(sad.found && sad.error.isEmpty());
        QCOMPARE(sad.language, QString("ja"));
        QCOMPARE(sad.matchKind, QString("reading"));
        QCOMPARE(sad.matchedWords, QStringList{"哀しい"});
        QCOMPARE(sad.groups.size(), 1);
        QCOMPARE(sad.groups[0].id, QString("alias1")); // Entry 18 only, not the 哀しい homograph (entry 20).
        QCOMPARE(repository.search("カナシイ").matchedWords, QStringList{"哀しい"});
        // Two entries with the same headword stay one result; the reading of an unrelated homograph does not leak.
        const auto pretty = repository.search("きれい");
        QCOMPARE(pretty.matchedWords, QStringList{"綺麗"});
        QVERIFY(pretty.spellings.isEmpty());
        const auto other = repository.search("べつよみ");
        QCOMPARE(other.matchedWords, QStringList{"綺麗"});
        QVERIFY(other.groups.isEmpty()); // Only entry 7 (alone in s5); entries 2/3 would bring s1's candidates.
        // Homophones: a choice of headwords with their first meaning, nothing merged.
        const auto kikai = repository.search("きかい");
        QVERIFY(kikai.found);
        QVERIFY(kikai.groups.isEmpty());
        QCOMPARE(kikai.choiceKind, QString("reading"));
        QCOMPARE(kikai.spellings, (QStringList{"機械", "機会"}));
        QCOMPARE(kikai.choiceHints, (QStringList{"動力で動く仕掛け", "ちょうどよい時"}));
        // Readings are a Japanese feature: a reading never matches in English.
        QVERIFY(!repository.search("かなしい", "en").found);
        QCOMPARE(repository.resolveLanguage("きかい"), QString("ja"));
        // UI: heading names the headword, and the choice list searches the chosen headword.
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        auto *tree = window.findChild<QTreeWidget *>("results");
        auto *choices = window.findChild<QListWidget *>("spellingChoices");
        input->setText("かなしい");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「かなしい」（「哀しい」の読み）の類語候補：1件"));
        input->setText("きかい");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「きかい」と読む見出しが2語あります"));
        QCOMPARE(choices->count(), 2);
        QCOMPARE(choices->item(0)->text(), QString("機械 — 動力で動く仕掛け"));
        QCOMPARE(tree->topLevelItemCount(), 0);
        // Keyboard: Down from the search box goes to the choices, Enter searches the row.
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        input->setFocus();
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(choices));
        QCOMPARE(choices->currentRow(), 0);
        QTest::keyClick(choices, Qt::Key_Return);
        QCOMPARE(input->text(), QString("機械"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("装置"));
        QCOMPARE(settings.value("history/entries").toStringList(), (QStringList{"ja:機械", "ja:きかい", "ja:かなしい"}));
        settings.setValue("history/entries", QStringList{});
    }
    void inflections() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        // Recorded irregular form.
        const auto went = repository.search("went");
        QCOMPARE(went.language, QString("en"));
        QCOMPARE(went.matchKind, QString("inflection"));
        QCOMPARE(went.matchedWords, QStringList{"go"});
        QCOMPARE(went.groups[0].words, QStringList{"travel"});
        QCOMPARE(repository.search("Went").matchedWords, QStringList{"go"}); // Case-folded.
        // Regular rule, accepted only for a headword of the matching part of speech:
        // "cats" -> cat (noun) and cat (verb) both qualify through the noun and verb "-s" rules.
        const auto cats = repository.search("cats");
        QCOMPARE(cats.matchKind, QString("inflection"));
        QCOMPARE(cats.matchedWords, QStringList{"cat"});
        QCOMPARE(cats.groups.size(), 1); // The verb entry's synset has no other members, so only cat1 shows.
        QCOMPARE(cats.groups[0].words, QStringList{"true cat"});
        QCOMPARE(repository.search("walked").matchedWords, QStringList{"walk"});
        QCOMPARE(repository.search("walking").matchedWords, QStringList{"walk"});
        QVERIFY(!repository.search("walker").found); // "-er" is an adjective rule; walk is a verb.
        QVERIFY(!repository.search("zzzs").found);
        // A form of several different headwords becomes a choice.
        const auto leaves = repository.search("leaves");
        QCOMPARE(leaves.choiceKind, QString("inflection"));
        QCOMPARE(leaves.spellings, (QStringList{"leaf", "leave"}));
        // A headword wins over a form, and the form is offered separately.
        const auto saw = repository.search("saw");
        QVERIFY(saw.matchKind.isEmpty());
        QCOMPARE(saw.formOf, QStringList{"see"});
        QCOMPARE(saw.formOfKind, QString("inflection"));
        QCOMPARE(saw.formOfHints, QStringList{"perceive by sight"});
        // Inflections are English only.
        QVERIFY(!repository.search("went", "ja").found);
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        auto *button = window.findChild<QPushButton *>("formOfButton");
        auto *choices = window.findChild<QListWidget *>("spellingChoices");
        input->setText("went");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「went」（「go」の活用形）の類語候補：1件"));
        QVERIFY(button->isHidden());
        input->setText("leaves");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(heading->text().contains("「leaves」は「leaf」「leave」の活用形です"));
        QCOMPARE(choices->item(1)->text(), QString("leave — permission to be absent"));
        input->setText("saw");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!button->isHidden());
        QCOMPARE(button->text(), QString("「see」の活用形としても検索"));
        button->click();
        QCOMPARE(input->text(), QString("see"));
        QVERIFY(heading->text().contains("「see」の類語候補：1件"));
        settings.setValue("history/entries", QStringList{});
    }
    void matchedEntriesAfterChoice() {
        DictionaryRepository repository;
        QVERIFY(repository.open(fixture_));
        const auto groupIds = [](const QVector<MeaningGroup> &groups) {
            QStringList ids;
            for (const auto &group : groups) ids.append(group.id);
            return ids;
        };
        // A plain headword search still covers every entry of "see" (noun and verb).
        QCOMPARE(groupIds(repository.search("see").groups), (QStringList{"see1", "seeN"}));
        // "saw" is registered as a form of the verb "see" only (entry 45).
        for (const auto &input : {"saw", "Saw", "SAW"}) {
            const auto saw = repository.search(input);
            QVERIFY(saw.found && saw.error.isEmpty());
            QCOMPARE(saw.formOf, QStringList{"see"});
            QCOMPARE(saw.formOfKind, QString("inflection"));
            QCOMPARE(saw.formOfEntries, (QVector<QVector<qint64>>{{45}}));
            QCOMPARE(saw.formOfHints, QStringList{"perceive by sight"}); // From entry 45, not the noun 39.
            // The original search shows "saw" itself, never the meanings of "see".
            QVERIFY(!groupIds(saw.groups).contains("see1") && !groupIds(saw.groups).contains("seeN"));
            QVERIFY(saw.relatedGroups.isEmpty());
        }
        QVERIFY(repository.search("saw").matchedWords.isEmpty());
        QCOMPARE(repository.search("Saw").matchedWords, QStringList{"saw"}); // Case-folded display kept.
        QCOMPARE(repository.search("SAW").matchKind, QString("normalized"));
        // Searching the matched entry only: no noun meaning, synonym or related word.
        const auto see = repository.searchEntries("see", "en", {45});
        QVERIFY(see.found && see.error.isEmpty());
        QCOMPARE(groupIds(see.groups), QStringList{"see1"});
        QCOMPARE(see.groups[0].words, QStringList{"understand"});
        QVERIFY(see.relatedGroups.isEmpty());
        QVERIFY(see.matchedWords.isEmpty());
        // Ids that are not this headword in this language are ignored, never widened.
        QVERIFY(!repository.searchEntries("see", "en", {40}).found);
        QVERIFY(!repository.searchEntries("see", "ja", {45}).found);
        // Several bases: each choice keeps its own entries and hint.
        const auto leaves = repository.search("leaves");
        QCOMPARE(leaves.spellings, (QStringList{"leaf", "leave"}));
        QCOMPARE(leaves.choiceEntries, (QVector<QVector<qint64>>{{47}, {48}}));
        QCOMPARE(leaves.choiceHints, (QStringList{"green part of a plant", "permission to be absent"}));
        QCOMPARE(groupIds(repository.searchEntries("leave", "en", {48}).groups), QStringList{});
        QCOMPARE(groupIds(repository.search("leave").groups), QStringList{"leaveV"}); // The verb has "depart".
        // Readings keep their entries too; case choices stay plain headword searches.
        QCOMPARE(repository.search("きかい").choiceEntries, (QVector<QVector<qint64>>{{26}, {27}}));
        const auto caseChoice = repository.search("Us");
        QCOMPARE(caseChoice.choiceKind, QString("case"));
        QCOMPARE(caseChoice.choiceEntries, (QVector<QVector<qint64>>{}));

        // UI: the single form-of button and the choice list both stay on the matched entries.
        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(fixture_);
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        auto *tree = window.findChild<QTreeWidget *>("results");
        auto *button = window.findChild<QPushButton *>("formOfButton");
        auto *choices = window.findChild<QListWidget *>("spellingChoices");
        const auto treeText = [tree] {
            QStringList lines;
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                lines.append(tree->topLevelItem(i)->text(0));
                for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                    lines.append(tree->topLevelItem(i)->child(j)->text(0));
            }
            return lines.join('\n');
        };
        for (const auto &typed : {"saw", "SAW"}) {
            settings.setValue("history/entries", QStringList{});
            input->setText(typed);
            QTest::keyClick(input, Qt::Key_Return);
            QVERIFY(!button->isHidden());
            QCOMPARE(button->text(), QString("「see」の活用形としても検索"));
            button->click();
            QCOMPARE(input->text(), QString("see"));
            QVERIFY(heading->text().contains("— 「see」の類語候補：1件（1意味）"));
            QVERIFY(!heading->text().contains("関連語"));
            QVERIFY(treeText().contains("understand"));
            QVERIFY(!treeText().contains("bishop")); // No noun meaning, synonym or related word.
            QVERIFY(!treeText().contains("diocese"));
            QCOMPARE(settings.value("history/entries").toStringList(),
                     (QStringList{"en:see", "en:" + QString::fromUtf8(typed)}));
        }
        input->setText("leaves");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(choices->item(1)->text(), QString("leave — permission to be absent"));
        emit choices->itemActivated(choices->item(1));
        QCOMPARE(input->text(), QString("leave"));
        QVERIFY(!treeText().contains("depart")); // The verb "leave" is not searched.
        QVERIFY(heading->text().contains("— 「leave」の類語候補：0件"));
        QCOMPARE(settings.value("history/entries").toStringList().first(), QString("en:leave"));
        // Re-searching from history is a plain headword search (the saved format is unchanged).
        QVERIFY(QStringList(settings.value("history/entries").toStringList()).contains("en:leaves"));
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
    void unavailableKaikki() {
        const auto oldPath = temporary_.filePath("old-kaikki.sqlite");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "old-kaikki-fixture");
            db.setDatabaseName(oldPath);
            QVERIFY(db.open());
            QVERIFY(QSqlQuery(db).exec("PRAGMA user_version=1"));
        }
        QSqlDatabase::removeDatabase("old-kaikki-fixture");
        KaikkiRepository old;
        QVERIFY(old.open(oldPath).contains(QStringLiteral("再生成")));
        QVERIFY(old.search({"bank"}, "en").isEmpty());
        const auto brokenPath = temporary_.filePath("broken-kaikki.sqlite");
        QFile brokenFile(brokenPath);
        QVERIFY(brokenFile.open(QIODevice::WriteOnly));
        QVERIFY(brokenFile.write("not SQLite") > 0);
        brokenFile.close();
        KaikkiRepository broken, missing;
        QVERIFY(!broken.open(brokenPath).isEmpty());
        QVERIFY(broken.search({"bank"}, "en").isEmpty());
        QVERIFY(missing.open(temporary_.filePath("missing-kaikki.sqlite")).isEmpty());
        QVERIFY(missing.search({"bank"}, "en").isEmpty());
    }
    void kaikkiSupplement() {
        // A WordNet fixture copy with a small format-2 Kaikki DB beside it, as the app expects.
        QDir(temporary_.path()).mkpath("kaikki-fixture");
        const auto folder = temporary_.filePath("kaikki-fixture/");
        QVERIFY(QFile::copy(fixture_, folder + "dictionary.sqlite"));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "kaikki-wordnet-fixture");
            db.setDatabaseName(folder + "dictionary.sqlite");
            QVERIFY(db.open());
            // うつくしい is a reading of the WordNet headword 美しい (entry 1).
            QVERIFY(QSqlQuery(db).exec("INSERT INTO lemma_aliases VALUES (1,'ja','うつくしい','reading')"));
        }
        QSqlDatabase::removeDatabase("kaikki-wordnet-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "kaikki-fixture");
            db.setDatabaseName(folder + "kaikki.sqlite");
            QVERIFY(db.open());
            QSqlQuery q(db);
            const QStringList sql = {
                "PRAGMA user_version=2",
                "CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
                "CREATE TABLE entries(id INTEGER PRIMARY KEY, source_line INTEGER, word TEXT, language TEXT, pos TEXT, forms_json TEXT DEFAULT '[]')",
                "CREATE TABLE senses(id INTEGER PRIMARY KEY, entry_id INTEGER, source_position INTEGER, glosses_json TEXT, gloss_language TEXT)",
                "CREATE TABLE relations(id INTEGER PRIMARY KEY, entry_id INTEGER, sense_id INTEGER, kind TEXT, target_word TEXT, target_language TEXT, source_label TEXT)",
                "CREATE TABLE spelling_references(sense_id INTEGER, target_entry_id INTEGER, PRIMARY KEY(sense_id, target_entry_id))",
                // 美しい only refers to one kana entry; うつくしい also has an empty gloss to skip.
                // 箸 refers to two kana entries はし (homographs), which must not be merged.
                "INSERT INTO entries(id,source_line,word,language,pos) VALUES "
                "(1,1,'美しい','ja','character'),(2,2,'うつくしい','ja','adj'),(3,3,'箸','ja','character'),"
                "(4,4,'はし','ja','noun'),(5,5,'はし','ja','noun'),(6,6,'フィクスチャ語','ja','noun'),"
                "(7,7,'beautiful','en','adj')",
                R"(INSERT INTO senses VALUES (1,1,1,'["うつくしいの漢字表記。"]','ja'),)"
                R"((2,2,1,'["見て感動を与える様。"]','ja'),(3,2,2,'[]','ja'),)"
                R"((4,3,1,'["「はし」の漢字表記。"]','ja'),(5,4,1,'["川を渡る構造物。"]','ja'),)"
                R"((6,5,1,'["食事に使う二本の棒。"]','ja'),(7,6,1,'["テスト専用の語。"]','ja'),)"
                R"((8,7,1,'["美しい。"]','ja'))",
                "INSERT INTO spelling_references VALUES (1,2),(4,4),(4,5)",
                "INSERT INTO relations(entry_id,sense_id,kind,target_word,target_language) VALUES (6,7,'synonym','試験語','ja')",
                // 綺麗 is a WordNet candidate of 美しい; 麗しい (the other candidate) has no Kaikki entry.
                "INSERT INTO entries(id,source_line,word,language,pos) VALUES (8,8,'綺麗','ja','adj')",
                R"(INSERT INTO senses VALUES (9,8,1,'["整っていて美しい様。"]','ja'))",
            };
            for (const auto &statement : sql)
                QVERIFY2(q.exec(statement), qPrintable(statement + "\n" + q.lastError().text()));
        }
        QSqlDatabase::removeDatabase("kaikki-fixture");

        QSettings settings;
        settings.setValue("history/entries", QStringList{});
        MainWindow window(folder + "dictionary.sqlite");
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *heading = window.findChild<QLabel *>("resultHeading");
        auto *panel = window.findChild<QPlainTextEdit *>("kaikkiSupplement");
        QVERIFY(input && heading && panel);
        const auto searchFor = [&](const QString &word) {
            input->setText(word);
            QTest::keyClick(input, Qt::Key_Return);
            return panel->toPlainText();
        };

        // One referenced kana entry: the pointer sense is replaced by a link, and the
        // target entry follows with its route. The empty-gloss sense is not shown.
        auto text = searchFor("美しい");
        QVERIFY(!panel->isHidden());
        QVERIFY(text.startsWith("Wiktionary / Kaikki"));
        QVERIFY(text.contains("→ うつくしい の項目"));
        QVERIFY(!text.contains("うつくしいの漢字表記"));
        QVERIFY(text.contains("うつくしい [adj]（美しいから参照）"));
        QCOMPARE(text.count("見て感動を与える様。"), 1);
        QVERIFY(!text.contains("[日本語] \n") && !text.endsWith("[日本語] "));
        QVERIFY(!heading->text().contains("WordNetには見出しがありません")); // WordNet has 美しい.

        // Selecting a candidate switches the panel to that word, without searching again
        // or touching history; a meaning heading goes back to the search word.
        auto *tree = window.findChild<QTreeWidget *>("results");
        auto *history = window.findChild<QListWidget *>("historyList");
        const auto historyCount = history->count();
        auto *pretty = tree->topLevelItem(0)->child(0);
        QCOMPARE(pretty->data(0, Qt::UserRole).toString(), QString("綺麗"));
        tree->setCurrentItem(pretty);
        text = panel->toPlainText();
        QVERIFY(text.startsWith("Wiktionary / Kaikki — 選択中の候補「綺麗」"));
        QVERIFY(text.contains("整っていて美しい様。"));
        QVERIFY(!text.contains("うつくしい [adj]"));
        QCOMPARE(input->text(), QString("美しい"));
        QCOMPARE(history->count(), historyCount);
        QVERIFY(heading->text().contains("「美しい」の類語候補"));
        auto *noEntry = tree->topLevelItem(1)->child(0); // 麗しい: no Kaikki entry.
        tree->setCurrentItem(noEntry);
        QVERIFY(!panel->isHidden());
        QVERIFY(panel->toPlainText().contains("選択中の候補「麗しい」"));
        QVERIFY(panel->toPlainText().contains("この候補の項目はありません。"));
        tree->setCurrentItem(tree->topLevelItem(0)); // A meaning heading: back to the search word.
        QVERIFY(panel->toPlainText().startsWith("Wiktionary / Kaikki\n"));
        QVERIFY(panel->toPlainText().contains("→ うつくしい の項目"));
        // Keyboard: Down from the search box selects the first candidate, which updates the panel too.
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        input->setFocus();
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(tree->currentItem(), pretty);
        QVERIFY(panel->toPlainText().contains("選択中の候補「綺麗」"));
        // A new search starts from the search word's entries again.
        searchFor("美しい");
        QVERIFY(panel->toPlainText().startsWith("Wiktionary / Kaikki\n"));

        // Kana input resolved by WordNet to 美しい: うつくしい is reached directly and through
        // 美しい's reference, but is shown once (deduplicated by entry id) with both routes.
        text = searchFor("うつくしい");
        QCOMPARE(text.count("うつくしい [adj]"), 1);
        QVERIFY(text.contains("うつくしい [adj]（うつくしい、美しいから参照）"));
        QVERIFY(text.contains("美しい [character]"));

        // Several referenced kana entries: the original gloss stays, with one preview per
        // entry, and the homographs are not merged into one entry.
        text = searchFor("箸");
        QVERIFY(text.contains("「はし」の漢字表記。"));
        QVERIFY(text.contains("→ はし [noun] [日本語] 川を渡る構造物。"));
        QVERIFY(text.contains("→ はし [noun] [日本語] 食事に使う二本の棒。"));
        QVERIFY(!text.contains("（箸から参照）"));
        QVERIFY(heading->text().contains("WordNetには見出しがありません"));

        // Kaikki-only headword with a sense-linked synonym.
        text = searchFor("フィクスチャ語");
        QVERIFY(text.contains("フィクスチャ語 [noun]（フィクスチャ語）"));
        QVERIFY(text.contains("テスト専用の語。"));
        QVERIFY(text.contains("類語：試験語"));
        QVERIFY(heading->text().contains("WordNetには見出しがありません"));

        // English headword with a Japanese gloss; the WordNet result stays as before.
        text = searchFor("beautiful");
        QVERIFY(text.contains("beautiful [adj]"));
        QVERIFY(text.contains("[日本語] 美しい。"));
        QVERIFY(heading->text().contains("英語で検索"));

        // In neither dictionary: no panel, the usual message.
        searchFor("どちらにもない語");
        QVERIFY(panel->isHidden());
        QVERIFY(heading->text().contains("見出しが見つかりませんでした"));

        // Without kaikki.sqlite beside the WordNet DB, the panel never appears.
        MainWindow plain(fixture_);
        auto *plainInput = plain.findChild<QLineEdit *>("searchInput");
        plainInput->setText("美しい");
        QTest::keyClick(plainInput, Qt::Key_Return);
        QVERIFY(plain.findChild<QPlainTextEdit *>("kaikkiSupplement")->isHidden());
        auto *plainTree = plain.findChild<QTreeWidget *>("results");
        plainTree->setCurrentItem(plainTree->topLevelItem(0)->child(0)); // Selecting does not show it either.
        QVERIFY(plain.findChild<QPlainTextEdit *>("kaikkiSupplement")->isHidden());
        settings.setValue("history/entries", QStringList{});
    }
    void realKaikki() {
        const QString folder = QStringLiteral(RUIGO_SOURCE_DIR "/data/");
        if (!QFileInfo::exists(folder + "kaikki.sqlite")) QSKIP("Optional Kaikki DB is not generated");
        KaikkiRepository repository;
        QVERIFY(repository.open(folder + "kaikki.sqlite").isEmpty());
        const auto run = repository.search({QStringLiteral("走る")}, "ja");
        QVERIFY(!run.isEmpty());
        QVERIFY(!run.first().senses.isEmpty());
        QVERIFY(run.first().senses.first().targets.isEmpty());
        const auto beautiful = repository.search({QStringLiteral("美しい")}, "ja");
        QVERIFY(!beautiful.isEmpty());
        QVERIFY(!beautiful.first().senses.isEmpty());
        QVERIFY(!beautiful.first().senses.first().targets.isEmpty());
        QCOMPARE(repository.entry(beautiful.first().senses.first().targets.first(), "ja").word,
                 QStringLiteral("うつくしい"));

        MainWindow window(folder + "dictionary.sqlite");
        auto *input = window.findChild<QLineEdit *>("searchInput");
        auto *panel = window.findChild<QPlainTextEdit *>("kaikkiSupplement");
        QVERIFY(input && panel);
        input->setText(QStringLiteral("はしる"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(panel->toPlainText().contains(QStringLiteral("Wiktionary / Kaikki")));
        QVERIFY(panel->toPlainText().contains(QStringLiteral("はしる")));
        QVERIFY(panel->toPlainText().contains(QStringLiteral("走る")));
        input->setText(QStringLiteral("走る"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(panel->toPlainText().contains(QStringLiteral("走る")));
        QVERIFY(!panel->toPlainText().contains(QStringLiteral("はしる [")));
        input->setText(QStringLiteral("こう"));
        QTest::keyClick(input, Qt::Key_Return);
        auto *choices = window.findChild<QListWidget *>("spellingChoices");
        QVERIFY(choices && choices->count() > 1);
        QVERIFY(panel->toPlainText().contains(QStringLiteral("こう [")));
        emit choices->itemActivated(choices->item(0));
        QVERIFY(input->text() != QStringLiteral("こう"));
        QVERIFY(panel->toPlainText().contains(QStringLiteral("こう [")));
        input->setText(QStringLiteral("ウィキクォート"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(panel->toPlainText().contains(QStringLiteral("ウィキクォート")));
        QVERIFY(window.findChild<QLabel *>("resultHeading")->text().contains(
            QStringLiteral("WordNetには見出しがありません")));
        input->setText(QStringLiteral("zzznosuchentryzzzz"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(panel->isHidden());
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
        qInfo("Sad spelling alias: %d candidates in %d meanings", sad.candidateCount(), int(sad.groups.size()));
        // Readings: one headword is searched directly; homophones become a choice.
        const auto sadReading = repository.search("かなしい");
        QCOMPARE(sadReading.matchKind, QString("reading"));
        QCOMPARE(sadReading.matchedWords, QStringList{"哀しい"});
        QCOMPARE(sadReading.candidateCount(), sadHeadword.candidateCount());
        const auto kikai = repository.search("きかい");
        QCOMPARE(kikai.choiceKind, QString("reading"));
        QVERIFY(kikai.spellings.contains("機械") && kikai.spellings.contains("機会"));
        QCOMPARE(kikai.choiceHints.size(), kikai.spellings.size());
        QVERIFY(!kikai.choiceHints.contains(QString()));
        QCOMPARE(repository.search("綺麗").groups.size(), repository.search("きれい").groups.size());
        // English inflections: recorded irregular forms, regular rules, choices, and headword-first.
        QCOMPARE(repository.search("went").matchedWords, QStringList{"go"});
        QCOMPARE(repository.search("children").matchedWords, QStringList{"child"});
        QCOMPARE(repository.search("happier").matchedWords, QStringList{"happy"});
        QCOMPARE(repository.search("cats").matchedWords, QStringList{"cat"});
        QCOMPARE(repository.search("walked").matchedWords, QStringList{"walk"});
        QCOMPARE(repository.search("studies").matchedWords, QStringList{"study"});
        QCOMPARE(repository.search("leaves").choiceKind, QString("inflection"));
        const auto saw = repository.search("saw");
        QVERIFY(saw.matchKind.isEmpty());
        QVERIFY(saw.formOf.contains("see"));
        // "saw" is a form of the verb "see" only. Choosing it must not bring in the noun "see"
        // (a bishop's seat), which a plain headword search of "see" does include.
        for (const auto &typed : {"Saw", "SAW"}) {
            const auto folded = repository.search(typed);
            QCOMPARE(folded.formOf, saw.formOf);
            QCOMPARE(folded.formOfEntries, saw.formOfEntries);
        }
        const auto seeVerb = repository.searchEntries("see", "en", saw.formOfEntries.value(saw.formOf.indexOf("see")));
        QVERIFY(seeVerb.found && !seeVerb.groups.isEmpty());
        for (const auto &group : seeVerb.groups) QVERIFY2(group.id.endsWith("-v"), qPrintable(group.id));
        for (const auto &group : seeVerb.relatedGroups) QVERIFY2(group.id.endsWith("-v"), qPrintable(group.id));
        // The noun "see" has the lower entry id, so a lookup by word alone would describe the
        // choice with the bishop's-seat meaning. The hint must come from the verb entry.
        const auto seeHint = saw.formOfHints.value(saw.formOf.indexOf("see"));
        QVERIFY2(!seeHint.isEmpty() && !seeHint.contains("bishop"), qPrintable(seeHint));
        qInfo("see from saw: %d meanings (verb entry only), hint: %s", int(seeVerb.groups.size()), qPrintable(seeHint));
        for (const auto &word : {"かなしい", "きかい", "こう", "went", "cats", "studies"}) {
            QElapsedTimer timer; timer.start();
            const auto result = repository.search(QString::fromUtf8(word));
            qInfo("Form/reading %s: %.3f ms, kind=%s, headwords=%s", word, timer.nsecsElapsed() / 1000000.0,
                  qPrintable(result.matchKind.isEmpty() ? result.choiceKind : result.matchKind),
                  qPrintable((result.matchedWords.isEmpty() ? result.spellings : result.matchedWords).join(',')));
        }
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
        const QList<QPair<const char *, QString>> previews = {
            {"AFGHAN", "AFGHAN"}, {"beautiful", "beautiful"}, {"存在しないzzzz", "missing"}, {"ＡＩ", "fullwidth"},
            {"きかい", "reading-choice"}, {"かなしい", "reading"}, {"went", "inflection"}, {"saw", "form-of"}};
        for (const auto &[word, name] : previews) {
            input->setText(QString::fromUtf8(word));
            QTest::keyClick(input, Qt::Key_Return);
            QApplication::processEvents();
            QVERIFY(window.grab().save("preview-" + name + ".png"));
        }
        window.close();
    }
};
QTEST_MAIN(Tests)
#include "Tests.moc"
