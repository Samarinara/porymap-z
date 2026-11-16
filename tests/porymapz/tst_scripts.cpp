#include "porymapz/scriptdocument.h"
#include "porymapz/scripteditor.h"
#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
using namespace PorymapZ;
class ScriptsTest : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &data) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), qint64(data.size()));
    }
    static QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
private slots:
    void indexAndSave() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("maps/Test"));
        auto map = dir.filePath("maps/Test/map.json");
        auto source = dir.filePath("shared.inc");
        write(map, R"({"object_events":[{"script":"Entry"}],"coord_events":[{"script":"0"}]})");
        const QByteArray prefix = "@ header\r\n";
        const QByteArray first = "Entry::\r\n\tcall Shared\r\n\tend\r\n";
        const QByteArray last = "Shared:: @ comment\r\n\tmsgbox Text\r\n\treturn\r\nText::\r\n\t.string \"héllo$\"\r\n";
        write(source, prefix + first + last);
        ScriptDocument doc;
        doc.scan(dir.path(), {source}, {map});
        QCOMPARE(doc.blocks().size(), 3);
        QCOMPARE(doc.maps().size(), 1);
        QCOMPARE(doc.maps()[0].groups.value("object events #1"), QStringList{"Entry"});
        QVERIFY(doc.blocks()[0].references.contains("Shared"));
        QString error;
        QVERIFY2(doc.save(0, "Entry::\n\tcall Shared\n\tsetvar VAR_RESULT, 1\n\tend\n", &error), qPrintable(error));
        QVERIFY(read(source).startsWith(prefix));
        QVERIFY(read(source).endsWith(last));
        // Save the next block without reopening: its offsets must follow the first edit.
        QVERIFY2(doc.save(1, "Shared::\n\treturn\n", &error), qPrintable(error));
        QVERIFY(read(source).contains("Shared::\r\n\treturn\r\nText::"));
        write(source, read(source) + "@ external change\n");
        const auto external = read(source);
        QVERIFY(!doc.save(0, "Entry::\n\tend\n", &error));
        QVERIFY(error.contains("changed outside"));
        QCOMPARE(read(source), external);
    }
    void commentsAndGeneratedSources() {
        QTemporaryDir dir;
        auto path = dir.filePath("script.inc");
        write(path, "/*\nFake::\n*/\nReal::\n\t.string \"Other::\"\n\tend\n");
        ScriptDocument doc; doc.scan(dir.path(), {path}, {});
        QCOMPARE(doc.blocks().size(), 1);
        QCOMPARE(doc.blocks()[0].label, QString("Real"));
        QVERIFY(!doc.blocks()[0].references.contains("Other"));
        write(dir.filePath("script.pory"), "script Real {}\n");
        doc.scan(dir.path(), {path}, {});
        QVERIFY(doc.blocks().isEmpty()); QVERIFY(!doc.warnings().isEmpty());
    }
    void mapScriptTablesAndSharedOwners() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("maps/Test"));
        QDir().mkpath(dir.filePath("maps/Owner"));
        const auto map = dir.filePath("maps/Test/map.json");
        const auto path = dir.filePath("maps/Owner/scripts.inc");
        write(map, R"({"shared_scripts_map":"Owner"})");
        write(path, QByteArray::fromHex("efbbbf") + "Owner_MapScripts::\n\tmap_script MAP_SCRIPT_ON_LOAD, Load\nLoad::\n\tend\n");
        ScriptDocument doc; doc.scan(dir.path(), {path}, {map});
        QCOMPARE(doc.blocks().size(), 2);
        QVERIFY(doc.maps()[0].groups.value("Map scripts").contains("Owner_MapScripts"));
        QVERIFY(doc.maps()[0].groups.value("Map scripts").contains("Load"));
        QVERIFY(doc.blocks()[0].references.contains("Load"));
        QString error;
        QVERIFY(doc.save(0, doc.text(0), &error));
        QVERIFY(read(path).startsWith(QByteArray::fromHex("efbbbf")));
    }
    void expansionProject() {
        const auto root = qEnvironmentVariable("PORYMAP_SCRIPT_TEST_PROJECT");
        if (root.isEmpty()) QSKIP("Set PORYMAP_SCRIPT_TEST_PROJECT to test a local decomp checkout.");
        QStringList sources, maps;
        QDirIterator files(root + "/data", QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            if (path.endsWith(".inc") || path.endsWith(".s")) sources.append(path);
            if (path.endsWith("/map.json")) maps.append(path);
        }
        ScriptDocument doc; doc.scan(root, sources, maps);
        QVERIFY(doc.blocks().size() > 1000);
        QVERIFY(doc.maps().size() > 100);
        QVERIFY(!doc.find("LittlerootTown_MapScripts").isEmpty());
        bool found = false;
        for (const auto &map : doc.maps()) if (map.name == "LittlerootTown") {
            QVERIFY(map.groups.value("Map scripts").contains("LittlerootTown_MapScripts"));
            found = true;
        }
        QVERIFY(found);
    }
    void popupReferencesAndSave() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("maps/Test"));
        const auto map = dir.filePath("maps/Test/map.json");
        const auto path = dir.filePath("shared.inc");
        write(map, R"({"bg_events":[{"script":"Entry"}]})");
        write(path, "Entry::\n\tcall Shared\n\tend\nShared::\n\tgoto Entry\n");
        ScriptEditor popup(dir.path(), {path}, {map}, "Test", "Entry", nullptr);
        auto *tree = popup.findChild<QTreeWidget *>("scriptTree");
        auto *text = popup.findChild<QPlainTextEdit *>("scriptSource");
        auto *save = popup.findChild<QPushButton *>("saveMapScript");
        QVERIFY(tree); QVERIFY(text); QVERIFY(save);
        QCOMPARE(tree->currentItem()->text(0), QString("Entry"));
        QCOMPARE(tree->topLevelItemCount(), 2); // event and transitive references; cycle terminates
        text->selectAll(); text->insertPlainText("Entry::\n\tend\n");
        QVERIFY(save->isEnabled()); save->click();
        QCOMPARE(read(path), QByteArray("Entry::\n\tend\nShared::\n\tgoto Entry\n"));
        tree->setCurrentItem(tree->topLevelItem(1)->child(0));
        QVERIFY(text->toPlainText().startsWith("Shared::"));
        const auto saved = read(path);
        text->insertPlainText("@ pending edit\n");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box); box->button(QMessageBox::Cancel)->click();
        });
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QCOMPARE(tree->currentItem()->text(0), QString("Shared"));
        QVERIFY(text->document()->isModified());
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box); box->button(QMessageBox::Discard)->click();
        });
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QCOMPARE(tree->currentItem()->text(0), QString("Entry"));
        QCOMPARE(read(path), saved);
        auto *scope = popup.findChild<QComboBox *>("scriptMap");
        scope->setCurrentIndex(0);
        QCOMPARE(tree->topLevelItemCount(), 1);
    }
};
QTEST_MAIN(ScriptsTest)
#include "tst_scripts.moc"
