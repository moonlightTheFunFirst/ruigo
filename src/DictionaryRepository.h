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
    // Meanings linked by the dictionary's "similar" relation. Kept apart from
    // same-meaning synonyms and not included in candidateCount().
    QVector<MeaningGroup> relatedGroups;
    QString error;
    QString language;
    QString alternativeLanguage;
    // Headwords to choose from when the input matches several different ones
    // (case variants, a reading shared by homophones, an inflection of several
    // words). choiceKind is "case", "reading" or "inflection"; choiceHints holds
    // the first definition of each headword to help the choice.
    QStringList spellings;
    QString choiceKind;
    QStringList choiceHints;
    // Entries (lemma ids) that matched for each choice, parallel to spellings.
    // Empty for case choices, which are plain headword searches. Pass them to
    // searchEntries() so that only the matched entries are searched.
    QVector<QVector<qint64>> choiceEntries;
    // Headwords actually searched when they differ from the input. matchKind says
    // how they were reached: "spelling" (variant), "normalized" (case/width),
    // "reading" or "inflection".
    QStringList matchedWords;
    QString matchKind;
    bool viaAlias = false; // matchKind == "spelling"
    // When the input is itself a headword but also a reading/inflection of other
    // headwords (e.g. "saw" -> see), those are offered separately, never merged.
    QStringList formOf;
    QString formOfKind;
    QStringList formOfHints;
    QVector<QVector<qint64>> formOfEntries; // Parallel to formOf, for searchEntries().
    bool found = false;
    int candidateCount() const;
    int relatedCount() const;
};
struct WordMeaning {
    QString id;
    QString definition;
    QString examples;
};
struct WordDetails {
    QStringList readings;
    QVector<WordMeaning> otherMeanings;
    QString currentExamples;
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
    // Searches only the given entries of a headword, as chosen from choiceEntries or
    // formOfEntries; entries that are not that headword in that language are ignored.
    SearchResult searchEntries(const QString &headword, const QString &language,
                               const QVector<qint64> &entryIds) const;
    QString resolveLanguage(const QString &word) const;
    static QString preferredLanguage(const QString &word);
    static QString languageName(const QString &language);
    WordDetails wordDetails(const QString &word, const QString &language,
                            const QString &currentSynset) const;
private:
    struct Lookup {
        QString word; QString error; bool found = false; QVector<qint64> lemmaIds;
        QString kind; QStringList choices; QString choiceKind; QVector<QVector<qint64>> choiceIds;
        QStringList formOf; QString formOfKind; QVector<QVector<qint64>> formOfIds;
    };
    struct AliasHit { qint64 id; QString headword; QString kind; };
    SearchResult resolve(const QString &word, const QString &language, Lookup &selected) const;
    SearchResult collect(SearchResult result, const Lookup &selected, const QString &input) const;
    Lookup lookup(const QString &word, const QString &language) const;
    QVector<AliasHit> aliasHits(const QString &word, const QString &language, const QStringList &kinds, QString &error) const;
    QVector<AliasHit> regularInflections(const QString &word, QString &error) const;
    QStringList firstDefinitions(const QStringList &words, const QString &language,
                                 const QVector<QVector<qint64>> &entryIds) const;
    QSqlDatabase db_;
    QString error_;
};
