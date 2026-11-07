#include "JmdictRepository.h"
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QUuid>
#include <QVariant>

JmdictRepository::~JmdictRepository() {
    if (!db_.isValid()) return;
    const auto name = db_.connectionName();
    db_ = {};
    QSqlDatabase::removeDatabase(name);
}

void JmdictRepository::open(const QString &path) {
    if (!QFileInfo(path).isFile()) {
        error_ = QStringLiteral("JMdict辞書が見つかりません。%1").arg(path);
        return;
    }
    db_ = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    db_.setConnectOptions("QSQLITE_OPEN_READONLY");
    db_.setDatabaseName(path);
    if (db_.open()) {
        QSqlQuery check(db_);
        if (check.exec("PRAGMA user_version") && check.next() && check.value(0).toInt() == 2 &&
            check.exec("SELECT s.entry,s.position,s.pos,f.word,f.reading,g.text,g.exact,g.folded,g.bare_verb "
                       "FROM senses s JOIN forms f ON f.sense=s.id JOIN glosses g ON g.sense=s.id LIMIT 0"))
            return;
    }
    error_ = QStringLiteral("JMdict辞書を読み込めませんでした。辞書DBを再生成してください。");
}

TranslationResult JmdictRepository::search(const QString &input, const QString &language) const {
    if (!error_.isEmpty()) return {{}, error_};
    const auto word = input.trimmed();
    if (word.isEmpty()) return {};
    const auto run = [&](const QString &filter, const QString &key) {
        TranslationResult result;
        QSqlQuery query(db_);
        query.prepare("SELECT s.id,s.pos,f.word,f.reading,g.text FROM senses s "
                      "JOIN forms f ON f.sense=s.id JOIN glosses g ON g.sense=s.id WHERE " + filter +
                      " ORDER BY s.entry,s.position,s.id,g.position,f.word,f.reading");
        query.bindValue(":key", key);
        if (!query.exec()) return TranslationResult{{}, query.lastError().text()};
        while (query.next()) {
            const auto id = query.value(0).toLongLong();
            if (result.senses.isEmpty() || result.senses.last().id != id)
                result.senses.append({id, query.value(1).toString(), {}, {}, {}, {}});
            auto &sense = result.senses.last();
            const auto append = [](QStringList &list, const QString &value) {
                if (!list.contains(value)) list.append(value);
            };
            append(sense.words, query.value(2).toString());
            append(sense.readings, query.value(3).toString());
            append(sense.readingsByWord[query.value(2).toString()], query.value(3).toString());
            append(sense.glosses, query.value(4).toString());
        }
        if (query.lastError().isValid()) return TranslationResult{{}, query.lastError().text()};
        return result;
    };
    if (language == "ja") {
        const auto match = QStringLiteral("s.id IN (SELECT sense FROM forms WHERE word=:key OR reading=:key)");
        auto result = run(match, word);
        if (!result.senses.isEmpty() || !result.error.isEmpty()) return result;
        auto fullWidth = word;
        for (auto &character : fullWidth) {
            const auto code = character.unicode();
            if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') ||
                (code >= 'a' && code <= 'z'))
                character = QChar(code + 0xfee0);
        }
        return fullWidth == word ? result : run(match, fullWidth);
    }
    const auto exact = word.normalized(QString::NormalizationForm_C);
    auto result = run("s.id IN (SELECT sense FROM glosses WHERE exact=:key)", exact);
    if (result.senses.isEmpty() && result.error.isEmpty())
        result = run("s.id IN (SELECT sense FROM glosses WHERE folded=:key)", exact.toLower());
    if (!result.error.isEmpty()) return result;
    // Bare verbs supplement the primary matches so noun glosses do not hide verb senses.
    const auto verbs = run("s.id IN (SELECT sense FROM glosses WHERE bare_verb=:key)", exact.toLower());
    if (!verbs.error.isEmpty()) return verbs;
    QSet<qint64> included;
    for (const auto &sense : result.senses) included.insert(sense.id);
    for (const auto &sense : verbs.senses)
        if (!included.contains(sense.id)) result.senses.append(sense);
    return result;
}
