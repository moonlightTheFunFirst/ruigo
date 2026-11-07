#include "SearchHistory.h"
namespace {
bool supported(const QString &language) { return language == "ja" || language == "en"; }
}
SearchHistory::SearchHistory(QSettings &settings) : settings_(settings) {
    if (!settings_.contains("history/entries")) {
        QStringList migrated;
        for (const auto &language : {QStringLiteral("ja"), QStringLiteral("en")})
            for (const auto &word : settings_.value("history/" + language).toStringList())
                migrated.append(language + ":" + word);
        settings_.setValue("history/entries", migrated);
        save(entries()); // Keep old keys for rollback; never import them again.
    }
}
QVector<HistoryEntry> SearchHistory::entries() const {
    QVector<HistoryEntry> result;
    QStringList seen;
    for (const auto &raw : settings_.value("history/entries").toStringList()) {
        const auto separator = raw.indexOf(':');
        if (separator < 0) continue;
        const auto language = raw.left(separator);
        const auto word = raw.mid(separator + 1).trimmed();
        const auto key = language + ":" + word;
        if (!supported(language) || word.isEmpty() || seen.contains(key)) continue;
        seen.append(key);
        result.append({word, language});
        if (result.size() == 100) break;
    }
    return result;
}
void SearchHistory::save(const QVector<HistoryEntry> &entries) {
    QStringList serialized;
    for (const auto &entry : entries) serialized.append(entry.language + ":" + entry.word);
    settings_.setValue("history/entries", serialized);
    settings_.sync();
}
QStringList SearchHistory::words(const QString &language) const {
    QStringList result;
    for (const auto &entry : entries()) if (entry.language == language) result.append(entry.word);
    return result;
}
bool SearchHistory::add(const QString &input, const QString &language) {
    const auto word = input.trimmed();
    if (word.isEmpty() || !supported(language)) return false;
    auto items = entries();
    for (const auto &entry : items) if (entry.word == word && entry.language == language) return false;
    items.prepend({word, language});
    if (items.size() > 100) items.removeLast();
    save(items);
    return true;
}
QString SearchHistory::edit(const QString &original, const QString &replacement, const QString &language,
                            const QString &replacementLanguage) {
    auto items = entries();
    const auto word = replacement.trimmed();
    const auto newLanguage = replacementLanguage.isEmpty() ? language : replacementLanguage;
    if (word.isEmpty()) return QStringLiteral("単語を入力してください。");
    if (!supported(newLanguage)) return QStringLiteral("未対応の言語です。");
    int index = -1;
    for (int i = 0; i < items.size(); ++i)
        if (items[i].word == original && items[i].language == language) index = i;
    if (index < 0) return QStringLiteral("編集する入力履歴が見つかりません。");
    for (int i = 0; i < items.size(); ++i)
        if (i != index && items[i].word == word && items[i].language == newLanguage)
            return QStringLiteral("同じ単語と言語の入力履歴があります。");
    items[index] = {word, newLanguage};
    save(items);
    return {};
}
void SearchHistory::remove(const QString &word, const QString &language) {
    auto items = entries();
    for (int i = items.size() - 1; i >= 0; --i)
        if (items[i].word == word && items[i].language == language) items.removeAt(i);
    save(items);
}
