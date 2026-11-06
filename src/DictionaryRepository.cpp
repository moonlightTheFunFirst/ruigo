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
    if (!check.exec("PRAGMA user_version") || !check.next() || check.value(0).toInt() != 2) {
        error_ = QStringLiteral("辞書DBの形式が未対応、または破損しています。日英辞書DBを再生成してください。");
    } else if (!check.exec("SELECT l.word,l.language,l.forms_json,l.search_key,src.reading_mode,s.id,s.source,d.definition,d.language,se.lemma_id "
                           "FROM lemmas l JOIN sources src ON src.id=l.source JOIN senses se ON se.lemma_id=l.id "
                           "JOIN synsets s ON s.id=se.synset_id "
                           "LEFT JOIN synset_definitions d ON d.synset_id=s.id LIMIT 0")) {
        error_ = QStringLiteral("辞書DBの構造が不正です。\n%1").arg(check.lastError().text());
    }
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
DictionaryRepository::Lookup DictionaryRepository::lookup(const QString &input, const QString &language) const {
    Lookup result;
    QSqlQuery query(db_);
    query.prepare("SELECT word FROM lemmas WHERE language=:language AND word=:word LIMIT 1");
    query.bindValue(":language", language);
    query.bindValue(":word", input);
    if (!query.exec()) { result.error = query.lastError().text(); return result; }
    if (query.next()) { result.word = query.value(0).toString(); result.found = true; return result; }
    QString word = input;
    for (auto &c : word)
        if ((c.unicode() >= 0xff21 && c.unicode() <= 0xff3a) || (c.unicode() >= 0xff41 && c.unicode() <= 0xff5a))
            c = QChar(c.unicode() - 0xfee0);
    if (language == "en") word = word.normalized(QString::NormalizationForm_C);
    query.bindValue(":word", word);
    if (!query.exec()) { result.error = query.lastError().text(); return result; }
    if (query.next()) { result.word = query.value(0).toString(); result.found = true; return result; }
    if (language != "en") return result;
    query.prepare("SELECT DISTINCT word FROM lemmas WHERE language=:language AND search_key=:key ORDER BY word");
    query.bindValue(":language", language);
    query.bindValue(":key", word.toLower());
    if (!query.exec()) { result.error = query.lastError().text(); return result; }
    while (query.next()) result.spellings.append(query.value(0).toString());
    if (query.lastError().isValid()) result.error = query.lastError().text();
    result.found = !result.spellings.isEmpty();
    if (result.spellings.size() == 1) { result.word = result.spellings.takeFirst(); }

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
    result.spellings = selected.spellings;
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
    if (!result.error.isEmpty() || !result.found || !result.spellings.isEmpty()) return result;
    const auto word = selected.word;
    const auto language = result.language;
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
    query.prepare("SELECT DISTINCT l.forms_json, s.synset_id, COALESCE(d.definition,''), src.reading_mode "
        "FROM lemmas l JOIN sources src ON src.id=l.source JOIN senses s ON s.lemma_id=l.id "
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
                result.otherMeanings.append({id, definition});
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
