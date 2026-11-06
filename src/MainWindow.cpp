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
#include <QHelpEvent>
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
    rightLayout->addWidget(heading_);
    heading_->hide();
    alternativeButton_ = new QPushButton;
    alternativeButton_->setObjectName("alternativeLanguageButton");
    alternativeButton_->hide();
    rightLayout->addWidget(alternativeButton_);
    connect(alternativeButton_, &QPushButton::clicked, this, [this] {
        search(alternativeButton_->property("word").toString(), alternativeLanguage_, true);
    });
    spellingChoices_ = new QListWidget;
    spellingChoices_->setObjectName("spellingChoices");
    spellingChoices_->setMaximumHeight(120);
    spellingChoices_->hide();
    rightLayout->addWidget(spellingChoices_);
    const auto chooseSpelling = [this](QListWidgetItem *item) {
        const auto word = item->text();
        const auto language = item->data(Qt::UserRole).toString();
        search(word, language, true);
    };
    connect(spellingChoices_, &QListWidget::itemClicked, this, chooseSpelling);
    connect(spellingChoices_, &QListWidget::itemActivated, this, chooseSpelling);
    results_ = new QTreeWidget;
    results_->setObjectName("results");
    results_->viewport()->installEventFilter(this);
    // Qt 6.8.2 does not invalidate QAccessibleTree's row cache on collapse.
    // Windows UI Automation can then traverse stale/cyclic sibling indexes.
    connect(results_, &QTreeWidget::itemCollapsed, this, [this] {
        QAccessibleTableModelChangeEvent event(results_, QAccessibleTableModelChangeEvent::ModelReset);
        QAccessible::updateAccessibility(&event);
    });
    results_->setHeaderHidden(true);
    results_->setWordWrap(true);
    results_->setContextMenuPolicy(Qt::CustomContextMenu);
    rightLayout->addWidget(results_);
    auto *hint = new QLabel(QStringLiteral("類語候補をダブルクリックでコピー ／ 右クリックで操作"));
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
void MainWindow::search(const QString &input, const QString &requestedLanguage, bool record) {
    const auto word = input.trimmed();
    if (word.isEmpty()) return;
    input_->setText(word);
    QToolTip::hideText();
    alternativeButton_->hide();
    spellingChoices_->hide();
    spellingChoices_->clear();
    results_->clear();
    heading_->show();
    const auto result = repository_.search(word, requestedLanguage);
    language_ = result.language;
    alternativeLanguage_ = result.alternativeLanguage;
    if (record) { history_.add(word, language_); refreshHistory(); }
    if (!result.error.isEmpty()) { heading_->setText(result.error); return; }
    if (!alternativeLanguage_.isEmpty()) {
        alternativeButton_->setProperty("word", word);
        const auto name = DictionaryRepository::languageName(alternativeLanguage_);
        alternativeButton_->setText(QStringLiteral("%1にも見出しがあります → %1で検索").arg(name));
        alternativeButton_->show();
    }
    heading_->setText(QStringLiteral("%4で検索 — 「%1」の類語候補：%2件（%3意味）")
        .arg(word).arg(result.candidateCount()).arg(result.groups.size()).arg(DictionaryRepository::languageName(language_)));
    if (!result.spellings.isEmpty()) {
        heading_->setText(QStringLiteral("%1 に一致しました。綴りを選んでください。").arg(result.spellings.join(" / ")));
        for (const auto &spelling : result.spellings) {
            auto *item = new QListWidgetItem(spelling, spellingChoices_);
            item->setData(Qt::UserRole, language_);
        }
        spellingChoices_->show();
        return;
    }
    if (result.groups.isEmpty()) {
        heading_->setText(heading_->text() + (result.found ? QStringLiteral("\n見出しはありますが、同じ意味の類語は収録されていません")
            : QStringLiteral("\n見出しが見つかりませんでした")));
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
            item->setData(0, Qt::UserRole + 1, group.id);
            item->setData(0, Qt::UserRole + 2, group.definition);
        }
        parent->setExpanded(true);
    }
}
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (object != results_->viewport() || event->type() != QEvent::ToolTip)
        return QMainWindow::eventFilter(object, event);
    auto *help = static_cast<QHelpEvent *>(event);
    auto *item = results_->itemAt(help->pos());
    if (!item || !item->parent())
        return QMainWindow::eventFilter(object, event);
    const auto word = item->data(0, Qt::UserRole).toString();
    const auto details = repository_.wordDetails(word, language_, item->data(0, Qt::UserRole + 1).toString());
    const auto escape = [](const QString &text) {
        return text.toHtmlEscaped().replace('\n', QStringLiteral("<br>"));
    };
    const auto definition = [&](const QString &text) {
        return escape(text.trimmed().isEmpty() ? QStringLiteral("説明なし") : text);
    };
    QString tooltip = QStringLiteral("<qt><table width='380'><tr><td><b>%1</b>").arg(escape(word));
    if (details.showReadings)
        tooltip += QStringLiteral("<br>読み：%1").arg(details.readings.isEmpty()
            ? QStringLiteral("未収録") : escape(details.readings.join(QStringLiteral("／"))));
    tooltip += QStringLiteral("<p><b>現在の意味</b><br>%1</p><p><b>ほかに収録されている意味</b><br>")
        .arg(definition(item->data(0, Qt::UserRole + 2).toString()));
    QStringList others;
    for (const auto &meaning : details.otherMeanings)
        others.append(QStringLiteral("%1. %2").arg(others.size() + 1).arg(definition(meaning.definition)));
    tooltip += others.isEmpty() ? QStringLiteral("なし") : others.join(QStringLiteral("<br><br>"));
    tooltip += QStringLiteral("</p></td></tr></table></qt>");
    if (!details.error.isEmpty())
        tooltip = QStringLiteral("<qt><table width='380'><tr><td>詳細を取得できませんでした。<br>%1</td></tr></table></qt>")
            .arg(escape(details.error));
    QToolTip::setFont(displayFont());
    QToolTip::showText(help->globalPos(), tooltip, results_->viewport(), results_->visualItemRect(item));
    return true;
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
