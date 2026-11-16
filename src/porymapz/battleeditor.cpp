#include "porymapz/battleeditor.h"
#include "porymapz/dialogueeditor.h"
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
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
#include <QSpinBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QHeaderView>
#include <QTreeWidgetItemIterator>
#include <QMap>
#include <QKeyEvent>

namespace PorymapZ {
namespace {
enum { FieldRole = Qt::UserRole + 1 };
QString trainerId(QString name) {
    if (name.startsWith('[') && name.endsWith(']')) name = name.mid(1, name.size()-2).trimmed();
    return name;
}
QString referencedParty(const BattleDocument &document, int record) {
    if (!document.isTrainer(record) || document.isPartySource()) return {};
    for (int child : document.nodes().at(record).children) {
        const auto field = document.nodes().at(child);
        if (field.name != ".party" && !field.name.startsWith(".party.")) continue;
        const int value = field.group && !field.children.isEmpty() ? field.children.first() : child;
        const auto node = document.nodes().at(value);
        auto reference = document.source().mid(node.valueStart, node.end-node.valueStart).trimmed();
        const auto macro = QRegularExpression("^(?:NO_ITEM_DEFAULT_MOVES|NO_ITEM_CUSTOM_MOVES|ITEM_DEFAULT_MOVES|ITEM_CUSTOM_MOVES)\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)$").match(reference);
        if (macro.hasMatch()) reference = macro.captured(1);
        if (reference != "NULL" && QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(reference).hasMatch()) return reference;
    }
    return {};
}
QString fieldLabel(const QString &name) {
    static const QMap<QString, QString> labels = {
        {".trainerName", QObject::tr("Name")}, {".trainerClass", QObject::tr("Trainer class")},
        {".trainerPic", QObject::tr("Portrait")}, {".encounterMusic_gender", QObject::tr("Encounter music / gender")},
        {".doubleBattle", QObject::tr("Battle format")}, {".aiFlags", QObject::tr("AI flags")},
        {".partyFlags", QObject::tr("Party format flags")}, {".partySize", QObject::tr("Party size")},
        {".party", QObject::tr("Party reference")}, {".items", QObject::tr("Battle items")},
        {".iv", QObject::tr("IV quality (0–255)")}, {".lvl", QObject::tr("Level")},
        {".species", QObject::tr("Species")}, {".heldItem", QObject::tr("Held item")},
        {".moves", QObject::tr("Moves")}, {".ability", QObject::tr("Ability")},
        {".nature", QObject::tr("Nature")}, {".ivs", QObject::tr("IVs (0–31 per stat)")},
        {".evs", QObject::tr("EVs")}, {".battleType", QObject::tr("Battle format")}
    };
    return labels.value(name, name);
}
QString trainerName(const QString &value) {
    const auto match = QRegularExpression(QStringLiteral("^(?:_\\()?\"((?:[^\"\\\\]|\\\\.)*)\"\\)?$")).match(value);
    return match.hasMatch() ? match.captured(1) : value;
}
class BattleValueDelegate : public QStyledItemDelegate {
public:
    explicit BattleValueDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QStringList constants;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.column() != 1) return nullptr;
        const QString field = index.sibling(index.row(), 0).data(FieldRole).toString();
        const QString value = index.data(Qt::EditRole).toString();
        if ((field == ".doubleBattle" && (value == "TRUE" || value == "FALSE" || value == "Yes" || value == "No"))
            || (field == ".battleType" && (value == "Singles" || value == "Doubles"))) {
            auto *combo = new QComboBox(parent);
            const bool yesNo = value == "Yes" || value == "No";
            combo->addItem(tr("Single battle"), field == ".battleType" ? "Singles" : yesNo ? "No" : "FALSE");
            combo->addItem(tr("Double battle"), field == ".battleType" ? "Doubles" : yesNo ? "Yes" : "TRUE");
            auto *delegate = const_cast<BattleValueDelegate *>(this);
            connect(combo, QOverload<int>::of(&QComboBox::activated), delegate, [delegate, combo] {
                emit delegate->commitData(combo); emit delegate->closeEditor(combo);
            });
            return combo;
        }
        if ((field == ".lvl" || field == ".iv") && QRegularExpression("^[0-9]+$").match(value).hasMatch()) {
            auto *spin = new QSpinBox(parent);
            spin->setRange(field == ".lvl" ? 1 : 0, field == ".lvl" ? 100 : 255);
            return spin;
        }
        auto *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            if (field == ".trainerName") return editor;
            QString prefix;
            if (field == ".species") prefix = "SPECIES_";
            else if (field == ".heldItem") prefix = "ITEM_";
            else if (field == ".ability") prefix = "ABILITY_";
            else if (field == ".nature") prefix = "NATURE_";
            else if (field == ".trainerClass") prefix = "TRAINER_CLASS_";
            else if (field == ".trainerPic") prefix = "TRAINER_PIC_";
            else if (index.parent().data(FieldRole).toString() == ".moves") prefix = "MOVE_";
            else if (index.parent().data(FieldRole).toString() == ".items") prefix = "ITEM_";
            QStringList choices;
            for (const auto &constant : constants) if (prefix.isEmpty() || constant.startsWith(prefix)) choices.append(constant);
            auto *completer = new QCompleter(choices, line);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            line->setCompleter(completer);
        }
        return editor;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) combo->setCurrentIndex(combo->findData(index.data(Qt::EditRole)));
        else if (auto *spin = qobject_cast<QSpinBox *>(editor)) spin->setValue(index.data(Qt::EditRole).toInt());
        else {
            QStyledItemDelegate::setEditorData(editor, index);
            if (auto *line = qobject_cast<QLineEdit *>(editor)) {
                line->setProperty("battleNameWrapped", QVariant());
                const QString field = index.sibling(index.row(), 0).data(FieldRole).toString();
                const QString value = index.data(Qt::EditRole).toString();
                const auto match = QRegularExpression(QStringLiteral("^(_\\()?\"([^\"\\\\]*)\"\\)?$")).match(value);
                if (field == ".trainerName" && match.hasMatch()) {
                    line->setProperty("battleNameWrapped", match.captured(1) == "_(");
                    line->setText(match.captured(2));
                }
            }
        }
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) model->setData(index, combo->currentData(), Qt::EditRole);
        else if (auto *spin = qobject_cast<QSpinBox *>(editor)) model->setData(index, QString::number(spin->value()), Qt::EditRole);
        else if (auto *line = qobject_cast<QLineEdit *>(editor); line && line->property("battleNameWrapped").isValid()) {
            QString value = line->text(); value.replace("\\", "\\\\"); value.replace("\"", "\\\"");
            value = "\"" + value + "\"";
            if (line->property("battleNameWrapped").toBool()) value = "_(" + value + ")";
            model->setData(index, value, Qt::EditRole);
        } else QStyledItemDelegate::setModelData(editor, model, index);
    }
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.column() == 1 && index.sibling(index.row(), 0).data(FieldRole).toString() == ".doubleBattle") {
            if (option->text == "FALSE" || option->text == "No") option->text = tr("Single battle");
            else if (option->text == "TRUE" || option->text == "Yes") option->text = tr("Double battle");
        } else if (index.column() == 1 && index.sibling(index.row(), 0).data(FieldRole).toString() == ".trainerName") option->text = trainerName(option->text);
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
    auto *help = new QLabel(tr("Choose a trainer, then use Edit Party to change its Pokémon. Click a value to edit it. Advanced Source is available for custom expressions and party format changes."), this);
    help->setWordWrap(true);
    dataLayout->addWidget(help);
    auto *fileRow = new QHBoxLayout;
    m_files = new QComboBox(this);
    m_files->setObjectName("battleFiles");
    auto *reload = new QPushButton(tr("Reload"), this);
    m_locate = new QPushButton(tr("Locate File…"), this);
    m_locate->setObjectName("locateBattleFile");
    m_locate->setToolTip(tr("Select the trainer data file inside this project when your fork uses a different location."));
    fileRow->addWidget(m_files, 1); fileRow->addWidget(m_locate); fileRow->addWidget(reload);
    m_fileControls = new QWidget(this);
    m_fileControls->setLayout(fileRow);
    layout->addWidget(m_fileControls);
    auto *scopeRow = new QHBoxLayout;
    m_scope = new QComboBox(this); m_scope->setObjectName("battleScope");
    m_scope->addItems({tr("Current map"), tr("Whole project")});
    scopeRow->addWidget(m_scope); scopeRow->addStretch(); dataLayout->addLayout(scopeRow);
    m_scene = new QLabel(this); m_scene->setObjectName("battleScene");
    m_scene->setWordWrap(true); m_scene->setTextFormat(Qt::PlainText); dataLayout->addWidget(m_scene);
    m_search = new QLineEdit(this);
    m_search->setObjectName("battleSearch");
    m_search->setPlaceholderText(tr("Search trainer IDs, party names, or values…"));
    dataLayout->addWidget(m_search);
    auto *split = new QSplitter(this);
    m_records = new QListWidget(split); m_records->setObjectName("battleRecords");
    m_fields = new QTreeWidget(split); m_fields->setObjectName("battleFields");
    m_fields->setItemDelegate(new BattleValueDelegate(m_fields));
    m_fields->setHeaderLabels({tr("Setting"), tr("Value")});
    m_fields->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_fields->header()->setStretchLastSection(true);
    m_fields->setColumnWidth(0, 210);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 3);
    dataLayout->addWidget(split, 1);
    auto *actions = new QHBoxLayout;
    auto *duplicate = m_duplicate = new QPushButton(tr("Duplicate Trainer"), this);
    duplicate->setObjectName("duplicateBattleRecord");
    auto *remove = m_remove = new QPushButton(tr("Delete Trainer"), this);
    remove->setObjectName("deleteBattleRecord");
    auto *addPokemon = m_addPokemon = new QPushButton(tr("Add Pokémon"), this);
    addPokemon->setObjectName("addBattlePokemon");
    auto *goParty = m_goParty = new QPushButton(tr("Edit Party"), this);
    goParty->setObjectName("goToBattleParty");
    actions->addWidget(addPokemon); actions->addWidget(goParty);
    actions->addWidget(duplicate); actions->addWidget(remove); actions->addStretch();
    dataLayout->addLayout(actions);
    m_status = new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);

    m_views->addTab(data, tr("Trainers & Parties"));
    m_source = new QPlainTextEdit(this); m_source->setObjectName("battleSource");
    m_source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_source->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *sourcePage = new QWidget(this);
    auto *sourceLayout = new QVBoxLayout(sourcePage);
    auto *sourceScope = new QLabel(tr("Advanced Source edits the entire selected file, including trainers outside the current map."), sourcePage);
    sourceScope->setWordWrap(true); sourceLayout->addWidget(sourceScope); sourceLayout->addWidget(m_source, 1);
    m_views->addTab(sourcePage, tr("Advanced Source"));
    m_scripts = new DialogueEditor(this);
    m_scripts->setObjectName("battleScripts");
    m_scripts->findChild<QTabWidget *>("dialogueViews")->setCurrentIndex(1);
    m_views->addTab(m_scripts, tr("Battle Scripts"));
    layout->addWidget(m_views, 1);
    layout->addWidget(m_status);
    m_save = new QPushButton(tr("Save Battles"), this); m_save->setObjectName("saveBattles");
    auto *history = new QHBoxLayout;
    auto *undo = new QPushButton(tr("Undo"), this); undo->setObjectName("undoBattleData");
    auto *redo = new QPushButton(tr("Redo"), this); redo->setObjectName("redoBattleData");
    undo->setEnabled(false); redo->setEnabled(false);
    history->addWidget(undo); history->addWidget(redo); history->addStretch(); history->addWidget(m_save);
    layout->addLayout(history);
    connect(undo, &QPushButton::clicked, m_source, &QPlainTextEdit::undo);
    connect(redo, &QPushButton::clicked, m_source, &QPlainTextEdit::redo);
    connect(m_source->document(), &QTextDocument::undoAvailable, undo, &QPushButton::setEnabled);
    connect(m_source->document(), &QTextDocument::redoAvailable, redo, &QPushButton::setEnabled);
    connect(m_views, &QTabWidget::currentChanged, this, [undo, redo](int view) {
        undo->setVisible(view != 2); redo->setVisible(view != 2);
    });
    connect(m_save, &QPushButton::clicked, this, &BattleEditor::saveChanges);
    connect(m_scripts, &DialogueEditor::dirtyChanged, this, [this] { m_save->setEnabled(hasUnsavedChanges()); emit dirtyChanged(hasUnsavedChanges()); });
    connect(m_scripts, &DialogueEditor::sourceSaved, this, [this](const QString &path) {
        refreshSceneIndex(); filterRecords(); emit sourceSaved(path);
    });
    connect(m_files, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BattleEditor::loadFile);
    connect(m_locate, &QPushButton::clicked, this, [this] {
        const int kind = m_files->currentIndex();
        if (kind < 0 || !confirmChanges()) return;
        const auto path = QFileDialog::getOpenFileName(this, tr("Locate Battle Data"), m_root, tr("Trainer source (*.party *.h *.c)"));
        if (path.isEmpty()) return;
        if (!editable(path) || (kind < 2 && !containsData(path, kind))) {
            m_status->setText(tr("Choose a supported .party or C trainer file inside this project containing %1. Generated headers are not editable when .party source exists.")
                .arg(kind == 0 ? tr("struct Trainer definitions") : tr("struct TrainerMon… parties")));
            return;
        }
        m_files->setItemData(kind, QDir(m_root).relativeFilePath(path));
        loadFile(kind);
    });
    connect(reload, &QPushButton::clicked, this, [this] { if (confirmChanges()) loadFile(m_files->currentIndex()); });
    connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSceneIndex(); filterRecords(); });
    connect(m_records, &QListWidget::currentRowChanged, this, &BattleEditor::showRecord);
    connect(m_search, &QLineEdit::textChanged, this, &BattleEditor::filterRecords);
    connect(m_fields, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (column == 1 && (item->flags() & Qt::ItemIsEditable)) m_fields->editItem(item, column);
    });
    connect(m_fields, &QTreeWidget::currentItemChanged, this, [this] { updateActions(); });
    connect(m_views, &QTabWidget::currentChanged, this, [this](int view) {
        m_fileControls->setVisible(view != 2);
        if (view == 1) m_source->setFocus();
    });
    connect(m_fields, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (column != 1) {
            QSignalBlocker blocker(m_fields);
            item->setText(0, fieldLabel(m_document.nodes().at(index).name));
            return;
        }
        const auto field = m_document.nodes().at(index).name;
        bool numeric;
        const auto number = item->text(1).toInt(&numeric);
        const bool rangeError = numeric && ((field == ".lvl" && (number < 1 || number > 100))
            || (field == ".iv" && (number < 0 || number > 255)));
        if (rangeError || !m_document.setValue(index, item->text(1))) {
            QSignalBlocker blocker(m_fields);
            const auto node = m_document.nodes().at(index);
            item->setText(1, m_document.source().mid(node.valueStart, node.end-node.valueStart));
            item->setToolTip(1, tr("Source: %1").arg(item->text(1)));
            m_status->setText(rangeError ? tr("Levels must be 1–100 and IV quality must be 0–255.")
                : tr("Enter one balanced C expression. Use Advanced Source for structural changes."));
            return;
        }
        syncSource();
        updateSummaries();
        updateActions();
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
        syncSource(); render(row);
        const int record = m_document.records().at(row);
        const auto party = m_document.nodes().at(record);
        const auto members = m_document.pokemon(record);
        if (!members.isEmpty()) {
            auto *item = m_fields->topLevelItem(0)->child(party.children.indexOf(members.last()));
            m_fields->setCurrentItem(item); m_fields->scrollToItem(item);
        }
    });
    connect(goParty, &QPushButton::clicked, this, [this] {
        if (m_document.isPartySource()) {
            const int row = m_records->currentRow();
            if (row < 0) return;
            const auto members = m_document.pokemon(m_document.records().at(row));
            if (!members.isEmpty()) {
                auto *root = m_fields->topLevelItem(0);
                const int child = m_document.nodes().at(m_document.records().at(row)).children.indexOf(members.first());
                m_fields->setCurrentItem(root->child(child)); m_fields->scrollToItem(root->child(child));
            }
            return;
        }
        const QString name = partyReference();
        if (name.isEmpty()) return;
        m_files->setCurrentIndex(1);
        if (m_fileIndex != 1) return; // A cancelled save prompt must preserve selection.
        m_search->clear();
        for (int i = 0; i < m_document.records().size(); ++i) {
            if (m_document.nodes().at(m_document.records().at(i)).name == name) {
                m_records->setCurrentRow(i); return;
            }
        }
        m_status->setText(tr("Party %1 was not found in this file. Check its declaration in Advanced Source.").arg(name));
    });
    connect(duplicate, &QPushButton::clicked, this, [this] { mutate(false); });
    connect(remove, &QPushButton::clicked, this, [this] { mutate(true); });
    setScene({}, {}, {});
}
bool BattleEditor::event(QEvent *event) {
    // Consume data undo shortcuts before the main window's map undo action.
    // Active text editors retain their own text undo through ShortcutOverride.
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && m_views && m_views->currentIndex() != 2) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool undo = key->matches(QKeySequence::Undo), redo = key->matches(QKeySequence::Redo);
        if (undo || redo) {
            if (event->type() == QEvent::KeyPress) {
                if (undo) m_source->undo(); else m_source->redo();
            }
            key->accept(); return true;
        }
    }
    return QWidget::event(event);
}
bool BattleEditor::hasUnsavedChanges() const { return m_dirty || m_scripts->hasUnsavedChanges(); }
void BattleEditor::setDirty(bool dirty) {
    m_dirty = dirty;
    m_save->setEnabled(hasUnsavedChanges());
    emit dirtyChanged(hasUnsavedChanges());
}
bool BattleEditor::editable(const QString &path) const {
    const QFileInfo info(path);
    const bool generated = QFileInfo::exists(m_root + "/src/data/trainers.party")
        && QStringList{"src/data/trainers.h", "src/data/trainer_parties.h", "src/data/trainers_frlg.h"}
            .contains(QDir(m_root).relativeFilePath(info.absoluteFilePath()));
    return !generated && !(info.suffix() == "h" && QFileInfo::exists(info.path() + "/" + info.completeBaseName() + ".party")) && !m_root.isEmpty() && !info.isSymLink() && info.canonicalFilePath().startsWith(m_root + '/');
}
bool BattleEditor::confirmChanges() {
    if (m_dirty) {
        const auto answer = QMessageBox::warning(this, tr("Unsaved Battle Edits"), tr("Save changes to %1?").arg(m_path), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save) { if (!saveChanges()) return false; }
        else {
            m_document.parse(baseline());
            { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
            render(); setDirty(false);
        }
    }
    return m_scripts->confirmChanges();
}
bool BattleEditor::setScene(const QString &root, const QString &map, const QString &scripts, bool supported, bool scriptsSupported) {
    if (!confirmChanges()) return false;
    if (!m_scripts->setScene(root, map, scripts, supported && scriptsSupported)) return false;
    const QString canonical = QFileInfo(root).canonicalFilePath();
    const bool available = !canonical.isEmpty() && !map.isEmpty() && supported;
    const bool sameProject = m_files->count() && m_root == canonical && m_files->isEnabled() == available;
    const bool sceneChanged = m_root != canonical || m_mapName != map || m_scriptsPath != scripts;
    m_root = canonical; m_mapName = map; m_scriptsPath = scripts;
    if (sceneChanged) {
        const QSignalBlocker scopeBlocker(m_scope), searchBlocker(m_search);
        m_scope->setCurrentIndex(0); m_search->clear();
    }
    m_scope->setEnabled(available);
    if (sameProject) {
        if (sceneChanged) {
            refreshSceneIndex();
            const int index = sceneFile();
            { QSignalBlocker blocker(m_files); m_files->setCurrentIndex(index); }
            if (available && index == m_fileIndex && m_loaded) filterRecords();
            else loadFile(available ? index : -1);
        }
        return true;
    }
    auto *delegate = static_cast<BattleValueDelegate *>(m_fields->itemDelegate());
    delegate->constants = QStringList{"TRUE", "FALSE"};
    for (const QString &name : QStringList{"species", "moves", "items", "trainers", "trainer_types", "battle_ai", "abilities", "pokemon"}) {
        QFile constants(m_root + "/include/constants/" + name + ".h");
        if (!m_root.isEmpty() && constants.open(QIODevice::ReadOnly)) {
            const auto source = QString::fromUtf8(constants.readAll());
            auto matches = QRegularExpression("^\\s*#define\\s+([A-Za-z_][A-Za-z0-9_]*)", QRegularExpression::MultilineOption).globalMatch(source);
            while (matches.hasNext()) delegate->constants.append(matches.next().captured(1));
        }
    }
    delegate->constants.removeDuplicates(); delegate->constants.sort();
    QSignalBlocker blocker(m_files); m_files->clear(); m_fileIndex = -1;
    if (QFileInfo::exists(m_root + "/src/data/trainers.party")) {
        m_files->addItem(tr("Expansion Trainers & Parties"), "src/data/trainers.party");
        if (QFileInfo::exists(m_root + "/src/data/trainers_frlg.party"))
            m_files->addItem(tr("Expansion FRLG Trainers & Parties"), "src/data/trainers_frlg.party");
    } else {
        m_files->addItem(tr("Trainers"), resolveDataPath(0));
        m_files->addItem(tr("Pokémon Parties"), resolveDataPath(1));
        m_files->addItem(tr("Trainer IDs (Advanced Source)"), "include/constants/trainers.h");
    }
    m_files->setEnabled(available);
    m_locate->setEnabled(available);
    m_source->setEnabled(available);
    m_fields->setEnabled(available);
    refreshSceneIndex();
    const int first = available ? sceneFile() : -1;
    m_files->setCurrentIndex(first); loadFile(first);
    if (!available) m_status->setText(tr("Open a pokeemerald or pokefirered project and select a map to edit trainer data."));
    return true;
}
void BattleEditor::loadFile(int index) {
    if (!confirmChanges()) { QSignalBlocker block(m_files); m_files->setCurrentIndex(m_fileIndex); return; }
    m_fileIndex = index;
    m_path = index >= 0 ? m_root + '/' + m_files->itemData(index).toString() : QString();
    m_file = DialogueDocument();
    QString error;
    bool loaded = false;
    if (index >= 0) {
        if (!QFileInfo::exists(m_path)) error = tr("File not found. Use Locate File to select your fork's C data file.");
        else if (!editable(m_path)) error = tr("This path is outside the editable project source, a symlink, or a generated header backed by .party source.");
        else loaded = m_file.load(m_path, &error);
    }
    m_document.parse(loaded ? baseline() : QString());
    m_loaded = loaded;
    refreshSceneIndex();
    { QSignalBlocker block(m_source); m_source->setPlainText(m_document.source()); }
    m_source->setEnabled(loaded); m_fields->setEnabled(loaded);
    render(); setDirty(false);
    m_status->setText(loaded ? tr("%1 records • %2").arg(m_document.records().size()).arg(m_files->itemData(index).toString()) : tr("Cannot load battle data: %1 %2").arg(m_path, error));
    if (loaded && index < 2 && !containsData(m_path, index)) {
        m_status->setText(tr("No supported %1 declarations in %2. Use Locate File for the correct C file. Custom or generated formats require an adapter.")
            .arg(index == 0 ? tr("trainer") : tr("party"), m_files->itemData(index).toString()));
    }
    if (loaded && index == 2 && !m_document.isPartySource()) m_views->setCurrentIndex(1);
}
void BattleEditor::render(int row) {
    QSignalBlocker blocker(m_records); m_records->clear();
    for (int index : m_document.records()) {
        const auto node = m_document.nodes().at(index);
        QString label = node.name;
        for (int child : node.children) {
            const auto field = m_document.nodes().at(child);
            if (field.name == ".trainerName") label = trainerName(m_document.source().mid(field.valueStart, field.end-field.valueStart)) + " — " + label;
        }
        auto *item = new QListWidgetItem(label, m_records);
        item->setToolTip(node.name);
    }
    m_records->setCurrentRow(m_records->count() ? qBound(0, row, m_records->count()-1) : -1);
    showRecord(m_records->currentRow());
    filterRecords();
}
void BattleEditor::showRecord(int row) {
    QSignalBlocker block(m_fields); m_fields->clear();
    if (row < 0 || row >= m_document.records().size()) { updateActions(); return; }
    addFields(m_document.records().at(row), nullptr);
    m_fields->expandAll();
    updateActions();
}
void BattleEditor::addFields(int index, QTreeWidgetItem *parent) {
    const auto node = m_document.nodes().at(index);
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_fields);
    item->setText(0, fieldLabel(node.name)); item->setData(0, Qt::UserRole, index);
    item->setData(0, FieldRole, node.name);
    item->setToolTip(0, node.name);
    if (m_document.isPartySource() && node.name == ".species") {
        item->setText(0, tr("Pokémon / held item"));
        item->setToolTip(0, tr("Showdown format: Nickname (Species) (M/F) @ Held Item. Species alone also works."));
    }
    if (node.parent >= 0 && (m_document.nodes().at(node.parent).name == ".moves" || m_document.nodes().at(node.parent).name == ".items")) {
        const auto parentNode = m_document.nodes().at(node.parent);
        item->setText(0, parentNode.name == ".moves" ? tr("Move %1").arg(parentNode.children.indexOf(index)+1)
            : tr("Item %1").arg(parentNode.children.indexOf(index)+1));
    }
    if (m_document.isPokemon(index)) {
        QString species, level = m_document.isPartySource() ? tr("100 (default)") : QString();
        for (int child : node.children) {
            const auto field = m_document.nodes().at(child);
            const auto value = m_document.source().mid(field.valueStart, field.end-field.valueStart);
            if (field.name == ".species") species = value;
            if (field.name == ".lvl") level = value;
        }
        item->setText(0, tr("Pokémon %1").arg(m_document.pokemon(node.parent).indexOf(index)+1));
        item->setText(1, tr("%1 • Level %2").arg(species, level));
    }
    if (!node.group) {
        item->setText(1, m_document.source().mid(node.valueStart, node.end-node.valueStart));
        item->setToolTip(1, tr("Source: %1").arg(item->text(1)));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    for (int child : node.children) addFields(child, item);
}
void BattleEditor::mutate(bool remove) {
    const int row = m_records->currentRow();
    if (row < 0) return;
    const int index = selectedTarget();
    if (index < 0) return;
    const auto node = m_document.nodes().at(index);
    const bool record = m_document.records().contains(index);
    const bool pokemon = m_document.isPokemon(index);
    if (!record && !pokemon) { m_status->setText(tr("Select a trainer, party, or Pokémon group first.")); return; }
    QString name;
    if (!remove && record) {
        bool ok;
        name = QInputDialog::getText(this, tr("Duplicate Battle Record"), m_document.isPartySource() ? tr("New TRAINER_ ID (generated from the section header):") : node.parent < 0 ? tr("New party C symbol:") : tr("New TRAINER_ constant (define it in include/constants/trainers.h too):"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
    }
    if (remove && QMessageBox::question(this, tr("Delete Battle Record"), tr("Delete %1? Update references in scripts and trainer data before building.").arg(node.name)) != QMessageBox::Yes) return;
    if (!(remove ? m_document.remove(index) : m_document.duplicate(index, name))) {
        m_status->setText(tr("Cannot change this record. Check that the new name is valid and unique, or use Advanced Source.")); return;
    }
    syncSource(); render(row);
}
bool BattleEditor::saveChanges() {
    if (m_dirty) {
        QString error;
        if (!editable(m_path)) error = tr("The data file is outside the project or is a symlink.");
        if (!error.isEmpty() || !m_file.saveSource(m_source->toPlainText(), &error)) {
            QMessageBox::warning(this, tr("Cannot Save Battles"), error); return false;
        }
        m_document.parse(baseline());
        refreshSceneIndex();
        render(m_records->currentRow()); setDirty(false);
        m_status->setText(tr("Saved. Build and test your project to verify battle behavior."));
        emit sourceSaved(m_path);
    }
    return m_scripts->saveChanges();
}
QString BattleEditor::baseline() const {
    return QString(m_file.sourceText()).replace("\r\n", "\n");
}
void BattleEditor::syncSource() {
    const QString before = m_source->toPlainText(), after = m_document.source();
    if (before != after) {
        int first = 0, tail = 0;
        while (first < before.size() && first < after.size() && before[first] == after[first]) ++first;
        while (tail < before.size()-first && tail < after.size()-first
               && before[before.size()-1-tail] == after[after.size()-1-tail]) ++tail;
        QSignalBlocker blocker(m_source);
        QTextCursor cursor(m_source->document());
        cursor.beginEditBlock();
        cursor.setPosition(first); cursor.setPosition(before.size()-tail, QTextCursor::KeepAnchor);
        cursor.insertText(after.mid(first, after.size()-first-tail)); cursor.endEditBlock();
    }
    setDirty(after != baseline());
}
void BattleEditor::filterRecords() {
    const auto text = m_search->text();
    int first = -1;
    for (int row = 0; row < m_records->count(); ++row) {
        const auto node = m_document.nodes().at(m_document.records().at(row));
        const bool inScene = m_scope->currentIndex() == 1 || (m_document.isTrainer(m_document.records().at(row))
            ? m_sceneTrainers.contains(trainerId(node.name)) : m_sceneParties.contains(node.name));
        const bool match = inScene && (node.name.contains(text, Qt::CaseInsensitive)
            || m_document.source().mid(node.start, node.end-node.start).contains(text, Qt::CaseInsensitive));
        m_records->item(row)->setHidden(!match);
        if (match && first < 0) first = row;
    }
    const auto *current = m_records->currentItem();
    if (!current || current->isHidden()) {
        m_records->setCurrentRow(first);
        showRecord(first);
    }
    int visible = 0;
    for (int row = 0; row < m_records->count(); ++row) if (!m_records->item(row)->isHidden()) ++visible;
    if (m_scope->currentIndex() == 1) m_scene->setText(tr("Whole project • %1 matching records in this file").arg(visible));
    else if (m_mapName.isEmpty()) m_scene->setText(tr("Select a map to see its trainers."));
    else if (!m_sceneIndexed) m_scene->setText(tr("Map: %1 • No readable scene scripts. Use Whole project to browse all trainers.").arg(m_mapName));
    else if (m_sceneTrainers.isEmpty()) m_scene->setText(tr("Map: %1 • No trainer IDs referenced in its scripts. Use Whole project for dynamic or shared battles.").arg(m_mapName));
    else m_scene->setText(tr("Map: %1 • %2 matching records in this file (%3 referenced trainer IDs). Search another data file or choose Whole project if needed.").arg(m_mapName).arg(visible).arg(m_sceneTrainers.size()));
    updateActions();
}
int BattleEditor::selectedTarget() const {
    const int row = m_records->currentRow();
    if (row < 0 || row >= m_document.records().size()) return -1;
    const int record = m_document.records().at(row);
    int node = m_fields->currentItem() ? m_fields->currentItem()->data(0, Qt::UserRole).toInt() : record;
    // Fields inside a Pokémon act on that Pokémon. Trainer fields act on the
    // trainer, never on its items or party-reference initializer.
    if (m_document.isPartySource() || m_document.nodes().at(record).parent < 0) {
        while (node != record && m_document.nodes().at(node).parent != record) node = m_document.nodes().at(node).parent;
        return node == record || m_document.nodes().at(node).group ? node : record;
    }
    return record;
}
QString BattleEditor::partyReference() const {
    const int row = m_records->currentRow();
    if (row < 0 || row >= m_document.records().size()) return {};
    return referencedParty(m_document, m_document.records().at(row));
}
void BattleEditor::updateActions() {
    const int target = selectedTarget();
    const bool valid = m_loaded && target >= 0;
    const int record = valid ? m_document.records().at(m_records->currentRow()) : -1;
    const bool party = valid && !m_document.isTrainer(record);
    const bool pokemon = valid && m_document.isPokemon(target);
    const bool partyContainer = valid && (party || m_document.isPartySource());
    const QString kind = pokemon ? tr("Pokémon") : party ? tr("Party") : tr("Trainer");
    m_duplicate->setText(tr("Duplicate %1").arg(kind)); m_remove->setText(tr("Delete %1").arg(kind));
    const bool mutableTarget = valid && m_document.canResize(target);
    const QString resizeHelp = valid && !mutableTarget ? tr("Use Advanced Source to change conditional layouts, macro-based teams, or explicitly sized arrays.") : QString();
    m_duplicate->setToolTip(resizeHelp); m_remove->setToolTip(resizeHelp); m_addPokemon->setToolTip(resizeHelp);
    m_duplicate->setEnabled(mutableTarget && (!pokemon || m_document.pokemon(m_document.nodes().at(target).parent).size() < 6));
    m_remove->setEnabled(mutableTarget && (!pokemon || m_document.pokemon(m_document.nodes().at(target).parent).size() > 1));
    m_addPokemon->setEnabled(partyContainer && mutableTarget && m_document.pokemon(record).size() < 6);
    m_goParty->setEnabled(valid && !party && (m_document.isPartySource() ? !m_document.pokemon(record).isEmpty() : !partyReference().isEmpty()));
}

void BattleEditor::updateSummaries() {
    QSignalBlocker blocker(m_fields);
    for (QTreeWidgetItemIterator it(m_fields); *it; ++it) {
        auto *item = *it;
        const int index = item->data(0, Qt::UserRole).toInt();
        const auto node = m_document.nodes().at(index);
        if (!node.group) item->setToolTip(1, tr("Source: %1").arg(item->text(1)));
        else if (node.parent >= 0 && m_document.records().contains(node.parent) && m_document.nodes().at(node.parent).parent < 0) {
            QString species, level = m_document.isPartySource() ? tr("100 (default)") : QString();
            for (int child : node.children) {
                const auto field = m_document.nodes().at(child);
                const auto value = m_document.source().mid(field.valueStart, field.end-field.valueStart);
                if (field.name == ".species") species = value;
                if (field.name == ".lvl") level = value;
            }
            item->setText(1, tr("%1 • Level %2").arg(species, level));
        }
    }
    for (int row = 0; row < m_records->count(); ++row) {
        const auto record = m_document.nodes().at(m_document.records().at(row));
        QString label = record.name;
        for (int child : record.children) {
            const auto field = m_document.nodes().at(child);
            if (field.name == ".trainerName") label = trainerName(m_document.source().mid(field.valueStart, field.end-field.valueStart)) + " — " + label;
        }
        m_records->item(row)->setText(label);
    }
}

bool BattleEditor::containsData(const QString &path, int kind) const {
    if (!editable(path)) return false;
    DialogueDocument source;
    QString error;
    if (!source.load(path, &error)) return false;
    BattleDocument data;
    data.parse(source.sourceText());
    for (int record : data.records()) {
        if (data.isPartySource() || ((data.nodes().at(record).parent < 0) == (kind == 1))) return true;
    }
    return false;
}
QString BattleEditor::resolveDataPath(int kind) const {
    const QString standard = kind == 0 ? "src/data/trainers.h" : "src/data/trainer_parties.h";
    if (m_root.isEmpty()) return standard;
    if (QFileInfo::exists(m_root + '/' + standard)) return standard;
    // Search only the trainer-related C headers under src/data. Do not pick
    // among multiple matches; Locate File lets the user resolve that case.
    QStringList matches;
    QDirIterator it(m_root + "/src/data", {"*.h", "*.c"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const auto relative = QDir(m_root).relativeFilePath(path);
        if (!relative.contains("trainer", Qt::CaseInsensitive) && !relative.contains("parties", Qt::CaseInsensitive)) continue;
        if (containsData(path, kind)) matches.append(relative);
    }
    return matches.size() == 1 ? matches.first() : standard;
}

void BattleEditor::refreshScene() {
    refreshSceneIndex(); filterRecords();
}
void BattleEditor::refreshSceneIndex() {
    m_sceneTrainers.clear(); m_sceneParties.clear(); m_sceneIndexed = false;
    if (m_root.isEmpty() || m_mapName.isEmpty() || m_scriptsPath.isEmpty()) return;
    const auto scene = resolveSceneSources(m_root, m_mapName, m_scriptsPath);
    m_sceneIndexed = scene.readable;
    const QRegularExpression ids("\\bTRAINER_[A-Za-z0-9_]+\\b");
    auto matches = ids.globalMatch(scene.code);
    while (matches.hasNext()) m_sceneTrainers.insert(matches.next().captured());
    // Stock C parties live separately from trainer definitions; use the actual
    // party references, never a guessed name derived from TRAINER_ constants.
    if (!m_files->count()) return;
    if (m_files->itemData(0).toString().endsWith(".party")) return;
    const QString trainerPath = m_root + '/' + m_files->itemData(0).toString();
    BattleDocument trainers;
    if (m_path == trainerPath && m_loaded) trainers = m_document;
    else {
        DialogueDocument source; QString error;
        if (!editable(trainerPath) || !source.load(trainerPath, &error)) return;
        trainers.parse(source.sourceText());
    }
    if (!trainers.isPartySource()) for (int record : trainers.records()) {
        if (!m_sceneTrainers.contains(trainerId(trainers.nodes().at(record).name))) continue;
        const auto party = referencedParty(trainers, record);
        if (!party.isEmpty()) m_sceneParties.insert(party);
    }
}
int BattleEditor::sceneFile() const {
    if (m_scope->currentIndex() == 0 && !m_sceneTrainers.isEmpty()) {
        for (int file = 0; file < m_files->count(); ++file) {
            const auto path = m_root + '/' + m_files->itemData(file).toString();
            BattleDocument document;
            if (path == m_path && m_loaded) document = m_document;
            else {
                DialogueDocument source; QString error;
                if (!editable(path) || !source.load(path, &error)) continue;
                document.parse(source.sourceText());
            }
            for (int record : document.records()) if (document.isTrainer(record) && m_sceneTrainers.contains(trainerId(document.nodes().at(record).name))) return file;
        }
    }
    return 0;
}

}
