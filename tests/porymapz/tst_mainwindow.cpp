#include <QtTest>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QListWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QMessageBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTextBlock>
#include "mainwindow.h"
#include "porymapz/dialogueeditor.h"
#include "porymapz/battleeditor.h"
#include "porymapz/trainermanager.h"
#include "eventframes.h"
#include "editor.h"
#include "project.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QScopeGuard>
#include "loadingscreen.h"
#include "playtest.h"

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void scriptToolbarOpensPopup_data() {
        QTest::addColumn<bool>("poryscriptEnabled");
        QTest::newRow("assembly-project") << false;
        QTest::newRow("assembly-map-with-poryscript-enabled") << true;
    }
    void scriptToolbarOpensPopup() {
        QFETCH(bool, poryscriptEnabled);
        QTemporaryDir root;
        MainWindow window(nullptr);
        window.initialize();
        Project project;
        project.setRoot(root.path());
        Map map;
        map.setName("Test");
        const auto oldRoot = projectConfig.root();
        const bool oldPoryscript = projectConfig.usePoryScript;
        projectConfig.setRoot(root.path());
        projectConfig.usePoryScript = poryscriptEnabled;
        window.editor->project = &project;
        window.editor->map = &map;
        const auto reset = qScopeGuard([&] {
            window.editor->map = nullptr;
            window.editor->project = nullptr;
            projectConfig.setRoot(oldRoot);
            projectConfig.usePoryScript = oldPoryscript;
        });
        const auto sourcePath = map.getScriptsFilepath();
        QVERIFY(sourcePath.endsWith(".inc"));
        QVERIFY(QDir().mkpath(QFileInfo(sourcePath).absolutePath()));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("Test_Event::\n\tend\n");
        source.close();
        auto *button = window.findChild<QToolButton *>("toolButton_Open_Scripts");
        QVERIFY(button);
        // This minimal fixture bypasses project loading, which normally enables the Events page.
        for (QWidget *widget = button; widget; widget = widget->parentWidget()) widget->setEnabled(true);
        // Check both user entry points: the toolbar and an event label.
        for (bool event : {false, true}) {
            bool popupOpened = false;
            QTimer::singleShot(0, &window, [&] {
                auto *popup = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!popup) return;
                const auto dismiss = qScopeGuard([&] { popup->reject(); });
                QCOMPARE(popup->objectName(), QString("mapScriptEditor"));
                auto *text = popup->findChild<QPlainTextEdit *>("scriptSource");
                QVERIFY(text);
                QVERIFY(text->toPlainText().contains("Test_Event::"));
                popupOpened = true;
            });
            if (event) QVERIFY(window.editor->openScript("Test_Event"));
            else button->click();
            QApplication::processEvents();
            QVERIFY(popupOpened);
        }
    }
    void playtestStoryControlsPersist() {
        if (QStandardPaths::findExecutable("python3").isEmpty() || QStandardPaths::findExecutable("make").isEmpty())
            QSKIP("Requires Python 3 and make for dialog validation.");
        QTemporaryDir root;
        MainWindow window(nullptr);
        window.initialize();
        Project project;
        project.setRoot(root.path());
        project.speciesNames = {"SPECIES_TREECKO"};
        project.flagNames = {"FLAG_RECEIVED_STARTER", "FLAG_HIDE_STARTER"};
        project.varNames = {"VAR_STARTER_STORY"};
        Map map;
        map.setConstantName("MAP_TEST");
        window.editor->project = &project;
        window.editor->map = &map;
        const QString prefix = "playtest/" + root.path() + "/";
        const auto reset = qScopeGuard([&] {
            window.editor->map = nullptr;
            window.editor->project = nullptr;
            QSettings().remove(prefix);
        });
        QSettings().setValue(prefix + "emulator", QStandardPaths::findExecutable("python3"));
        auto *playtest = window.findChild<Playtest *>();
        QVERIFY(playtest);
        // This fixture has no rendered map; test configuration without switching
        // the main window into the map editor after accepting the dialog.
        QObject::disconnect(playtest, &Playtest::choosingTile, &window, nullptr);
        for (bool saved : {false, true}) {
            QTimer::singleShot(0, &window, [&, saved] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto dismiss = qScopeGuard([&] { if (dialog->result() != QDialog::Accepted) dialog->reject(); });
                auto *empty = dialog->findChild<QCheckBox *>("demoEmptyParty");
                auto *species = dialog->findChild<QComboBox *>("demoSpecies");
                auto *level = dialog->findChild<QSpinBox *>("demoLevel");
                auto *flags = dialog->findChild<QTableWidget *>("demoFlags");
                auto *vars = dialog->findChild<QTableWidget *>("demoVariables");
                QVERIFY(empty && species && level && flags && vars);
                if (!saved) {
                    QVERIFY(!empty->isChecked());
                    empty->setChecked(true);
                    QVERIFY(!species->isEnabled() && !level->isEnabled());
                    // The empty party must also work when no species is selected.
                    species->setCurrentIndex(-1);
                    dialog->findChild<QPushButton *>("demoFlagsAdd")->click();
                    auto *flag = qobject_cast<QComboBox *>(flags->cellWidget(0, 0));
                    QVERIFY(flag && flag->findText("FLAG_RECEIVED_STARTER") >= 0);
                    flag->setCurrentText("FLAG_RECEIVED_STARTER");
                    qobject_cast<QComboBox *>(flags->cellWidget(0, 1))->setCurrentIndex(1); // Clear
                    dialog->findChild<QPushButton *>("demoVariablesAdd")->click();
                    qobject_cast<QComboBox *>(vars->cellWidget(0, 0))->setCurrentText("VAR_STARTER_STORY");
                    qobject_cast<QSpinBox *>(vars->cellWidget(0, 1))->setValue(2);
                    dialog->findChild<QPushButton *>("demoFlagsAdd")->click();
                    dialog->findChild<QPushButton *>("demoFlagsRemove")->click();
                    QCOMPARE(flags->rowCount(), 1);
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    QCOMPARE(dialog->result(), int(QDialog::Accepted));
                } else {
                    QVERIFY(empty->isChecked());
                    QVERIFY(!species->isEnabled() && !level->isEnabled());
                    QCOMPARE(flags->rowCount(), 1);
                    QCOMPARE(vars->rowCount(), 1);
                    QCOMPARE(qobject_cast<QComboBox *>(flags->cellWidget(0, 0))->currentText(), QString("FLAG_RECEIVED_STARTER"));
                    QVERIFY(!qobject_cast<QComboBox *>(flags->cellWidget(0, 1))->currentData().toBool());
                    QCOMPARE(qobject_cast<QSpinBox *>(vars->cellWidget(0, 1))->value(), 2);
                }
            });
            playtest->configure();
        }
    }
    void playtestScopeControls() {
        QTemporaryDir root;
        MainWindow window(nullptr);
        window.initialize();
        Project project;
        project.setRoot(root.path());
        project.speciesNames = {"SPECIES_TREECKO"};
        Map map;
        window.editor->project = &project;
        window.editor->map = &map;
        const auto reset = qScopeGuard([&] {
            window.editor->map = nullptr;
            window.editor->project = nullptr;
        });
        auto *playtest = window.findChild<Playtest *>();
        QVERIFY(playtest);
        const QString prefix = "playtest/" + root.path() + "/";
        for (bool full : {false, true}) {
            QSettings settings;
            settings.setValue(prefix + "wholeGame", full);
            settings.setValue(prefix + "scope", "nearby");
            QTimer::singleShot(0, &window, [=] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                auto *whole = dialog->findChild<QCheckBox *>("demoWholeGame");
                auto *scope = dialog->findChild<QComboBox *>("demoScope");
                // Always dismiss the modal, including on an assertion failure.
                const auto dismiss = qScopeGuard([&] { dialog->reject(); });
                QVERIFY(whole && scope);
                QCOMPARE(whole->isChecked(), full);
                QCOMPARE(scope->currentData().toString(), QString("nearby"));
                QCOMPARE(scope->count(), 3);
                QCOMPARE(scope->isEnabled(), !full);
                whole->setChecked(!full);
                QCOMPARE(scope->isEnabled(), full);
            });
            playtest->configure();
        }
        QSettings().remove(prefix);
    }
    void dialogueIsAMainPage() {
        QTemporaryDir config;
        qputenv("XDG_DATA_HOME", config.path().toUtf8());
        MainWindow window(nullptr);
        window.initialize();
        auto *tabs = window.findChild<QTabBar *>("mainTabBar");
        auto *pages = window.findChild<QStackedWidget *>("mainStackedWidget");
        auto *dialogue = window.findChild<PorymapZ::DialogueEditor *>();
        QVERIFY(tabs && pages && dialogue);
        auto *play = window.findChild<QAction *>("actionPlayMap");
        auto *playToolbar = window.findChild<QToolBar *>("playtestToolBar");
        QVERIFY(play && playToolbar);
        QVERIFY(playToolbar->actions().contains(play));
        // The toolbar is disabled without a project; the controller also guards
        // direct calls so they cannot enter tile selection.
        QVERIFY(!play->isEnabled());
        auto *playtest = window.findChild<Playtest *>();
        QVERIFY(playtest);
        bool sawMissingProject = false;
        QTimer::singleShot(0, &window, [&] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (message) {
                sawMissingProject = message->text().contains("Open a project and map first");
                message->accept();
            }
        });
        playtest->configure();
        QVERIFY(sawMissingProject);
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
    void expansionEngineDetection() {
        QTemporaryDir dir;
        const auto root = dir.filePath("pokefirered");
        QVERIFY(QDir().mkpath(root + "/include/config"));
        QFile general(root + "/include/config/general.h");
        QVERIFY(general.open(QIODevice::WriteOnly));
        general.write("#define RHH_EXPANSION\n");
        general.close();
        ProjectConfig config;
        QVERIFY(config.load(root));
        QCOMPARE(config.baseGameVersion, BaseGameVersion::pokeemerald);
    }

    void mixedExpansionFormats() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("data/layouts"));
        const auto oldRoot = projectConfig.root();
        projectConfig.setRoot(dir.path());
        Project project;
        project.setRoot(dir.path());
        QFile layouts(dir.filePath("data/layouts/layouts.json"));
        QVERIFY(layouts.open(QIODevice::WriteOnly));
        layouts.write(R"({"layouts_table_label":"gMapLayouts","layouts":[
          {"id":"LAYOUT_HOENN","name":"Hoenn_Layout","width":3,"height":4,
           "layout_version":"emerald","primary_tileset":"gTileset_General","secondary_tileset":"gTileset_Petalburg",
           "border_filepath":"hoenn.bin","blockdata_filepath":"hoenn-map.bin"},
          {"id":"LAYOUT_KANTO","name":"Kanto_Layout","width":5,"height":6,
           "layout_version":"frlg","border_width":4,"border_height":3,
           "primary_tileset":"gTileset_GeneralFrlg","secondary_tileset":"gTileset_PalletTown",
           "border_filepath":"kanto.bin","blockdata_filepath":"kanto-map.bin"}]})");
        layouts.close();
        QVERIFY(project.readMapLayouts());
        auto *hoenn = project.getLayout("LAYOUT_HOENN");
        auto *kanto = project.getLayout("LAYOUT_KANTO");
        QVERIFY(hoenn && kanto);
        QCOMPARE(hoenn->getBorderWidth(), 2);
        QCOMPARE(kanto->getBorderWidth(), 4);
        QCOMPARE(kanto->getBorderHeight(), 3);
        QVERIFY(kanto->hasCustomBorderSize());
        QVERIFY(project.saveMapLayouts());
        QVERIFY(layouts.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(layouts.readAll()).object()["layouts"].toArray();
        QCOMPARE(saved[0].toObject()["layout_version"].toString(), QString("emerald"));
        QCOMPARE(saved[1].toObject()["layout_version"].toString(), QString("frlg"));
        QCOMPARE(saved[1].toObject()["border_width"].toInt(), 4);

        Tileset emerald, frlg;
        emerald.attributesVersion = BaseGameVersion::pokeemerald;
        frlg.attributesVersion = BaseGameVersion::pokefirered;
        emerald.resizeMetatiles(2);
        frlg.resizeMetatiles(2);
        emerald.metatile_attrs_path = dir.filePath("emerald-attributes.bin");
        frlg.metatile_attrs_path = dir.filePath("frlg-attributes.bin");
        const QByteArray emeraldBytes = QByteArray::fromHex("01a023b0");
        const QByteArray frlgBytes = QByteArray::fromHex("ff3f009802020067");
        for (const auto &pair : {qMakePair(emerald.metatile_attrs_path, emeraldBytes),
                                 qMakePair(frlg.metatile_attrs_path, frlgBytes)}) {
            QFile file(pair.first);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(pair.second), qint64(pair.second.size()));
        }
        QVERIFY(emerald.loadMetatileAttributes());
        QVERIFY(frlg.loadMetatileAttributes());
        QCOMPARE(frlg.metatileAt(0)->behavior(), uint32_t(511));
        QCOMPARE(frlg.metatileAt(0)->terrainType(), uint32_t(31));
        QCOMPARE(frlg.metatileAt(1)->encounterType(), uint32_t(7));
        QCOMPARE(frlg.metatileAt(1)->layerType(), uint32_t(3));
        for (const auto *context : {kanto, hoenn, kanto}) {
            Project::setLayoutContext(context);
            QVERIFY(emerald.saveMetatileAttributes());
            QVERIFY(frlg.saveMetatileAttributes());
            QFile e(emerald.metatile_attrs_path), f(frlg.metatile_attrs_path);
            QVERIFY(e.open(QIODevice::ReadOnly)); QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(e.readAll(), emeraldBytes);
            QCOMPARE(f.readAll(), frlgBytes);
            QCOMPARE(Project::getNumMetatilesPrimary(), context == kanto ? 640 : 512);
        }
        Tileset frlgSecondary(frlg);
        frlg.is_secondary = false;
        frlgSecondary.is_secondary = true;
        Project::setLayoutContext(hoenn);
        QCOMPARE(Tileset::getMetatile(640, &frlg, &frlgSecondary), frlgSecondary.metatileAt(0));
        QCOMPARE(Tileset::getMetatile(512, &frlg, &frlgSecondary), nullptr);
        QCOMPARE(frlgSecondary.firstTileId(), uint16_t(640));
        QCOMPARE(frlg.primaryPaletteCount(), 7);
        const auto originalAttributes = frlg.metatileAt(0)->getAttributes();
        frlg.metatiles().at(0)->setBehavior(1);
        QCOMPARE(frlg.metatileAt(0)->getAttributes(), (originalAttributes & ~uint32_t(0x1FF)) | 1);
        Tileset copy(frlg);
        QCOMPARE(copy.attributesSize(), 4);
        QCOMPARE(copy.metatileAt(0)->getAttributes(), frlg.metatileAt(0)->getAttributes());
        Project::setLayoutContext(nullptr);
        projectConfig.setRoot(oldRoot);
    }

    void expansionCorpus() {
        const auto root = qEnvironmentVariable("PORYMAP_Z_EXPANSION_PROJECT");
        if (root.isEmpty()) QSKIP("Set PORYMAP_Z_EXPANSION_PROJECT for real mixed-format map and tileset checks.");
        const auto oldRoot = projectConfig.root();
        PorymapLoadingScreen splash;
        auto *previousSplash = porysplash;
        porysplash = &splash;
        const auto restore = qScopeGuard([&] { porysplash = previousSplash; projectConfig.setRoot(oldRoot); });
        projectConfig.setRoot(root);
        Project project;
        project.setRoot(root);
        QVERIFY(project.load());
        QTemporaryDir output;
        for (const auto &name : {QString("Route105"), QString("PalletTown_Frlg")}) {
            auto *map = project.loadMap(name);
            QVERIFY2(map, qPrintable(name));
            auto *layout = map->layout();
            QVERIFY(layout);
            for (auto *tileset : {layout->tileset_primary, layout->tileset_secondary}) {
                QFile original(tileset->metatile_attrs_path);
                QVERIFY(original.open(QIODevice::ReadOnly));
                const auto bytes = original.readAll();
                const auto path = tileset->metatile_attrs_path;
                tileset->metatile_attrs_path = output.filePath(tileset->name + ".bin");
                QVERIFY(tileset->saveMetatileAttributes());
                QFile saved(tileset->metatile_attrs_path);
                QVERIFY(saved.open(QIODevice::ReadOnly));
                QCOMPARE(saved.readAll(), bytes);
                tileset->metatile_attrs_path = path;
            }
            QVERIFY(!layout->render(true).isNull());
        }
        auto settings = project.newMapSettings;
        settings.name = "PorymapKantoTest";
        settings.group = project.mapNameToMapGroup("PalletTown_Frlg");
        settings.layout = project.getMap("PalletTown_Frlg")->layout()->settings();
        settings.layout.id = "LAYOUT_PORYMAP_KANTO_TEST";
        settings.layout.name = "PorymapKantoTest_Layout";
        auto *created = project.createNewMap(settings);
        QVERIFY(created);
        QCOMPARE(created->customAttributes()["region"].toString(), QString("REGION_KANTO"));
        QCOMPARE(created->layout()->customData["layout_version"].toString(), QString("frlg"));
        projectConfig.setRoot(oldRoot);
    }

    // Drives the real window. Point this at a disposable copy: the test writes to it.
    void trainerToolsOnRealProject() {
        const auto root = qEnvironmentVariable("PORYMAP_Z_TRAINER_PROJECT");
        if (root.isEmpty()) QSKIP("Set PORYMAP_Z_TRAINER_PROJECT to a disposable pokeemerald copy.");
        const auto shots = qEnvironmentVariable("PORYMAP_Z_TRAINER_SHOTS");
        const auto capture = [&](QWidget *widget, const QString &name) {
            if (!shots.isEmpty()) widget->grab().save(shots + '/' + name + ".png");
        };
        PorymapLoadingScreen splash;
        auto *previousSplash = porysplash;
        porysplash = &splash;
        // Other tests would otherwise reopen this project at startup.
        const auto recentProjects = porymapConfig.getRecentProjects();
        const auto restore = qScopeGuard([&] {
            porysplash = previousSplash;
            porymapConfig.setRecentProjects(recentProjects);
            porymapConfig.save();
        });
        MainWindow window(nullptr);
        window.initialize();
        window.resize(1500, 1000);
        window.show();
        QTimer watchdog;
        connect(&watchdog, &QTimer::timeout, &window, [] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *box = qobject_cast<QMessageBox *>(modal)) { qWarning() << "Dismissing:" << box->text(); box->accept(); }
        });
        watchdog.start(500);
        QVERIFY(window.openProject(root, true));
        watchdog.stop();
        QVERIFY(window.userSetMap("Route102"));
        window.ui->mainTabBar->setCurrentIndex(MainTab::Events);
        window.on_mainTabBar_tabBarClicked(MainTab::Events);
        QApplication::processEvents();

        auto *menu = window.ui->newEventToolButton->QToolButton::menu();
        QAction *newTrainer = nullptr, *check = nullptr;
        for (auto *action : menu->actions()) {
            if (action->objectName() == "action_NewTrainer") newTrainer = action;
            if (action->objectName() == "action_CheckTrainerFiles") check = action;
        }
        QVERIFY(newTrainer);
        QVERIFY(check);

        // An existing trainer on the map reports its battle.
        ObjectEvent *calvin = nullptr;
        for (auto *event : window.editor->map->getEvents(Event::Group::Object)) {
            auto *object = dynamic_cast<ObjectEvent *>(event);
            if (object && object->getScript() == "Route102_EventScript_Calvin") calvin = object;
        }
        QVERIFY(calvin);
        window.editor->selectMapEvent(calvin);
        window.updateSelectedEvents();
        QApplication::processEvents();
        auto *frame = qobject_cast<ObjectFrame *>(calvin->getEventFrame());
        QVERIFY(frame);
        QVERIFY2(frame->label_trainer_status->text().contains("TRAINER_CALVIN_1"), qPrintable(frame->label_trainer_status->text()));
        QVERIFY(frame->button_edit_trainer->isEnabled());
        capture(&window, "events-tab");

        // Create a trainer from the New Event menu.
        const int before = window.editor->map->getNumEvents(Event::Group::Object);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<PorymapZ::NewTrainerDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            dialog->findChild<QLineEdit *>("trainerName")->setText("MARLOW");
            QApplication::processEvents();
            capture(dialog, "new-trainer");
            dialog->findChild<QPushButton *>("createTrainer")->click();
        });
        newTrainer->trigger();
        QCOMPARE(window.editor->map->getNumEvents(Event::Group::Object), before + 1);
        auto *created = dynamic_cast<ObjectEvent *>(window.editor->selectedEvents.value(0));
        QVERIFY(created);
        QCOMPARE(created->getScript(), QString("Route102_EventScript_Marlow"));
        QCOMPARE(created->getTrainerType(), QString("TRAINER_TYPE_NORMAL"));
        QCOMPARE(created->getSightRadiusBerryTreeID(), QString("3"));
        QCOMPARE(window.ui->newEventToolButton->defaultAction()->text(), QString("New Object"));
        const auto read = [&](const QString &file) {
            QFile in(root + '/' + file);
            return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
        };
        QVERIFY(read("include/constants/opponents.h").contains("TRAINER_MARLOW"));
        QVERIFY(read("src/data/trainers.h").contains("[TRAINER_MARLOW]"));
        QVERIFY(read("src/data/trainer_parties.h").contains("sParty_Marlow"));
        QVERIFY(read("data/maps/Route102/scripts.inc").contains("Route102_EventScript_Marlow::"));
        QApplication::processEvents();
        auto *createdFrame = qobject_cast<ObjectFrame *>(created->getEventFrame());
        QVERIFY2(createdFrame->label_trainer_status->text().contains("TRAINER_MARLOW"), qPrintable(createdFrame->label_trainer_status->text()));
        QCOMPARE(createdFrame->combo_script->currentText(), QString("Route102_EventScript_Marlow"));
        QCOMPARE(createdFrame->combo_trainer_type->currentText(), QString("TRAINER_TYPE_NORMAL"));
        QCOMPARE(createdFrame->combo_radius_treeid->currentText(), QString("3"));
        QCOMPARE(createdFrame->combo_sprite->currentText(), created->getGfx());
        QCOMPARE(created->getScript(), QString("Route102_EventScript_Marlow"));
        capture(&window, "trainer-created");

        // Damage a file, then repair it from the same menu.
        QFile table(root + "/src/data/trainers.h");
        QVERIFY(table.open(QIODevice::ReadOnly));
        QByteArray damaged = table.readAll();
        table.close();
        const QByteArray intact = ".trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"SAWYER\")";
        const int damageAt = damaged.indexOf(intact);
        QVERIFY(damageAt >= 0);
        damaged.remove(damageAt + intact.indexOf(','), 1);
        QVERIFY(table.open(QIODevice::WriteOnly));
        table.write(damaged);
        table.close();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<PorymapZ::TrainerRepairDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->findChild<QTreeWidget *>("trainerIssues")->topLevelItemCount(), 1);
            QApplication::processEvents();
            capture(dialog, "repair");
            dialog->findChild<QPushButton *>("selectAllRepairs")->click();
            dialog->findChild<QPushButton *>("applyTrainerRepairs")->click();
            QCOMPARE(dialog->findChild<QTreeWidget *>("trainerIssues")->topLevelItemCount(), 0);
        });
        check->trigger();
        QVERIFY(read("src/data/trainers.h").contains(".trainerPic = TRAINER_PIC_HIKER,\n        .trainerName = _(\"SAWYER\")"));

        // Parties stay in the Battle tab.
        window.openTrainerInBattleTab("TRAINER_MARLOW");
        QCOMPARE(window.ui->mainTabBar->currentIndex(), int(MainTab::Battle));
        QCOMPARE(window.ui->mainStackedWidget->currentWidget(), window.findChild<QWidget *>("battleEditor"));
        auto *source = window.findChild<QPlainTextEdit *>("battleSource");
        QVERIFY(source);
        QVERIFY(source->toPlainText().contains("[TRAINER_MARLOW]"));
        QVERIFY(source->textCursor().block().text().contains("TRAINER_MARLOW"));
        QApplication::processEvents();
        capture(&window, "battle-tab");
    }

};
QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
