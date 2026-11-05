#pragma once
#include "DictionaryRepository.h"
#include "SearchHistory.h"
#include <QMainWindow>
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QSplitter;
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(const QString &dictionaryPath, QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void search(const QString &word);
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
    const QString language_ = QStringLiteral("ja");
    QLineEdit *input_;
    QListWidget *historyList_;
    QTreeWidget *results_;
    QLabel *heading_;
    QSplitter *splitter_;
};
