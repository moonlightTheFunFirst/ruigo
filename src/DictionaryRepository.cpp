#include "DictionaryRepository.h"
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

int SearchResult::candidateCount() const {
    int count = 0;
    for (const auto &group : groups) count += group.words.size();
    return count;
}
int SearchResult::relatedCount() const {
    int count = 0;
    for (const auto &group : relatedGroups) count += group.words.size();
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
    if (!check.exec("PRAGMA user_version") || !check.next() || check.value(0).toInt() != 6) {
        error_ = QStringLiteral("辞書DBの形式が未対応、または破損しています。日英辞書DBを再生成してください。");
    } else if (!check.exec("SELECT l.word,l.language,l.forms_json,l.search_key,src.reading_mode,s.id,s.source,d.definition,d.language,se.lemma_id "
                           "FROM lemmas l JOIN sources src ON src.id=l.source JOIN senses se ON se.lemma_id=l.id "
                           "JOIN synsets s ON s.id=se.synset_id "
                           "LEFT JOIN synset_definitions d ON d.synset_id=s.id LIMIT 0")) {
        error_ = QStringLiteral("辞書DBの構造が不正です。\n%1").arg(check.lastError().text());
    }
    if (error_.isEmpty() && !check.exec("SELECT lemma_id,language,word,kind FROM lemma_aliases LIMIT 0"))
        error_ = QStringLiteral("辞書DBの別表記索引が不正です。再生成してください。");
    if (error_.isEmpty() && !check.exec("SELECT synset_id,target_id,rel_type FROM synset_relations LIMIT 0"))
        error_ = QStringLiteral("辞書DBの関連語データが不正です。再生成してください。");
    if (error_.isEmpty() && !check.exec("SELECT synset_id,language,position,text,source FROM synset_examples LIMIT 0"))
        error_ = QStringLiteral("辞書DBの用例データが不正です。再生成してください。");
    check.finish();
    if (!error_.isEmpty()) db_.close();
    return error_.isEmpty();
}
QString DictionaryRepository::languageName(const QString &language) {
    return language == "en" ? QStringLiteral("英語") : QStringLiteral("日本語");
}
QString DictionaryRepository::preferredLanguage(const QString &word) {
    bool latin = false;
    for (const auto c : word.toUcs4()) {
        const auto script = QChar::script(c);
        if (script == QChar::Script_Hiragana || script == QChar::Script_Katakana ||
            script == QChar::Script_Han || c == 0x30fc || c == 0xff70 || c == 0x3006)
            return QStringLiteral("ja");
        latin |= script == QChar::Script_Latin;
    }
    return latin ? QStringLiteral("en") : QStringLiteral("ja");
}
QVector<DictionaryRepository::AliasHit> DictionaryRepository::aliasHits(const QString &word, const QString &language,
                                                                        const QStringList &kinds, QString &error) const {
    QVector<AliasHit> hits;
    QStringList placeholders;
    for (int i = 0; i < kinds.size(); ++i) placeholders.append(":kind" + QString::number(i));
    QSqlQuery query(db_);
    query.prepare("SELECT a.lemma_id, l.word, a.kind FROM lemma_aliases a JOIN lemmas l ON l.id=a.lemma_id "
                  "WHERE a.language=:language AND a.word=:word AND a.kind IN (" + placeholders.join(',') + ") "
                  "ORDER BY l.id");
    query.bindValue(":language", language);
    query.bindValue(":word", word);
    for (int i = 0; i < kinds.size(); ++i) query.bindValue(":kind" + QString::number(i), kinds[i]);
    if (!query.exec()) { error = query.lastError().text(); return hits; }
    while (query.next())
        hits.append({query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString()});
    if (query.lastError().isValid()) error = query.lastError().text();
    return hits;
}
QVector<DictionaryRepository::AliasHit> DictionaryRepository::regularInflections(const QString &word, QString &error) const {
    // WordNet's "morphy" detachment rules for regular English inflections. A
    // candidate is accepted only when the dictionary has it as a headword of the
    // matching part of speech, so nothing outside the dictionary is produced.
    struct Rule { const char *suffix; const char *ending; const char *pos; };
    static const Rule rules[] = {
        {"s", "", "n"}, {"ses", "s", "n"}, {"xes", "x", "n"}, {"zes", "z", "n"}, {"ches", "ch", "n"},
        {"shes", "sh", "n"}, {"men", "man", "n"}, {"ies", "y", "n"},
        {"s", "", "v"}, {"ies", "y", "v"}, {"es", "e", "v"}, {"es", "", "v"}, {"ed", "e", "v"}, {"ed", "", "v"},
        {"ing", "e", "v"}, {"ing", "", "v"},
        {"er", "", "a"}, {"est", "", "a"}, {"er", "e", "a"}, {"est", "e", "a"},
    };
    QVector<AliasHit> hits;
    QSqlQuery query(db_);
    query.prepare("SELECT id, word FROM lemmas WHERE language='en' AND word=:word AND pos IN (:pos, :satellite) ORDER BY id");
    for (const auto &rule : rules) {
        const auto suffix = QString::fromLatin1(rule.suffix);
        if (!word.endsWith(suffix) || word.size() <= suffix.size()) continue;
        const auto base = word.left(word.size() - suffix.size()) + QString::fromLatin1(rule.ending);
        const auto pos = QString::fromLatin1(rule.pos);
        query.bindValue(":word", base);
        query.bindValue(":pos", pos);
        query.bindValue(":satellite", pos == "a" ? QStringLiteral("s") : pos);
        if (!query.exec()) { error = query.lastError().text(); return {}; }
        while (query.next()) {
            const auto id = query.value(0).toLongLong();
            if (std::none_of(hits.begin(), hits.end(), [&](const AliasHit &hit) { return hit.id == id; }))
                hits.append({id, query.value(1).toString(), QStringLiteral("inflection")});
        }
    }
    return hits;
}
QStringList DictionaryRepository::firstDefinitions(const QStringList &words, const QString &language,
                                                   const QVector<QVector<qint64>> &entryIds) const {
    // The dictionary's first-listed meaning of each headword, to tell homophones apart.
    // When the matched entries are known, the meaning comes from those entries only
    // (e.g. the verb "see" reached from "saw", not the noun "see").
    QStringList result;
    for (int i = 0; i < words.size(); ++i) {
        const auto ids = entryIds.value(i);
        QStringList placeholders;
        for (int j = 0; j < ids.size(); ++j) placeholders.append(":id" + QString::number(j));
        QSqlQuery query(db_);
        query.prepare("SELECT d.definition FROM lemmas l JOIN senses s ON s.lemma_id=l.id "
                      "JOIN synset_definitions d ON d.synset_id=s.synset_id AND d.language=l.language "
                      "WHERE l.language=:language AND l.word=:word "
                      + (ids.isEmpty() ? QString() : "AND l.id IN (" + placeholders.join(',') + ") ")
                      + "ORDER BY l.id, s.rowid LIMIT 1");
        query.bindValue(":language", language);
        query.bindValue(":word", words[i]);
        for (int j = 0; j < ids.size(); ++j) query.bindValue(":id" + QString::number(j), ids[j]);
        result.append(query.exec() && query.next() ? query.value(0).toString().section('\n', 0, 0) : QString());
    }
    return result;
}
DictionaryRepository::Lookup DictionaryRepository::lookup(const QString &input, const QString &language) const {
    // Order: headword, spelling variant, width/NFC-normalized headword and variant,
    // case-insensitive headword (en), reading (ja) / recorded inflection (en),
    // regular inflection rules (en). The first step that matches wins.
    Lookup result;
    const auto readingKinds = QStringList{QStringLiteral("reading"), QStringLiteral("inflection")};
    QSqlQuery query(db_);
    query.prepare("SELECT word FROM lemmas WHERE language=:language AND word=:word LIMIT 1");
    const auto headword = [&](const QString &word) {
        query.bindValue(":language", language);
        query.bindValue(":word", word);
        if (!query.exec()) { result.error = query.lastError().text(); return false; }
        if (!query.next()) return false;
        result.word = query.value(0).toString();
        result.found = true;
        return true;
    };
    // Several entries reached at once: one headword is searched directly by entry
    // id; different headwords become a choice so their meanings are never merged.
    // Groups hits by headword, keeping the matched entry ids of each headword.
    const auto group = [](const QVector<AliasHit> &hits, QStringList &headwords, QVector<QVector<qint64>> &ids) {
        for (const auto &hit : hits) {
            auto index = headwords.indexOf(hit.headword);
            if (index < 0) { headwords.append(hit.headword); ids.append(QVector<qint64>()); index = headwords.size() - 1; }
            if (!ids[index].contains(hit.id)) ids[index].append(hit.id);
        }
    };
    const auto take = [&](const QVector<AliasHit> &hits, const QString &spelling, const QString &kind) {
        if (hits.isEmpty()) return false;
        QStringList headwords;
        QVector<QVector<qint64>> ids;
        group(hits, headwords, ids);
        result.found = true;
        result.kind = kind;
        if (headwords.size() > 1) {
            result.choices = headwords;
            result.choiceIds = ids;
            result.choiceKind = kind;
            return true;
        }
        result.word = spelling;
        result.lemmaIds = ids.first();
        return true;
    };
    const auto withFormOf = [&]() {
        // The matched headword may also be a reading/inflection of other headwords
        // (e.g. "saw" -> see). Forms are stored as written, so look up the headword.
        QVector<AliasHit> others;
        for (const auto &hit : aliasHits(result.word, language, readingKinds, result.error))
            if (hit.headword != result.word) { others.append(hit); result.formOfKind = hit.kind; }
        group(others, result.formOf, result.formOfIds);
        return result;
    };

    if (headword(input)) return withFormOf();
    if (!result.error.isEmpty()) return result;
    if (take(aliasHits(input, language, {QStringLiteral("spelling")}, result.error), input, QStringLiteral("spelling"))
        || !result.error.isEmpty())
        return result;
    QString word = input;
    for (auto &c : word)
        if ((c.unicode() >= 0xff21 && c.unicode() <= 0xff3a) || (c.unicode() >= 0xff41 && c.unicode() <= 0xff5a))
            c = QChar(c.unicode() - 0xfee0);
    if (language == "en") word = word.normalized(QString::NormalizationForm_C);
    if (word != input) {
        if (headword(word)) { result.kind = QStringLiteral("normalized"); return withFormOf(); }
        if (!result.error.isEmpty()) return result;
        if (take(aliasHits(word, language, {QStringLiteral("spelling")}, result.error), word, QStringLiteral("spelling"))
            || !result.error.isEmpty())
            return result;
    }
    if (language == "en") {
        QSqlQuery folded(db_);
        folded.prepare("SELECT DISTINCT word FROM lemmas WHERE language=:language AND search_key=:key ORDER BY word");
        folded.bindValue(":language", language);
        folded.bindValue(":key", word.toLower());
        if (!folded.exec()) { result.error = folded.lastError().text(); return result; }
        QStringList spellings;
        while (folded.next()) spellings.append(folded.value(0).toString());
        if (folded.lastError().isValid()) { result.error = folded.lastError().text(); return result; }
        if (spellings.size() == 1) {
            result.word = spellings.first(); result.found = true; result.kind = QStringLiteral("normalized");
            return withFormOf(); // "Saw"/"SAW" offer "see" just like "saw".
        }
        if (spellings.size() > 1) {
            result.found = true; result.choices = spellings; result.choiceKind = QStringLiteral("case");
            return result;
        }
    }
    QStringList forms{input};
    if (word != input) forms.append(word);
    if (language == "en" && word.toLower() != word) forms.append(word.toLower());
    for (const auto &form : forms) {
        auto hits = aliasHits(form, language, readingKinds, result.error);
        if (!result.error.isEmpty()) return result;
        if (take(hits, form, hits.isEmpty() ? QString() : hits.first().kind)) return result;
    }
    if (language == "en") {
        const auto hits = regularInflections(word.toLower(), result.error);
        if (!result.error.isEmpty()) return result;
        take(hits, word.toLower(), QStringLiteral("inflection"));
    }
    return result;
}
SearchResult DictionaryRepository::resolve(const QString &input, const QString &requestedLanguage, Lookup &selected) const {
    SearchResult result;
    const auto inputWord = input.trimmed();
    result.language = requestedLanguage.isEmpty() ? preferredLanguage(inputWord) : requestedLanguage;
    if (inputWord.isEmpty()) return result;
    if (result.language != "ja" && result.language != "en") {
        result.error = QStringLiteral("未対応の検索言語です。"); return result;
    }
    if (!db_.isOpen()) {
        result.error = error_.isEmpty() ? QStringLiteral("辞書DBが開かれていません。") : error_;
        return result;
    }
    selected = lookup(inputWord, result.language);
    const auto otherLanguage = result.language == "ja" ? QStringLiteral("en") : QStringLiteral("ja");
    const auto other = lookup(inputWord, otherLanguage);
    if (!selected.error.isEmpty()) { result.error = selected.error; return result; }
    if (!other.error.isEmpty() && !other.found) { result.error = other.error; return result; }
    if (requestedLanguage.isEmpty() && !selected.found && other.found) {
        selected = other; result.language = otherLanguage;
        if (!selected.error.isEmpty()) { result.error = selected.error; return result; }
    } else if (other.found) result.alternativeLanguage = otherLanguage;
    result.found = selected.found;
    result.spellings = selected.choices;
    result.choiceKind = selected.choiceKind;
    result.choiceEntries = selected.choiceIds;
    result.matchKind = selected.kind;
    result.viaAlias = selected.kind == "spelling";
    result.formOf = selected.formOf;
    result.formOfKind = selected.formOfKind;
    result.formOfEntries = selected.formOfIds;
    return result;
}
QString DictionaryRepository::resolveLanguage(const QString &word) const {
    Lookup selected;
    const auto result = resolve(word, {}, selected);
    return result.error.isEmpty() ? result.language : preferredLanguage(word);
}
SearchResult DictionaryRepository::search(const QString &input, const QString &requestedLanguage) const {
    Lookup selected;
    auto result = resolve(input, requestedLanguage, selected);
    if (!result.error.isEmpty() || !result.found) return result;
    if (!result.spellings.isEmpty()) {
        result.choiceHints = firstDefinitions(result.spellings, result.language, result.choiceEntries);
        return result;
    }
    if (!result.formOf.isEmpty())
        result.formOfHints = firstDefinitions(result.formOf, result.language, result.formOfEntries);
    return collect(result, selected, input);
}
SearchResult DictionaryRepository::searchEntries(const QString &headword, const QString &language,
                                                 const QVector<qint64> &entryIds) const {
    SearchResult result;
    result.language = language;
    const auto word = headword.trimmed();
    if (word.isEmpty()) return result;
    if (language != "ja" && language != "en") { result.error = QStringLiteral("未対応の検索言語です。"); return result; }
    if (!db_.isOpen()) {
        result.error = error_.isEmpty() ? QStringLiteral("辞書DBが開かれていません。") : error_;
        return result;
    }
    // Keep only ids that really are this headword in this language.
    Lookup selected;
    selected.word = word;
    QSqlQuery check(db_);
    check.prepare("SELECT 1 FROM lemmas WHERE id=:id AND language=:language AND word=:word");
    for (const auto id : entryIds) {
        check.bindValue(":id", id);
        check.bindValue(":language", language);
        check.bindValue(":word", word);
        if (!check.exec()) { result.error = check.lastError().text(); return result; }
        if (check.next() && !selected.lemmaIds.contains(id)) selected.lemmaIds.append(id);
    }
    if (selected.lemmaIds.isEmpty()) return result; // Nothing valid: not found, never widened.
    selected.found = result.found = true;
    return collect(result, selected, word);
}
SearchResult DictionaryRepository::collect(SearchResult result, const Lookup &selected, const QString &input) const {
    const auto word = selected.word;
    const auto language = result.language;
    QString match = "matched.word=:word";
    if (!selected.lemmaIds.isEmpty()) {
        QStringList parameters;
        for (int i = 0; i < selected.lemmaIds.size(); ++i) parameters.append(":lemma" + QString::number(i));
        match = "matched.id IN (" + parameters.join(',') + ")";
    }
    const auto bindMatch = [&](QSqlQuery &query) {
        query.bindValue(":language", language);
        query.bindValue(":word", word);
        for (int i = 0; i < selected.lemmaIds.size(); ++i)
            query.bindValue(":lemma" + QString::number(i), selected.lemmaIds[i]);
    };
    // Report which headword was searched when it is not literally the input.
    if (!selected.lemmaIds.isEmpty()) {
        QSqlQuery headwords(db_);
        headwords.prepare("SELECT DISTINCT matched.word FROM lemmas matched WHERE matched.language=:language AND "
                          + match + " ORDER BY matched.word");
        bindMatch(headwords);
        if (headwords.exec())
            while (headwords.next()) result.matchedWords.append(headwords.value(0).toString());
        // Searching chosen entries of the input headword itself is not a different headword.
        if (result.matchedWords == QStringList{input.trimmed()}) result.matchedWords.clear();
    } else if (word != input.trimmed()) {
        result.matchedWords.append(word);
    }
    // Same-meaning synonyms come from the matched synsets; related words come
    // from synsets the dictionary marks as "similar" to them. Both exclude the
    // input and the matched headwords, and neither is merged into the other.
    const auto gather = [&](const QString &synsetColumn, const QString &joins, QVector<MeaningGroup> &groups) {
        QSqlQuery query(db_);
        query.prepare("SELECT DISTINCT " + synsetColumn + ", COALESCE(d.definition,''), candidate.word "
            "FROM lemmas matched JOIN senses hit ON hit.lemma_id=matched.id " + joins +
            "JOIN lemmas candidate ON candidate.id=member.lemma_id "
            "LEFT JOIN synset_definitions d ON d.synset_id=" + synsetColumn + " AND d.language=:language "
            "WHERE matched.language=:language AND " + match + " "
            "AND candidate.language=:language AND candidate.word<>:word AND candidate.word<>matched.word "
            "ORDER BY " + synsetColumn + ", candidate.word");
        bindMatch(query);
        if (!query.exec()) {
            result.error = QStringLiteral("辞書の検索に失敗しました。\n%1").arg(query.lastError().text());
            return false;
        }
        while (query.next()) {
            const auto id = query.value(0).toString();
            if (groups.isEmpty() || groups.last().id != id)
                groups.append({id, query.value(1).toString(), {}});
            groups.last().words.append(query.value(2).toString());
        }
        if (query.lastError().isValid()) { result.error = query.lastError().text(); return false; }
        return true;
    };
    if (!gather("s.id", "JOIN synsets s ON s.id=hit.synset_id JOIN senses member ON member.synset_id=s.id ",
                 result.groups)
        || !gather("r.target_id", "JOIN synset_relations r ON r.synset_id=hit.synset_id AND r.rel_type='similar' "
                    "JOIN senses member ON member.synset_id=r.target_id ", result.relatedGroups)) {
        result.groups.clear();
        result.relatedGroups.clear();
        return result;
    }
    // A meaning reached both directly and through "similar" is shown once, as synonyms.
    QSet<QString> direct;
    for (const auto &group : result.groups) direct.insert(group.id);
    result.relatedGroups.erase(std::remove_if(result.relatedGroups.begin(), result.relatedGroups.end(),
        [&](const MeaningGroup &group) { return direct.contains(group.id); }), result.relatedGroups.end());
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
    query.prepare("SELECT DISTINCT l.forms_json, s.synset_id, COALESCE(d.definition,''), src.reading_mode, "
        "(SELECT group_concat(line,char(10)) FROM (SELECT text || "
        "CASE WHEN source='' THEN '' ELSE ' — ' || source END AS line FROM synset_examples "
        "WHERE synset_id=s.synset_id AND language=:language ORDER BY position)) "
        "FROM lemmas l JOIN sources src ON src.id=l.source JOIN senses s ON s.lemma_id=l.id "
        "LEFT JOIN synset_definitions d ON d.synset_id=s.synset_id AND d.language=:language "
        "WHERE l.word=:word AND l.language=:language "
        "AND (:allMeanings OR EXISTS (SELECT 1 FROM senses current WHERE current.lemma_id=l.id "
        "AND current.synset_id=:current)) ORDER BY s.synset_id,l.id");
    query.bindValue(":word", word);
    query.bindValue(":language", language);
    query.bindValue(":current", currentSynset);
    query.bindValue(":allMeanings", currentSynset.isEmpty());
    if (!query.exec()) {
        result.error = query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.showReadings |= query.value(3).toString() == "kana";
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
                result.otherMeanings.append({id, definition, query.value(4).toString()});
        } else {
            result.currentExamples = query.value(4).toString();
        }
    }
    const auto kana = [](QString text) {
        text = text.normalized(QString::NormalizationForm_KC);
        for (auto &ch : text)
            if (ch.unicode() >= 0x30a1 && ch.unicode() <= 0x30f6) ch = QChar(ch.unicode() - 0x60);
        return text;
    };
    bool redundant = !result.readings.isEmpty();
    const auto normalized = kana(word);
    for (const auto &ch : normalized)
        if (!(ch.unicode() >= 0x3041 && ch.unicode() <= 0x3096) && ch != QChar(0x30fc)) redundant = false;
    for (const auto &reading : result.readings)
        if (kana(reading) != normalized) redundant = false;
    if (redundant) result.showReadings = false;
    if (query.lastError().isValid()) result.error = query.lastError().text();
    return result;
}
