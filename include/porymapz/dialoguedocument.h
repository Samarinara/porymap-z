#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QStringList>

namespace PorymapZ {

struct SceneSources {
    QStringList paths;
    QString code;
    bool readable = false;
};

// Follow event-script labels into shared source without treating every trainer
// in a shared file as a trainer on the selected map.
SceneSources resolveSceneSources(const QString &root, const QString &mapName, const QString &scriptsPath);

// Edits only quoted operands. All other source bytes remain untouched.
class DialogueDocument {
public:
    struct Literal { int offset; int length; };
    struct Entry {
        QString label;
        int line;
        QString text; // One source .string operand per physical line; escapes stay literal.
        QVector<Literal> literals;
        int insertionOffset;
        QByteArray indent;
    };

    bool load(const QString &path, QString *error);
    bool save(int entryIndex, const QString &text, QString *error);
    QString sourceText() const;
    bool saveSource(const QString &text, QString *error);
    const QVector<Entry> &entries() const { return m_entries; }

private:
    void parse();
    bool writeSource(const QByteArray &updated, QString *error);
    QString m_path;
    QByteArray m_source;
    QVector<Entry> m_entries;
};
}
