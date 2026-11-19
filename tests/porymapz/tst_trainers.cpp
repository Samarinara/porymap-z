#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include "porymapz/trainermanager.h"
#include "porymapz/trainerproject.h"

using namespace PorymapZ;

namespace {
const char *kIds =
    "#ifndef GUARD_CONSTANTS_OPPONENTS_H\n#define GUARD_CONSTANTS_OPPONENTS_H\n\n"
    "#define TRAINER_NONE      0\n#define TRAINER_ANNA      1\n#define TRAINER_BEN       2\n\n"
    "#define TRAINERS_COUNT    3\n#define MAX_TRAINERS_COUNT 6\n\n#endif\n";
const char *kTable =
    "const struct Trainer gTrainers[] = {\n"
    "    [TRAINER_NONE] =\n    {\n        .trainerName = _(\"\"),\n        .party = {.NoItemDefaultMoves = NULL},\n    },\n\n"
    "    [TRAINER_ANNA] =\n    {\n        .trainerClass = TRAINER_CLASS_HIKER,\n        .encounterMusic_gender = TRAINER_ENCOUNTER_MUSIC_MALE,\n"
    "        .trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"ANNA\"),\n        .items = {},\n        .doubleBattle = FALSE,\n"
    "        .aiFlags = AI_SCRIPT_CHECK_BAD_MOVE,\n        .party = NO_ITEM_DEFAULT_MOVES(sParty_Anna),\n    },\n\n"
    "    [TRAINER_BEN] =\n    {\n        .trainerClass = TRAINER_CLASS_HIKER,\n        .encounterMusic_gender = TRAINER_ENCOUNTER_MUSIC_MALE,\n"
    "        .trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"BEN\"),\n        .items = {},\n        .doubleBattle = FALSE,\n"
    "        .aiFlags = AI_SCRIPT_CHECK_BAD_MOVE,\n        .party = ITEM_CUSTOM_MOVES(sParty_Ben),\n    },\n};\n";
const char *kParties =
    "static const struct TrainerMonNoItemDefaultMoves sParty_Anna[] = {\n    {\n    .iv = 0,\n    .lvl = 5,\n    .species = SPECIES_POOCHYENA,\n    }\n};\n\n"
    "static const struct TrainerMonItemCustomMoves sParty_Ben[] = {\n    {\n    .iv = 0,\n    .lvl = 5,\n    .species = SPECIES_POOCHYENA,\n"
    "    .heldItem = ITEM_NONE,\n    .moves = {MOVE_TACKLE, MOVE_NONE, MOVE_NONE, MOVE_NONE},\n    }\n};\n";
const char *kConstants =
    "#define TRAINER_ENCOUNTER_MUSIC_MALE 0\n#define TRAINER_ENCOUNTER_MUSIC_FEMALE 1\n"
    "#define TRAINER_PIC_HIKER 0\n#define TRAINER_PIC_YOUNGSTER 1\n#define TRAINER_CLASS_PKMN_TRAINER_1 0\n#define TRAINER_CLASS_HIKER 1\n";
const char *kScripts =
    "Route1_EventScript_Anna::\n\ttrainerbattle_single TRAINER_ANNA, Route1_Text_AnnaIntro, Route1_Text_AnnaDefeat\n\tend\n\n"
    "Route1_EventScript_Shared::\n\tgoto Route1_EventScript_Anna\n\n"
    "Route1_Text_AnnaIntro:\n\t.string \"Hi$\"\n\nRoute1_Text_AnnaDefeat:\n\t.string \"Bye$\"\n";
const char *kParty =
    "=== TRAINER_NONE ===\nName:\nClass: Pkmn Trainer 1\nPic: Hiker\nGender: Male\nMusic: Male\nDouble Battle: No\n\n"
    "=== TRAINER_ANNA ===\nName: ANNA\nClass: Hiker\nPic: Hiker\nGender: Female\nMusic: Male\nDouble Battle: No\n\nPoochyena\nLevel: 5\n\n"
    "=== TRAINER_BEN ===\nName: BEN\nClass: Hiker\nPic: Hiker\nGender: Male\nMusic: Male\nDouble Battle: No\n\nPoochyena\nLevel: 5\n";

void write(const QString &root, const QString &file, const QByteArray &text) {
    QVERIFY(QDir(root).mkpath(QFileInfo(root + '/' + file).path()));
    QFile out(root + '/' + file);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(text);
}
QString read(const QString &root, const QString &file) {
    QFile in(root + '/' + file);
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
}
void writeStock(const QString &root) {
    write(root, "include/constants/opponents.h", kIds);
    write(root, "include/constants/trainers.h", kConstants);
    write(root, "include/constants/global.h", "#define TRAINER_NAME_LENGTH 10\n");
    write(root, "include/constants/species.h", "#define SPECIES_NONE 0\n#define SPECIES_POOCHYENA 1\n");
    write(root, "src/data/trainers.h", kTable);
    write(root, "src/data/trainer_parties.h", kParties);
    write(root, "data/maps/Route1/scripts.inc", kScripts);
}
void writeExpansion(const QString &root) {
    write(root, "include/constants/opponents.h", kIds);
    write(root, "include/constants/trainers.h", kConstants);
    write(root, "include/constants/global.h", "#define TRAINER_NAME_LENGTH 10\n");
    write(root, "src/data/trainers.party", kParty);
    write(root, "data/maps/Route1/scripts.inc", kScripts);
}
QStringList kinds(const QVector<TrainerIssue> &issues) {
    QStringList result;
    for (const auto &issue : issues) result << issue.key.section('|', 0, 0);
    result.sort();
    return result;
}
QSet<QString> allKeys(const QVector<TrainerIssue> &issues) {
    QSet<QString> keys;
    for (const auto &issue : issues) if (issue.fixable()) keys.insert(issue.key);
    return keys;
}
NewTrainerSpec basicSpec() {
    NewTrainerSpec spec;
    spec.id = "TRAINER_CARA";
    spec.name = "CARA";
    spec.trainerClass = "TRAINER_CLASS_HIKER";
    spec.pic = "TRAINER_PIC_YOUNGSTER";
    spec.music = "TRAINER_ENCOUNTER_MUSIC_FEMALE";
    spec.female = true;
    spec.scriptsFile = "data/maps/Route1/scripts.inc";
    spec.scriptLabel = "Route1_EventScript_Cara";
    spec.intro = "Let's battle!";
    spec.defeat = "I lost!";
    spec.postBattle = "You're strong!";
    return spec;
}
}

class TrainerTest : public QObject {
    Q_OBJECT
private slots:
    void cleanProjectHasNoIssues() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        const auto issues = project.analyze();
        QVERIFY2(issues.isEmpty(), qPrintable(kinds(issues).join(", ")));
    }

    void createsEveryStockFile() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY2(project.create(basicSpec(), &error), qPrintable(error));
        QCOMPARE(project.changedFiles().size(), 4);
        QVERIFY2(project.save(&error), qPrintable(error));

        const QString ids = read(dir.path(), "include/constants/opponents.h");
        QVERIFY(ids.contains(QRegularExpression("#define TRAINER_CARA\\s+3\\n")));
        QVERIFY(ids.contains(QRegularExpression("#define TRAINERS_COUNT\\s+4\\n")));
        QVERIFY(ids.contains(QRegularExpression("#define MAX_TRAINERS_COUNT 6\\n")));
        const QString table = read(dir.path(), "src/data/trainers.h");
        QVERIFY(table.contains("[TRAINER_CARA] =\n    {\n"));
        QVERIFY(table.contains(".encounterMusic_gender = F_TRAINER_FEMALE | TRAINER_ENCOUNTER_MUSIC_FEMALE,"));
        QVERIFY(table.contains(".trainerName = _(\"CARA\"),"));
        QVERIFY(table.contains(".party = NO_ITEM_DEFAULT_MOVES(sParty_Cara),\n    },\n};\n"));
        QVERIFY(read(dir.path(), "src/data/trainer_parties.h").contains("struct TrainerMonNoItemDefaultMoves sParty_Cara[] = {"));
        const QString scripts = read(dir.path(), "data/maps/Route1/scripts.inc");
        QVERIFY(scripts.startsWith(kScripts));
        QVERIFY(scripts.contains("Route1_EventScript_Cara::\n\ttrainerbattle_single TRAINER_CARA, Route1_Text_CaraIntro, Route1_Text_CaraDefeat\n"));
        QVERIFY(scripts.contains("Route1_Text_CaraIntro:\n\t.string \"Let's battle!$\"\n"));

        TrainerProject reloaded;
        QVERIFY(reloaded.load(dir.path(), false, &error));
        const auto issues = reloaded.analyze();
        QVERIFY2(issues.isEmpty(), qPrintable(kinds(issues).join(", ")));
        QCOMPARE(reloaded.trainersForScript("Route1_EventScript_Cara"), QStringList{"TRAINER_CARA"});
    }

    void createsDoubleBattleWithTwoMons() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        auto spec = basicSpec();
        spec.doubleBattle = true;
        QVERIFY2(project.create(spec, &error), qPrintable(error));
        QVERIFY(project.save(&error));
        QCOMPARE(read(dir.path(), "src/data/trainer_parties.h").count(".lvl = 5"), 4);
        const QString scripts = read(dir.path(), "data/maps/Route1/scripts.inc");
        QVERIFY(scripts.contains("trainerbattle_double TRAINER_CARA, Route1_Text_CaraIntro, Route1_Text_CaraDefeat, Route1_Text_CaraNotEnoughMons"));
        QVERIFY(scripts.contains("Route1_Text_CaraNotEnoughMons:"));
    }

    void writesPoryscript() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "data/maps/Route1/scripts.pory", "script Route1_EventScript_Old {\n    end\n}\n");
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        auto spec = basicSpec();
        spec.scriptsFile = "data/maps/Route1/scripts.pory";
        QVERIFY2(project.create(spec, &error), qPrintable(error));
        QVERIFY(project.save(&error));
        const QString scripts = read(dir.path(), spec.scriptsFile);
        QVERIFY(scripts.contains("script Route1_EventScript_Cara {\n    trainerbattle_single(TRAINER_CARA, Route1_Text_CaraIntro, Route1_Text_CaraDefeat)\n"));
        QVERIFY(scripts.contains("text Route1_Text_CaraIntro {\n    \"Let's battle!\"\n}\n"));
    }

    void createsExpansionSection() {
        QTemporaryDir dir; writeExpansion(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY2(project.analyze().isEmpty(), qPrintable(kinds(project.analyze()).join(", ")));
        auto spec = basicSpec();
        QVERIFY2(project.create(spec, &error), qPrintable(error));
        QVERIFY(project.save(&error));
        const QString party = read(dir.path(), "src/data/trainers.party");
        QVERIFY(party.contains("=== TRAINER_CARA ===\nName: CARA\nClass: Hiker\nPic: Youngster\nGender: Female\nMusic: Female\nDouble Battle: No\n\nPoochyena\nLevel: 5\n"));
        TrainerProject reloaded;
        QVERIFY(reloaded.load(dir.path(), false, &error));
        QVERIFY2(reloaded.analyze().isEmpty(), qPrintable(kinds(reloaded.analyze()).join(", ")));
    }

    void rejectsBadInput_data() {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("expected");
        QTest::newRow("existing id") << "TRAINER_ANNA" << "CARA" << "Route1_EventScript_Cara" << "already exists";
        QTest::newRow("bad id") << "trainer_cara" << "CARA" << "Route1_EventScript_Cara" << "must look like";
        QTest::newRow("reserved") << "TRAINER_PIC_CARA" << "CARA" << "Route1_EventScript_Cara" << "cannot start";
        QTest::newRow("long name") << "TRAINER_CARA" << "ABCDEFGHIJK" << "Route1_EventScript_Cara" << "at most 10";
        QTest::newRow("quote") << "TRAINER_CARA" << "CA\"RA" << "Route1_EventScript_Cara" << "quotes";
        QTest::newRow("used label") << "TRAINER_CARA" << "CARA" << "Route1_EventScript_Anna" << "already used";
    }
    void rejectsBadInput() {
        QFETCH(QString, id); QFETCH(QString, name); QFETCH(QString, label); QFETCH(QString, expected);
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        auto spec = basicSpec();
        spec.id = id; spec.name = name; spec.scriptLabel = label;
        QVERIFY2(project.validate(spec).contains(expected), qPrintable(project.validate(spec)));
        QVERIFY(!project.create(spec, &error));
        QVERIFY(project.changedFiles().isEmpty());
    }

    void refusesPastTrainerLimit() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "include/constants/opponents.h", QByteArray(kIds).replace("MAX_TRAINERS_COUNT 6", "MAX_TRAINERS_COUNT 3"));
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY(project.validate(basicSpec()).contains("MAX_TRAINERS_COUNT"));
    }

    void repairs_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("kind");
        const QString table = "src/data/trainers.h", parties = "src/data/trainer_parties.h", ids = "include/constants/opponents.h";
        QTest::newRow("field comma") << table << ".trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"ANNA\")" << ".trainerPic = TRAINER_PIC_HIKER\n        .trainerName = _(\"ANNA\")" << "field-comma";
        QTest::newRow("items bracket") << table << ".items = {},\n        .doubleBattle = FALSE,\n        .aiFlags = AI_SCRIPT_CHECK_BAD_MOVE,\n        .party = NO_ITEM_DEFAULT_MOVES(sParty_Anna)" << ".items = {,\n        .doubleBattle = FALSE,\n        .aiFlags = AI_SCRIPT_CHECK_BAD_MOVE,\n        .party = NO_ITEM_DEFAULT_MOVES(sParty_Anna)" << "field-bracket";
        QTest::newRow("entry comma") << table << "sParty_Anna),\n    },\n" << "sParty_Anna),\n    }\n" << "entry-comma";
        QTest::newRow("entry close") << table << "sParty_Anna),\n    },\n" << "sParty_Anna),\n" << "entry-close";
        QTest::newRow("entry open") << table << "[TRAINER_ANNA] =\n    {\n" << "[TRAINER_ANNA] =\n" << "entry-open";
        QTest::newRow("extra brace") << table << "sParty_Anna),\n    },\n" << "sParty_Anna),\n    },\n    },\n" << "entry-extra";
        QTest::newRow("table close") << table << "sParty_Ben),\n    },\n};\n" << "sParty_Ben),\n    },\n" << "table-close";
        QTest::newRow("party type") << table << "ITEM_CUSTOM_MOVES(sParty_Ben)" << "NO_ITEM_DEFAULT_MOVES(sParty_Ben)" << "party-type";
        QTest::newRow("party typo") << table << "ITEM_CUSTOM_MOVES(sParty_Ben)" << "ITEM_CUSTOM_MOVES(sParty_Benn)" << "party-missing";
        QTest::newRow("party deleted") << parties << "static const struct TrainerMonItemCustomMoves sParty_Ben[] = {\n    {\n    .iv = 0,\n    .lvl = 5,\n    .species = SPECIES_POOCHYENA,\n    .heldItem = ITEM_NONE,\n    .moves = {MOVE_TACKLE, MOVE_NONE, MOVE_NONE, MOVE_NONE},\n    }\n};\n" << "" << "party-missing";
        QTest::newRow("party semicolon") << parties << "    }\n};\n\nstatic" << "    }\n}\n\nstatic" << "party-semicolon";
        QTest::newRow("party close") << parties << "    }\n};\n\nstatic" << "    }\n\nstatic" << "party-close";
        QTest::newRow("count low") << ids << "TRAINERS_COUNT    3" << "TRAINERS_COUNT    2" << "count";
        QTest::newRow("count high") << ids << "TRAINERS_COUNT    3" << "TRAINERS_COUNT    5" << "count";
        QTest::newRow("id missing") << ids << "#define TRAINER_BEN       2\n" << "" << "define-missing";
        QTest::newRow("id reused") << ids << "#define TRAINER_BEN       2" << "#define TRAINER_BEN       1" << "id-duplicate";
        QTest::newRow("id repeated") << ids << "#define TRAINER_BEN       2\n" << "#define TRAINER_BEN       2\n#define TRAINER_BEN       2\n" << "define-duplicate";
    }
    void repairs() {
        QFETCH(QString, file); QFETCH(QString, before); QFETCH(QString, after); QFETCH(QString, kind);
        QTemporaryDir dir; writeStock(dir.path());
        QString damaged = read(dir.path(), file);
        QVERIFY(damaged.contains(before));
        write(dir.path(), file, damaged.replace(damaged.indexOf(before), before.size(), after).toUtf8());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        const auto issues = project.analyze();
        QVERIFY2(kinds(issues).contains(kind), qPrintable(kinds(issues).join(", ")));
        for (const auto &issue : issues) if (issue.key.startsWith(kind + "|")) QVERIFY(!project.previewEdits(issue).isEmpty());
        QVERIFY(project.applyFixes(allKeys(issues)) > 0);
        QVERIFY2(project.save(&error), qPrintable(error));
        TrainerProject reloaded;
        QVERIFY(reloaded.load(dir.path(), false, &error));
        const auto remaining = reloaded.analyze();
        QVERIFY2(remaining.isEmpty(), qPrintable(kinds(remaining).join(", ")));
    }

    void typoRepairKeepsTheRealParty() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "src/data/trainers.h", QByteArray(kTable).replace("ITEM_CUSTOM_MOVES(sParty_Ben)", "ITEM_CUSTOM_MOVES(sParty_Benn)"));
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        project.applyFixes(allKeys(project.analyze()));
        QVERIFY(project.save(&error));
        QCOMPARE(read(dir.path(), "src/data/trainers.h"), QString(kTable));
        QCOMPARE(read(dir.path(), "src/data/trainer_parties.h"), QString(kParties));
    }

    void repairsSeveralNewIdsWithoutCollisions() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "include/constants/opponents.h", QByteArray(kIds).replace("#define TRAINER_ANNA      1\n#define TRAINER_BEN       2\n", "").replace("TRAINERS_COUNT    3", "TRAINERS_COUNT    1"));
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        const auto issues = project.analyze();
        QCOMPARE(project.applyFixes(allKeys(issues)), 2);
        QVERIFY(project.save(&error));
        const QString ids = read(dir.path(), "include/constants/opponents.h");
        QVERIFY(ids.contains(QRegularExpression("TRAINER_ANNA\\s+1\\n")));
        QVERIFY(ids.contains(QRegularExpression("TRAINER_BEN\\s+2\\n")));
        QVERIFY(ids.contains(QRegularExpression("TRAINERS_COUNT\\s+3\\n")));
    }

    void repairsScriptReferences() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "data/maps/Route1/scripts.inc", QByteArray(kScripts) + "\nRoute1_EventScript_Gone::\n\ttrainerbattle_single TRAINER_GONE, Route1_Text_GoneIntro, Route1_Text_AnnaDefeat\n\tend\n");
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        const auto issues = project.analyze();
        QCOMPARE(kinds(issues), QStringList({"script-text", "script-trainer"}));
        QCOMPARE(project.applyFixes(allKeys(issues)), 2);
        QVERIFY(project.save(&error));
        TrainerProject reloaded;
        QVERIFY(reloaded.load(dir.path(), false, &error));
        QVERIFY2(reloaded.analyze().isEmpty(), qPrintable(kinds(reloaded.analyze()).join(", ")));
        QVERIFY(read(dir.path(), "src/data/trainers.h").contains("[TRAINER_GONE]"));
        QVERIFY(read(dir.path(), "data/maps/Route1/scripts.inc").contains("Route1_Text_GoneIntro:\n\t.string \"...$\"\n"));
    }

    void repairsExpansionParty_data() {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("kind");
        QTest::newRow("header") << "=== TRAINER_BEN ===" << "== TRAINER_BEN ==" << "party-header";
        QTest::newRow("blank line") << "Double Battle: No\n\nPoochyena\nLevel: 5\n\n===" << "Double Battle: No\nPoochyena\nLevel: 5\n\n===" << "party-blank";
        QTest::newRow("name") << "Name: BEN\n" << "" << "party-required";
        QTest::newRow("pic") << "Name: BEN\nClass: Hiker\nPic: Hiker\n" << "Name: BEN\nClass: Hiker\n" << "party-required";
        QTest::newRow("no mons") << "Double Battle: No\n\nPoochyena\nLevel: 5\n" << "Double Battle: No\n" << "party-empty";
    }
    void repairsExpansionParty() {
        QFETCH(QString, before); QFETCH(QString, after); QFETCH(QString, kind);
        QTemporaryDir dir; writeExpansion(dir.path());
        QString damaged = read(dir.path(), "src/data/trainers.party");
        const int at = kind == "party-empty" ? damaged.lastIndexOf(before) : damaged.indexOf(before);
        QVERIFY(at >= 0);
        write(dir.path(), "src/data/trainers.party", damaged.replace(at, before.size(), after).toUtf8());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        const auto issues = project.analyze();
        QVERIFY2(kinds(issues).contains(kind), qPrintable(kinds(issues).join(", ")));
        QVERIFY(project.applyFixes(allKeys(issues)) > 0);
        QVERIFY(project.save(&error));
        TrainerProject reloaded;
        QVERIFY(reloaded.load(dir.path(), false, &error));
        QVERIFY2(reloaded.analyze().isEmpty(), qPrintable(kinds(reloaded.analyze()).join(", ")));
    }

    void reportsWithoutGuessing_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("kind");
        QTest::newRow("unknown class") << "src/data/trainers.h" << ".trainerClass = TRAINER_CLASS_HIKER" << ".trainerClass = TRAINER_CLASS_NOPE" << "constant";
        QTest::newRow("long name") << "src/data/trainers.h" << "_(\"ANNA\")" << "_(\"ANNAANNAANNA\")" << "name-long";
        QTest::newRow("empty party") << "src/data/trainer_parties.h" << "sParty_Anna[] = {\n    {\n    .iv = 0,\n    .lvl = 5,\n    .species = SPECIES_POOCHYENA,\n    }\n};" << "sParty_Anna[] = {\n};" << "party-size";
        QTest::newRow("conflicting defines") << "include/constants/opponents.h" << "#define TRAINER_BEN       2\n" << "#define TRAINER_BEN       2\n#define TRAINER_BEN       9\n" << "define-duplicate";
    }
    void reportsWithoutGuessing() {
        QFETCH(QString, file); QFETCH(QString, before); QFETCH(QString, after); QFETCH(QString, kind);
        QTemporaryDir dir; writeStock(dir.path());
        QString damaged = read(dir.path(), file);
        QVERIFY(damaged.contains(before));
        write(dir.path(), file, damaged.replace(damaged.indexOf(before), before.size(), after).toUtf8());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        bool found = false;
        for (const auto &issue : project.analyze()) {
            if (!issue.key.startsWith(kind + "|")) continue;
            found = true;
            QVERIFY(!issue.fixable());
        }
        QVERIFY(found);
    }

    void leavesCommentedOutCodeAlone() {
        QTemporaryDir dir; writeStock(dir.path());
        QString table = read(dir.path(), "src/data/trainers.h");
        table.replace("    [TRAINER_BEN] =", "    // [TRAINER_OLD] = { broken\n    /* [TRAINER_OLDER] = */\n    [TRAINER_BEN] =");
        write(dir.path(), "src/data/trainers.h", table.toUtf8());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY2(project.analyze().isEmpty(), qPrintable(kinds(project.analyze()).join(", ")));
    }

    void preservesLineEndingsAndBom() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "include/constants/opponents.h", "\xEF\xBB\xBF" + QByteArray(kIds).replace("\n", "\r\n"));
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY2(project.create(basicSpec(), &error), qPrintable(error));
        QVERIFY(project.save(&error));
        QFile ids(dir.path() + "/include/constants/opponents.h");
        QVERIFY(ids.open(QIODevice::ReadOnly));
        const QByteArray bytes = ids.readAll();
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
        QVERIFY(bytes.contains("TRAINER_CARA"));
        QCOMPARE(bytes.count("\n"), bytes.count("\r\n"));
    }

    void refusesExternalChanges() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QVERIFY(project.create(basicSpec(), &error));
        write(dir.path(), "src/data/trainers.h", QByteArray(kTable) + "// edited elsewhere\n");
        QVERIFY(!project.save(&error));
        QVERIFY(error.contains("changed outside"));
        QVERIFY(!read(dir.path(), "include/constants/opponents.h").contains("TRAINER_CARA"));
    }

    void followsSharedScripts() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        QCOMPARE(project.trainersForScript("Route1_EventScript_Shared"), QStringList{"TRAINER_ANNA"});
        QVERIFY(project.trainersForScript("Route1_Text_AnnaIntro").isEmpty());
    }

    void encodesDialogue() {
        QCOMPARE(TrainerProject::encodeDialogue("Hi there", true), QStringList{"Hi there$"});
        QCOMPARE(TrainerProject::encodeDialogue("One\n\nTwo", true), QStringList({"One\\p", "Two$"}));
        QCOMPARE(TrainerProject::encodeDialogue("", true), QStringList{"...$"});
        QCOMPARE(TrainerProject::encodeDialogue("Say \"hi\"", false), QStringList{"Say \\\"hi\\\""});
        const auto wrapped = TrainerProject::encodeDialogue("aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff gggggggggg hhhhhhhhhh", true);
        QCOMPARE(wrapped.size(), 3);
        QVERIFY(wrapped[0].endsWith("\\n"));
        QVERIFY(wrapped[1].endsWith("\\l"));
        QVERIFY(wrapped[2].endsWith("$"));
    }

    void newTrainerDialogCreatesTrainer() {
        QTemporaryDir dir; writeStock(dir.path());
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        TrainerEventOptions options;
        options.sprites = QStringList{"OBJ_EVENT_GFX_BOY_1", "OBJ_EVENT_GFX_YOUNGSTER"};
        options.trainerTypes = QStringList{"TRAINER_TYPE_NONE", "TRAINER_TYPE_NORMAL"};
        options.sprite = "OBJ_EVENT_GFX_YOUNGSTER";
        options.trainerType = "TRAINER_TYPE_NORMAL";
        NewTrainerDialog dialog(&project, "Route1", "data/maps/Route1/scripts.inc", options);
        auto *create = dialog.findChild<QPushButton *>("createTrainer");
        QVERIFY(create);
        QVERIFY(!create->isEnabled());
        dialog.findChild<QLineEdit *>("trainerName")->setText("Cara Lee");
        QCOMPARE(dialog.findChild<QLineEdit *>("trainerId")->text(), QString("TRAINER_CARA_LEE"));
        QCOMPARE(dialog.findChild<QLineEdit *>("trainerScriptLabel")->text(), QString("Route1_EventScript_CaraLee"));
        QVERIFY(create->isEnabled());
        dialog.findChild<QLineEdit *>("trainerId")->setText("TRAINER_ANNA");
        emit dialog.findChild<QLineEdit *>("trainerId")->textEdited("TRAINER_ANNA");
        QVERIFY(!create->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("trainerMessage")->text().contains("already exists"));
        dialog.findChild<QLineEdit *>("trainerId")->setText("TRAINER_CARA_LEE");
        emit dialog.findChild<QLineEdit *>("trainerId")->textEdited("TRAINER_CARA_LEE");
        QVERIFY(create->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.sprite(), QString("OBJ_EVENT_GFX_YOUNGSTER"));
        QVERIFY(read(dir.path(), "include/constants/opponents.h").contains("TRAINER_CARA_LEE"));
    }

    void repairDialogPreviewsAndRepairs() {
        QTemporaryDir dir; writeStock(dir.path());
        write(dir.path(), "src/data/trainers.h", QByteArray(kTable).replace(".trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"ANNA\")", ".trainerPic = TRAINER_PIC_HIKER\n        .trainerName = _(\"ANNA\")"));
        TrainerProject project; QString error;
        QVERIFY(project.load(dir.path(), false, &error));
        TrainerRepairDialog dialog(&project);
        QSignalSpy changed(&dialog, &TrainerRepairDialog::filesChanged);
        auto *list = dialog.findChild<QTreeWidget *>("trainerIssues");
        QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(dialog.findChild<QPlainTextEdit *>("trainerIssueDetails")->toPlainText().contains("+         .trainerPic = TRAINER_PIC_HIKER,"));
        auto *apply = dialog.findChild<QPushButton *>("applyTrainerRepairs");
        QVERIFY(!apply->isEnabled());
        dialog.findChild<QPushButton *>("selectAllRepairs")->click();
        QVERIFY(apply->isEnabled());
        apply->click();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(list->topLevelItemCount(), 0);
        QCOMPARE(read(dir.path(), "src/data/trainers.h"), QString(kTable));
    }
};

QTEST_MAIN(TrainerTest)
#include "tst_trainers.moc"
