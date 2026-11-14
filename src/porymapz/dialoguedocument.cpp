#include "porymapz/dialoguedocument.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace PorymapZ {

bool DialogueDocument::load(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    const auto source = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        *error = file.errorString();
        return false;
    }
    const auto utf8 = source.startsWith("\xEF\xBB\xBF") ? source.mid(3) : source;
    if (QString::fromUtf8(utf8).toUtf8() != utf8) {
        *error = QStringLiteral("Only UTF-8 source files can be edited.");
        return false;
    }
    m_path = path;
    m_source = source;
    parse();
    return true;
}

void DialogueDocument::parse() {
    m_entries.clear();
    // Latin-1 is used only for matching: its indices equal the original byte offsets.
    static const QRegularExpression labelRe(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_]*)::?\\s*(?:@.*)?$"));
    static const QRegularExpression stringRe(QStringLiteral("^(\\s*)\\.string\\s+\"((?:[^\"\\\\]|\\\\.)*)\"\\s*(?:@.*)?$"));
    QString label;
    int current = -1;
    bool blocked = false;
    int lineNumber = 0;
    for (int start = 0; start < m_source.size();) {
        ++lineNumber;
        int end = m_source.indexOf('\n', start);
        if (end < 0) end = m_source.size();
        const int next = end < m_source.size() ? end + 1 : end;
        auto bytes = m_source.mid(start, end - start);
        if (bytes.endsWith('\r')) bytes.chop(1);
        if (start == 0 && bytes.startsWith("\xEF\xBB\xBF")) {
            start += 3;
            bytes.remove(0, 3);
        }
        const auto line = QString::fromLatin1(bytes);
        const auto labelMatch = labelRe.match(line);
        const auto stringMatch = stringRe.match(line);
        if (labelMatch.hasMatch()) {
            label = labelMatch.captured(1);
            current = -1;
            blocked = false;
        } else if (stringMatch.hasMatch() && !label.isEmpty() && !blocked) {
            if (current < 0) {
                current = m_entries.size();
                m_entries.append({label, lineNumber, {}, {}, 0, {}});
            }
            auto &entry = m_entries[current];
            if (!entry.literals.isEmpty()) entry.text += QLatin1Char('\n');
            const int offset = start + stringMatch.capturedStart(2);
            const int length = stringMatch.capturedLength(2);
            entry.text += QString::fromUtf8(m_source.mid(offset, length));
            entry.literals.append({offset, length});
            entry.insertionOffset = next;
            entry.indent = m_source.mid(start, stringMatch.capturedLength(1));
        } else if (!line.trimmed().isEmpty() && !line.trimmed().startsWith(QLatin1Char('@'))) {
            // Don't expose a partial block containing unsupported assembly expressions.
            if (current >= 0 && line.trimmed().startsWith(QStringLiteral(".string")))
                m_entries.removeLast();
            current = -1;
            label.clear();
            blocked = true;
        }
        start = next;
    }
}

bool DialogueDocument::save(int entryIndex, const QString &text, QString *error) {
    if (entryIndex < 0 || entryIndex >= m_entries.size()) {
        *error = QStringLiteral("Select a dialogue entry first.");
        return false;
    }
    const auto lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression operandRe(QStringLiteral("^(?:[^\"\\\\\\r\\n]|\\\\[^\\r\\n])*$"));
    for (const auto &line : lines) {
        if (!operandRe.match(line).hasMatch() || line.contains(QChar::Null)) {
            *error = QStringLiteral("Escape quotes as \\\" and backslashes as \\\\. A trailing backslash is not valid.");
            return false;
        }
    }
    const auto &entry = m_entries.at(entryIndex);
    QByteArray updated = m_source;
    const QByteArray newline = m_source.contains("\r\n") ? "\r\n" : "\n";
    if (lines.size() > entry.literals.size()) {
        QByteArray extra;
        if (entry.insertionOffset == m_source.size() && !m_source.endsWith('\n')) extra += newline;
        for (int i = entry.literals.size(); i < lines.size(); ++i)
            extra += entry.indent + ".string \"" + lines.at(i).toUtf8() + "\"" + newline;
        updated.insert(entry.insertionOffset, extra);
    }
    for (int i = entry.literals.size() - 1; i >= 0; --i) {
        const auto &literal = entry.literals.at(i);
        // Empty operands preserve comments/directives when the user shortens a block.
        updated.replace(literal.offset, literal.length, i < lines.size() ? lines.at(i).toUtf8() : QByteArray());
    }
    return writeSource(updated, error);
}

QString DialogueDocument::sourceText() const {
    return QString::fromUtf8(m_source);
}

bool DialogueDocument::saveSource(const QString &text, QString *error) {
    if (text.contains(QChar::Null)) {
        *error = QStringLiteral("Scripts cannot contain NUL characters.");
        return false;
    }
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (m_source.contains("\r\n")) normalized.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    QByteArray updated = normalized.toUtf8();
    if (m_source.startsWith("\xEF\xBB\xBF")) updated.prepend("\xEF\xBB\xBF");
    return writeSource(updated, error);
}

bool DialogueDocument::writeSource(const QByteArray &updated, QString *error) {
    QFile existing(m_path);
    if (!existing.open(QIODevice::ReadOnly)) {
        *error = existing.errorString();
        return false;
    }
    const auto disk = existing.readAll();
    if (existing.error() != QFileDevice::NoError || disk != m_source) {
        *error = QStringLiteral("The file changed outside the dialogue editor. Copy your edits, then reload the file before saving.");
        return false;
    }
    existing.close();
    QSaveFile output(m_path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() || !output.commit()) {
        *error = output.errorString();
        return false;
    }
    m_source = updated;
    parse();
    return true;
}
}
