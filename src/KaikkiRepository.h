#pragma once
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>

struct KaikkiEntry {
    qint64 id = 0;
    QString word;
    QString pos;
    QString language;
    QStringList synonyms;
    struct Sense {
        QStringList glosses;
        QString language;
        QStringList synonyms;
        QVector<qint64> targets;
    };
    QVector<Sense> senses;
};

class KaikkiRepository {
public:
    KaikkiRepository() = default;
    ~KaikkiRepository();
    KaikkiRepository(const KaikkiRepository &) = delete;
    KaikkiRepository &operator=(const KaikkiRepository &) = delete;
    QString open(const QString &path);
    QVector<KaikkiEntry> search(const QStringList &words, const QString &language) const;
    KaikkiEntry entry(qint64 id, const QString &language) const;
private:
    KaikkiEntry load(qint64 id, const QString &word, const QString &pos, const QString &language) const;
    QSqlDatabase db_;
};
