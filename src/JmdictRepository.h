#pragma once

#include <QSqlDatabase>
#include <QMap>
#include <QStringList>
#include <QVector>

struct TranslationSense {
    qint64 id;
    QString pos;
    QStringList words, readings, glosses;
    QMap<QString, QStringList> readingsByWord;
};

struct TranslationResult {
    QVector<TranslationSense> senses;
    QString error;
};

class JmdictRepository {
public:
    ~JmdictRepository();
    void open(const QString &path);
    TranslationResult search(const QString &word, const QString &language) const;
    QStringList examples(qint64 senseId) const;
private:
    QSqlDatabase db_;
    QString error_;
};
