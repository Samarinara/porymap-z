#include "porymapz/dialogueeditor.h"
#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTabWidget>
#include <QLabel>

class DialogueEditorTest : public QObject {
    Q_OBJECT
private slots:
    static void answer(QMessageBox::StandardButton choice) {
        QTimer::singleShot(0, [choice] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            message->button(choice)->click();
        });
    }
    void sceneScopeAndScripts() {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath("data/maps/SceneA"));
        QVERIFY(root.mkpath("data/maps/SceneB"));
        const auto a = dir.filePath("data/maps/SceneA/scripts.inc");
        const auto b = dir.filePath("data/maps/SceneB/scripts.inc");
        for (const auto &path : {a, b}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("OnEnter::\n setflag FLAG_TEST\n end\n");
        }
        PorymapZ::DialogueEditor editor;
        QVERIFY(editor.setScene(dir.path(), "SceneA", a));
        auto *files = editor.findChild<QComboBox *>("dialogueFiles");
        auto *views = editor.findChild<QTabWidget *>("dialogueViews");
        auto *script = editor.findChild<QPlainTextEdit *>("sceneScript");
        QCOMPARE(files->count(), 1);
        QCOMPARE(files->currentText(), QString("data/maps/SceneA/scripts.inc"));
        views->setCurrentIndex(1);
        script->setPlainText("OnEnter::\n setflag FLAG_CHANGED\n end\n");
        QVERIFY(editor.hasUnsavedChanges());
        answer(QMessageBox::Cancel);
        QVERIFY(!editor.setScene(dir.path(), "SceneB", b));
        QVERIFY(editor.hasUnsavedChanges());
        QVERIFY(editor.findChild<QLabel *>("dialogueScene")->text().contains("SceneA"));
        answer(QMessageBox::Save);
        QVERIFY(editor.setScene(dir.path(), "SceneB", b));
        QVERIFY(!editor.hasUnsavedChanges());
        QFile file(a);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("FLAG_CHANGED"));
        QCOMPARE(files->currentText(), QString("data/maps/SceneB/scripts.inc"));
        QVERIFY(!script->toPlainText().contains("FLAG_CHANGED"));
        // A scene may share another map's scripts; use the resolved script path.
        QVERIFY(editor.setScene(dir.path(), "SharedScene", a));
        QCOMPARE(files->currentText(), QString("data/maps/SceneA/scripts.inc"));
        QVERIFY(editor.findChild<QLabel *>("dialogueScene")->text().contains("SharedScene"));
        script->setPlainText("Unsaved$");
        answer(QMessageBox::Cancel);
        views->setCurrentIndex(0);
        QCOMPARE(views->currentIndex(), 1);
        answer(QMessageBox::Discard);
        views->setCurrentIndex(0);
        QCOMPARE(views->currentIndex(), 0);
        QVERIFY(!editor.hasUnsavedChanges());
        QVERIFY(editor.setScene({}, {}, {}));
        QCOMPARE(files->count(), 0);
        QVERIFY(script->toPlainText().isEmpty());
    }
    void editSaveAndDiscard() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("data"));
        const auto path = dir.filePath("data/text.inc");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("First::\n .string \"Hello$\" @ preserved\nSecond::\n .string \"Bye$\"\n");
        file.close();
        PorymapZ::DialogueEditor editor;
        QVERIFY(editor.setScene(dir.path(), "TestMap", path));
        auto *files = editor.findChild<QComboBox *>("dialogueFiles");
        auto *entries = editor.findChild<QListWidget *>();
        auto *text = editor.findChild<QPlainTextEdit *>("dialogueText");
        auto *search = editor.findChild<QLineEdit *>();
        QVERIFY(files && entries && text && search);
        QCOMPARE(files->count(), 1);
        QCOMPARE(entries->count(), 2);
        QCOMPARE(text->toPlainText(), QString("Hello$"));
        QPushButton *save = nullptr;
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->text() == "Save Dialogue") save = button;
        QVERIFY(save);
        text->setPlainText("Welcome {PLAYER}!$");
        QVERIFY(save->isEnabled());
        save->click();
        QVERIFY(!save->isEnabled());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("\"Welcome {PLAYER}!$\" @ preserved"));
        file.close();
        text->setPlainText("Pending$");
        QTimer::singleShot(0, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            message->button(QMessageBox::Cancel)->click();
        });
        entries->setCurrentRow(1);
        QCOMPARE(entries->currentRow(), 0);
        QCOMPARE(text->toPlainText(), QString("Pending$"));
        QTimer::singleShot(0, [] {
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(message);
            message->button(QMessageBox::Discard)->click();
        });
        entries->setCurrentRow(1);
        QCOMPARE(text->toPlainText(), QString("Bye$"));
        QCOMPARE(entries->currentRow(), 1);
        search->setText("Second");
        QVERIFY(entries->item(0)->isHidden());
        QVERIFY(!entries->item(1)->isHidden());
    }
};
QTEST_MAIN(DialogueEditorTest)
#include "tst_dialogueeditor.moc"
