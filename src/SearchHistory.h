#pragma once
#include <QSettings>
#include <QStringList>
class SearchHistory {
public:
    explicit SearchHistory(QSettings &settings) : settings_(settings) {}
    QStringList words(const QString &language) const;
    void add(const QString &word, const QString &language);
    QString edit(const QString &original, const QString &replacement, const QString &language);
    void remove(const QString &word, const QString &language);
private:
    QSettings &settings_;
};
