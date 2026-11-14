#include "porymapz/battleeditor.h"
#include "porymapz/dialogueeditor.h"
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace PorymapZ {
namespace {
class BattleValueDelegate : public QStyledItemDelegate {
public:
    explicit BattleValueDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QStringList constants;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.column() != 1) return nullptr;
        auto *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            auto *completer = new QCompleter(constants, line);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            line->setCompleter(completer);
        }
        return editor;
    }
};
}

BattleEditor::BattleEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("battleEditor");
    auto *layout = new QVBoxLayout(this);
    m_views = new QTabWidget(this);
    m_views->setObjectName("battleViews");
    auto *data = new QWidget(this);
    auto *dataLayout = new QVBoxLayout(data);
    auto *help = new QLabel(tr("Edit trainers and their Pokémon parties. Expand a record and double-click a value to edit any C expression: class, name, portrait, music, items, AI, battle format, species, level, IVs, held item, or moves. Party flags and party type must agree. Build and test your game after saving."), this);
    help->setWordWrap(true);
    dataLayout->addWidget(help);
    auto *fileRow = new QHBoxLayout;
    m_files = new QComboBox(this);
    m_files->setObjectName("battleFiles");
    auto *reload = new QPushButton(tr("Reload"), this);
    fileRow->addWidget(m_files, 1); fileRow->addWidget(reload);
    layout->addLayout(fileRow);
    m_search = new QLineEdit(this);
    m_search->setObjectName("battleSearch");
    m_search->setPlaceholderText(tr("Search trainer IDs, party names, or values…"));
    dataLayout->addWidget(m_search);
    auto *split = new QSplitter(this);
    m_records = new QListWidget(split); m_records->setObjectName("battleRecords");
    m_fields = new QTreeWidget(split); m_fields->setObjectName("battleFields");
    m_fields->setItemDelegate(new BattleValueDelegate(m_fields));
    m_fields->setHeaderLabels({tr("Field"), tr("Value / C expression")});
    m_fields->setColumnWidth(0, 210);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 3);
    dataLayout->addWidget(split, 1);
    auto *actions = new QHBoxLayout;
    auto *duplicate = new QPushButton(tr("Duplicate Trainer / Party / Pokémon"), this);
    duplicate->setObjectName("duplicateBattleRecord");
    auto *remove = new QPushButton(tr("Delete Selected"), this);
    remove->setObjectName("deleteBattleRecord");
    auto *addPokemon = new QPushButton(tr("Add Pokémon"), this);
    addPokemon->setObjectName("addBattlePokemon");
    auto *goParty = new QPushButton(tr("Go to Party"), this);
    goParty->setObjectName("goToBattleParty");
    actions->addWidget(addPokemon); actions->addWidget(goParty);
    actions->addWidget(duplicate); actions->addWidget(remove); actions->addStretch();
    dataLayout->addLayout(actions);
    m_status = new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);

    m_views->addTab(data, tr("Trainers & Parties"));
    m_source = new QPlainTextEdit(this); m_source->setObjectName("battleSource");
    m_source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_source->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_views->addTab(m_source, tr("Data Source"));
    m_scripts = new DialogueEditor(this);
    m_scripts->setObjectName("battleScripts");
    m_scripts->findChild<QTabWidget *>("dialogueViews")->setCurrentIndex(1);
    m_views->addTab(m_scripts, tr("Battle Scripts"));
    layout->addWidget(m_views, 1);
    layout->addWidget(m_status);
    m_save = new QPushButton(tr("Save Battles"), this); m_save->setObjectName("saveBattles");
    layout->addWidget(m_save, 0, Qt::AlignRight);
    connect(m_save, &QPushButton::clicked, this, &BattleEditor::saveChanges);
    connect(m_scripts, &DialogueEditor::dirtyChanged, this, [this] { m_save->setEnabled(hasUnsavedChanges()); emit dirtyChanged(hasUnsavedChanges()); });
    connect(m_scripts, &DialogueEditor::sourceSaved, this, &BattleEditor::sourceSaved);
    connect(m_files, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BattleEditor::loadFile);
    connect(reload, &QPushButton::clicked, this, [this] { if (confirmChanges()) loadFile(m_files->currentIndex()); });
    connect(m_records, &QListWidget::currentRowChanged, this, &BattleEditor::showRecord);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < m_records->count(); ++row) {
            const auto node = m_document.nodes().at(m_document.records().at(row));
            m_records->item(row)->setHidden(!node.name.contains(text, Qt::CaseInsensitive)
                && !m_document.source().mid(node.start, node.end-node.start).contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_fields, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (column != 1) {
            QSignalBlocker blocker(m_fields);
            item->setText(0, m_document.nodes().at(index).name);
            return;
        }
        if (!m_document.setValue(index, item->text(1))) {
            QSignalBlocker blocker(m_fields);
            const auto node = m_document.nodes().at(index);
            item->setText(1, m_document.source().mid(node.valueStart, node.end-node.valueStart));
            m_status->setText(tr("Enter one balanced C expression. Use Data Source for structural changes."));
            return;
        }
        QSignalBlocker blocker(m_source); m_source->setPlainText(m_document.source());
        setDirty(m_document.source() != m_file.sourceText());
    });
    connect(m_source, &QPlainTextEdit::textChanged, this, [this] {
        m_document.parse(m_source->toPlainText());
        render(m_records->currentRow());
        setDirty(m_source->toPlainText() != QString(m_file.sourceText()).replace("\r\n", "\n"));
    });
    connect(addPokemon, &QPushButton::clicked, this, [this] {
        const int row = m_records->currentRow();
        if (row < 0 || !m_document.appendPokemon(m_document.records().at(row))) {
            m_status->setText(tr("Select a Pokémon party with fewer than six members first.")); return;
        }
        { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
        render(row); setDirty(true);
    });
    connect(goParty, &QPushButton::clicked, this, [this] {
        const int row = m_records->currentRow();
        if (row < 0) return;
        const auto node = m_document.nodes().at(m_document.records().at(row));
        const auto match = QRegularExpression("\\bsParty_[A-Za-z0-9_]+\\b").match(m_document.source().mid(node.valueStart, node.end-node.valueStart));
        if (!match.hasMatch()) { m_status->setText(tr("No sParty_ reference found. Select Pokémon Parties to browse custom party names.")); return; }
        const auto name = match.captured();
        m_files->setCurrentIndex(1);
        for (int i=0; i<m_records->count(); ++i) if (m_records->item(i)->text() == name) { m_search->clear(); m_records->setCurrentRow(i); break; }
    });
    connect(duplicate, &QPushButton::clicked, this, [this] { mutate(false); });
    connect(remove, &QPushButton::clicked, this, [this] { mutate(true); });
    setScene({}, {}, {});
}
bool BattleEditor::hasUnsavedChanges() const { return m_dirty || m_scripts->hasUnsavedChanges(); }
void BattleEditor::setDirty(bool dirty) {
    m_dirty = dirty;
    m_save->setEnabled(hasUnsavedChanges());
    emit dirtyChanged(hasUnsavedChanges());
}
bool BattleEditor::editable(const QString &path) const {
    const QFileInfo info(path);
    return !m_root.isEmpty() && !info.isSymLink() && info.canonicalFilePath().startsWith(m_root + '/');
}
bool BattleEditor::confirmChanges() {
    if (m_dirty) {
        const auto answer = QMessageBox::warning(this, tr("Unsaved Battle Edits"), tr("Save changes to %1?").arg(m_path), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save) { if (!saveChanges()) return false; }
        else {
            m_document.parse(m_file.sourceText());
            { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
            render(); setDirty(false);
        }
    }
    return m_scripts->confirmChanges();
}
bool BattleEditor::setScene(const QString &root, const QString &map, const QString &scripts, bool supported) {
    if (!confirmChanges()) return false;
    if (!m_scripts->setScene(root, map, scripts, supported)) return false;
    const QString canonical = QFileInfo(root).canonicalFilePath();
    if (m_files->count() && m_root == canonical && m_files->isEnabled() == (!canonical.isEmpty() && !map.isEmpty() && supported)) return true;
    m_root = canonical;
    auto *delegate = static_cast<BattleValueDelegate *>(m_fields->itemDelegate());
    delegate->constants = {"TRUE", "FALSE"};
    for (const QString &name : {"species", "moves", "items", "trainers", "trainer_types", "battle_ai"}) {
        QFile constants(m_root + "/include/constants/" + name + ".h");
        if (!m_root.isEmpty() && constants.open(QIODevice::ReadOnly)) {
            const auto source = QString::fromUtf8(constants.readAll());
            auto matches = QRegularExpression("^\\s*#define\\s+([A-Za-z_][A-Za-z0-9_]*)", QRegularExpression::MultilineOption).globalMatch(source);
            while (matches.hasNext()) delegate->constants.append(matches.next().captured(1));
        }
    }
    delegate->constants.removeDuplicates(); delegate->constants.sort();
    const bool available = !m_root.isEmpty() && !map.isEmpty() && supported;
    QSignalBlocker blocker(m_files); m_files->clear(); m_fileIndex = -1;
    m_files->addItem(tr("Trainers"), "src/data/trainers.h");
    m_files->addItem(tr("Pokémon Parties"), "src/data/trainer_parties.h");
    m_files->addItem(tr("Trainer IDs (Data Source)"), "include/constants/trainers.h");
    m_files->setEnabled(available);
    m_source->setEnabled(available);
    m_fields->setEnabled(available);
    loadFile(available ? 0 : -1);
    if (!available) m_status->setText(tr("Open a pokeemerald assembly project and select a map to edit battles."));
    return true;
}
void BattleEditor::loadFile(int index) {
    if (!confirmChanges()) { QSignalBlocker block(m_files); m_files->setCurrentIndex(m_fileIndex); return; }
    m_fileIndex = index;
    m_path = index >= 0 ? m_root + '/' + m_files->itemData(index).toString() : QString();
    m_file = DialogueDocument();
    QString error;
    const bool loaded = index >= 0 && editable(m_path) && m_file.load(m_path, &error);
    m_document.parse(loaded ? m_file.sourceText() : QString());
    { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
    m_source->setEnabled(loaded); m_fields->setEnabled(loaded);
    render(); setDirty(false);
    m_status->setText(loaded ? tr("%1 records. Select a trainer or party. Select a Pokémon group to duplicate or delete it. New trainer IDs must also be defined in include/constants/trainers.h.").arg(m_document.records().size()) : tr("Cannot load battle data: %1 %2").arg(m_path, error));
}
void BattleEditor::render(int row) {
    QSignalBlocker blocker(m_records); m_records->clear();
    for (int index : m_document.records()) m_records->addItem(m_document.nodes().at(index).name);
    m_records->setCurrentRow(m_records->count() ? qBound(0, row, m_records->count()-1) : -1);
    showRecord(m_records->currentRow());
    emit m_search->textChanged(m_search->text());
}
void BattleEditor::showRecord(int row) {
    QSignalBlocker block(m_fields); m_fields->clear();
    if (row < 0 || row >= m_document.records().size()) return;
    addFields(m_document.records().at(row), nullptr);
    m_fields->expandAll();
}
void BattleEditor::addFields(int index, QTreeWidgetItem *parent) {
    const auto node = m_document.nodes().at(index);
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_fields);
    item->setText(0, node.name); item->setData(0, Qt::UserRole, index);
    if (!node.group) {
        item->setText(1, m_document.source().mid(node.valueStart, node.end-node.valueStart));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    for (int child : node.children) addFields(child, item);
}
void BattleEditor::mutate(bool remove) {
    const int row = m_records->currentRow();
    if (row < 0) return;
    const int index = m_fields->currentItem() ? m_fields->currentItem()->data(0, Qt::UserRole).toInt() : m_document.records().at(row);
    const auto node = m_document.nodes().at(index);
    const bool record = m_document.records().contains(index);
    const bool pokemon = node.group && node.parent >= 0 && m_document.records().contains(node.parent);
    if (!record && !pokemon) { m_status->setText(tr("Select a trainer, party, or Pokémon group first.")); return; }
    QString name;
    if (!remove && record) {
        bool ok;
        name = QInputDialog::getText(this, tr("Duplicate Battle Record"), node.parent < 0 ? tr("New party C symbol:") : tr("New TRAINER_ constant (define it in include/constants/trainers.h too):"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
    }
    if (remove && QMessageBox::question(this, tr("Delete Battle Record"), tr("Delete %1? Update references in scripts and trainer data before building.").arg(node.name)) != QMessageBox::Yes) return;
    if (!(remove ? m_document.remove(index) : m_document.duplicate(index, name))) {
        m_status->setText(tr("Cannot change this record. Check that the new name is valid and unique, or use Data Source.")); return;
    }
    { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
    render(row); setDirty(m_document.source() != m_file.sourceText());
}
bool BattleEditor::saveChanges() {
    if (m_dirty) {
        QString error;
        if (!editable(m_path)) error = tr("The data file is outside the project or is a symlink.");
        if (!error.isEmpty() || !m_file.saveSource(m_source->toPlainText(), &error)) {
            QMessageBox::warning(this, tr("Cannot Save Battles"), error); return false;
        }
        m_document.parse(m_file.sourceText());
        { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
        render(m_records->currentRow()); setDirty(false);
        m_status->setText(tr("Saved. Build and test your project to verify battle behavior."));
        emit sourceSaved(m_path);
    }
    return m_scripts->saveChanges();
}
}
