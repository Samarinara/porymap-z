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
using namespace PorymapZ;
static const QString trainers = "// untouched\nconst struct Trainer gTrainers[] = {\n"
    " [TRAINER_TEST] = {\n .trainerName = _(\"A, {B} \\\"C\\\"\"), // name\n .doubleBattle = FALSE,\n .items = {ITEM_NONE, ITEM_POTION},\n .party = {.NoItemCustomMoves = sParty_Test},\n },\n};\n";
static const QString parties = "static const struct TrainerMonItemCustomMoves sParty_Test[] = {\n"
    " { .iv = 100, .lvl = 5, .species = SPECIES_TREECKO, .heldItem = ITEM_NONE,\n"
    " .moves = {MOVE_POUND, MOVE_LEER, MOVE_NONE, MOVE_NONE}, },\n};\n";
class BattleTest : public QObject {
    Q_OBJECT
    int field(const BattleDocument &doc, const QString &name) {
        for (int i = 0; i < doc.nodes().size(); ++i) if (doc.nodes()[i].name == name) return i;
        return -1;
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
        for (int i=0; i<root->childCount(); ++i) if (root->child(i)->text(0) == ".doubleBattle") root->child(i)->setText(1, "TRUE");
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
