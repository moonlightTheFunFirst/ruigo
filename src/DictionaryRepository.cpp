#include "DictionaryRepository.h"
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

int SearchResult::candidateCount() const {
    int count = 0;
    for (const auto &group : groups) count += group.words.size();
    return count;
}
DictionaryRepository::DictionaryRepository()
    : db_(QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString())) {}
DictionaryRepository::~DictionaryRepository() {
    const auto name = db_.connectionName();
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}
bool DictionaryRepository::open(const QString &path) {
    db_.close();
    error_.clear();
    if (!QFileInfo(path).isFile()) {
        error_ = QStringLiteral("辞書DBが見つかりません。\n%1").arg(path);
        return false;
    }
    db_.setConnectOptions("QSQLITE_OPEN_READONLY");
    db_.setDatabaseName(path);
    if (!db_.open()) {
        error_ = QStringLiteral("辞書DBを開けません。\n%1").arg(db_.lastError().text());
        return false;
    }
    QSqlQuery check(db_);
    if (!check.exec("PRAGMA user_version") || !check.next() || check.value(0).toInt() != 1) {
        error_ = QStringLiteral("辞書DBの形式が未対応、または破損しています。");
    } else if (!check.exec("SELECT l.word,l.language,l.forms_json,s.id,s.source,d.definition,d.language,se.lemma_id "
                           "FROM lemmas l JOIN senses se ON se.lemma_id=l.id "
                           "JOIN synsets s ON s.id=se.synset_id "
                           "LEFT JOIN synset_definitions d ON d.synset_id=s.id LIMIT 0")) {
        error_ = QStringLiteral("辞書DBの構造が不正です。\n%1").arg(check.lastError().text());
    }
    check.finish();
    if (!error_.isEmpty()) db_.close();
    return error_.isEmpty();
}
SearchResult DictionaryRepository::search(const QString &input, const QString &language) const {
    SearchResult result;
    const auto word = input.trimmed();
    if (word.isEmpty()) return result;
    if (!db_.isOpen()) {
        result.error = error_.isEmpty() ? QStringLiteral("辞書DBが開かれていません。") : error_;
        return result;
    }
    QSqlQuery query(db_);
    query.prepare("SELECT DISTINCT s.id, COALESCE(d.definition,''), candidate.word "
        "FROM lemmas matched JOIN senses hit ON hit.lemma_id=matched.id "
        "JOIN synsets s ON s.id=hit.synset_id "
        "JOIN senses member ON member.synset_id=s.id "
        "JOIN lemmas candidate ON candidate.id=member.lemma_id "
        "LEFT JOIN synset_definitions d ON d.synset_id=s.id AND d.language=:language "
        "WHERE matched.language=:language AND matched.word=:word "
        "AND candidate.language=:language AND candidate.word<>:word "
        "ORDER BY s.id, candidate.word");
    query.bindValue(":language", language);
    query.bindValue(":word", word);
    if (!query.exec()) {
        result.error = QStringLiteral("辞書の検索に失敗しました。\n%1").arg(query.lastError().text());
        return result;
    }
    while (query.next()) {
        const auto id = query.value(0).toString();
        if (result.groups.isEmpty() || result.groups.last().id != id)
            result.groups.append({id, query.value(1).toString(), {}});
        result.groups.last().words.append(query.value(2).toString());
    }
    if (query.lastError().isValid()) {
        result.groups.clear();
        result.error = query.lastError().text();
    }
    return result;
}
WordDetails DictionaryRepository::wordDetails(const QString &word, const QString &language,
                                               const QString &currentSynset) const {
    WordDetails result;
    if (!db_.isOpen()) {
        result.error = error_.isEmpty() ? QStringLiteral("辞書DBが開かれていません。") : error_;
        return result;
    }
    QSqlQuery query(db_);
    // Keep readings and other senses tied to entries belonging to this meaning,
    // rather than mixing unrelated homographs with the same spelling.
    query.prepare("SELECT DISTINCT l.forms_json, s.synset_id, COALESCE(d.definition,'') "
        "FROM lemmas l JOIN senses s ON s.lemma_id=l.id "
        "LEFT JOIN synset_definitions d ON d.synset_id=s.synset_id AND d.language=:language "
        "WHERE l.word=:word AND l.language=:language "
        "AND EXISTS (SELECT 1 FROM senses current WHERE current.lemma_id=l.id "
        "AND current.synset_id=:current) ORDER BY s.synset_id,l.id");
    query.bindValue(":word", word);
    query.bindValue(":language", language);
    query.bindValue(":current", currentSynset);
    if (!query.exec()) {
        result.error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        QStringList hiragana, katakana;
        const auto forms = QJsonDocument::fromJson(query.value(0).toString().toUtf8()).array();
        for (const auto &form : forms) {
            const auto object = form.toObject();
            const auto reading = object.value("writtenForm").toString().trimmed();
            if (reading.isEmpty()) continue;
            const auto script = object.value("script").toString();
            if (script == "hira") hiragana.append(reading);
            else if (script == "kana") katakana.append(reading);
        }
        const auto readings = hiragana.isEmpty() ? katakana : hiragana;
        for (const auto &reading : readings)
            if (!result.readings.contains(reading)) result.readings.append(reading);
        if (query.value(1).toString() != currentSynset) {
            const auto definition = query.value(2).toString().trimmed();
            const auto id = query.value(1).toString();
            if (result.otherMeanings.isEmpty() || result.otherMeanings.last().id != id)
                result.otherMeanings.append({id, definition});
        }
    }
    if (query.lastError().isValid()) result.error = query.lastError().text();
    return result;
}
