#include "porymapz/dialogueeditor.h"

#include <QCloseEvent>
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
#include <QVBoxLayout>

namespace PorymapZ {
DialogueEditor::DialogueEditor(const QString &root, QWidget *parent)
    : QDialog(parent), m_root(QFileInfo(root).canonicalFilePath()) {
    setWindowTitle(tr("Porymap-Z — Dialogue Editor"));
    resize(1000, 650);
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Edit pokeemerald dialogue. Each line below is one .string operand. "
                              "Keep game controls such as \\n, \\l, \\p, {PLAYER}, and the ending $."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *fileRow = new QHBoxLayout;
    m_files = new QComboBox(this);
    m_files->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_files->setMinimumContentsLength(30);
    auto *reload = new QPushButton(tr("Reload Files"), this);
    fileRow->addWidget(m_files, 1);
    fileRow->addWidget(reload);
    layout->addLayout(fileRow);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search labels or dialogue in the selected file…"));
    layout->addWidget(m_search);
    auto *split = new QSplitter(this);
    m_entries = new QListWidget(split);
    m_text = new QPlainTextEdit(split);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setEnabled(false);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_save = new QPushButton(tr("Save Dialogue"), this);
    m_save->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_save);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(m_files, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DialogueEditor::loadFile);
    connect(m_entries, &QListWidget::currentRowChanged, this, &DialogueEditor::selectEntry);
    connect(m_search, &QLineEdit::textChanged, this, &DialogueEditor::filter);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (m_entryIndex < 0) return;
        m_dirty = m_text->toPlainText() != m_document.entries().at(m_entryIndex).text;
        m_save->setEnabled(m_dirty);
        setWindowTitle(tr("Porymap-Z — Dialogue Editor%1").arg(m_dirty ? " *" : ""));
    });
    connect(m_save, &QPushButton::clicked, this, &DialogueEditor::save);
    connect(close, &QPushButton::clicked, this, &DialogueEditor::reject);
    connect(reload, &QPushButton::clicked, this, [this] { if (confirmDiscard()) scan(); });
    scan();
}

bool DialogueEditor::confirmDiscard() {
    return !m_dirty || QMessageBox::warning(this, tr("Unsaved Dialogue"),
        tr("Discard the unsaved dialogue edit?"), QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel) == QMessageBox::Discard;
}

void DialogueEditor::scan() {
    m_dirty = false;
    QSignalBlocker blocker(m_files);
    m_files->clear();
    m_fileIndex = -1;
    QStringList paths;
    int skipped = 0;
    // Do not follow symlink directories, or index generated .inc siblings of .pory sources.
    QDirIterator it(m_root + "/data", {"*.inc", "*.s"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isSymLink() || !info.canonicalFilePath().startsWith(m_root + '/')) continue;
        if (QFileInfo::exists(info.path() + '/' + info.completeBaseName() + ".pory")) continue;
        DialogueDocument document;
        QString error;
        if (!document.load(path, &error)) { ++skipped; continue; }
        if (!document.entries().isEmpty()) paths.append(QDir(m_root).relativeFilePath(path));
    }
    paths.sort(Qt::CaseInsensitive);
    m_files->addItems(paths);
    loadFile(m_files->currentIndex());
    if (paths.isEmpty())
        m_status->setText(tr("No editable assembly dialogue found under data/. Poryscript and C strings are not supported yet."));
    if (skipped)
        m_status->setText(m_status->text() + tr(" %1 unreadable or non-UTF-8 files skipped.").arg(skipped));
}

void DialogueEditor::loadFile(int index) {
    if (!confirmDiscard()) {
        QSignalBlocker blocker(m_files);
        m_files->setCurrentIndex(m_fileIndex);
        return;
    }
    m_dirty = false;
    m_fileIndex = index;
    m_entryIndex = -1;
    QSignalBlocker blocker(m_entries);
    m_entries->clear();
    m_text->clear();
    m_text->setEnabled(false);
    m_save->setEnabled(false);
    setWindowTitle(tr("Porymap-Z — Dialogue Editor"));
    if (index < 0) return;
    const auto path = m_root + '/' + m_files->itemText(index);
    if (!QFileInfo(path).canonicalFilePath().startsWith(m_root + '/')) {
        m_status->setText(tr("The selected file is outside the project."));
        return;
    }
    QString error;
    if (!m_document.load(path, &error)) {
        m_status->setText(tr("Cannot load file: %1").arg(error));
        return;
    }
    for (const auto &entry : m_document.entries())
        m_entries->addItem(tr("%1 (line %2)").arg(entry.label).arg(entry.line));
    m_status->setText(tr("%1 dialogue entries. Saving updates only the selected entry in this file.").arg(m_entries->count()));
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
    if (!confirmDiscard()) {
        QSignalBlocker blocker(m_entries);
        m_entries->setCurrentRow(m_entryIndex);
        return;
    }
    m_entryIndex = row;
    m_dirty = false;
    QSignalBlocker blocker(m_text);
    m_text->setPlainText(row >= 0 ? m_document.entries().at(row).text : QString());
    m_text->setEnabled(row >= 0);
    m_save->setEnabled(false);
    setWindowTitle(tr("Porymap-Z — Dialogue Editor"));
}

void DialogueEditor::filter() {
    const auto search = m_search->text();
    for (int i = 0; i < m_entries->count(); ++i) {
        const auto &entry = m_document.entries().at(i);
        m_entries->item(i)->setHidden(!entry.label.contains(search, Qt::CaseInsensitive)
                                     && !entry.text.contains(search, Qt::CaseInsensitive));
    }
}

void DialogueEditor::save() {
    QString error;
    const auto path = m_root + '/' + m_files->currentText();
    if (!QFileInfo(path).canonicalFilePath().startsWith(m_root + '/')) {
        QMessageBox::warning(this, tr("Cannot Save Dialogue"), tr("The selected file is outside the project."));
        return;
    }
    if (!m_document.save(m_entryIndex, m_text->toPlainText(), &error)) {
        QMessageBox::warning(this, tr("Cannot Save Dialogue"), error);
        return;
    }
    const int row = m_entryIndex;
    m_dirty = false;
    loadFile(m_fileIndex);
    m_entries->setCurrentRow(row);
    m_status->setText(tr("Saved. Build your pokeemerald project to test the dialogue in game."));
}

void DialogueEditor::closeEvent(QCloseEvent *event) {
    if (confirmDiscard()) event->accept(); else event->ignore();
}
void DialogueEditor::reject() {
    if (confirmDiscard()) QDialog::reject();
}
}
