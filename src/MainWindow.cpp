#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QStyledItemDelegate>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QToolTip>
#include <functional>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QSpinBox>
#include <QPushButton>
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
class HistoryEditorDelegate : public QStyledItemDelegate {
public:
    using Save = std::function<QString(const QString &, const QString &)>;
    HistoryEditorDelegate(QListWidget *list, Save save, std::function<void(const QString &)> committed)
        : QStyledItemDelegate(list), list_(list), save_(std::move(save)), committed_(std::move(committed)) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *editor = new QLineEdit(parent);
        editor->setObjectName("historyEditor");
        editor->setFont(list_->font());
        return editor;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override {
        auto *editor = static_cast<QLineEdit *>(widget);
        editor->setText(index.data(Qt::EditRole).toString());
        editor->setProperty("originalWord", editor->text());
        editor->selectAll();
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model, const QModelIndex &index) const override {
        QSignalBlocker blocker(list_); // Search once, explicitly, after committing and closing.
        model->setData(index, static_cast<QLineEdit *>(widget)->text().trimmed(), Qt::EditRole);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *editor = qobject_cast<QLineEdit *>(object);
        if (!editor) return QStyledItemDelegate::eventFilter(object, event);
        if (event->type() == QEvent::KeyPress) {
            const auto key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                const auto error = save_(editor->property("originalWord").toString(), editor->text());
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
    historyList_->setItemDelegate(new HistoryEditorDelegate(historyList_, [this](const QString &original, const QString &replacement) {
        const auto error = history_.edit(original, replacement, language_);
        if (!error.isEmpty()) statusBar()->showMessage(error, 4000);
        return error;
    }, [this](const QString &word) { search(word); }));
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
    heading_ = new QLabel(QStringLiteral("単語を入力して検索してください"));
    heading_->setObjectName("resultHeading");
    heading_->setTextFormat(Qt::PlainText);
    heading_->setWordWrap(true);
    rightLayout->addWidget(heading_);
    results_ = new QTreeWidget;
    results_->setObjectName("results");
    results_->setHeaderHidden(true);
    results_->setWordWrap(true);
    results_->setContextMenuPolicy(Qt::CustomContextMenu);
    rightLayout->addWidget(results_);
    auto *hint = new QLabel(QStringLiteral("類語候補をダブルクリックでコピー ／ 右クリックで操作"));
    hint->setObjectName("resultHint");
    hint->setWordWrap(true);
    rightLayout->addWidget(hint);
    setCentralWidget(splitter_);
    splitter_->setSizes({300, 700});
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const auto word = input_->text().trimmed();
        if (word.isEmpty()) return;
        const auto before = history_.words(language_);
        history_.add(word, language_);
        if (history_.words(language_) != before) refreshHistory();
        search(word);
    });
    connect(historyList_, &QListWidget::currentTextChanged, this, [this](const QString &word) { search(word); });
    connect(historyList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (input_->text() != item->text()) search(item->text());
    });
    connect(historyList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { search(item->text()); });
    connect(results_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { copyWord(item); });
    connect(results_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = results_->itemAt(point);
        if (!item || !item->parent()) return;
        const auto word = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        auto *copy = menu.addAction(QStringLiteral("コピー"));
        auto *find = menu.addAction(QStringLiteral("この単語で検索"));
        auto *selected = menu.exec(results_->viewport()->mapToGlobal(point));
        if (selected == copy) copyWord(item);
        else if (selected == find) search(word);
    });
    refreshHistory();
    restoreGeometry(settings_.value("window/geometry").toByteArray());
    bool visible = false;
    for (auto *screen : QGuiApplication::screens())
        visible |= screen->availableGeometry().intersects(frameGeometry());
    if (!visible && QGuiApplication::primaryScreen())
        move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
    splitter_->restoreState(settings_.value("window/splitter").toByteArray());
    if (!repository_.open(dictionaryPath)) heading_->setText(repository_.error());
    statusBar()->showMessage(QStringLiteral("日本語・完全一致検索"));
    applyDisplaySettings();
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
    historyList_->clear();
    historyList_->addItems(history_.words(language_));
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
    history_.remove(item->text(), language_);
    QSignalBlocker blocker(historyList_);
    delete historyList_->takeItem(historyList_->row(item));
    historyList_->setCurrentRow(-1);
}
void MainWindow::search(const QString &input) {
    const auto word = input.trimmed();
    if (word.isEmpty()) return;
    input_->setText(word);
    results_->clear();
    const auto result = repository_.search(word, language_);
    if (!result.error.isEmpty()) { heading_->setText(result.error); return; }
    heading_->setText(QStringLiteral("「%1」の類語候補：%2件（%3意味）")
        .arg(word).arg(result.candidateCount()).arg(result.groups.size()));
    if (result.groups.isEmpty()) {
        heading_->setText(heading_->text() + QStringLiteral("\n類語が見つかりませんでした"));
        return;
    }
    for (const auto &group : result.groups) {
        auto *parent = new QTreeWidgetItem(results_, {QStringLiteral("意味：") +
            (group.definition.isEmpty() ? QStringLiteral("説明なし") : group.definition)});
        parent->setToolTip(0, parent->text(0));
        parent->setForeground(0, QColor(palette().color(QPalette::Base).lightness() < 128 ? "#b4b4b4" : "#555555"));
        for (const auto &candidate : group.words) {
            auto *item = new QTreeWidgetItem(parent, {candidate});
            item->setData(0, Qt::UserRole, candidate);
        }
        parent->setExpanded(true);
    }
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
    settings_.sync();
    QMainWindow::closeEvent(event);
}
