#pragma once
#include <QSettings>
#include <QStringList>
#include <QVector>
struct HistoryEntry {
    QString word;
    QString language;
};
class SearchHistory {
public:
    explicit SearchHistory(QSettings &settings);
    QVector<HistoryEntry> entries() const;
    QStringList words(const QString &language) const;
    bool add(const QString &word, const QString &language);
    QString edit(const QString &original, const QString &replacement, const QString &language,
                 const QString &replacementLanguage = {});
    void remove(const QString &word, const QString &language);
private:
    void save(const QVector<HistoryEntry> &entries);
    QSettings &settings_;
};
