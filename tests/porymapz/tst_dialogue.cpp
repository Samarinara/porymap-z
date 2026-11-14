#include "porymapz/dialoguedocument.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QDirIterator>

using PorymapZ::DialogueDocument;

class DialogueTest : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &data) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), qint64(data.size()));
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private slots:
    void preservesSource() {
        QTemporaryDir dir;
        const auto path = dir.filePath("scripts.inc");
        const QByteArray source = QByteArray::fromHex("efbbbf") +
            "Text:: @ label\r\n\t.string \"Héllo {PLAYER}!\\n\" @ keep\r\n"
            "\t@ comment\r\n\t.string \"Bye$\"\r\n\t.align 2\r\n"
            "Other::\r\n\t.string \"Untouched$\"\r\n";
        write(path, source);
        DialogueDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        QCOMPARE(doc.entries().size(), 2);
        QCOMPARE(doc.entries().at(0).label, QString("Text"));
        QCOMPARE(doc.entries().at(0).line, 2);
        QCOMPARE(doc.entries().at(0).text, QString::fromUtf8("Héllo {PLAYER}!\\n\nBye$"));
        QVERIFY2(doc.save(0, QString::fromUtf8("Pokémon \\\"Z\\\"!\\n\nWelcome$"), &error), qPrintable(error));
        auto expected = source;
        expected.replace("Héllo {PLAYER}!\\n", "Pokémon \\\"Z\\\"!\\n");
        expected.replace("Bye$", "Welcome$");
        QCOMPARE(read(path), expected);
        // A second edit uses the new byte offsets after UTF-8 text changed length.
        QVERIFY(doc.save(1, "Second$", &error));
        expected.replace("Untouched$", "Second$");
        QCOMPARE(read(path), expected);
    }
    void addsAndShortensLines() {
        QTemporaryDir dir;
        const auto path = dir.filePath("text.inc");
        write(path, "Text::\n\t.string \"Old$\" @ stay\nNext::\n\t.string \"Other$\"\n");
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        QVERIFY(doc.save(0, "One\\n\nTwo\\l\nThree$", &error));
        QCOMPARE(read(path), QByteArray("Text::\n\t.string \"One\\n\" @ stay\n\t.string \"Two\\l\"\n\t.string \"Three$\"\nNext::\n\t.string \"Other$\"\n"));
        QVERIFY(doc.save(0, "Short$", &error));
        QVERIFY(read(path).contains("\t.string \"Short$\" @ stay\n\t.string \"\"\n\t.string \"\"\n"));
        QCOMPARE(doc.entries().size(), 2);
    }
    void noFinalNewline() {
        QTemporaryDir dir;
        const auto path = dir.filePath("text.inc");
        write(path, "Text:\n .string \"Old$\"");
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        QVERIFY(doc.save(0, "First\\n\nLast$", &error));
        QCOMPARE(read(path), QByteArray("Text:\n .string \"First\\n\"\n .string \"Last$\"\n"));
    }
    void rejectsInvalidOperands() {
        QTemporaryDir dir;
        const auto path = dir.filePath("text.inc");
        const QByteArray source("Text::\n .string \"Old$\"\n");
        write(path, source);
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        for (const auto &text : QStringList{"unescaped \"quote$", "trailing\\", "bad\rline"}) {
            QVERIFY(!doc.save(0, text, &error));
            QCOMPARE(read(path), source);
        }
        QVERIFY(!doc.save(-1, "Bad$", &error));
        QCOMPARE(read(path), source);
    }
    void rejectsExternalChanges() {
        QTemporaryDir dir;
        const auto path = dir.filePath("text.inc");
        write(path, "Text::\n .string \"Old$\"\n");
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        const QByteArray external("Text::\n .string \"External$\"\n");
        write(path, external);
        QVERIFY(!doc.save(0, "Mine$", &error));
        QCOMPARE(read(path), external);
        QVERIFY(QFile::remove(path));
        QVERIFY(!doc.save(0, "Mine$", &error));
        QVERIFY(!QFile::exists(path));
    }
    void scriptSourcePreservesFormattingAndConflicts() {
        QTemporaryDir dir;
        const auto path = dir.filePath("scripts.inc");
        const auto source = QByteArray::fromHex("efbbbf") + "Enter::\r\n\tsetflag FLAG_OLD\r\n\tend";
        write(path, source);
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        auto changed = doc.sourceText();
        changed.replace("FLAG_OLD", "FLAG_NEW");
        QVERIFY(doc.saveSource(changed, &error));
        auto expected = source;
        expected.replace("FLAG_OLD", "FLAG_NEW");
        QCOMPARE(read(path), expected);
        write(path, expected + "\r\n@ External edit");
        QVERIFY(!doc.saveSource("Replacement", &error));
        QCOMPARE(read(path), expected + "\r\n@ External edit");
    }
    void pokeemeraldCorpus() {
        const QString root = qEnvironmentVariable("PORYMAP_Z_TEST_PROJECT");
        if (root.isEmpty()) QSKIP("Set PORYMAP_Z_TEST_PROJECT to round-trip a real pokeemerald data directory.");
        QTemporaryDir dir;
        int files = 0;
        int entries = 0;
        QDirIterator it(root + "/data", {"*.inc", "*.s"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next();
            DialogueDocument doc;
            QString error;
            QVERIFY2(doc.load(path, &error), qPrintable(path + ": " + error));
            if (doc.entries().isEmpty()) continue;
            ++files;
            entries += doc.entries().size();
            const auto source = read(path);
            const auto copy = dir.filePath("roundtrip.inc");
            write(copy, source);
            QVERIFY(doc.load(copy, &error));
            QVERIFY2(doc.save(0, doc.entries().first().text, &error), qPrintable(error));
            QCOMPARE(read(copy), source);
        }
        QVERIFY(files > 100);
        QVERIFY(entries > 1000);
        qInfo("Round-tripped %d source files containing %d dialogue entries", files, entries);
    }
    void skipsUnsupportedSource() {
        QTemporaryDir dir;
        const auto path = dir.filePath("text.inc");
        write(path, "@ .string \"Comment$\"\n .string \"NoLabel$\"\n"
            "Code::\n msgbox Text\n .string \"NotDialogue$\"\n"
            "Bad::\n .string \"Partial\"\n .string SOME_MACRO\n"
            "Good::\n .string \"Good$\"\n .align 2\n");
        DialogueDocument doc;
        QString error;
        QVERIFY(doc.load(path, &error));
        QCOMPARE(doc.entries().size(), 1);
        QCOMPARE(doc.entries().first().text, QString("Good$"));
        write(path, QByteArray("Text::\n .string \"") + char(0xff) + "$\"\n");
        QVERIFY(!doc.load(path, &error));
    }
};
QTEST_GUILESS_MAIN(DialogueTest)
#include "tst_dialogue.moc"
