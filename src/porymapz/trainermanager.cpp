#include "porymapz/trainermanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace PorymapZ {
namespace {

QComboBox *searchableCombo(const QStringList &items, const QString &current, QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->addItems(items);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->completer()->setFilterMode(Qt::MatchContains);
    combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    const int index = combo->findText(current);
    if (index >= 0) combo->setCurrentIndex(index);
    return combo;
}

QPlainTextEdit *dialogueBox(const QString &text, QWidget *parent) {
    auto *box = new QPlainTextEdit(text, parent);
    box->setTabChangesFocus(true);
    box->setFixedHeight(box->fontMetrics().lineSpacing() * 3 + 12);
    return box;
}

QString countOf(qsizetype count) {
    return count == 1 ? QObject::tr("1 problem") : QObject::tr("%1 problems").arg(count);
}

// Prefer a constant by name so the defaults stay sensible across projects.
QString preferred(const QStringList &items, const QStringList &names) {
    for (const auto &name : names) if (items.contains(name)) return name;
    return items.value(0);
}

}

NewTrainerDialog::NewTrainerDialog(TrainerProject *project, const QString &mapName, const QString &scriptsFile,
                                   const TrainerEventOptions &options, QWidget *parent)
    : QDialog(parent), m_project(project), m_mapName(mapName), m_scriptsFile(scriptsFile) {
    setObjectName("newTrainerDialog");
    setWindowTitle(tr("New Trainer"));
    auto *layout = new QVBoxLayout(this);

    auto *trainer = new QGroupBox(tr("Trainer"), this);
    auto *trainerForm = new QFormLayout(trainer);
    m_set = new QComboBox(this);
    m_set->setObjectName("trainerSet");
    for (const auto &set : project->sets()) m_set->addItem(set.label);
    m_set->setCurrentIndex(project->defaultSet());
    if (project->sets().size() > 1) trainerForm->addRow(tr("Add to"), m_set);
    else m_set->hide();
    m_name = new QLineEdit(this);
    m_name->setObjectName("trainerName");
    m_name->setPlaceholderText(tr("CALVIN"));
    if (project->nameLimit() > 0) m_name->setMaxLength(project->nameLimit());
    trainerForm->addRow(tr("Name"), m_name);
    m_id = new QLineEdit(this);
    m_id->setObjectName("trainerId");
    m_id->setToolTip(tr("The constant scripts use to start this battle."));
    trainerForm->addRow(tr("ID"), m_id);
    m_class = searchableCombo(project->classes(), preferred(project->classes(), {"TRAINER_CLASS_YOUNGSTER"}), this);
    m_class->setObjectName("trainerClass");
    trainerForm->addRow(tr("Class"), m_class);
    m_pic = searchableCombo(project->pics(), preferred(project->pics(), {"TRAINER_PIC_YOUNGSTER"}), this);
    m_pic->setObjectName("trainerPic");
    trainerForm->addRow(tr("Picture"), m_pic);
    m_music = searchableCombo(project->musics(), preferred(project->musics(), {"TRAINER_ENCOUNTER_MUSIC_MALE"}), this);
    m_music->setObjectName("trainerMusic");
    trainerForm->addRow(tr("Encounter music"), m_music);
    m_gender = new QComboBox(this);
    m_gender->setObjectName("trainerGender");
    m_gender->addItems({tr("Male"), tr("Female")});
    trainerForm->addRow(tr("Gender"), m_gender);
    m_battle = new QComboBox(this);
    m_battle->setObjectName("trainerBattleType");
    m_battle->addItems({tr("Single battle"), tr("Double battle")});
    trainerForm->addRow(tr("Battle"), m_battle);
    layout->addWidget(trainer);

    auto *event = new QGroupBox(tr("Map event"), this);
    auto *eventForm = new QFormLayout(event);
    m_sprite = searchableCombo(options.sprites, options.sprite, this);
    m_sprite->setObjectName("trainerSprite");
    eventForm->addRow(tr("Sprite"), m_sprite);
    m_type = searchableCombo(options.trainerTypes, options.trainerType, this);
    m_type->setObjectName("trainerEventType");
    eventForm->addRow(tr("Trainer type"), m_type);
    m_sight = new QSpinBox(this);
    m_sight->setObjectName("trainerSight");
    m_sight->setRange(0, 15);
    m_sight->setValue(options.sightRadius);
    eventForm->addRow(tr("Sight radius"), m_sight);
    m_label = new QLineEdit(this);
    m_label->setObjectName("trainerScriptLabel");
    eventForm->addRow(tr("Script label"), m_label);
    layout->addWidget(event);

    auto *dialogue = new QGroupBox(tr("Dialogue"), this);
    auto *dialogueForm = new QFormLayout(dialogue);
    m_intro = dialogueBox(tr("Let's battle!"), this);
    m_intro->setObjectName("trainerIntro");
    dialogueForm->addRow(tr("Before battle"), m_intro);
    m_defeat = dialogueBox(tr("I lost!"), this);
    m_defeat->setObjectName("trainerDefeat");
    dialogueForm->addRow(tr("On defeat"), m_defeat);
    m_post = dialogueBox(tr("You're really strong!"), this);
    m_post->setObjectName("trainerPost");
    dialogueForm->addRow(tr("After battle"), m_post);
    layout->addWidget(dialogue);

    auto *note = new QLabel(tr("The trainer starts with a one-Pokémon placeholder party. Edit it in the Battle tab."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    if (!options.replacedScript.isEmpty()) {
        auto *replaced = new QLabel(tr("This object's script, %1, will be replaced. The old script stays in its file.").arg(options.replacedScript), this);
        replaced->setObjectName("trainerReplacedScript");
        replaced->setWordWrap(true);
        replaced->setTextFormat(Qt::PlainText);
        layout->addWidget(replaced);
    }
    m_message = new QLabel(this);
    m_message->setObjectName("trainerMessage");
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);
    layout->addWidget(m_message);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_create = buttons->addButton(tr("Create Trainer"), QDialogButtonBox::AcceptRole);
    m_create->setObjectName("createTrainer");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewTrainerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewTrainerDialog::reject);

    connect(m_name, &QLineEdit::textChanged, this, &NewTrainerDialog::nameEdited);
    connect(m_id, &QLineEdit::textEdited, this, [this] { m_idEdited = true; refresh(); });
    connect(m_label, &QLineEdit::textEdited, this, [this] { m_labelEdited = true; refresh(); });
    for (auto *combo : {m_set, m_class, m_pic, m_music})
        connect(combo, &QComboBox::currentTextChanged, this, &NewTrainerDialog::refresh);
    nameEdited();
}

void NewTrainerDialog::nameEdited() {
    const QString name = m_name->text().trimmed();
    if (!m_idEdited) m_id->setText(name.isEmpty() ? QString() : m_project->suggestedId(name));
    if (!m_labelEdited) m_label->setText(name.isEmpty() ? QString() : m_project->suggestedScriptLabel(m_mapName, name));
    refresh();
}

NewTrainerSpec NewTrainerDialog::spec() const {
    NewTrainerSpec spec;
    spec.set = m_set->currentIndex();
    spec.id = m_id->text().trimmed();
    spec.name = m_name->text().trimmed();
    spec.trainerClass = m_class->currentText();
    spec.pic = m_pic->currentText();
    spec.music = m_music->currentText();
    spec.female = m_gender->currentIndex() == 1;
    spec.doubleBattle = m_battle->currentIndex() == 1;
    spec.scriptsFile = m_scriptsFile;
    spec.scriptLabel = m_label->text().trimmed();
    spec.intro = m_intro->toPlainText();
    spec.defeat = m_defeat->toPlainText();
    spec.postBattle = m_post->toPlainText();
    return spec;
}

QString NewTrainerDialog::sprite() const { return m_sprite->currentText(); }
QString NewTrainerDialog::trainerType() const { return m_type->currentText(); }
int NewTrainerDialog::sightRadius() const { return m_sight->value(); }

void NewTrainerDialog::refresh() {
    const QString problem = m_name->text().trimmed().isEmpty() ? tr("Enter the trainer's in-game name.") : m_project->validate(spec());
    m_message->setText(problem);
    m_create->setEnabled(problem.isEmpty());
}

void NewTrainerDialog::accept() {
    QString error;
    if (!m_project->create(spec(), &error) || !m_project->save(&error)) {
        m_message->setText(error);
        return;
    }
    QDialog::accept();
}

TrainerRepairDialog::TrainerRepairDialog(TrainerProject *project, QWidget *parent) : QDialog(parent), m_project(project) {
    setObjectName("trainerRepairDialog");
    setWindowTitle(tr("Check Trainer Files"));
    resize(860, 600);
    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setObjectName("trainerRepairSummary");
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_list = new QTreeWidget(split);
    m_list->setObjectName("trainerIssues");
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({tr("Problem"), tr("Type"), tr("Location")});
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_details = new QPlainTextEdit(split);
    m_details->setObjectName("trainerIssueDetails");
    m_details->setReadOnly(true);
    m_details->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    auto *row = new QHBoxLayout;
    m_selectAll = new QPushButton(tr("Select All Repairs"), this);
    m_selectAll->setObjectName("selectAllRepairs");
    auto *rescanButton = new QPushButton(tr("Check Again"), this);
    rescanButton->setObjectName("rescanTrainers");
    m_battle = new QPushButton(tr("Open in Battle Tab"), this);
    m_battle->setObjectName("openTrainerInBattle");
    m_apply = new QPushButton(tr("Repair Selected"), this);
    m_apply->setObjectName("applyTrainerRepairs");
    auto *close = new QPushButton(tr("Close"), this);
    row->addWidget(m_selectAll);
    row->addWidget(rescanButton);
    row->addWidget(m_battle);
    row->addStretch();
    row->addWidget(m_apply);
    row->addWidget(close);
    layout->addLayout(row);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(rescanButton, &QPushButton::clicked, this, &TrainerRepairDialog::rescan);
    connect(m_apply, &QPushButton::clicked, this, &TrainerRepairDialog::applySelected);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &TrainerRepairDialog::showDetails);
    connect(m_list, &QTreeWidget::itemChanged, this, &TrainerRepairDialog::updateButtons);
    connect(m_selectAll, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            auto *item = m_list->topLevelItem(i);
            if (item->flags() & Qt::ItemIsUserCheckable) item->setCheckState(0, Qt::Checked);
        }
    });
    connect(m_battle, &QPushButton::clicked, this, [this] {
        const auto *item = m_list->currentItem();
        if (!item) return;
        const QString trainer = m_issues.value(item->data(0, Qt::UserRole).toInt()).trainer;
        if (trainer.isEmpty()) return;
        emit openTrainer(trainer);
        accept();
    });
    rescan();
}

void TrainerRepairDialog::rescan() {
    m_issues = m_project->analyze();
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    int repairable = 0;
    for (int i = 0; i < m_issues.size(); ++i) {
        const auto &issue = m_issues[i];
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, issue.summary);
        item->setText(1, issue.severity == TrainerIssue::Error ? tr("Error") : tr("Warning"));
        item->setText(2, issue.line > 0 ? QStringLiteral("%1:%2").arg(issue.file).arg(issue.line) : issue.file);
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(0, issue.summary);
        if (issue.fixable()) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
            ++repairable;
        }
    }
    if (m_issues.isEmpty()) {
        m_summary->setText(tr("No problems found. IDs, trainer data, parties, and battle scripts all line up."));
        m_details->clear();
    } else {
        m_summary->setText(tr("%1 found, %2 repaired automatically. Select a problem to preview its repair.")
                               .arg(countOf(m_issues.size()), repairable == 1 ? tr("1 can be") : tr("%1 can be").arg(repairable)));
        m_list->setCurrentItem(m_list->topLevelItem(0));
    }
    showDetails();
    updateButtons();
}

void TrainerRepairDialog::showDetails() {
    const auto *item = m_list->currentItem();
    if (!item) { m_details->clear(); updateButtons(); return; }
    const TrainerIssue issue = m_issues.value(item->data(0, Qt::UserRole).toInt());
    QString text = issue.detail;
    if (issue.fixable()) text += QStringLiteral("\n\n") + tr("Repair: %1").arg(issue.fix) + QStringLiteral("\n\n") + m_project->previewEdits(issue);
    else text += QStringLiteral("\n\n") + tr("This one needs a decision from you, so it has no automatic repair.");
    m_details->setPlainText(text);
    updateButtons();
}

void TrainerRepairDialog::updateButtons() {
    int checked = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(0) == Qt::Checked) ++checked;
    m_apply->setEnabled(checked > 0);
    m_apply->setText(checked > 0 ? tr("Repair %1 Selected").arg(checked) : tr("Repair Selected"));
    const auto *item = m_list->currentItem();
    m_battle->setEnabled(item && !m_issues.value(item->data(0, Qt::UserRole).toInt()).trainer.isEmpty());
    bool anyRepairable = false;
    for (const auto &issue : m_issues) anyRepairable = anyRepairable || issue.fixable();
    m_selectAll->setEnabled(anyRepairable);
}

void TrainerRepairDialog::applySelected() {
    QSet<QString> keys;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const auto *item = m_list->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) keys.insert(m_issues.value(item->data(0, Qt::UserRole).toInt()).key);
    }
    const int applied = m_project->applyFixes(keys);
    QString error;
    if (!m_project->save(&error)) {
        QMessageBox::warning(this, tr("Cannot Repair Trainer Files"), error);
        rescan();
        return;
    }
    emit filesChanged();
    rescan();
    m_summary->setText(tr("Repaired %1. %2").arg(countOf(applied), m_summary->text()));
}

}
