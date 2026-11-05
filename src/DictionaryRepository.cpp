#include "DictionaryRepository.h"
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>

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
    } else if (!check.exec("SELECT l.word,l.language,s.id,s.source,d.definition,d.language,se.lemma_id "
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
