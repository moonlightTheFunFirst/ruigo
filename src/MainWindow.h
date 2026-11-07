#pragma once
#include "DictionaryRepository.h"
#include "JmdictRepository.h"
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
class QRadioButton;
class QSplitter;
class QPlainTextEdit;
class QTextBrowser;
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
    TranslationResult translate(const QString &word, QString &language, bool automatic) const;
    void showTranslation(const QString &word, const TranslationResult &result);
    void showKaikkiFor(QTreeWidgetItem *item);
    QString kaikkiText(const QStringList &words, const QString &language) const;
    void showDetailsFor(QTreeWidgetItem *item);
    void applyBottomSizes();
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
    JmdictRepository jmdict_;
    KaikkiRepository kaikki_;
    QString language_ = QStringLiteral("ja");
    QString alternativeLanguage_;
    SearchResult lastResult_;
    QString lastWord_;
    QString originalWord_;
    QString searchKaikki_; // Kaikki text for the search word; empty when there is none.
    QPlainTextEdit *kaikkiPanel_;
    QTextBrowser *detailsPanel_;  // Reading and meanings of the selected candidate.
    QSplitter *resultSplitter_;   // Results above, the bottom row below.
    QSplitter *bottomSplitter_;   // Kaikki panel on the left, details on the right.
    int bottomHeight_ = 180;      // Bottom row height chosen by the user (window/kaikkiPanelHeight).
    int detailsWidth_ = 360;      // Details width chosen by the user (window/detailsPanelWidth).
    bool bottomSized_ = false;    // The saved height is applied once the window is laid out.
    QPushButton *alternativeButton_;
    QPushButton *formOfButton_;
    QCheckBox *showRelatedCheck_;
    QRadioButton *translationMode_;
    QLineEdit *input_;
    QListWidget *historyList_;
    QListWidget *spellingChoices_;
    QTreeWidget *results_;
    QLabel *heading_;
    QSplitter *splitter_;
};
