#include "porymapz/battledocument.h"
#include "porymapz/battleeditor.h"
#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QLineEdit>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QCompleter>
#include <QStyleOptionViewItem>
#include <QAction>
#include <QLabel>
#include <QTabWidget>
#include <QSet>
#include <QProcess>
#include "porymapz/dialogueeditor.h"
using namespace PorymapZ;
static const QString trainers = "// untouched\nconst struct Trainer gTrainers[] = {\n"
    " [TRAINER_TEST] = {\n .trainerName = _(\"A, {B} \\\"C\\\"\"), // name\n .doubleBattle = FALSE,\n .items = {ITEM_NONE, ITEM_POTION},\n .party = {.NoItemCustomMoves = sParty_Test},\n },\n};\n";
static const QString parties = "static const struct TrainerMonItemCustomMoves sParty_Test[] = {\n"
    " { .iv = 100, .lvl = 5, .species = SPECIES_TREECKO, .heldItem = ITEM_NONE,\n"
    " .moves = {MOVE_POUND, MOVE_LEER, MOVE_NONE, MOVE_NONE}, },\n};\n";
static const QString expansion =
    "/* retained comment: === TRAINER_FAKE === */\n"
    "=== TRAINER_TEST ===\nName: ALICE\nClass: Hiker\nPic: Hiker\nDouble Battle: No\nAI: Check Bad Move\n\n"
    "Geodude @ Oran Berry\nLevel: 21\nAbility: Rock Head\nIVs: 31 HP / 0 Atk\nEVs: 252 HP / 252 Def\nNature: Bold\n- Tackle\n- Defense Curl\n\n"
    "=== TRAINER_NONE ===\nName:\nPic: Hiker\n";
class BattleTest : public QObject {
    Q_OBJECT
    int field(const BattleDocument &doc, const QString &name) {
        for (int i = 0; i < doc.nodes().size(); ++i) if (doc.nodes()[i].name == name) return i;
        return -1;
    }
    bool createProject(QTemporaryDir &dir, const QString &trainerSource = trainers, const QString &partySource = parties) {
        if (!QDir(dir.path()).mkpath("src/data")) return false;
        for (const auto &pair : {qMakePair(QString("trainers.h"), trainerSource), qMakePair(QString("trainer_parties.h"), partySource)}) {
            QFile file(dir.filePath("src/data/" + pair.first));
            if (!file.open(QIODevice::WriteOnly) || file.write(pair.second.toUtf8()) != pair.second.toUtf8().size()) return false;
        }
        return true;
    }
    QTreeWidgetItem *findField(QTreeWidget *tree, const QString &field) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) if ((*it)->data(0, Qt::UserRole+1).toString() == field) return *it;
        return nullptr;
    }
    QModelIndex valueIndex(QTreeWidget *tree, QTreeWidgetItem *item) {
        QList<int> rows;
        for (auto *current = item; current; current = current->parent()) rows.prepend(current->parent() ? current->parent()->indexOfChild(current) : tree->indexOfTopLevelItem(current));
        QModelIndex index;
        for (int row : rows) index = tree->model()->index(row, 0, index);
        return index.sibling(index.row(), 1);
    }
    void answer(QMessageBox::StandardButton choice) {
        QTimer::singleShot(0, [choice] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(choice)->click(); });
    }
    bool compileParty(const QString &tool, const QString &input, const QString &output, QString *error) {
        QProcess preprocess;
        preprocess.start("cpp", {"-traditional-cpp", "-P", input, "-o", input + ".preprocessed"});
        if (!preprocess.waitForFinished(10000) || preprocess.exitStatus() != QProcess::NormalExit || preprocess.exitCode() != 0) {
            *error = preprocess.errorString() + QString::fromUtf8(preprocess.readAllStandardError()); return false;
        }
        QProcess proc; proc.start(tool, {"-o", output, input + ".preprocessed"});
        if (!proc.waitForFinished(10000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            *error = proc.errorString() + QString::fromUtf8(proc.readAllStandardError()); return false;
        }
        return true;
    }
private slots:
    void trainerFieldsPreserveSource() {
        BattleDocument doc;
        QVERIFY(doc.parse(trainers));
        QCOMPARE(doc.records().size(), 1);
        const int name = field(doc, ".trainerName");
        QVERIFY(name >= 0);
        QVERIFY(doc.setValue(name, "_(\"NEW\")"));
        QCOMPARE(doc.source(), QString(trainers).replace("_(\"A, {B} \\\"C\\\"\")", "_(\"NEW\")"));
        QVERIFY(doc.setValue(field(doc, ".doubleBattle"), "TRUE"));
        QVERIFY(doc.source().contains(".doubleBattle = TRUE"));
        const auto before = doc.source();
        QVERIFY(!doc.setValue(field(doc, ".doubleBattle"), "FALSE, .bad = TRUE"));
        QVERIFY(!doc.setValue(field(doc, ".doubleBattle"), "\"unterminated"));
        QVERIFY(!doc.setValue(field(doc, ".doubleBattle"), "([)]"));
        QCOMPARE(doc.source(), before);
        QVERIFY(doc.duplicate(doc.records().first(), "TRAINER_NEW"));
        QCOMPARE(doc.records().size(), 2);
        QVERIFY(!doc.duplicate(doc.records().first(), "TRAINER_NEW"));
        QVERIFY(doc.remove(doc.records().first()));
        QCOMPARE(doc.records().size(), 1);
        QVERIFY(doc.source().contains("[TRAINER_NEW]"));
    }
    void partyAndMoveOperations() {
        BattleDocument doc; QVERIFY(doc.parse(parties));
        const int party = doc.records().first();
        QCOMPARE(doc.nodes()[party].children.size(), 1);
        QVERIFY(doc.setValue(field(doc, ".lvl"), "50"));
        QVERIFY(doc.duplicate(doc.nodes()[party].children.first()));
        QCOMPARE(doc.nodes()[party].children.size(), 2);
        QVERIFY(doc.remove(doc.nodes()[party].children.last()));
        QCOMPARE(doc.nodes()[party].children.size(), 1);
        QVERIFY(doc.appendPokemon(party));
        QCOMPARE(doc.nodes()[party].children.size(), 2);
        QVERIFY(doc.source().contains(".heldItem = ITEM_NONE"));
        QVERIFY(doc.source().contains(".moves = {MOVE_POUND"));
        QVERIFY(doc.duplicate(party, "sParty_New"));
        QCOMPARE(doc.records().size(), 2);
        QVERIFY(doc.source().contains("static const struct TrainerMonItemCustomMoves sParty_New[]"));
        QVERIFY(doc.remove(doc.records().first()));
        QVERIFY(!doc.source().contains("sParty_Test[]"));
        QVERIFY(!doc.source().contains("static ;"));
    }
    void commentsAndLineEndings() {
        BattleDocument doc;
        QVERIFY(doc.parse(QString(trainers).replace(".doubleBattle = FALSE,", ".doubleBattle = FALSE /* keep , { } */," ).replace("\n", "\r\n")));
        QVERIFY(doc.setValue(field(doc, ".doubleBattle"), "TRUE"));
        QVERIFY(doc.source().contains("TRUE /* keep , { } */,"));
        QVERIFY(doc.source().contains("\r\n"));
    }
    void partyLimitsAndSafeMutation() {
        BattleDocument doc; QVERIFY(doc.parse(parties));
        int party = doc.records().first();
        QVERIFY(!doc.remove(doc.nodes()[party].children.first()));
        QVERIFY(!doc.duplicate(field(doc, ".moves")));
        QVERIFY(!doc.remove(field(doc, ".moves")));
        for (int count = 1; count < 6; ++count) QVERIFY(doc.appendPokemon(party));
        const auto full = doc.source();
        QVERIFY(!doc.appendPokemon(party));
        QVERIFY(!doc.duplicate(doc.nodes()[party].children.first()));
        QCOMPARE(doc.source(), full);
        QVERIFY(doc.parse(QString(parties).replace("[]", "[1]")));
        QVERIFY(!doc.appendPokemon(doc.records().first()));
        QVERIFY(doc.parse("static const struct TrainerMonNoItemDefaultMoves team[] = { DUMMY_TRAINER_MON };"));
        QVERIFY(!doc.appendPokemon(doc.records().first()));
        QVERIFY(doc.parse(trainers));
        const auto before = doc.source();
        QVERIFY(!doc.remove(field(doc, ".items")));
        QVERIFY(!doc.duplicate(field(doc, ".party")));
        QCOMPARE(doc.source(), before);
    }
    void intuitiveControlsAndNavigation() {
        QTemporaryDir dir;
        QVERIFY(createProject(dir, QString(trainers).replace("sParty_Test", "customTeam"), QString(parties).replace("sParty_Test", "customTeam")));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}));
        auto *tree = editor.findChild<QTreeWidget *>("battleFields");
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        auto *duplicate = editor.findChild<QPushButton *>("duplicateBattleRecord");
        auto *remove = editor.findChild<QPushButton *>("deleteBattleRecord");
        auto *add = editor.findChild<QPushButton *>("addBattlePokemon");
        auto *go = editor.findChild<QPushButton *>("goToBattleParty");
        QVERIFY(go->isEnabled()); QVERIFY(!add->isEnabled());
        tree->setCurrentItem(findField(tree, ".items"));
        QCOMPARE(remove->text(), QString("Delete Trainer"));
        auto *battle = findField(tree, ".doubleBattle");
        QVERIFY(battle); QCOMPARE(battle->text(0), QString("Battle format"));
        QWidget host;
        auto *delegate = tree->itemDelegate();
        const auto battleIndex = valueIndex(tree, battle);
        auto *combo = qobject_cast<QComboBox *>(delegate->createEditor(&host, QStyleOptionViewItem(), battleIndex));
        QVERIFY(combo); delegate->setEditorData(combo, battleIndex);
        QCOMPARE(combo->currentText(), QString("Single battle"));
        combo->setCurrentIndex(1); delegate->setModelData(combo, tree->model(), battleIndex);
        QVERIFY(source->toPlainText().contains(".doubleBattle = TRUE"));
        QVERIFY(editor.hasUnsavedChanges());
        answer(QMessageBox::Cancel); go->click();
        QCOMPARE(files->currentIndex(), 0); QVERIFY(editor.hasUnsavedChanges());
        source->undo(); QVERIFY(!editor.hasUnsavedChanges());
        go->click(); QCOMPARE(files->currentIndex(), 1);
        QCOMPARE(editor.findChild<QListWidget *>("battleRecords")->currentItem()->text(), QString("customTeam"));
        auto *level = findField(tree, ".lvl"); QVERIFY(level);
        const auto levelIndex = valueIndex(tree, level);
        auto *spin = qobject_cast<QSpinBox *>(delegate->createEditor(&host, QStyleOptionViewItem(), levelIndex));
        QVERIFY(spin); QCOMPARE(spin->minimum(), 1); QCOMPARE(spin->maximum(), 100);
        delegate->setEditorData(spin, levelIndex); QCOMPARE(spin->value(), 5);
        spin->setValue(50); delegate->setModelData(spin, tree->model(), levelIndex);
        QVERIFY(tree->topLevelItem(0)->child(0)->text(1).contains("Level 50"));
        tree->setCurrentItem(findField(tree, ".species"));
        QCOMPARE(duplicate->text(), QString("Duplicate Pokémon"));
        QVERIFY(!remove->isEnabled()); // The last Pokémon cannot be removed.
        duplicate->click(); QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        source->undo(); QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        source->undo(); QVERIFY(!editor.hasUnsavedChanges());
        level = findField(tree, ".lvl"); level->setText(1, "101");
        QCOMPARE(level->text(1), QString("5")); QVERIFY(!editor.hasUnsavedChanges());
        auto *search = editor.findChild<QLineEdit *>("battleSearch");
        search->setText("no matching records");
        QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(!duplicate->isEnabled()); QVERIFY(!remove->isEnabled()); QVERIFY(!add->isEnabled());
        search->clear(); QCOMPARE(tree->topLevelItemCount(), 1);
    }
    void plainNamesAndScopedCompletion() {
        QTemporaryDir dir;
        QVERIFY(createProject(dir, QString(trainers).replace("_(\"A, {B} \\\"C\\\"\")", "_(\"ALICE\")")));
        QVERIFY(QDir(dir.path()).mkpath("include/constants"));
        QFile constants(dir.filePath("include/constants/species.h"));
        QVERIFY(constants.open(QIODevice::WriteOnly)); constants.write("#define SPECIES_TREECKO 1\n#define MOVE_POUND 1\n#define ITEM_NONE 0\n"); constants.close();
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}));
        auto *tree = editor.findChild<QTreeWidget *>("battleFields");
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        QWidget host;
        auto *delegate = tree->itemDelegate();
        auto *name = findField(tree, ".trainerName"); QVERIFY(name);
        const auto nameIndex = valueIndex(tree, name);
        auto *line = qobject_cast<QLineEdit *>(delegate->createEditor(&host, QStyleOptionViewItem(), nameIndex));
        QVERIFY(line); delegate->setEditorData(line, nameIndex); QCOMPARE(line->text(), QString("ALICE"));
        line->setText("BOB"); delegate->setModelData(line, tree->model(), nameIndex);
        QVERIFY(source->toPlainText().contains("_(\"BOB\")"));
        QVERIFY(editor.findChild<QListWidget *>("battleRecords")->currentItem()->text().startsWith("BOB"));
        auto *undo = editor.findChild<QPushButton *>("undoBattleData");
        auto *redo = editor.findChild<QPushButton *>("redoBattleData");
        QVERIFY(undo->isEnabled()); undo->click(); QVERIFY(!editor.hasUnsavedChanges());
        QVERIFY(redo->isEnabled()); redo->click(); QVERIFY(editor.hasUnsavedChanges());
        editor.show(); tree->setFocus();
        QAction mapUndo(&editor); mapUndo.setShortcut(QKeySequence::Undo); editor.addAction(&mapUndo);
        QSignalSpy mapUndoTriggered(&mapUndo, &QAction::triggered);
        QTest::keyClick(tree, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!editor.hasUnsavedChanges()); QCOMPARE(mapUndoTriggered.count(), 0);
        editor.findChild<QPushButton *>("goToBattleParty")->click();
        auto *species = findField(tree, ".species"); QVERIFY(species);
        const auto speciesIndex = valueIndex(tree, species);
        auto *speciesEditor = qobject_cast<QLineEdit *>(delegate->createEditor(&host, QStyleOptionViewItem(), speciesIndex));
        QVERIFY(speciesEditor && speciesEditor->completer());
        auto *model = speciesEditor->completer()->model();
        QCOMPARE(model->rowCount(), 1); QCOMPARE(model->index(0,0).data().toString(), QString("SPECIES_TREECKO"));
    }
    void expansionSourceEditing() {
        BattleDocument doc; QVERIFY(doc.parse(expansion)); QVERIFY(doc.isPartySource());
        QCOMPARE(doc.records().size(), 2);
        const int trainer = doc.records().first(); QVERIFY(doc.isTrainer(trainer));
        QCOMPARE(doc.pokemon(trainer).size(), 1);
        const int mon = doc.pokemon(trainer).first(); QVERIFY(doc.isPokemon(mon));
        QVERIFY(doc.setValue(field(doc, ".lvl"), "50"));
        QCOMPARE(doc.source(), QString(expansion).replace("Level: 21", "Level: 50"));
        const auto before = doc.source();
        const int moves = field(doc, ".moves");
        QVERIFY(!doc.setValue(doc.nodes().at(moves).children.last(), ""));
        QVERIFY(!doc.setValue(field(doc, ".lvl"), "50\n=== TRAINER_INJECT ==="));
        QCOMPARE(doc.source(), before);
        QVERIFY(doc.setValue(field(doc, ".trainerName"), "BOB"));
        QVERIFY(doc.source().contains("Name: BOB"));
        QVERIFY(doc.duplicate(mon)); QCOMPARE(doc.pokemon(trainer).size(), 2);
        QVERIFY(doc.remove(doc.pokemon(trainer).first())); QCOMPARE(doc.pokemon(trainer).size(), 1);
        QVERIFY(!doc.remove(doc.pokemon(trainer).first()));
        QVERIFY(doc.appendPokemon(trainer)); QCOMPARE(doc.pokemon(trainer).size(), 2);
        QVERIFY(doc.source().contains("Treecko\nLevel: 5"));
        QVERIFY(doc.duplicate(trainer, "TRAINER_COPY")); QCOMPARE(doc.records().size(), 3);
        QVERIFY(!doc.duplicate(trainer, "TRAINER_COPY"));
        QVERIFY(doc.source().startsWith("/* retained comment"));
        QVERIFY(doc.parse(QString(expansion).replace("\n", "\r\n")));
        QVERIFY(doc.appendPokemon(doc.records().first()));
        QVERIFY(!QString(doc.source()).replace("\r\n", "").contains('\n'));
    }
    void expansionWorkspace() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        QFile party(dir.filePath("src/data/trainers.party")); QVERIFY(party.open(QIODevice::WriteOnly));
        party.write(expansion.toUtf8()); party.close();
        // The fixture includes stock headers: .party must win over generated C.
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}));
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        QCOMPARE(files->count(), 1);
        QCOMPARE(files->currentData().toString(), QString("src/data/trainers.party"));
        auto *tree = editor.findChild<QTreeWidget *>("battleFields");
        auto *add = editor.findChild<QPushButton *>("addBattlePokemon");
        auto *go = editor.findChild<QPushButton *>("goToBattleParty");
        auto *remove = editor.findChild<QPushButton *>("deleteBattleRecord");
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        QVERIFY(add->isEnabled()); QVERIFY(go->isEnabled());
        QCOMPARE(remove->text(), QString("Delete Trainer"));
        go->click(); QCOMPARE(remove->text(), QString("Delete Pokémon"));
        QCOMPARE(tree->currentItem()->text(0), QString("Pokémon 1"));
        QVERIFY(!remove->isEnabled()); add->click();
        QVERIFY(source->toPlainText().contains("Treecko"));
        QCOMPARE(tree->currentItem()->text(0), QString("Pokémon 2"));
        source->undo(); QVERIFY(!editor.hasUnsavedChanges());
        findField(tree, ".lvl")->setText(1, "42");
        QVERIFY(editor.saveChanges());
        QVERIFY(party.open(QIODevice::ReadOnly));
        QVERIFY(party.readAll().contains("Level: 42")); party.close();
        QFile generated(dir.filePath("src/data/trainers.h")); QVERIFY(generated.open(QIODevice::ReadOnly));
        QCOMPARE(generated.readAll(), trainers.toUtf8());
    }
    void expansionCorpus() {
        const QString path = qEnvironmentVariable("PORYMAP_Z_EXPANSION_CORPUS");
        if (path.isEmpty()) QSKIP("Set PORYMAP_Z_EXPANSION_CORPUS to a copy of src/data/trainers.party.");
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly));
        const auto bytes = source.readAll();
        BattleDocument doc; QVERIFY(doc.parse(QString::fromUtf8(bytes))); QVERIFY(doc.isPartySource());
        const QRegularExpression headers("^=== TRAINER_[A-Za-z0-9_]+ ===", QRegularExpression::MultilineOption);
        int count = 0; auto matches = headers.globalMatch(doc.source()); while (matches.hasNext()) { matches.next(); ++count; }
        QVERIFY(count > 500); QCOMPARE(doc.records().size(), count);
        const auto original = doc.source();
        QVERIFY(doc.setValue(field(doc, ".lvl"), "42"));
        QVERIFY(doc.source().contains("Level: 42"));
        QTemporaryDir dir;
        const auto generatedInput = dir.filePath("trainers.party");
        QFile output(generatedInput); QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(doc.source().toUtf8()); output.close();
        const QString trainerproc = qEnvironmentVariable("PORYMAP_Z_TRAINERPROC");
        if (!trainerproc.isEmpty()) {
            const auto compiled = dir.filePath("trainers.h");
            QString error; QVERIFY2(compileParty(trainerproc, generatedInput, compiled, &error), qPrintable(error));
            QFile generated(compiled); QVERIFY(generated.open(QIODevice::ReadOnly));
            QVERIFY(generated.readAll().contains(".lvl = 42"));
        }
        QVERIFY(doc.parse(original));
        const int trainer = doc.records().at(1);
        QVERIFY(doc.appendPokemon(trainer));
        QVERIFY(doc.duplicate(trainer, "TRAINER_EDITOR_TEST_COPY"));
        if (!trainerproc.isEmpty()) {
            QVERIFY(output.open(QIODevice::WriteOnly)); output.write(doc.source().toUtf8()); output.close();
            QString error;
            QVERIFY2(compileParty(trainerproc, generatedInput, dir.filePath("mutated.h"), &error), qPrintable(error));
        }
    }
    void relocatedDataAndMissingFileMessages() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        QVERIFY(QDir(dir.path()).mkpath("src/data/trainers"));
        QVERIFY(QFile::rename(dir.filePath("src/data/trainer_parties.h"), dir.filePath("src/data/trainers/parties.h")));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}));
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        QCOMPARE(files->itemData(1).toString(), QString("src/data/trainers/parties.h"));
        files->setCurrentIndex(1);
        QCOMPARE(editor.findChild<QListWidget *>("battleRecords")->count(), 1);
        QVERIFY(QFile::remove(dir.filePath("src/data/trainers/parties.h")));
        files->setCurrentIndex(0); files->setCurrentIndex(1);
        QVERIFY(!editor.findChild<QPlainTextEdit *>("battleSource")->isEnabled());
        bool found = false;
        for (auto *label : editor.findChildren<QLabel *>()) if (label->text().contains("File not found") && label->text().contains("Locate File")) found = true;
        QVERIFY(found);
        QVERIFY(editor.findChild<QPushButton *>("locateBattleFile")->isEnabled());
    }
    void stockPartyMacros_data() {
        QTest::addColumn<QString>("macro");
        for (const QString &macro : QStringList{"NO_ITEM_DEFAULT_MOVES", "NO_ITEM_CUSTOM_MOVES", "ITEM_DEFAULT_MOVES", "ITEM_CUSTOM_MOVES"})
            QTest::newRow(qPrintable(macro)) << macro;
    }
    void stockPartyMacros() {
        QFETCH(QString, macro);
        QTemporaryDir dir;
        QVERIFY(createProject(dir, QString(trainers).replace("{.NoItemCustomMoves = sParty_Test}", macro + "(sParty_Test)")));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}, true, false));
        // C trainer data is still available when scene scripts use Poryscript.
        QVERIFY(editor.findChild<QPlainTextEdit *>("battleSource")->isEnabled());
        auto *go = editor.findChild<QPushButton *>("goToBattleParty"); QVERIFY(go->isEnabled());
        go->click();
        QCOMPARE(editor.findChild<QComboBox *>("battleFiles")->currentIndex(), 1);
        QCOMPARE(editor.findChild<QListWidget *>("battleRecords")->currentItem()->text(), QString("sParty_Test"));
        auto *scripts = editor.findChild<DialogueEditor *>("battleScripts");
        QVERIFY(!scripts->findChild<QPlainTextEdit *>("sceneScript")->isEnabled());
    }
    void stockCorpora() {
        const auto root = qEnvironmentVariable("PORYMAP_Z_BATTLE_CORPUS");
        if (root.isEmpty()) QSKIP("Set PORYMAP_Z_BATTLE_CORPUS to a folder with emerald/ and firered/ stock source copies.");
        for (const QString &game : QStringList{"emerald", "firered"}) {
            const QString project = root + '/' + game;
            QSet<QString> partiesFound;
            for (const QString &name : QStringList{"trainer_parties.h", "trainers.h"}) {
                QFile file(project + "/src/data/" + name); QVERIFY(file.open(QIODevice::ReadOnly));
                const auto bytes = file.readAll();
                BattleDocument document; QVERIFY(document.parse(QString::fromUtf8(bytes)));
                QVERIFY(document.records().size() > 500);
                if (name == "trainer_parties.h") {
                    for (int node : document.records()) partiesFound.insert(document.nodes().at(node).name);
                    const auto original = document.source();
                    const int record = document.records().last();
                    const int mon = document.nodes().at(record).children.first();
                    bool edited = false;
                    for (int child : document.nodes().at(mon).children) {
                        if (document.nodes().at(child).name != ".lvl") continue;
                        const auto field = document.nodes().at(child);
                        const auto value = original.mid(field.valueStart, field.end-field.valueStart);
                        QVERIFY(document.setValue(child, "42"));
                        QCOMPARE(document.source(), QString(original).replace(field.valueStart, field.end-field.valueStart, "42"));
                        QVERIFY(document.setValue(child, value)); QCOMPARE(document.source(), original);
                        edited = true; break;
                    }
                    QVERIFY(edited);
                } else {
                    int checked = 0;
                    const QRegularExpression macro("(?:NO_ITEM_DEFAULT_MOVES|NO_ITEM_CUSTOM_MOVES|ITEM_DEFAULT_MOVES|ITEM_CUSTOM_MOVES)\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)");
                    auto matches = macro.globalMatch(document.source());
                    while (matches.hasNext()) { QVERIFY(partiesFound.contains(matches.next().captured(1))); ++checked; }
                    QVERIFY(checked > 500);
                }
            }
            BattleEditor editor; QVERIFY(editor.setScene(project, "Test", {}));
            auto *go = editor.findChild<QPushButton *>("goToBattleParty");
            auto *records = editor.findChild<QListWidget *>("battleRecords");
            // TRAINER_NONE has no party. The next stock trainer uses a macro.
            QVERIFY(!go->isEnabled());
            records->setCurrentRow(1); QVERIFY(go->isEnabled()); go->click();
            QCOMPARE(editor.findChild<QComboBox *>("battleFiles")->currentIndex(), 1);
            QVERIFY(records->currentItem()); QVERIFY(partiesFound.contains(records->currentItem()->text()));
            QVERIFY(!editor.hasUnsavedChanges());
        }
    }
    void crlfUndoAndSave() {
        QTemporaryDir dir; QVERIFY(createProject(dir, QString(trainers).replace("\n", "\r\n")));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Test", {}));
        auto *tree = editor.findChild<QTreeWidget *>("battleFields");
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        findField(tree, ".doubleBattle")->setText(1, "TRUE");
        QVERIFY(editor.hasUnsavedChanges());
        source->undo(); QVERIFY(!editor.hasUnsavedChanges());
        source->redo(); QVERIFY(editor.hasUnsavedChanges());
        QVERIFY(editor.saveChanges()); QVERIFY(!editor.hasUnsavedChanges());
        source->undo(); QVERIFY(editor.hasUnsavedChanges());
        source->redo(); QVERIFY(!editor.hasUnsavedChanges());
        QFile file(dir.filePath("src/data/trainers.h")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll(); QVERIFY(bytes.contains("\r\n"));
        QVERIFY(!QByteArray(bytes).replace("\r\n", "").contains('\n'));
    }
    void editorSavesAndProtectsExternalChanges() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("src/data"));
        for (const auto &pair : {qMakePair(QString("trainers.h"), trainers), qMakePair(QString("trainer_parties.h"), parties)}) {
            QFile file(dir.filePath("src/data/" + pair.first));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(pair.second.toUtf8());
        }
        BattleEditor editor;
        QVERIFY(editor.setScene(dir.path(), "Test", dir.filePath("data/maps/Test/scripts.inc")));
        auto *records = editor.findChild<QListWidget *>("battleRecords");
        auto *fields = editor.findChild<QTreeWidget *>("battleFields");
        QCOMPARE(records->count(), 1);
        auto *root = fields->topLevelItem(0);
        QVERIFY(root);
        for (int i=0; i<root->childCount(); ++i) if (root->child(i)->data(0, Qt::UserRole+1).toString() == ".doubleBattle") root->child(i)->setText(1, "TRUE");
        QVERIFY(editor.hasUnsavedChanges());
        QVERIFY(editor.saveChanges());
        QVERIFY(!editor.hasUnsavedChanges());
        QFile file(dir.filePath("src/data/trainers.h")); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains(".doubleBattle = TRUE")); file.close();
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        source->appendPlainText("// local change");
        QVERIFY(file.open(QIODevice::Append)); file.write("// external\n"); file.close();
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->accept(); });
        QVERIFY(!editor.saveChanges());
        QVERIFY(editor.hasUnsavedChanges());
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Discard)->click(); });
        QVERIFY(editor.setScene({}, {}, {}));
        QVERIFY(!editor.hasUnsavedChanges());
        QCOMPARE(records->count(), 0);
    }
};
QTEST_MAIN(BattleTest)
#include "tst_battle.moc"
