#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace PorymapZ {

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
    const QVector<Entry> &entries() const { return m_entries; }

private:
    void parse();
    QString m_path;
    QByteArray m_source;
    QVector<Entry> m_entries;
};
}
