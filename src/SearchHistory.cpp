#include "SearchHistory.h"
QStringList SearchHistory::words(const QString &language) const {
    QStringList result;
    for (const auto &raw : settings_.value("history/" + language).toStringList()) {
        const auto word = raw.trimmed();
        if (!word.isEmpty() && !result.contains(word)) result.append(word);
        if (result.size() == 100) break;
    }
    return result;
}
void SearchHistory::add(const QString &input, const QString &language) {
    const auto word = input.trimmed();
    if (word.isEmpty()) return;
    auto entries = words(language);
    if (entries.contains(word)) return;
    entries.prepend(word);
    while (entries.size() > 100) entries.removeLast();
    settings_.setValue("history/" + language, entries);
    settings_.sync();
}
QString SearchHistory::edit(const QString &original, const QString &replacement, const QString &language) {
    auto entries = words(language);
    const int index = entries.indexOf(original);
    if (index < 0) return QStringLiteral("編集する入力履歴が見つかりません。");
    const auto word = replacement.trimmed();
    if (word.isEmpty()) return QStringLiteral("単語を入力してください。");
    if (word != original && entries.contains(word))
        return QStringLiteral("同じ単語がすでに入力履歴にあります。");
    entries[index] = word;
    settings_.setValue("history/" + language, entries);
    settings_.sync();
    return {};
}
void SearchHistory::remove(const QString &word, const QString &language) {
    auto entries = words(language);
    entries.removeAll(word);
    settings_.setValue("history/" + language, entries);
    settings_.sync();
}
