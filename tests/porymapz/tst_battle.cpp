#include "porymapz/battledocument.h"
#include "porymapz/battleeditor.h"
#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QLineEdit>
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
    bool openWholeProject(BattleEditor &editor, const QString &root, const QString &map, const QString &scripts = {}, bool supported = true, bool scriptsSupported = true) {
        if (!editor.setScene(root, map, scripts, supported, scriptsSupported)) return false;
        editor.findChild<QComboBox *>("battleScope")->setCurrentIndex(1);
        return true;
    }
    QStringList visibleRecords(BattleEditor &editor) {
        QStringList names;
        auto *list = editor.findChild<QListWidget *>("battleRecords");
        for (int row = 0; row < list->count(); ++row) if (!list->item(row)->isHidden()) names.append(list->item(row)->toolTip());
        return names;
    }
    bool writeFile(const QString &path, const QString &text) {
        if (!QDir().mkpath(QFileInfo(path).path())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size();
    }
    bool createProject(QTemporaryDir &dir, const QString &trainerSource = trainers, const QString &partySource = parties) {
        if (!QDir(dir.path()).mkpath("src/data")) return false;
        for (const auto &pair : {qMakePair(QString("trainers.h"), trainerSource), qMakePair(QString("trainer_parties.h"), partySource)}) {
            QFile file(dir.filePath("src/data/" + pair.first));
            if (!file.open(QIODevice::WriteOnly) || file.write(pair.second.toUtf8()) != pair.second.toUtf8().size()) return false;
        }
        return true;
    }
    void replaceText(BattleEditor &editor, const QString &before, const QString &after) {
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        auto cursor = source->document()->find(before);
        QVERIFY(!cursor.isNull());
        cursor.insertText(after);
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
    void textEditingAndNavigation() {
        QTemporaryDir dir;
        const auto extra = QString(trainers).replace("TRAINER_TEST", "TRAINER_OTHER");
        QVERIFY(createProject(dir, trainers + extra));
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test"));
        QVERIFY(!editor.findChild<QTreeWidget *>("battleFields"));
        QVERIFY(!editor.findChild<QPushButton *>("addBattlePokemon"));
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        auto *records = editor.findChild<QListWidget *>("battleRecords");
        QCOMPARE(source->toPlainText(), trainers + extra);
        records->setCurrentRow(1);
        QVERIFY(source->textCursor().position() >= trainers.size());
        replaceText(editor, ".doubleBattle = FALSE", ".doubleBattle = TRUE");
        QVERIFY(editor.hasUnsavedChanges());
        auto *undo = editor.findChild<QPushButton *>("undoBattleData");
        auto *redo = editor.findChild<QPushButton *>("redoBattleData");
        QVERIFY(undo->isEnabled()); undo->click(); QVERIFY(!editor.hasUnsavedChanges());
        QVERIFY(redo->isEnabled()); redo->click(); QVERIFY(editor.hasUnsavedChanges());
        const int position = source->textCursor().position();
        editor.findChild<QLineEdit *>("battleSearch")->setText("no matching records");
        QVERIFY(visibleRecords(editor).isEmpty());
        QCOMPARE(source->textCursor().position(), position);
        QVERIFY(source->toPlainText().contains(".doubleBattle = TRUE"));
        QVERIFY(editor.saveChanges());
        QVERIFY(!editor.hasUnsavedChanges());
    }
    void fileSwitchAndUndoShortcut() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test"));
        replaceText(editor, ".doubleBattle = FALSE", ".doubleBattle = TRUE");
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        answer(QMessageBox::Cancel); files->setCurrentIndex(1);
        QCOMPARE(files->currentIndex(), 0);
        QVERIFY(editor.hasUnsavedChanges());
        editor.show();
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        source->setFocus();
        QAction mapUndo(&editor); mapUndo.setShortcut(QKeySequence::Undo); editor.addAction(&mapUndo);
        QSignalSpy mapUndoTriggered(&mapUndo, &QAction::triggered);
        QTest::keyClick(source, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!editor.hasUnsavedChanges()); QCOMPARE(mapUndoTriggered.count(), 0);
        files->setCurrentIndex(1);
        QCOMPARE(source->toPlainText(), parties);
        QVERIFY(!editor.hasUnsavedChanges());
    }
    void typingIncompleteSourceRetainsCursor() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test"));
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        auto cursor = source->textCursor();
        cursor.movePosition(QTextCursor::End); source->setTextCursor(cursor);
        source->insertPlainText("\n// unfinished edit");
        QCOMPARE(source->textCursor().position(), source->toPlainText().size());
        source->selectAll(); source->insertPlainText("const struct Trainer gTrainers[] = {");
        QCOMPARE(source->textCursor().position(), source->toPlainText().size());
        QVERIFY(editor.hasUnsavedChanges());
        source->undo();
        QVERIFY(source->toPlainText().contains("// unfinished edit"));
    }
    void currentMapExpansionTrainers() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        const QString other = "\n=== TRAINER_OTHER ===\nName: BOB\nPic: Hiker\n\nZubat\nLevel: 10\n";
        QVERIFY(writeFile(dir.filePath("src/data/trainers.party"), expansion));
        QVERIFY(writeFile(dir.filePath("src/data/trainers_frlg.party"), other));
        const auto a = dir.filePath("data/maps/A/scripts.inc");
        const auto b = dir.filePath("data/maps/B/scripts.inc");
        const auto empty = dir.filePath("data/maps/Empty/scripts.inc");
        QVERIFY(writeFile(a, "Battle::\n trainerbattle_single TRAINER_TEST, Intro, Defeat\n @ TRAINER_OTHER\n .string \"TRAINER_OTHER$\"\n"));
        QVERIFY(writeFile(b, "Battle::\n trainerbattle_single TRAINER_OTHER, Intro, Defeat\n"));
        QVERIFY(writeFile(empty, "OnEnter::\n end\n"));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "A", a));
        auto *scope = editor.findChild<QComboBox *>("battleScope");
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        QCOMPARE(scope->currentIndex(), 0);
        QCOMPARE(visibleRecords(editor), QStringList{"TRAINER_TEST"});
        scope->setCurrentIndex(1); QCOMPARE(visibleRecords(editor).size(), 2);
        QVERIFY(editor.setScene(dir.path(), "B", b));
        QCOMPARE(scope->currentIndex(), 0); QCOMPARE(files->currentIndex(), 1);
        QCOMPARE(visibleRecords(editor), QStringList{"TRAINER_OTHER"});
        // A shared script path uses the resolved folder, regardless of map name.
        QVERIFY(editor.setScene(dir.path(), "Shared", a));
        QCOMPARE(files->currentIndex(), 0); QCOMPARE(visibleRecords(editor), QStringList{"TRAINER_TEST"});
        replaceText(editor, "Level: 21", "Level: 30");
        answer(QMessageBox::Cancel);
        QVERIFY(!editor.setScene(dir.path(), "B", b));
        QCOMPARE(visibleRecords(editor), QStringList{"TRAINER_TEST"});
        QVERIFY(editor.findChild<QLabel *>("battleScene")->text().contains("Shared"));
        answer(QMessageBox::Discard);
        QVERIFY(editor.setScene(dir.path(), "Empty", empty));
        QVERIFY(visibleRecords(editor).isEmpty());
        QVERIFY(editor.findChild<QPlainTextEdit *>("battleSource")->isEnabled());
        QVERIFY(editor.findChild<QLabel *>("battleScene")->text().contains("No trainer IDs"));
        scope->setCurrentIndex(1); QCOMPARE(visibleRecords(editor).size(), 2);
    }
    void currentMapStockPartiesAndPoryscript() {
        QTemporaryDir dir;
        const auto trainerSource = QString(trainers).replace("};\n", " [TRAINER_OTHER] = {.party = NO_ITEM_DEFAULT_MOVES(unrelatedTeam)},\n};\n");
        QVERIFY(createProject(dir, trainerSource, parties + "static const struct TrainerMonNoItemDefaultMoves unrelatedTeam[] = {{.lvl = 9, .species = SPECIES_ZUBAT}};\n"));
        const auto scripts = dir.filePath("data/maps/A/scripts.inc");
        QVERIFY(writeFile(scripts, "trainerbattle_single TRAINER_OTHER, Intro, Defeat\n")); // stale generated file
        QVERIFY(writeFile(dir.filePath("data/maps/A/scripts.pory"), "script Battle { trainerbattle(TRAINER_TEST, Intro, Defeat) } // TRAINER_OTHER\n"));
        QVERIFY(writeFile(dir.filePath("data/maps/A/text.inc"), "Text::\n .string \"TRAINER_OTHER$\"\n"));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "A", scripts, true, false));
        QCOMPARE(visibleRecords(editor), QStringList{"[TRAINER_TEST]"});
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        files->setCurrentIndex(1);
        QCOMPARE(files->currentIndex(), 1);
        QCOMPARE(visibleRecords(editor), QStringList{"sParty_Test"});
        editor.findChild<QLineEdit *>("battleSearch")->setText("ZUBAT");
        QVERIFY(visibleRecords(editor).isEmpty());
        editor.findChild<QComboBox *>("battleScope")->setCurrentIndex(1);
        QCOMPARE(visibleRecords(editor), QStringList{"unrelatedTeam"});
        editor.findChild<QLineEdit *>("battleSearch")->clear();
        editor.findChild<QComboBox *>("battleScope")->setCurrentIndex(0);
        files->setCurrentIndex(0);
        // Battle Scripts saves must update the map index without reopening it.
        const auto cleanScript = dir.filePath("data/maps/B/scripts.inc");
        QVERIFY(writeFile(cleanScript, "OnEnter::\n end\n"));
        QVERIFY(editor.setScene(dir.path(), "B", cleanScript));
        QVERIFY(visibleRecords(editor).isEmpty());
        auto *sceneScripts = editor.findChild<DialogueEditor *>("battleScripts");
        auto *text = sceneScripts->findChild<QPlainTextEdit *>("sceneScript");
        text->setPlainText("Battle::\n trainerbattle_single TRAINER_OTHER, Intro, Defeat\n");
        QVERIFY(sceneScripts->saveChanges());
        QCOMPARE(visibleRecords(editor), QStringList{"[TRAINER_OTHER]"});
    }
    void kantoRouteSharedTrainers() {
        QTemporaryDir dir; QVERIFY(createProject(dir));
        QVERIFY(writeFile(dir.filePath("src/data/trainers.party"), expansion));
        QVERIFY(writeFile(dir.filePath("src/data/trainers_frlg.party"),
            "=== TRAINER_ROUTE ===\nName: ROUTE\n\nPidgey\nLevel: 9\n\n"
            "=== TRAINER_REMATCH ===\nName: REMATCH\n\nPidgeotto\nLevel: 20\n\n"
            "=== TRAINER_ELSEWHERE ===\nName: OTHER\n\nZubat\nLevel: 10\n"));
        const auto scripts = dir.filePath("data/maps/Route3_Frlg/scripts.inc");
        QVERIFY(writeFile(scripts, "Route3_MapScripts::\n .byte 0\n"));
        QVERIFY(writeFile(dir.filePath("data/maps/Route3_Frlg/map.json"),
            R"({"object_events":[{"script":"Route3_Trainer"}]})"));
        QVERIFY(writeFile(dir.filePath("data/scripts/trainers_frlg.inc"),
            "Route3_Trainer::\n trainerbattle_single TRAINER_ROUTE, Intro, Defeat\n"
            " goto_if_set FLAG_REMATCH, Route3_Rematch\n end\n"
            "Route3_Rematch::\n trainerbattle_rematch TRAINER_REMATCH, Intro, Defeat\n end\n"
            "Route4_Trainer::\n trainerbattle_single TRAINER_ELSEWHERE, Intro, Defeat\n end\n"));
        BattleEditor editor; QVERIFY(editor.setScene(dir.path(), "Route3_Frlg", scripts));
        QCOMPARE(editor.findChild<QComboBox *>("battleFiles")->currentData().toString(), QString("src/data/trainers_frlg.party"));
        QCOMPARE(visibleRecords(editor), (QStringList{"TRAINER_ROUTE", "TRAINER_REMATCH"}));
        const auto route1 = dir.filePath("data/maps/Route1_Frlg/scripts.inc");
        QVERIFY(writeFile(route1, "Route1_MapScripts::\n .byte 0\n"));
        QVERIFY(editor.setScene(dir.path(), "Route1_Frlg", route1));
        QVERIFY(visibleRecords(editor).isEmpty());
    }
    void kantoRoutesCorpus() {
        const auto root = qEnvironmentVariable("PORYMAP_Z_EXPANSION_PROJECT");
        if (root.isEmpty()) QSKIP("Set PORYMAP_Z_EXPANSION_PROJECT for real Kanto scene checks.");
        DialogueEditor dialogue;
        QVERIFY(dialogue.setScene(root, "Route1_Frlg", root + "/data/maps/Route1_Frlg/scripts.inc"));
        auto *entries = dialogue.findChild<QListWidget *>("dialogueEntries");
        QVERIFY(entries);
        QVERIFY(entries->count() > 0);
        BattleEditor battle;
        QVERIFY(battle.setScene(root, "Route3_Frlg", root + "/data/maps/Route3_Frlg/scripts.inc"));
        const auto records = visibleRecords(battle);
        QVERIFY2(records.contains("TRAINER_LASS_JANICE"), qPrintable(records.join(", ")));
        QVERIFY(records.contains("TRAINER_LASS_ROBIN"));
        QVERIFY(!records.contains("TRAINER_LASS_CRISSY"));
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
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test", {}));
        auto *files = editor.findChild<QComboBox *>("battleFiles");
        QCOMPARE(files->count(), 1);
        QCOMPARE(files->currentData().toString(), QString("src/data/trainers.party"));
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        QCOMPARE(source->toPlainText(), expansion);
        replaceText(editor, "Level: 21", "Level: 42");
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
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test", {}));
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
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test", {}, true, false));
        // C trainer data is still available when scene scripts use Poryscript.
        QVERIFY(editor.findChild<QPlainTextEdit *>("battleSource")->isEnabled());
        editor.findChild<QComboBox *>("battleFiles")->setCurrentIndex(1);
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
            BattleEditor editor; QVERIFY(openWholeProject(editor, project, "Test", {}));
            editor.findChild<QComboBox *>("battleFiles")->setCurrentIndex(1);
            auto *records = editor.findChild<QListWidget *>("battleRecords");
            QVERIFY(records->currentItem()); QVERIFY(partiesFound.contains(records->currentItem()->text()));
            QVERIFY(!editor.hasUnsavedChanges());
        }
    }
    void crlfUndoAndSave() {
        QTemporaryDir dir; QVERIFY(createProject(dir, QString(trainers).replace("\n", "\r\n")));
        BattleEditor editor; QVERIFY(openWholeProject(editor, dir.path(), "Test", {}));
        auto *source = editor.findChild<QPlainTextEdit *>("battleSource");
        replaceText(editor, ".doubleBattle = FALSE", ".doubleBattle = TRUE");
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
        QVERIFY(openWholeProject(editor, dir.path(), "Test", dir.filePath("data/maps/Test/scripts.inc")));
        auto *records = editor.findChild<QListWidget *>("battleRecords");
        QCOMPARE(records->count(), 1);
        replaceText(editor, ".doubleBattle = FALSE", ".doubleBattle = TRUE");
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
        QVERIFY(openWholeProject(editor, {}, {}, {}));
        QVERIFY(!editor.hasUnsavedChanges());
        QCOMPARE(records->count(), 0);
    }
};
QTEST_MAIN(BattleTest)
#include "tst_battle.moc"
