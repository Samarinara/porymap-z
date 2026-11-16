#include "porymapz/scriptdocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace PorymapZ {
namespace {
QString codeOnly(QString text) {
    static const QRegularExpression noise(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"|/\\*[\\s\\S]*?\\*/|//[^\\r\\n]*|@[^\\r\\n]*"));
    // Preserve offsets and line numbers while hiding strings and comments.
    auto matches = noise.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        for (int i = match.capturedStart(); i < match.capturedEnd(); ++i)
            if (text[i] != '\n' && text[i] != '\r') text[i] = ' ';
    }
    return text;
}
QStringList references(const QString &text) {
    static const QRegularExpression token(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*\\b"));
    QStringList result;
    auto matches = token.globalMatch(text);
    while (matches.hasNext()) result.append(matches.next().captured());
    result.removeDuplicates();
    return result;
}
}
void ScriptDocument::scan(const QString &root, const QStringList &sources, const QStringList &mapJsonPaths) {
    m_blocks.clear(); m_maps.clear(); m_sources.clear(); m_labels.clear(); m_warnings.clear();
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    QMap<QString, QStringList> folderLabels;
    QStringList paths = sources;
    paths.removeDuplicates(); paths.sort();
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (canonicalRoot.isEmpty() || !info.canonicalFilePath().startsWith(canonicalRoot + '/')) continue;
        if (info.suffix() != "inc" && info.suffix() != "s") continue;
        if (QFileInfo::exists(info.path() + '/' + info.completeBaseName() + ".pory")) {
            m_warnings.append(QStringLiteral("Skipped generated source: %1").arg(path)); continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_warnings.append(path + ": " + file.errorString()); continue; }
        const QByteArray bytes = file.readAll();
        const auto utf8 = bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes;
        if (QString::fromUtf8(utf8).toUtf8() != utf8) { m_warnings.append(path + ": source must be UTF-8"); continue; }
        m_sources.insert(path, bytes);
        // Latin-1 gives byte offsets even for UTF-8 text in the original source.
        QString code = codeOnly(QString::fromLatin1(bytes));
        if (bytes.startsWith("\xEF\xBB\xBF")) code.replace(0, 3, "   ");
        static const QRegularExpression labels(QStringLiteral("(?m)^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)::?(?!:)"));
        auto matches = labels.globalMatch(code);
        QVector<QRegularExpressionMatch> definitions;
        while (matches.hasNext()) definitions.append(matches.next());
        for (int i = 0; i < definitions.size(); ++i) {
            const auto &match = definitions[i];
            const int start = match.capturedStart() == 0 && bytes.startsWith("\xEF\xBB\xBF") ? 3 : match.capturedStart();
            const int end = i + 1 < definitions.size() ? definitions[i + 1].capturedStart() : bytes.size();
            const int index = m_blocks.size();
            m_blocks.append({match.captured(1), path, start, end - start,
                bytes.left(start).count('\n') + 1, references(code.mid(match.capturedEnd(), end - match.capturedEnd()))});
            m_labels[match.captured(1)].append(index);
            folderLabels[info.absolutePath()].append(match.captured(1));
        }
    }
    for (const auto &path : mapJsonPaths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_warnings.append(path + ": " + file.errorString()); continue; }
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) { m_warnings.append(path + ": invalid map JSON"); continue; }
        const auto object = json.object();
        MapScripts map;
        map.name = QFileInfo(path).dir().dirName();
        for (const auto &key : {"object_events", "coord_events", "bg_events"}) {
            int n = 0;
            for (const auto &value : object.value(key).toArray()) {
                const auto event = value.toObject();
                const auto label = event.value("script").toString();
                ++n;
                if (label.isEmpty() || label == "0" || label == "0x0" || label == "NULL") continue;
                QString category = QString::fromLatin1(key);
                category.replace('_', ' ');
                map.groups[category + QStringLiteral(" #%1").arg(n)].append(label);
            }
        }
        const QString shared = object.value("shared_scripts_map").toString();
        const QString owner = shared.isEmpty() ? map.name : shared;
        const QString folder = QFileInfo(path).dir().absolutePath();
        const QString ownerFolder = QDir(folder + "/../" + owner).absolutePath();
        if (folderLabels.contains(ownerFolder))
            map.groups[QStringLiteral("Map scripts")] = folderLabels.value(ownerFolder);
        // Map script tables can also live in a monolithic source file.
        if (!find(owner + "_MapScripts").isEmpty())
            map.groups[QStringLiteral("Map scripts")].append(owner + "_MapScripts");
        for (auto it = map.groups.begin(); it != map.groups.end(); ++it) it.value().removeDuplicates();
        m_maps.append(map);
    }
}
QVector<int> ScriptDocument::find(const QString &label) const { return m_labels.value(label); }
QString ScriptDocument::text(int index) const {
    const auto &block = m_blocks.at(index);
    return QString::fromUtf8(m_sources.value(block.path).mid(block.offset, block.length));
}
bool ScriptDocument::save(int index, const QString &text, QString *error) {
    if (index < 0 || index >= m_blocks.size()) { *error = "Select a script first."; return false; }
    const auto block = m_blocks.at(index);
    QFile current(block.path);
    if (!current.open(QIODevice::ReadOnly)) { *error = current.errorString(); return false; }
    if (current.readAll() != m_sources.value(block.path)) {
        *error = "This file changed outside the script editor. Copy your edits and reopen the window before saving."; return false;
    }
    current.close();
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    if (!normalized.endsWith('\n')) normalized += '\n';
    if (m_sources.value(block.path).contains("\r\n")) normalized.replace("\n", "\r\n");
    QByteArray updated = m_sources.value(block.path);
    updated.replace(block.offset, block.length, normalized.toUtf8());
    QSaveFile file(block.path);
    if (!file.open(QIODevice::WriteOnly) || file.write(updated) != updated.size() || !file.commit()) {
        *error = file.errorString(); return false;
    }
    const int delta = normalized.toUtf8().size() - block.length;
    m_sources[block.path] = updated;
    m_blocks[index].length += delta;
    m_blocks[index].references = references(codeOnly(normalized));
    for (int i = 0; i < m_blocks.size(); ++i) {
        auto &following = m_blocks[i];
        if (following.path == block.path && following.offset > block.offset) {
            following.offset += delta;
            following.line = updated.left(following.offset).count('\n') + 1;
        }
    }
    return true;
}
}
