#include "porymapz/dialogueeditor.h"

#include <QComboBox>
#include <QDirIterator>
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

namespace PorymapZ {
DialogueEditor::DialogueEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("dialogueEditor");
    auto *layout = new QVBoxLayout(this);
    m_scene = new QLabel(this);
    m_scene->setObjectName("dialogueScene");
    m_scene->setTextFormat(Qt::PlainText);
    layout->addWidget(m_scene);
    auto *fileRow = new QHBoxLayout;
    m_scope = new QComboBox(this);
    m_scope->setObjectName("dialogueScope");
    m_scope->addItems({tr("Current scene"), tr("Whole project")});
    m_files = new QComboBox(this);
    m_files->setObjectName("dialogueFiles");
    m_files->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_files->setMinimumContentsLength(25);
    auto *reload = new QPushButton(tr("Reload Files"), this);
    fileRow->addWidget(m_scope);
    fileRow->addWidget(m_files, 1);
    fileRow->addWidget(reload);
    layout->addLayout(fileRow);
    m_views = new QTabWidget(this);
    m_views->setObjectName("dialogueViews");
    auto *dialogue = new QWidget(m_views);
    auto *dialogueLayout = new QVBoxLayout(dialogue);
    auto *help = new QLabel(tr("Each line is one .string operand. Keep game controls such as \\n, \\l, \\p, {PLAYER}, and the ending $."), this);
    help->setWordWrap(true);
    dialogueLayout->addWidget(help);
    m_search = new QLineEdit(this);
    m_search->setObjectName("dialogueSearch");
    m_search->setPlaceholderText(tr("Search labels or dialogue in this file…"));
    dialogueLayout->addWidget(m_search);
    auto *split = new QSplitter(this);
    m_entries = new QListWidget(split);
    m_entries->setObjectName("dialogueEntries");
    m_text = new QPlainTextEdit(split);
    m_text->setObjectName("dialogueText");
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    dialogueLayout->addWidget(split, 1);
    m_views->addTab(dialogue, tr("Dialogue"));
    m_script = new QPlainTextEdit(this);
    m_script->setObjectName("sceneScript");
    m_script->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_script->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_views->addTab(m_script, tr("Script"));
    layout->addWidget(m_views, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_save = new QPushButton(tr("Save Dialogue"), this);
    m_save->setObjectName("saveDialogue");
    buttons->addWidget(m_save);
    layout->addLayout(buttons);
    connect(m_files, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DialogueEditor::loadFile);
    connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!confirmChanges()) {
            QSignalBlocker blocker(m_scope);
            m_scope->setCurrentIndex(1 - index);
            return;
        }
        scan();
    });
    connect(m_views, &QTabWidget::currentChanged, this, [this](int mode) {
        if (!confirmChanges()) {
            QSignalBlocker blocker(m_views);
            m_views->setCurrentIndex(m_mode);
            return;
        }
        m_mode = mode;
        m_save->setText(mode ? tr("Save Script") : tr("Save Dialogue"));
    });
    connect(m_entries, &QListWidget::currentRowChanged, this, &DialogueEditor::selectEntry);
    connect(m_search, &QLineEdit::textChanged, this, &DialogueEditor::filter);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (m_entryIndex >= 0 && m_mode == 0)
            setDirty(m_text->toPlainText() != m_document.entries().at(m_entryIndex).text);
    });
    connect(m_script, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loaded && m_mode == 1)
            setDirty(m_script->toPlainText() != QString(m_document.sourceText()).replace("\r\n", "\n"));
    });
    connect(m_save, &QPushButton::clicked, this, &DialogueEditor::saveChanges);
    connect(reload, &QPushButton::clicked, this, [this] { if (confirmChanges()) scan(); });
    scan();
}

void DialogueEditor::setDirty(bool dirty) {
    m_save->setEnabled(dirty);
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit dirtyChanged(dirty);
}

bool DialogueEditor::confirmChanges() {
    if (!m_dirty) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved Scene Edits"),
        tr("Save changes to %1 before continuing?").arg(m_files->itemText(m_fileIndex)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Save) return saveChanges();
    if (choice == QMessageBox::Cancel) return false;
    setDirty(false);
    displayDocument();
    return true;
}

bool DialogueEditor::setScene(const QString &root, const QString &mapName, const QString &scriptsPath, bool supported) {
    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (m_root == canonicalRoot && m_mapName == mapName && m_scriptsPath == scriptsPath && m_supported == supported)
        return true;
    if (!confirmChanges()) return false;
    m_root = canonicalRoot;
    m_mapName = mapName;
    m_scriptsPath = scriptsPath;
    m_supported = supported;
    QSignalBlocker scopeBlocker(m_scope);
    m_scope->setCurrentIndex(0);
    m_search->clear();
    scan();
    return true;
}

bool DialogueEditor::editablePath(const QString &path) const {
    const QFileInfo info(path);
    return !m_root.isEmpty() && !info.isSymLink() && info.canonicalFilePath().startsWith(m_root + '/')
        && !QFileInfo::exists(info.path() + '/' + info.completeBaseName() + ".pory");
}

QString DialogueEditor::currentPath() const {
    return m_fileIndex < 0 ? QString() : m_root + '/' + m_files->itemText(m_fileIndex);
}

void DialogueEditor::scan() {
    QSignalBlocker blocker(m_files);
    m_files->clear();
    m_fileIndex = -1;
    m_loaded = false;
    m_document = DialogueDocument();
    displayDocument();
    m_scene->setText(m_mapName.isEmpty() ? tr("Open a map to edit its dialogue and scripts.") : tr("Scene: %1").arg(m_mapName));
    const bool available = !m_root.isEmpty() && !m_mapName.isEmpty() && m_supported;
    m_scope->setEnabled(available);
    m_files->setEnabled(available);
    m_views->setEnabled(available);
    m_status->setText(available ? tr("No editable files found for this scene.")
        : tr("Scene editing is unavailable for this project or map. Supported assembly source is required."));
    if (!available) return;
    const QString folder = m_scope->currentIndex() == 0 ? QFileInfo(m_scriptsPath).absolutePath() : m_root + "/data";
    const auto canonicalFolder = QFileInfo(folder).canonicalFilePath();
    if (canonicalFolder != m_root && !canonicalFolder.startsWith(m_root + '/')) return;
    QStringList paths;
    int skipped = 0;
    QDirIterator it(folder, {"*.inc", "*.s"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!editablePath(path)) continue;
        DialogueDocument document;
        QString error;
        if (!document.load(path, &error)) { ++skipped; continue; }
        // Scene scope includes files containing only event scripts, with no dialogue.
        if (m_scope->currentIndex() == 0 || !document.entries().isEmpty())
            paths.append(QDir(m_root).relativeFilePath(path));
    }
    paths.sort(Qt::CaseInsensitive);
    const QString scripts = QDir(m_root).relativeFilePath(m_scriptsPath);
    if (paths.removeOne(scripts)) paths.prepend(scripts);
    m_files->addItems(paths);
    loadFile(m_files->currentIndex());
    if (skipped) m_status->setText(m_status->text() + tr(" %1 unreadable or non-UTF-8 files skipped.").arg(skipped));
}

void DialogueEditor::loadFile(int index) {
    if (!confirmChanges()) {
        QSignalBlocker blocker(m_files);
        m_files->setCurrentIndex(m_fileIndex);
        return;
    }
    m_fileIndex = index;
    m_loaded = false;
    m_document = DialogueDocument();
    QString error;
    if (index >= 0 && editablePath(currentPath())) m_loaded = m_document.load(currentPath(), &error);
    displayDocument();
    if (!m_loaded && index >= 0) m_status->setText(tr("Cannot load file: %1").arg(error));
}

void DialogueEditor::displayDocument() {
    const QSignalBlocker entriesBlocker(m_entries), textBlocker(m_text), scriptBlocker(m_script);
    m_entryIndex = -1;
    m_entries->clear();
    m_text->clear();
    m_text->setEnabled(false);
    m_script->setPlainText(m_loaded ? m_document.sourceText() : QString());
    m_script->setEnabled(m_loaded);
    setDirty(false);
    if (!m_loaded) return;
    for (const auto &entry : m_document.entries()) m_entries->addItem(tr("%1 (line %2)").arg(entry.label).arg(entry.line));
    m_status->setText(m_entries->count() ? tr("%1 dialogue entries. Script view edits the complete selected file.").arg(m_entries->count())
        : tr("No dialogue in this file. Choose Script to edit its event scripts."));
    filter();
    for (int i = 0; i < m_entries->count(); ++i) {
        if (!m_entries->item(i)->isHidden()) {
            m_entries->setCurrentRow(i);
            selectEntry(i);
            break;
        }
    }
}

void DialogueEditor::selectEntry(int row) {
    if (row == m_entryIndex) return;
    if (!confirmChanges()) {
        QSignalBlocker blocker(m_entries);
        m_entries->setCurrentRow(m_entryIndex);
        return;
    }
    m_entryIndex = row;
    const QSignalBlocker selectionBlocker(m_entries);
    m_entries->setCurrentRow(row);
    QSignalBlocker blocker(m_text);
    m_text->setPlainText(row >= 0 ? m_document.entries().at(row).text : QString());
    m_text->setEnabled(row >= 0);
    setDirty(false);
}

void DialogueEditor::filter() {
    const auto search = m_search->text();
    for (int i = 0; i < m_entries->count(); ++i) {
        const auto &entry = m_document.entries().at(i);
        m_entries->item(i)->setHidden(!entry.label.contains(search, Qt::CaseInsensitive) && !entry.text.contains(search, Qt::CaseInsensitive));
    }
}

bool DialogueEditor::saveChanges() {
    if (!m_dirty) return true;
    QString error;
    if (!editablePath(currentPath())) error = tr("The selected file is outside the project or is generated source.");
    const bool saved = error.isEmpty() && (m_mode == 0
        ? m_document.save(m_entryIndex, m_text->toPlainText(), &error)
        : m_document.saveSource(m_script->toPlainText(), &error));
    if (!saved) {
        QMessageBox::warning(this, tr("Cannot Save Scene Edits"), error);
        return false;
    }
    const int row = m_entryIndex;
    displayDocument();
    if (row >= 0 && row < m_entries->count()) m_entries->setCurrentRow(row);
    m_status->setText(tr("Saved. Build your game project to test the changes in game."));
    emit sourceSaved(currentPath());
    return true;
}
}
