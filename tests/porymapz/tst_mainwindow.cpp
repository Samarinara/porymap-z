#include <QtTest>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QListWidget>
#include "mainwindow.h"
#include "porymapz/dialogueeditor.h"
#include "porymapz/battleeditor.h"

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void dialogueIsAMainPage() {
        QTemporaryDir config;
        qputenv("XDG_DATA_HOME", config.path().toUtf8());
        MainWindow window(nullptr);
        window.initialize();
        auto *tabs = window.findChild<QTabBar *>("mainTabBar");
        auto *pages = window.findChild<QStackedWidget *>("mainStackedWidget");
        auto *dialogue = window.findChild<PorymapZ::DialogueEditor *>();
        QVERIFY(tabs && pages && dialogue);
        QCOMPARE(tabs->tabText(MainTab::Dialogue), QString("Dialogue"));
        QCOMPARE(pages->widget(4), static_cast<QWidget *>(dialogue));
        QVERIFY(!dialogue->isWindow());
        auto *battle = window.findChild<PorymapZ::BattleEditor *>();
        QVERIFY(battle);
        QCOMPARE(tabs->tabText(MainTab::Battle), QString("Battle"));
        QCOMPARE(pages->widget(5), static_cast<QWidget *>(battle));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_mainTabBar_tabBarClicked", Q_ARG(int, int(MainTab::Battle))));
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(battle));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_mainTabBar_tabBarClicked", Q_ARG(int, int(MainTab::Dialogue))));
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(dialogue));
        QVERIFY(QMetaObject::invokeMethod(&window, "on_mainTabBar_tabBarClicked", Q_ARG(int, int(MainTab::Header))));
        QCOMPARE(pages->currentIndex(), 1);
        QVERIFY(!window.findChild<QAction *>("actionDialogueEditor"));
        const auto path = config.filePath("scripts.inc");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Text::\n .string \"Hello$\"\n");
        file.close();
        QVERIFY(dialogue->setScene(config.path(), "Scene", path));
        auto *text = dialogue->findChild<QPlainTextEdit *>("dialogueText");
        QVERIFY(text);
        text->setPlainText("Saved scene text$");
        QCOMPARE(tabs->tabText(MainTab::Dialogue), QString("Dialogue *"));
        QVERIFY(dialogue->saveChanges());
        QVERIFY(!dialogue->hasUnsavedChanges());
        QCOMPARE(tabs->tabText(MainTab::Dialogue), QString("Dialogue"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Saved scene text$"));
        QVERIFY(QDir(config.path()).mkpath("src/data"));
        QFile trainers(config.filePath("src/data/trainers.h"));
        QVERIFY(trainers.open(QIODevice::WriteOnly));
        trainers.write("const struct Trainer gTrainers[] = { [TRAINER_TEST] = {.doubleBattle = FALSE}, };\n");
        trainers.close();
        QVERIFY(battle->setScene(config.path(), "Scene", path));
        auto *battleSource = battle->findChild<QPlainTextEdit *>("battleSource");
        battleSource->setPlainText(QString(battleSource->toPlainText()).replace("FALSE", "TRUE"));
        QCOMPARE(tabs->tabText(MainTab::Battle), QString("Battle *"));
        QVERIFY(battle->saveChanges());
        QCOMPARE(tabs->tabText(MainTab::Battle), QString("Battle"));
        auto *records = battle->findChild<QListWidget *>("battleRecords");
        QVERIFY(!records->currentItem());
        dialogue->findChild<QTabWidget *>("dialogueViews")->setCurrentIndex(1);
        dialogue->findChild<QPlainTextEdit *>("sceneScript")->setPlainText("Battle::\n trainerbattle_single TRAINER_TEST, Intro, Defeat\n");
        QVERIFY(dialogue->saveChanges());
        QVERIFY(records->currentItem());
        QCOMPARE(records->currentItem()->toolTip(), QString("[TRAINER_TEST]"));
    }
};
QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
