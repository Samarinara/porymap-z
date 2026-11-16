#include "porymapz/scripteditor.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

namespace PorymapZ {
ScriptEditor::ScriptEditor(const QString &root, const QStringList &sources, const QStringList &maps,
                         const QString &currentMap, const QString &label, QWidget *parent)
    : QDialog(parent), m_root(root), m_sources(sources), m_maps(maps) {
    setObjectName("mapScriptEditor");
    setWindowTitle(tr("Map Script Editor"));
    resize(1100, 720);
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Browse map events and their referenced scripts. Saving replaces the selected label block in its original source file."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    m_scope = new QComboBox(this);
    m_scope->setObjectName("scriptMap");
    layout->addWidget(m_scope);
    m_search = new QLineEdit(this);
    m_search->setObjectName("scriptSearch");
    m_search->setPlaceholderText(tr("Search script labels, event groups, or source paths…"));
    layout->addWidget(m_search);
    auto *split = new QSplitter(this);
    m_tree = new QTreeWidget(split);
    m_tree->setObjectName("scriptTree");
    m_tree->setHeaderLabels({tr("Scripts"), tr("Source")});
    m_text = new QPlainTextEdit(split);
    m_text->setObjectName("scriptSource");
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setReadOnly(true);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    m_location = new QLabel(this);
    m_location->setTextFormat(Qt::PlainText);
    m_location->setWordWrap(true);
    layout->addWidget(m_location);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *reload = buttons->addButton(tr("Reload Scripts"), QDialogButtonBox::ActionRole);
    connect(reload, &QPushButton::clicked, this, [this] {
        if (!confirmChanges()) return;
        const QString label = m_selected ? m_selected->text(0) : QString();
        m_document.scan(m_root, m_sources, m_maps);
        rebuild(label);
    });
    m_save = buttons->addButton(tr("Save Script"), QDialogButtonBox::ActionRole);
    m_save->setObjectName("saveMapScript");
    m_save->setEnabled(false);
    layout->addWidget(buttons);
    m_document.scan(root, sources, maps);
    m_scope->addItem(tr("All project scripts"));
    for (const auto &map : m_document.maps()) m_scope->addItem(map.name);
    const int current = m_scope->findText(currentMap);
    m_scope->setCurrentIndex(current < 0 ? 0 : current);
    m_scopeIndex = m_scope->currentIndex();
    connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!confirmChanges()) { QSignalBlocker block(m_scope); m_scope->setCurrentIndex(m_scopeIndex); return; }
        m_scopeIndex = index; rebuild();
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) { select(item); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(m_text->document(), &QTextDocument::modificationChanged, this, [this](bool dirty) { m_save->setEnabled(dirty && m_index >= 0); });
    connect(m_save, &QPushButton::clicked, this, [this] { save(); });
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] { if (m_save->isEnabled()) save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &ScriptEditor::reject);
    // A requested event label may be shared and absent from the map's local file.
    rebuild(label);
    if (!label.isEmpty() && (!m_selected || m_selected->text(0) != label)) {
        m_scope->setCurrentIndex(0); rebuild(label);
    }
}
void ScriptEditor::rebuild(const QString &label) {
    QSignalBlocker blocker(m_tree);
    m_tree->clear(); m_selected = nullptr; m_index = -1;
    m_text->clear(); m_text->document()->setModified(false); m_text->setReadOnly(true);
    const auto addBlock = [this](QTreeWidgetItem *parent, int index) {
        const auto &block = m_document.blocks().at(index);
        auto *item = new QTreeWidgetItem(parent, {block.label, QDir(m_root).relativeFilePath(block.path)});
        item->setData(0, Qt::UserRole, index);
        item->setToolTip(0, block.path + ':' + QString::number(block.line));
        return item;
    };
    if (m_scopeIndex == 0) {
        QMap<QString, QTreeWidgetItem *> files;
        for (int i = 0; i < m_document.blocks().size(); ++i) {
            const auto path = m_document.blocks().at(i).path;
            if (!files.contains(path)) files[path] = new QTreeWidgetItem(m_tree, {QDir(m_root).relativeFilePath(path)});
            addBlock(files[path], i);
        }
    } else {
        const auto &map = m_document.maps().at(m_scopeIndex - 1);
        QSet<int> roots, reachable;
        for (auto group = map.groups.cbegin(); group != map.groups.cend(); ++group) {
            auto *category = new QTreeWidgetItem(m_tree, {group.key()});
            for (const auto &entry : group.value()) {
                const auto found = m_document.find(entry);
                if (found.isEmpty()) {
                    auto *item = new QTreeWidgetItem(category, {entry, tr("Definition not found")});
                    item->setToolTip(0, tr("No editable assembly definition was found. Poryscript-generated sources are excluded."));
                }
                for (int index : found) { addBlock(category, index); roots.insert(index); }
            }
        }
        QVector<int> pending;
        for (int index : roots) pending.append(index);
        while (!pending.isEmpty()) {
            int index = pending.takeLast();
            if (reachable.contains(index)) continue;
            reachable.insert(index);
            for (const auto &reference : m_document.blocks().at(index).references)
                for (int target : m_document.find(reference))
                    if (!reachable.contains(target)) pending.append(target);
        }
        auto *shared = new QTreeWidgetItem(m_tree, {tr("Referenced scripts and text")});
        for (int i = 0; i < m_document.blocks().size(); ++i)
            if (reachable.contains(i) && !roots.contains(i)) addBlock(shared, i);
        if (!shared->childCount()) delete shared;
    }
    m_tree->expandAll();
    QTreeWidgetItem *target = nullptr;
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).isValid() && (!target || (*it)->text(0) == label)) target = *it;
        if (target && target->text(0) == label) break;
        ++it;
    }
    m_tree->setCurrentItem(target);
    select(target);
    filter();
    if (!target) m_location->setText(tr("No scripts found for this selection. Try All project scripts.") + '\n' + m_document.warnings().join('\n'));
}
void ScriptEditor::select(QTreeWidgetItem *item) {
    if (item == m_selected) return;
    if (!confirmChanges()) { QSignalBlocker block(m_tree); m_tree->setCurrentItem(m_selected); return; }
    m_selected = item;
    m_index = item && item->data(0, Qt::UserRole).isValid() ? item->data(0, Qt::UserRole).toInt() : -1;
    m_text->setReadOnly(m_index < 0);
    m_text->setPlainText(m_index < 0 ? QString() : m_document.text(m_index));
    m_text->document()->setModified(false);
    if (m_index >= 0) {
        const auto &block = m_document.blocks().at(m_index);
        m_location->setText(tr("%1 : line %2").arg(QDir(m_root).relativeFilePath(block.path)).arg(block.line));
        m_location->setToolTip(m_document.warnings().join('\n'));
    } else m_location->setText(tr("Select a script label to edit."));
}
void ScriptEditor::filter() {
    const QString query = m_search->text();
    std::function<bool(QTreeWidgetItem *, bool)> visit = [&](QTreeWidgetItem *item, bool parentMatches) {
        const bool matches = parentMatches || item->text(0).contains(query, Qt::CaseInsensitive) || item->text(1).contains(query, Qt::CaseInsensitive);
        bool visible = matches;
        for (int i = 0; i < item->childCount(); ++i) visible = visit(item->child(i), matches) || visible;
        item->setHidden(!visible); return visible;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i), false);
}
bool ScriptEditor::save() {
    if (m_index < 0) return false;
    QString error;
    if (!m_document.save(m_index, m_text->toPlainText(), &error)) {
        QMessageBox::warning(this, tr("Cannot Save Script"), error); return false;
    }
    m_text->document()->setModified(false);
    m_location->setText(tr("Saved. Use Reload Scripts to refresh added or renamed labels and map references."));
    return true;
}
bool ScriptEditor::confirmChanges() {
    if (!m_text->document()->isModified()) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved Script"), tr("Save changes to the selected script?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) return save();
    if (choice == QMessageBox::Cancel) return false;
    m_text->document()->setModified(false); return true;
}
void ScriptEditor::reject() { if (confirmChanges()) QDialog::reject(); }
void ScriptEditor::closeEvent(QCloseEvent *event) {
    if (confirmChanges()) event->accept(); else event->ignore();
}
}
