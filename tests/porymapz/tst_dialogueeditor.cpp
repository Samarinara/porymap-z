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

class DialogueEditorTest : public QObject {
    Q_OBJECT
private slots:
    void editSaveAndDiscard() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("data"));
        const auto path = dir.filePath("data/text.inc");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("First::\n .string \"Hello$\" @ preserved\nSecond::\n .string \"Bye$\"\n");
        file.close();
        PorymapZ::DialogueEditor editor(dir.path());
        auto *files = editor.findChild<QComboBox *>();
        auto *entries = editor.findChild<QListWidget *>();
        auto *text = editor.findChild<QPlainTextEdit *>();
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
        search->setText("Second");
        QVERIFY(entries->item(0)->isHidden());
        QVERIFY(!entries->item(1)->isHidden());
    }
};
QTEST_MAIN(DialogueEditorTest)
#include "tst_dialogueeditor.moc"
