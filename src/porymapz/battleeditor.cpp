#include "porymapz/battleeditor.h"
#include "porymapz/dialogueeditor.h"
#include <QComboBox>
#include <QRegularExpression>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <QKeyEvent>

namespace PorymapZ {
namespace {
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
QString trainerName(const QString &value) {
    const auto match = QRegularExpression(QStringLiteral("^(?:_\\()?\"((?:[^\"\\\\]|\\\\.)*)\"\\)?$")).match(value);
    return match.hasMatch() ? match.captured(1) : value;
}
}

BattleEditor::BattleEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("battleEditor");
    auto *layout = new QVBoxLayout(this);
    m_views = new QTabWidget(this);
    m_views->setObjectName("battleViews");
    auto *data = new QWidget(this);
    auto *dataLayout = new QVBoxLayout(data);
    auto *help = new QLabel(tr("Edit trainer and party source as text. Select a record to jump to its definition. Scope and search filter the list; the text editor always contains the entire selected file."), this);
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
    m_source = new QPlainTextEdit(split); m_source->setObjectName("battleSource");
    m_source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_source->setLineWrapMode(QPlainTextEdit::NoWrap);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 3);
    dataLayout->addWidget(split, 1);
    m_status = new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);

    m_views->addTab(data, tr("Trainers & Parties"));
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
        undo->setVisible(view != 1); redo->setVisible(view != 1);
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
    connect(m_records, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showRecord(m_records->row(item));
    });
    connect(m_search, &QLineEdit::textChanged, this, &BattleEditor::filterRecords);
    connect(m_views, &QTabWidget::currentChanged, this, [this](int view) {
        m_fileControls->setVisible(view != 1);
        if (view == 0) m_source->setFocus();
    });
    connect(m_source, &QPlainTextEdit::textChanged, this, [this] {
        m_updatingSource = true;
        m_document.parse(m_source->toPlainText());
        render(m_records->currentRow());
        m_updatingSource = false;
        setDirty(m_source->toPlainText() != QString(m_file.sourceText()).replace("\r\n", "\n"));
    });
    setScene({}, {}, {});
}
bool BattleEditor::event(QEvent *event) {
    // Consume data undo shortcuts before the main window's map undo action.
    // Active text editors retain their own text undo through ShortcutOverride.
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && m_views && m_views->currentIndex() != 1) {
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
    QSignalBlocker blocker(m_files); m_files->clear(); m_fileIndex = -1;
    if (QFileInfo::exists(m_root + "/src/data/trainers.party")) {
        m_files->addItem(tr("Expansion Trainers & Parties"), "src/data/trainers.party");
        if (QFileInfo::exists(m_root + "/src/data/trainers_frlg.party"))
            m_files->addItem(tr("Expansion FRLG Trainers & Parties"), "src/data/trainers_frlg.party");
    } else {
        m_files->addItem(tr("Trainers"), resolveDataPath(0));
        m_files->addItem(tr("Pokémon Parties"), resolveDataPath(1));
        m_files->addItem(tr("Trainer IDs"), "include/constants/trainers.h");
    }
    m_files->setEnabled(available);
    m_locate->setEnabled(available);
    m_source->setEnabled(available);
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
    m_source->setEnabled(loaded);
    render(); setDirty(false);
    m_status->setText(loaded ? tr("%1 records • %2").arg(m_document.records().size()).arg(m_files->itemData(index).toString()) : tr("Cannot load battle data: %1 %2").arg(m_path, error));
    if (loaded && index < 2 && !containsData(m_path, index)) {
        m_status->setText(tr("No supported %1 declarations in %2. Use Locate File for the correct C file. Custom or generated formats require an adapter.")
            .arg(index == 0 ? tr("trainer") : tr("party"), m_files->itemData(index).toString()));
    }
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
    if (m_updatingSource || row < 0 || row >= m_document.records().size()) return;
    const auto node = m_document.nodes().at(m_document.records().at(row));
    auto cursor = m_source->textCursor();
    cursor.setPosition(node.start);
    m_source->setTextCursor(cursor);
    m_source->ensureCursorVisible();
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
        m_updatingSource = true;
        render(m_records->currentRow());
        m_updatingSource = false;
        setDirty(false);
        m_status->setText(tr("Saved. Build and test your project to verify battle behavior."));
        emit sourceSaved(m_path);
    }
    return m_scripts->saveChanges();
}
QString BattleEditor::baseline() const {
    return QString(m_file.sourceText()).replace("\r\n", "\n");
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
