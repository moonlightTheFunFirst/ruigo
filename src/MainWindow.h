#pragma once
#include "DictionaryRepository.h"
#include "KaikkiRepository.h"
#include "SearchHistory.h"
#include <QMainWindow>
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QCheckBox;
class QSplitter;
class QPlainTextEdit;
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(const QString &dictionaryPath, QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void search(const QString &word, const QString &language = {}, bool record = false,
                const QVector<qint64> &entries = {}, const QString &originalInput = {});
    void updateKaikki();
    void showResult();
    void showChoices(const QStringList &headwords, const QStringList &hints,
                     const QVector<QVector<qint64>> &entries);
    void refreshHistory();
    void editHistory();
    void deleteHistory();
    void showDisplaySettings();
    void applyDisplaySettings();
    QFont displayFont() const;
    void copyWord(QTreeWidgetItem *item);
    QSettings settings_;
    SearchHistory history_{settings_};
    DictionaryRepository repository_;
    KaikkiRepository kaikki_;
    QString language_ = QStringLiteral("ja");
    QString alternativeLanguage_;
    SearchResult lastResult_;
    QString lastWord_;
    QString originalWord_;
    QPlainTextEdit *kaikkiPanel_;
    QPushButton *alternativeButton_;
    QPushButton *formOfButton_;
    QCheckBox *showRelatedCheck_;
    QLineEdit *input_;
    QListWidget *historyList_;
    QListWidget *spellingChoices_;
    QTreeWidget *results_;
    QLabel *heading_;
    QSplitter *splitter_;
};
