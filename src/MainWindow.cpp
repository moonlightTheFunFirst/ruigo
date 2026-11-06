#include "MainWindow.h"
#include <QApplication>
#include <QAccessible>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QStyledItemDelegate>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QToolTip>
#include <QHash>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <functional>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QDir>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
// The language is normally obvious from the word itself, so it is shown only
// when the same spelling is saved in both languages (e.g. "AI" after using
// the other-language button) and the two rows would otherwise look identical.
QString historyLabel(const QString &word, const QString &language, bool sameSpellingInOtherLanguage) {
    return sameSpellingInOtherLanguage && language == "en" ? word + QStringLiteral("  [英語]") : word;
}
class HistoryEditorDelegate : public QStyledItemDelegate {
public:
    using Save = std::function<QString(int, const QString &)>;
    HistoryEditorDelegate(QListWidget *list, Save save, std::function<QString(int)> language, std::function<void(const QString &)> committed)
        : QStyledItemDelegate(list), list_(list), save_(std::move(save)), language_(std::move(language)), committed_(std::move(committed)) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *editor = new QLineEdit(parent);
        editor->setObjectName("historyEditor");
        editor->setFont(list_->font());
        return editor;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override {
        auto *editor = static_cast<QLineEdit *>(widget);
        editor->setText(index.data(Qt::UserRole + 1).toString());
        editor->setProperty("row", index.row());
        editor->setProperty("originalWord", editor->text());
        editor->selectAll();
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model, const QModelIndex &index) const override {
        QSignalBlocker blocker(list_); // Search once, explicitly, after committing and closing.
        const auto word = static_cast<QLineEdit *>(widget)->text().trimmed();
        const auto language = language_(index.row());
        model->setData(index, language, Qt::UserRole);
        model->setData(index, word, Qt::UserRole + 1);
        model->setData(index, word, Qt::EditRole); // Labels are refreshed after the commit.
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *editor = qobject_cast<QLineEdit *>(object);
        if (!editor) return QStyledItemDelegate::eventFilter(object, event);
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                const auto error = save_(editor->property("row").toInt(), editor->text());
                if (!error.isEmpty()) {
                    editor->setToolTip(error);
                    QToolTip::showText(editor->mapToGlobal(QPoint(0, editor->height())), error, editor);
                    editor->selectAll();
                } else {
                    const auto word = editor->text().trimmed();
                    QToolTip::hideText();
                    emit commitData(editor);
                    emit closeEditor(editor, QAbstractItemDelegate::NoHint);
                    committed_(word);
                }
                return true;
            }
            if (key == Qt::Key_Escape || key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                QToolTip::hideText();
                emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            // Opening the editor's own context menu does not end editing.
            if (static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) {
                QToolTip::hideText();
                emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
            }
            return false;
        }
        // Avoid the default delegate's implicit commit on focus loss / Tab.
        return QObject::eventFilter(object, event);
    }
private:
    QListWidget *list_;
    Save save_;
    std::function<QString(int)> language_;
    std::function<void(const QString &)> committed_;
};

QFont defaultDisplayFont() {
    auto font = QApplication::font();
    font.setPointSizeF(qMax(11.0, font.pointSizeF()));
    return font;
}
}

MainWindow::MainWindow(const QString &dictionaryPath, QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Ruigo — オフライン類語辞典"));
    resize(1000, 680);
    auto *displayAction = menuBar()->addMenu(QStringLiteral("設定"))->addAction(QStringLiteral("表示設定…"));
    displayAction->setObjectName("displaySettingsAction");
    connect(displayAction, &QAction::triggered, this, &MainWindow::showDisplaySettings);
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setObjectName("splitter");
    splitter_->setChildrenCollapsible(false);
    auto *left = new QWidget(splitter_);
    left->setMinimumWidth(220);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(QStringLiteral("検索")));
    input_ = new QLineEdit;
    input_->setObjectName("searchInput");
    input_->setPlaceholderText(QStringLiteral("単語を入力して Enter"));
    input_->setClearButtonEnabled(true);
    leftLayout->addWidget(input_);
    leftLayout->addWidget(new QLabel(QStringLiteral("入力履歴")));
    historyList_ = new QListWidget;
    historyList_->setObjectName("historyList");
    historyList_->setToolTip(QStringLiteral("ダブルクリック・F2で編集 ／ Enterで確定 ／ Escで取消 ／ Deleteで削除"));
    historyList_->setItemDelegate(new HistoryEditorDelegate(historyList_, [this](int row, const QString &replacement) {
        auto *item = historyList_->item(row);
        if (!item) return QStringLiteral("編集する入力履歴が見つかりません。");
        const auto original = item->data(Qt::UserRole + 1).toString();
        const auto language = item->data(Qt::UserRole).toString();
        const auto word = replacement.trimmed();
        auto newLanguage = language;
        if (!word.isEmpty() && word != original) {
            newLanguage = repository_.resolveLanguage(word);
        }
        const auto error = history_.edit(original, word, language, newLanguage);
        if (!error.isEmpty()) statusBar()->showMessage(error, 4000);

        return error;
    }, [this](int row) { return history_.entries().at(row).language; }, [this](const QString &word) {
        const auto *item = historyList_->currentItem();
        const auto language = item ? item->data(Qt::UserRole).toString() : QString();
        refreshHistory(); // An edit can create or remove a same-spelling pair.
        if (!language.isEmpty()) search(word, language);
    }));
    historyList_->setEditTriggers(QAbstractItemView::DoubleClicked);
    historyList_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *editAction = new QAction(QStringLiteral("編集"), historyList_);
    editAction->setShortcut(Qt::Key_F2);
    editAction->setShortcutContext(Qt::WidgetShortcut);
    historyList_->addAction(editAction);
    connect(editAction, &QAction::triggered, this, &MainWindow::editHistory);
    auto *deleteAction = new QAction(QStringLiteral("削除"), historyList_);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    historyList_->addAction(deleteAction);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteHistory);
    connect(historyList_, &QListWidget::customContextMenuRequested, this,
        [this, editAction, deleteAction](const QPoint &point) {
            auto *item = historyList_->itemAt(point);
            if (!item) return;
            QSignalBlocker blocker(historyList_);
            historyList_->setCurrentItem(item);
            QMenu menu;
            menu.addAction(editAction);
            menu.addAction(deleteAction);
            menu.exec(historyList_->viewport()->mapToGlobal(point));
        });
    leftLayout->addWidget(historyList_);
    auto *right = new QWidget(splitter_);
    right->setMinimumWidth(320);
    auto *rightLayout = new QVBoxLayout(right);
    heading_ = new QLabel;
    heading_->setObjectName("resultHeading");
    heading_->setTextFormat(Qt::PlainText);
    heading_->setWordWrap(true);
    // The heading and the related-words switch share one row; the switch stays
    // visible before the first search so the preference can be set any time.
    auto *headingRow = new QHBoxLayout;
    headingRow->addWidget(heading_, 1);
    showRelatedCheck_ = new QCheckBox(QStringLiteral("似た意味も表示"));
    showRelatedCheck_->setObjectName("showRelatedCheck");
    showRelatedCheck_->setToolTip(QStringLiteral("辞書で「似た意味」とされる関連語を、類語の後に別枠で表示します"));
    showRelatedCheck_->setChecked(settings_.value("display/showRelated", true).toBool());
    headingRow->addWidget(showRelatedCheck_, 0, Qt::AlignTop);
    rightLayout->addLayout(headingRow);
    heading_->hide();
    connect(showRelatedCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        settings_.setValue("display/showRelated", checked);
        settings_.sync();
        showResult(); // Redraw from the last result: no new query, no history change.
    });
    alternativeButton_ = new QPushButton;
    alternativeButton_->setObjectName("alternativeLanguageButton");
    alternativeButton_->hide();
    rightLayout->addWidget(alternativeButton_);
    connect(alternativeButton_, &QPushButton::clicked, this, [this] {
        search(alternativeButton_->property("word").toString(), alternativeLanguage_, true);
    });
    // The input is a headword but also a reading/inflection of other headwords
    // (e.g. "saw" -> see): offer those without mixing them into this result.
    formOfButton_ = new QPushButton;
    formOfButton_->setObjectName("formOfButton");
    formOfButton_->hide();
    rightLayout->addWidget(formOfButton_);
    connect(formOfButton_, &QPushButton::clicked, this, [this] {
        // Search only the entries that the form/reading belongs to (e.g. the verb "see").
        if (lastResult_.formOf.size() == 1) {
            search(lastResult_.formOf.first(), language_, true, lastResult_.formOfEntries.value(0), originalWord_);
            return;
        }
        showChoices(lastResult_.formOf, lastResult_.formOfHints, lastResult_.formOfEntries);
    });
    spellingChoices_ = new QListWidget;
    spellingChoices_->setObjectName("spellingChoices");
    spellingChoices_->setMaximumHeight(180);
    spellingChoices_->hide();
    rightLayout->addWidget(spellingChoices_);
    const auto chooseSpelling = [this](QListWidgetItem *item) {
        const auto word = item->data(Qt::UserRole + 1).toString();
        const auto language = item->data(Qt::UserRole).toString();
        QVector<qint64> entries;
        for (const auto &id : item->data(Qt::UserRole + 2).toList()) entries.append(id.toLongLong());
        search(word, language, true, entries, originalWord_);
    };
    connect(spellingChoices_, &QListWidget::itemClicked, this, chooseSpelling);
    connect(spellingChoices_, &QListWidget::itemActivated, this, chooseSpelling);
    results_ = new QTreeWidget;
    results_->setObjectName("results");
    // Qt 6.8.2 does not invalidate QAccessibleTree's row cache on collapse.
    // Windows UI Automation can then traverse stale/cyclic sibling indexes.
    connect(results_, &QTreeWidget::itemCollapsed, this, [this] {
        QAccessibleTableModelChangeEvent event(results_, QAccessibleTableModelChangeEvent::ModelReset);
        QAccessible::updateAccessibility(&event);
    });
    results_->setHeaderHidden(true);
    results_->setWordWrap(true);
    results_->setContextMenuPolicy(Qt::CustomContextMenu);
    results_->installEventFilter(this); // Enter copies, Up from the first word returns to the search box.
    input_->installEventFilter(this);   // Down moves to the first candidate.
    auto *copyAction = new QAction(QStringLiteral("コピー"), results_);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetShortcut);
    results_->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, [this] { copyWord(results_->currentItem()); });
    // Results sit above a bottom row (vertical splitter); the row holds the Kaikki panel
    // and the always-visible details of the selected candidate (horizontal splitter).
    // Sizes are kept separately (not saveState): the Kaikki panel is often hidden,
    // and a hidden pane would be restored with no size.
    resultSplitter_ = new QSplitter(Qt::Vertical, right);
    resultSplitter_->setObjectName("resultSplitter");
    resultSplitter_->setChildrenCollapsible(false);
    resultSplitter_->installEventFilter(this); // Applies the saved height once laid out.
    results_->setMinimumHeight(80);
    resultSplitter_->addWidget(results_);
    bottomSplitter_ = new QSplitter(Qt::Horizontal);
    bottomSplitter_->setObjectName("bottomSplitter");
    bottomSplitter_->setChildrenCollapsible(false);
    bottomSplitter_->setMinimumHeight(60);
    kaikkiPanel_ = new QPlainTextEdit;
    kaikkiPanel_->setObjectName("kaikkiSupplement");
    kaikkiPanel_->setAccessibleName(QStringLiteral("Wiktionary / Kaikki の補足"));
    kaikkiPanel_->setReadOnly(true);
    kaikkiPanel_->setMinimumWidth(160);
    kaikkiPanel_->hide();
    bottomSplitter_->addWidget(kaikkiPanel_);
    detailsPanel_ = new QTextBrowser;
    detailsPanel_->setObjectName("wordDetails");
    detailsPanel_->setAccessibleName(QStringLiteral("選択中の候補の詳細"));
    detailsPanel_->setOpenLinks(false);
    detailsPanel_->setMinimumWidth(160);
    bottomSplitter_->addWidget(detailsPanel_);
    bottomSplitter_->setStretchFactor(0, 1);
    bottomSplitter_->setStretchFactor(1, 0);
    resultSplitter_->addWidget(bottomSplitter_);
    resultSplitter_->setStretchFactor(0, 1);
    resultSplitter_->setStretchFactor(1, 0);
    bottomHeight_ = qBound(60, settings_.value("window/kaikkiPanelHeight", 180).toInt(), 2000);
    detailsWidth_ = qBound(160, settings_.value("window/detailsPanelWidth", 360).toInt(), 2000);
    connect(resultSplitter_, &QSplitter::splitterMoved, this, [this] {
        bottomHeight_ = resultSplitter_->sizes().value(1, bottomHeight_);
    });
    connect(bottomSplitter_, &QSplitter::splitterMoved, this, [this] {
        if (!kaikkiPanel_->isHidden()) detailsWidth_ = bottomSplitter_->sizes().value(1, detailsWidth_);
    });
    rightLayout->addWidget(resultSplitter_);
    auto *hint = new QLabel(QStringLiteral("類語候補をダブルクリック・Enter・Ctrl+Cでコピー ／ 検索欄で↓で候補へ移動 ／ 右クリックで操作"));
    hint->setObjectName("resultHint");
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(hint, 1);
    setCentralWidget(splitter_);
    splitter_->setSizes({300, 700});
    connect(input_, &QLineEdit::returnPressed, this, [this] { search(input_->text(), {}, true); });
    const auto searchHistoryItem = [this](QListWidgetItem *item) {
        if (item) search(item->data(Qt::UserRole + 1).toString(), item->data(Qt::UserRole).toString());
    };
    connect(historyList_, &QListWidget::currentItemChanged, this,
        [searchHistoryItem](QListWidgetItem *current, QListWidgetItem *) { searchHistoryItem(current); });
    connect(historyList_, &QListWidget::itemClicked, this, searchHistoryItem);
    connect(historyList_, &QListWidget::itemActivated, this, searchHistoryItem);
    // Selecting a candidate (mouse or keyboard) switches the Kaikki panel and the details to that word.
    connect(results_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem *current, QTreeWidgetItem *) { showKaikkiFor(current); showDetailsFor(current); });
    connect(results_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { copyWord(item); });
    connect(results_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = results_->itemAt(point);
        if (!item || !item->parent()) return;
        const auto word = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        auto *copy = menu.addAction(QStringLiteral("コピー"));
        copy->setShortcut(QKeySequence::Copy); // Shown as a hint; the tree's own action handles the key.
        auto *find = menu.addAction(QStringLiteral("この単語で検索"));
        auto *selected = menu.exec(results_->viewport()->mapToGlobal(point));
        if (selected == copy) copyWord(item);
        else if (selected == find) search(word, language_);
    });
    refreshHistory();
    restoreGeometry(settings_.value("window/geometry").toByteArray());
    bool visible = false;
    for (auto *screen : QGuiApplication::screens())
        visible |= screen->availableGeometry().intersects(frameGeometry());
    if (!visible && QGuiApplication::primaryScreen())
        move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
    splitter_->restoreState(settings_.value("window/splitter").toByteArray());
    if (!repository_.open(dictionaryPath)) {
        heading_->setText(repository_.error());
        heading_->show();
    }
    const auto warning = kaikki_.open(QFileInfo(dictionaryPath).dir().filePath("kaikki.sqlite"));
    if (!warning.isEmpty()) statusBar()->showMessage(warning, 15000);
    applyDisplaySettings();
    showDetailsFor(nullptr);
    input_->setFocus();
}
QFont MainWindow::displayFont() const {
    auto font = defaultDisplayFont();
    const auto family = settings_.value("display/fontFamily").toString();
    if (!family.isEmpty() && QFontDatabase::families().contains(family, Qt::CaseInsensitive)) {
        font.setFamily(family);
        font.setPointSize(qBound(8, settings_.value("display/fontSize", font.pointSize()).toInt(), 32));
    }
    return font;
}
void MainWindow::applyDisplaySettings() {
    setFont(displayFont());
    QToolTip::setFont(displayFont());
    auto colors = QApplication::palette();
    const bool dark = colors.color(QPalette::Base).lightness() < 128;
    const QColor text(dark ? "#d2d2d2" : "#303030");
    const QColor secondary(dark ? "#b4b4b4" : "#555555");
    const QColor selection(dark ? "#454b52" : "#dce4eb");
    for (auto group : {QPalette::Active, QPalette::Inactive}) {
        colors.setColor(group, QPalette::Text, text);
        colors.setColor(group, QPalette::WindowText, text);
        colors.setColor(group, QPalette::ButtonText, text);
        colors.setColor(group, QPalette::Highlight, selection);
        colors.setColor(group, QPalette::HighlightedText, text);
    }
    setPalette(colors);
    setStyleSheet(QStringLiteral(
        "QListWidget::item, QTreeWidget::item { padding: 3px 2px; }"
        "QListWidget::item:selected, QTreeWidget::item:selected { background: %1; color: %2; }"
        "QLabel#resultHint { color: %3; }").arg(selection.name(), text.name(), secondary.name()));
    // Native widget styles can resolve their own font instead of inheriting the
    // main window's font once a style sheet is present.
    const auto font = displayFont();
    for (auto *widget : findChildren<QWidget *>()) {
        widget->setFont(font);
        widget->setPalette(colors);
    }
    for (int i = 0; i < results_->topLevelItemCount(); ++i)
        results_->topLevelItem(i)->setForeground(0, secondary);
}
void MainWindow::showDisplaySettings() {
    QDialog dialog(this);
    dialog.setFont(displayFont());
    dialog.setObjectName("displaySettingsDialog");
    dialog.setWindowTitle(QStringLiteral("表示設定"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *family = new QFontComboBox;
    family->setObjectName("displayFontFamily");
    family->setCurrentFont(displayFont());
    auto *size = new QSpinBox;
    size->setObjectName("displayFontSize");
    size->setRange(8, 32);
    size->setSuffix(QStringLiteral(" pt"));
    size->setValue(qRound(displayFont().pointSizeF()));
    form->addRow(QStringLiteral("フォント"), family);
    form->addRow(QStringLiteral("サイズ"), size);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral("プレビュー")));
    auto *preview = new QTreeWidget;
    preview->setObjectName("displayPreview");
    preview->setHeaderHidden(true);
    preview->setMinimumSize(400, 180);
    auto *meaning = new QTreeWidgetItem(preview, {QStringLiteral("意味：見た目が整っている（表示見本）")});
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    meaning->setForeground(0, QColor(dark ? "#b4b4b4" : "#555555"));
    new QTreeWidgetItem(meaning, {QStringLiteral("美しい　綺麗　麗しい")});
    auto *selected = new QTreeWidgetItem(meaning, {QStringLiteral("選択中の文字　日本語 ABC 123")});
    meaning->setExpanded(true);
    preview->setCurrentItem(selected);
    layout->addWidget(preview);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("標準に戻す"));
    layout->addWidget(buttons);
    bool reset = false;
    auto updatePreview = [&] {
        auto font = family->currentFont();
        font.setPointSize(size->value());
        preview->setFont(font);
        reset = false;
    };
    connect(family, &QFontComboBox::currentFontChanged, &dialog, updatePreview);
    connect(size, &QSpinBox::valueChanged, &dialog, updatePreview);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        family->setCurrentFont(defaultDisplayFont());
        size->setValue(qRound(defaultDisplayFont().pointSizeF()));
        updatePreview();
        reset = true;
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updatePreview();
    if (dialog.exec() != QDialog::Accepted) return;
    if (reset) {
        settings_.remove("display/fontFamily");
        settings_.remove("display/fontSize");
    } else {
        settings_.setValue("display/fontFamily", family->currentFont().family());
        settings_.setValue("display/fontSize", size->value());
    }
    settings_.sync();
    applyDisplaySettings();
}
void MainWindow::refreshHistory() {
    QSignalBlocker blocker(historyList_);
    const auto *selected = historyList_->currentItem();
    const auto selectedWord = selected ? selected->data(Qt::UserRole + 1).toString() : QString();
    const auto selectedLanguage = selected ? selected->data(Qt::UserRole).toString() : QString();
    historyList_->clear();
    const auto entries = history_.entries();
    QHash<QString, QSet<QString>> languagesByWord;
    for (const auto &entry : entries) languagesByWord[entry.word].insert(entry.language);
    for (const auto &entry : entries) {
        const bool sameSpelling = languagesByWord.value(entry.word).size() > 1;
        auto *item = new QListWidgetItem(historyLabel(entry.word, entry.language, sameSpelling), historyList_);
        item->setData(Qt::UserRole, entry.language);
        item->setData(Qt::UserRole + 1, entry.word);
        if (entry.word == selectedWord && entry.language == selectedLanguage) historyList_->setCurrentItem(item);
    }
    for (int i = 0; i < historyList_->count(); ++i)
        historyList_->item(i)->setFlags(historyList_->item(i)->flags() | Qt::ItemIsEditable);
}
void MainWindow::editHistory() {
    auto *item = historyList_->currentItem();
    if (!item) return;
    historyList_->editItem(item);
}
void MainWindow::deleteHistory() {
    auto *item = historyList_->currentItem();
    if (!item) return;
    history_.remove(item->data(Qt::UserRole + 1).toString(), item->data(Qt::UserRole).toString());
    QSignalBlocker blocker(historyList_);
    delete historyList_->takeItem(historyList_->row(item));
    historyList_->setCurrentRow(-1);
    refreshHistory(); // The remaining same-spelling row may no longer need its label.
}
void MainWindow::search(const QString &input, const QString &requestedLanguage, bool record,
                        const QVector<qint64> &entries, const QString &originalInput) {
    const auto word = input.trimmed();
    if (word.isEmpty()) return;
    input_->setText(word);
    // A headword chosen from a reading/inflection keeps its matched entries; history
    // still stores only the word and language.
    lastResult_ = entries.isEmpty() ? repository_.search(word, requestedLanguage)
                                    : repository_.searchEntries(word, requestedLanguage, entries);
    lastWord_ = word;
    originalWord_ = originalInput.isEmpty() ? word : originalInput;
    language_ = lastResult_.language;
    alternativeLanguage_ = lastResult_.alternativeLanguage;
    if (record) { history_.add(word, language_); refreshHistory(); }
    updateKaikki();
    showResult();
}

void MainWindow::updateKaikki() {
    // The search word's entries (input, searched headword, WordNet's matched headwords).
    searchKaikki_.clear();
    if (lastResult_.error.isEmpty()) {
        QStringList words{originalWord_, lastWord_};
        words.append(lastResult_.matchedWords);
        searchKaikki_ = kaikkiText(words);
    }
    showKaikkiFor(nullptr);
}
void MainWindow::showKaikkiFor(QTreeWidgetItem *item) {
    // A selected candidate shows its own Kaikki entries; a meaning heading or no
    // selection shows the search word's. Nothing is searched again or recorded.
    QString text;
    if (kaikki_.isOpen()) {
        if (item && item->parent()) {
            const auto word = item->data(0, Qt::UserRole).toString();
            const auto body = kaikkiText({word});
            text = QStringLiteral("Wiktionary / Kaikki\n\n") + (body.isEmpty()
                       ? QStringLiteral("「%1」の項目はありません。").arg(word) : body);
        } else if (!searchKaikki_.isEmpty()) {
            text = QStringLiteral("Wiktionary / Kaikki\n\n") + searchKaikki_;
        }
    }
    if (text.isEmpty()) {
        kaikkiPanel_->clear();
        kaikkiPanel_->hide();
        return;
    }
    kaikkiPanel_->setPlainText(text);
    if (kaikkiPanel_->isHidden()) {
        // Reappearing: the details keep the width the user last chose (switching while visible keeps it).
        kaikkiPanel_->show();
        const auto total = qMax(bottomSplitter_->width(), detailsWidth_ + kaikkiPanel_->minimumWidth());
        bottomSplitter_->setSizes({qMax(total - detailsWidth_, kaikkiPanel_->minimumWidth()), detailsWidth_});
    }
}
void MainWindow::applyBottomSizes() {
    const auto total = qMax(resultSplitter_->height(), bottomHeight_ + results_->minimumHeight());
    resultSplitter_->setSizes({qMax(total - bottomHeight_, results_->minimumHeight()), bottomHeight_});
}
void MainWindow::showDetailsFor(QTreeWidgetItem *item) {
    // The selected candidate's reading and meanings; queried for this one word only.
    const auto escape = [](const QString &text) {
        return text.toHtmlEscaped().replace('\n', QStringLiteral("<br>"));
    };
    if (!item || !item->parent()) {
        detailsPanel_->setHtml(QStringLiteral("<qt>%1</qt>").arg(escape(QStringLiteral("候補を選ぶと、読みと意味を表示します。"))));
        return;
    }
    const auto word = item->data(0, Qt::UserRole).toString();
    const auto details = repository_.wordDetails(word, language_, item->data(0, Qt::UserRole + 1).toString());
    if (!details.error.isEmpty()) {
        detailsPanel_->setHtml(QStringLiteral("<qt>詳細を取得できませんでした。<br>%1</qt>").arg(escape(details.error)));
        return;
    }
    const auto definition = [&](const QString &text) {
        return escape(text.trimmed().isEmpty() ? QStringLiteral("説明なし") : text);
    };
    QString html = QStringLiteral("<qt><b>%1</b>").arg(escape(word));
    if (details.showReadings)
        html += QStringLiteral("<br>読み：%1").arg(details.readings.isEmpty()
            ? QStringLiteral("未収録") : escape(details.readings.join(QStringLiteral("／"))));
    html += QStringLiteral("<p><b>現在の意味</b><br>%1</p><p><b>ほかに収録されている意味</b><br>")
        .arg(definition(item->data(0, Qt::UserRole + 2).toString()));
    QStringList others;
    for (const auto &meaning : details.otherMeanings)
        others.append(QStringLiteral("%1. %2").arg(others.size() + 1).arg(definition(meaning.definition)));
    html += others.isEmpty() ? QStringLiteral("なし") : others.join(QStringLiteral("<br><br>"));
    html += QStringLiteral("</p></qt>");
    detailsPanel_->setHtml(html);
}
QString MainWindow::kaikkiText(const QStringList &words) const {
    const auto direct = kaikki_.search(words, language_);
    QHash<qint64, QStringList> paths;
    QHash<qint64, KaikkiEntry> targets;
    QVector<KaikkiEntry> additional;
    for (const auto &entry : direct) paths[entry.id].append(entry.word);
    for (const auto &entry : direct) {
        for (const auto &sense : entry.senses) {
            for (const auto id : sense.targets) {
                if (!targets.contains(id)) targets.insert(id, kaikki_.entry(id, language_));
                const auto &target = targets[id];
                if (sense.targets.size() != 1 || !target.id || target.senses.isEmpty()) continue;
                if (!paths.contains(id)) additional.append(target);
                paths[id].append(QStringLiteral("%1から参照").arg(entry.word));
            }
        }
    }
    QStringList sections;
    QSet<qint64> listedPreviews;
    const auto appendEntry = [&](const KaikkiEntry &entry, bool followReferences) {
        QStringList lines;
        for (const auto &sense : entry.senses) {
            const auto single = sense.targets.size() == 1 ? targets.value(sense.targets.first()) : KaikkiEntry{};
            if (followReferences && single.id && !single.senses.isEmpty()) {
                lines.append(QStringLiteral("→ %1 の項目").arg(single.word));
            } else {
                lines.append(QStringLiteral("[%1] %2").arg(DictionaryRepository::languageName(sense.language),
                                                            sense.glosses.join(QStringLiteral(" / "))));
                if (followReferences && sense.targets.size() > 1) {
                    for (const auto id : sense.targets) {
                        const auto target = targets.value(id);
                        if (!target.id || target.senses.isEmpty()) continue;
                        const auto label = QStringLiteral("  → %1 [%2]").arg(target.word, target.pos);
                        if (paths.contains(id) || listedPreviews.contains(id)) {
                            lines.append(label + QStringLiteral("（別掲）"));
                        } else {
                            const auto &preview = target.senses.first();
                            lines.append(label + QStringLiteral(" [%1] %2").arg(
                                DictionaryRepository::languageName(preview.language), preview.glosses.first()));
                            listedPreviews.insert(id);
                        }
                    }
                }
            }
            if (!sense.synonyms.isEmpty()) lines.append(QStringLiteral("類語：%1").arg(sense.synonyms.join("、")));
        }
        if (!entry.synonyms.isEmpty()) lines.append(QStringLiteral("類語：%1").arg(entry.synonyms.join("、")));
        if (lines.isEmpty()) return;
        auto routes = paths.value(entry.id);
        routes.removeDuplicates();
        sections.append(QStringLiteral("%1 [%2]（%3）\n%4").arg(entry.word, entry.pos,
                            routes.join(QStringLiteral("、")), lines.join('\n')));
    };
    for (const auto &entry : direct) appendEntry(entry, true);
    for (const auto &entry : additional) appendEntry(entry, false);
    return sections.join("\n\n");
}
void MainWindow::showChoices(const QStringList &headwords, const QStringList &hints,
                             const QVector<QVector<qint64>> &entries) {
    // Each row shows the headword and, when known, its first meaning; choosing a
    // row searches that headword (recorded in history like the other-language button),
    // limited to its matched entries when there are any.
    spellingChoices_->clear();
    for (int i = 0; i < headwords.size(); ++i) {
        const auto hint = hints.value(i);
        auto *item = new QListWidgetItem(hint.isEmpty() ? headwords[i]
                                                        : QStringLiteral("%1 — %2").arg(headwords[i], hint), spellingChoices_);
        item->setData(Qt::UserRole, language_);
        item->setData(Qt::UserRole + 1, headwords[i]);
        QVariantList ids;
        for (const auto id : entries.value(i)) ids.append(id);
        item->setData(Qt::UserRole + 2, ids);
        if (!hint.isEmpty()) item->setToolTip(item->text());
    }
    spellingChoices_->show();
}
void MainWindow::showResult() {
    const auto &result = lastResult_;
    const auto &word = lastWord_;
    alternativeButton_->hide();
    formOfButton_->hide();
    spellingChoices_->hide();
    spellingChoices_->clear();
    results_->clear();
    showDetailsFor(nullptr);
    if (word.isEmpty()) return;
    heading_->show();
    if (!result.error.isEmpty()) { heading_->setText(result.error); return; }
    // Related ("similar") meanings can be long lists, so the viewer can hide them.
    const bool showRelated = showRelatedCheck_->isChecked() && !result.relatedGroups.isEmpty();
    const bool relatedHidden = !showRelatedCheck_->isChecked() && !result.relatedGroups.isEmpty();
    if (!alternativeLanguage_.isEmpty()) {
        alternativeButton_->setProperty("word", word);
        const auto name = DictionaryRepository::languageName(alternativeLanguage_);
        alternativeButton_->setText(QStringLiteral("%1にも見出しがあります → %1で検索").arg(name));
        alternativeButton_->show();
    }
    const auto quoted = [](const QStringList &words) {
        return QStringLiteral("「%1」").arg(words.join(QStringLiteral("」「")));
    };
    const auto relation = [](const QString &kind) {
        if (kind == "spelling") return QStringLiteral("の別表記");
        if (kind == "reading") return QStringLiteral("の読み");
        if (kind == "inflection") return QStringLiteral("の活用形");
        return QStringLiteral("として検索");
    };
    if (!result.formOf.isEmpty()) {
        formOfButton_->setText(QStringLiteral("%1%2としても検索").arg(quoted(result.formOf), relation(result.formOfKind)));
        formOfButton_->show();
    }
    // Say which headword was searched when it is not literally the input.
    QString target = QStringLiteral("「%1」").arg(word);
    if (!result.matchedWords.isEmpty())
        target += QStringLiteral("（%1%2）").arg(quoted(result.matchedWords), relation(result.matchKind));
    QString heading = QStringLiteral("%1で検索 — %2の類語候補：%3件（%4意味）")
        .arg(DictionaryRepository::languageName(language_), target)
        .arg(result.candidateCount()).arg(result.groups.size());
    if (showRelated)
        heading += QStringLiteral("、関連語：%1件").arg(result.relatedCount());
    else if (relatedHidden)
        heading += QStringLiteral("（似た意味の関連語%1件は非表示）").arg(result.relatedCount());
    heading_->setText(heading);
    if (!result.spellings.isEmpty()) {
        if (result.choiceKind == "reading")
            heading_->setText(QStringLiteral("「%1」と読む見出しが%2語あります。選んでください。").arg(word).arg(result.spellings.size()));
        else if (result.choiceKind == "inflection")
            heading_->setText(QStringLiteral("「%1」は%2の活用形です。選んでください。").arg(word, quoted(result.spellings)));
        else
            heading_->setText(QStringLiteral("%1 に一致しました。綴りを選んでください。").arg(result.spellings.join(" / ")));
        showChoices(result.spellings, result.choiceHints, result.choiceEntries);
        return;
    }
    if (!result.found) {
        heading_->setText(heading_->text() + (searchKaikki_.isEmpty()
            ? QStringLiteral("\n見出しが見つかりませんでした")
            : QStringLiteral("\nWordNetには見出しがありません")));
        return;
    }
    if (result.groups.isEmpty())
        heading_->setText(heading_->text() + (showRelated
            ? QStringLiteral("\n同じ意味の類語は収録されていないため、似た意味の関連語を表示します")
            : QStringLiteral("\n見出しはありますが、同じ意味の類語は収録されていません")));
    const auto addGroups = [this](const QVector<MeaningGroup> &groups, const QString &prefix) {
        for (const auto &group : groups) {
            auto *parent = new QTreeWidgetItem(results_, {prefix +
                (group.definition.isEmpty() ? QStringLiteral("説明なし") : group.definition)});
            parent->setToolTip(0, parent->text(0));
            parent->setForeground(0, QColor(palette().color(QPalette::Base).lightness() < 128 ? "#b4b4b4" : "#555555"));
            for (const auto &candidate : group.words) {
                auto *item = new QTreeWidgetItem(parent, {candidate});
                item->setData(0, Qt::UserRole, candidate);
                item->setData(0, Qt::UserRole + 1, group.id);
                item->setData(0, Qt::UserRole + 2, group.definition);
            }
            parent->setExpanded(true);
        }
    };
    addGroups(result.groups, QStringLiteral("意味："));
    if (showRelated) addGroups(result.relatedGroups, QStringLiteral("関連（似た意味）："));
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool plain = key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::KeypadModifier;
        // Down in the search box moves to the first candidate so that a word can be
        // chosen and copied without the mouse.
        if (object == input_ && plain && key->key() == Qt::Key_Down) {
            // A headword choice (homophones, case variants, several bases) comes first;
            // Enter on a row then searches it.
            if (!spellingChoices_->isHidden() && spellingChoices_->count() > 0) {
                spellingChoices_->setFocus(Qt::TabFocusReason);
                spellingChoices_->setCurrentRow(0);
                return true;
            }
            for (int i = 0; i < results_->topLevelItemCount(); ++i) {
                if (auto *group = results_->topLevelItem(i); group->childCount() > 0) {
                    results_->setFocus(Qt::TabFocusReason);
                    results_->setCurrentItem(group->child(0));
                    return true;
                }
            }
            return false;
        }
        if (object == results_ && plain) {
            auto *current = results_->currentItem();
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && current && current->parent()) {
                copyWord(current);
                return true;
            }
            // Up from the first candidate returns to the search box.
            if (key->key() == Qt::Key_Up && current && current->parent()
                && results_->itemAbove(current) == current->parent()
                && results_->indexOfTopLevelItem(current->parent()) == 0) {
                input_->setFocus(Qt::TabFocusReason);
                input_->selectAll();
                return true;
            }
        }
        return QMainWindow::eventFilter(object, event);
    }
    // The saved bottom-row height needs the real splitter height, known after the first layout.
    if (object == resultSplitter_ && event->type() == QEvent::Resize && !bottomSized_
        && resultSplitter_->isVisible() && resultSplitter_->height() > 0) {
        bottomSized_ = true;
        QTimer::singleShot(0, this, [this] { applyBottomSizes(); });
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::copyWord(QTreeWidgetItem *item) {
    if (!item || !item->parent()) return;
    const auto word = item->data(0, Qt::UserRole).toString();
    QApplication::clipboard()->setText(word);
    statusBar()->showMessage(QStringLiteral("「%1」をコピーしました").arg(word), 2500);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    settings_.setValue("window/geometry", saveGeometry());
    settings_.setValue("window/splitter", splitter_->saveState());
    settings_.setValue("window/kaikkiPanelHeight", bottomHeight_);
    settings_.setValue("window/detailsPanelWidth", detailsWidth_);
    settings_.sync();
    QMainWindow::closeEvent(event);
}
