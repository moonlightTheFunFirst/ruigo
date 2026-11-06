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
    QString language;
    QString alternativeLanguage;
    QStringList spellings;
    bool found = false;
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
    bool showReadings = false;
};
class DictionaryRepository {
public:
    DictionaryRepository();
    ~DictionaryRepository();
    DictionaryRepository(const DictionaryRepository &) = delete;
    DictionaryRepository &operator=(const DictionaryRepository &) = delete;
    bool open(const QString &path);
    QString error() const { return error_; }
    SearchResult search(const QString &word, const QString &language = {}) const;
    QString resolveLanguage(const QString &word) const;
    static QString preferredLanguage(const QString &word);
    static QString languageName(const QString &language);
    WordDetails wordDetails(const QString &word, const QString &language,
                            const QString &currentSynset) const;
private:
    struct Lookup { QString word; QString error; bool found = false; QStringList spellings; };
    SearchResult resolve(const QString &word, const QString &language, Lookup &selected) const;
    Lookup lookup(const QString &word, const QString &language) const;
    QSqlDatabase db_;
    QString error_;
};
