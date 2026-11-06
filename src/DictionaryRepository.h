#pragma once
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>

struct MeaningGroup {
    QString id;
    QString definition;
    QStringList words;
};
struct SearchResult {
    QVector<MeaningGroup> groups;
    QString error;
    int candidateCount() const;
};
struct WordMeaning {
    QString id;
    QString definition;
};
struct WordDetails {
    QStringList readings;
    QVector<WordMeaning> otherMeanings;
    QString error;
};
class DictionaryRepository {
public:
    DictionaryRepository();
    ~DictionaryRepository();
    DictionaryRepository(const DictionaryRepository &) = delete;
    DictionaryRepository &operator=(const DictionaryRepository &) = delete;
    bool open(const QString &path);
    QString error() const { return error_; }
    SearchResult search(const QString &word, const QString &language) const;
    WordDetails wordDetails(const QString &word, const QString &language,
                            const QString &currentSynset) const;
private:
    QSqlDatabase db_;
    QString error_;
};
