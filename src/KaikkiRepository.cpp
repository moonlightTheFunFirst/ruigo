#include "KaikkiRepository.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

KaikkiRepository::~KaikkiRepository() {
    if (!db_.isValid()) return;
    const auto name = db_.connectionName();
    db_ = {};
    QSqlDatabase::removeDatabase(name);
}

QString KaikkiRepository::open(const QString &path) {
    if (!QFileInfo::exists(path)) return {};
    db_ = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    db_.setConnectOptions("QSQLITE_OPEN_READONLY");
    db_.setDatabaseName(path);
    if (db_.open()) {
        QSqlQuery query(db_);
        if (query.exec("PRAGMA user_version") && query.next()) {
            const auto version = query.value(0).toInt();
            if (version == 2
                && query.exec("SELECT e.id,e.word,e.pos,e.language,s.glosses_json,s.gloss_language,"
                              "r.target_entry_id,l.kind,l.target_word,l.target_language,l.sense_id FROM entries e "
                              "JOIN senses s ON s.entry_id=e.id JOIN spelling_references r ON r.sense_id=s.id "
                              "JOIN relations l ON l.entry_id=e.id LIMIT 0"))
                return {};
            if (version == 1) {
                db_.close();
                return QStringLiteral("Kaikki DBの形式が古いため再生成してください。");
            }
        }
        db_.close();
    }
    return QStringLiteral("Kaikki DBを読み込めませんでした。");
}

KaikkiEntry KaikkiRepository::load(qint64 id, const QString &word, const QString &pos,
                                  const QString &language) const {
    KaikkiEntry result{id, word, pos, language, {}, {}};
    QSqlQuery senses(db_), references(db_), synonyms(db_);
    senses.prepare("SELECT id,glosses_json,gloss_language FROM senses WHERE entry_id=? ORDER BY source_position");
    senses.addBindValue(id);
    references.prepare("SELECT target_entry_id FROM spelling_references WHERE sense_id=? ORDER BY target_entry_id");
    synonyms.prepare("SELECT target_word FROM relations WHERE entry_id=? AND sense_id IS ? "
                     "AND kind='synonym' AND target_language=? ORDER BY id");
    const auto words = [&](const QVariant &senseId) {
        QStringList list;
        synonyms.bindValue(0, id);
        synonyms.bindValue(1, senseId);
        synonyms.bindValue(2, language);
        if (synonyms.exec())
            while (synonyms.next()) list.append(synonyms.value(0).toString());
        list.removeDuplicates();
        return list;
    };
    result.synonyms = words(QVariant());
    if (senses.exec()) {
        while (senses.next()) {
            KaikkiEntry::Sense sense;
            sense.language = senses.value(2).toString();
            for (const auto gloss : QJsonDocument::fromJson(senses.value(1).toByteArray()).array()) {
                const auto text = gloss.toString().trimmed();
                if (!text.isEmpty()) sense.glosses.append(text);
            }
            if (sense.glosses.isEmpty()) continue;
            const auto senseId = senses.value(0);
            references.bindValue(0, senseId);
            if (references.exec())
                while (references.next()) sense.targets.append(references.value(0).toLongLong());
            sense.synonyms = words(senseId);
            result.senses.append(sense);
        }
    }
    return result;
}

QVector<KaikkiEntry> KaikkiRepository::search(const QStringList &words, const QString &language) const {
    QVector<KaikkiEntry> results;
    if (!db_.isOpen()) return results;
    QSqlQuery query(db_);
    query.prepare("SELECT id,word,pos FROM entries WHERE language=? AND word=? ORDER BY id");
    QSet<QString> seen;
    for (const auto &word : words) {
        if (seen.contains(word)) continue;
        seen.insert(word);
        query.bindValue(0, language);
        query.bindValue(1, word);
        if (query.exec())
            while (query.next()) results.append(load(query.value(0).toLongLong(), query.value(1).toString(),
                                                     query.value(2).toString(), language));
    }
    return results;
}

KaikkiEntry KaikkiRepository::entry(qint64 id, const QString &language) const {
    if (!db_.isOpen()) return {};
    QSqlQuery query(db_);
    query.prepare("SELECT word,pos FROM entries WHERE id=? AND language=?");
    query.addBindValue(id);
    query.addBindValue(language);
    return query.exec() && query.next() ? load(id, query.value(0).toString(), query.value(1).toString(), language)
                                        : KaikkiEntry{};
}
