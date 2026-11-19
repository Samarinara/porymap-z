#include "porymapz/trainerproject.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace PorymapZ {
namespace {

const int kPartySize = 6;
const QString kPlaceholderSpecies = QStringLiteral("SPECIES_POOCHYENA");

// Blanks comments, string contents, and preprocessor lines, keeping every offset.
QString maskC(QString text) {
    enum { Code, String, Character, Line, Block } state = Code;
    bool lineStart = true, directive = false;
    QChar previous;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i], next = i + 1 < text.size() ? text[i + 1] : QChar();
        const QChar before = previous;
        previous = c;
        if (c == '\n') {
            if (state == Line) state = Code;
            if (before != '\\') directive = false;
            lineStart = true;
            continue;
        }
        if (state == Code && lineStart && c == '#') directive = true;
        if (!c.isSpace()) lineStart = false;
        if (state == Code) {
            if (c == '/' && next == '/') { state = Line; text[i] = ' '; }
            else if (c == '/' && next == '*') { state = Block; text[i] = ' '; }
            else if (c == '"' && !directive) state = String;
            else if (c == '\'' && !directive) state = Character;
            else if (directive) text[i] = ' ';
        } else if (state == String || state == Character) {
            if (c == '\\') { text[i] = ' '; if (i + 1 < text.size() && next != '\n') text[++i] = ' '; }
            else if ((state == String && c == '"') || (state == Character && c == '\'')) state = Code;
            else text[i] = ' ';
        } else if (state == Block) {
            if (c == '*' && next == '/') { text[i] = ' '; text[++i] = ' '; state = Code; }
            else text[i] = ' ';
        } else {
            text[i] = ' ';
        }
    }
    return text;
}

// Keeps preprocessor text, which holds the trainer ID defines.
QString maskComments(QString text) {
    enum { Code, String, Line, Block } state = Code;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i], next = i + 1 < text.size() ? text[i + 1] : QChar();
        if (c == '\n') { if (state == Line) state = Code; continue; }
        if (state == Code) {
            if (c == '/' && next == '/') { state = Line; text[i] = ' '; }
            else if (c == '/' && next == '*') { state = Block; text[i] = ' '; }
            else if (c == '"') state = String;
        } else if (state == String) {
            if (c == '\\') ++i;
            else if (c == '"') state = Code;
        } else if (state == Block) {
            if (c == '*' && next == '/') { text[i] = ' '; text[++i] = ' '; state = Code; }
            else text[i] = ' ';
        } else {
            text[i] = ' ';
        }
    }
    return text;
}

QString maskBlockComments(QString text) {
    auto comments = QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")).globalMatch(text);
    while (comments.hasNext()) {
        const auto match = comments.next();
        for (int i = match.capturedStart(); i < match.capturedEnd(); ++i)
            if (text[i] != '\n') text[i] = ' ';
    }
    return text;
}

int previousToken(const QString &mask, int from) {
    int i = from - 1;
    while (i >= 0 && mask[i].isSpace()) --i;
    return i;
}

int nextToken(const QString &mask, int from, int limit) {
    int i = from;
    while (i < limit && mask[i].isSpace()) ++i;
    return i;
}

// TRAINER_CALVIN_1 -> Calvin1
QString camelName(QString id) {
    id.remove(QRegularExpression(QStringLiteral("^TRAINER_")));
    QString result;
    for (const auto &part : id.split('_', Qt::SkipEmptyParts))
        result += part.left(1).toUpper() + part.mid(1).toLower();
    return result;
}

// TRAINER_PIC_RS_BRENDAN -> Rs Brendan, which trainerproc maps back to the constant.
QString humanName(const QString &constant, const QString &prefix) {
    QStringList words;
    for (const auto &part : constant.mid(prefix.size()).split('_', Qt::SkipEmptyParts))
        words << part.left(1).toUpper() + part.mid(1).toLower();
    return words.join(' ');
}

QString trainerproc(QString value, const QString &prefix) {
    value = value.trimmed();
    if (value.startsWith(prefix)) return value;
    QString result = prefix;
    for (const QChar c : value) {
        if (c == '\'') continue;
        result += c.isLetterOrNumber() ? c.toUpper() : QChar('_');
    }
    return result;
}

QStringList splitArguments(const QString &text) {
    QStringList result;
    QString current;
    int depth = 0;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) { if (c == '\\') ++i; else if (c == '"') quoted = false; continue; }
        if (c == '"') { quoted = true; continue; }
        if (c == '(') ++depth;
        if (c == ')') { if (depth == 0) break; --depth; }
        if (c == ',' && depth == 0) { result << current.trimmed(); current.clear(); continue; }
        current += c;
    }
    if (!current.trimmed().isEmpty()) result << current.trimmed();
    return result;
}

QString stripScriptComment(const QString &line) {
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) { if (c == '\\') ++i; else if (c == '"') quoted = false; continue; }
        if (c == '"') quoted = true;
        else if (c == '@' || c == '#' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) return line.left(i);
    }
    return line;
}

QString partyTypeForMacro(const QString &macro) {
    static const QHash<QString, QString> types = {
        {"NO_ITEM_DEFAULT_MOVES", "TrainerMonNoItemDefaultMoves"},
        {"NO_ITEM_CUSTOM_MOVES", "TrainerMonNoItemCustomMoves"},
        {"ITEM_DEFAULT_MOVES", "TrainerMonItemDefaultMoves"},
        {"ITEM_CUSTOM_MOVES", "TrainerMonItemCustomMoves"},
    };
    return types.value(macro);
}

QString macroForPartyType(const QString &type) {
    static const QHash<QString, QString> macros = {
        {"TrainerMonNoItemDefaultMoves", "NO_ITEM_DEFAULT_MOVES"},
        {"TrainerMonNoItemCustomMoves", "NO_ITEM_CUSTOM_MOVES"},
        {"TrainerMonItemDefaultMoves", "ITEM_DEFAULT_MOVES"},
        {"TrainerMonItemCustomMoves", "ITEM_CUSTOM_MOVES"},
    };
    return macros.value(type);
}

TrainerIssue makeIssue(const QString &key, const QString &trainer, const QString &file, int line,
                       const QString &summary, const QString &detail,
                       TrainerIssue::Severity severity = TrainerIssue::Error) {
    TrainerIssue issue;
    issue.key = key;
    issue.trainer = trainer;
    issue.file = file;
    issue.line = line;
    issue.summary = summary;
    issue.detail = detail;
    issue.severity = severity;
    return issue;
}

void withFix(TrainerIssue &issue, const QString &fix, const QVector<TrainerTextEdit> &edits) {
    issue.fix = fix;
    issue.edits = edits;
}

} // namespace

bool TrainerProject::load(const QString &root, bool preferFrlg, QString *error) {
    m_root = QFileInfo(root).canonicalFilePath();
    m_sets.clear();
    m_files.clear();
    m_unreadable.clear();
    m_scripts = {};
    m_defaultSet = 0;
    if (m_root.isEmpty()) {
        if (error) *error = QStringLiteral("The project folder could not be found.");
        return false;
    }
    if (QFileInfo::exists(m_root + "/src/data/trainers.party")) {
        m_sets.append({QStringLiteral("Emerald trainers"), "include/constants/opponents.h", "src/data/trainers.party", {}, true});
        if (QFileInfo::exists(m_root + "/src/data/trainers_frlg.party")) {
            m_sets.append({QStringLiteral("FireRed/LeafGreen trainers"), "include/constants/opponents_frlg.h", "src/data/trainers_frlg.party", {}, true});
            if (preferFrlg) m_defaultSet = 1;
        }
    } else {
        m_sets.append({QStringLiteral("Trainers"), "include/constants/opponents.h", "src/data/trainers.h", "src/data/trainer_parties.h", false});
    }
    for (const auto &set : m_sets) {
        ensureFile(set.idsFile);
        ensureFile(set.tableFile);
        if (!set.partiesFile.isEmpty()) ensureFile(set.partiesFile);
    }
    readConstants();
    return true;
}

bool TrainerProject::ensureFile(const QString &file, QString *error) {
    if (m_files.contains(file)) return true;
    const QString path = m_root + '/' + file;
    const QFileInfo info(path);
    const auto fail = [&](const QString &message) {
        if (!m_unreadable.contains(file)) m_unreadable << file;
        if (error) *error = message;
        return false;
    };
    if (!info.exists()) return fail(QStringLiteral("%1 does not exist.").arg(file));
    if (info.isSymLink() || !info.canonicalFilePath().startsWith(m_root + '/'))
        return fail(QStringLiteral("%1 is a symlink or is outside the project.").arg(file));
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return fail(QStringLiteral("%1: %2").arg(file, input.errorString()));
    SourceFile source;
    source.original = input.readAll();
    source.bom = source.original.startsWith("\xEF\xBB\xBF");
    const QByteArray utf8 = source.bom ? source.original.mid(3) : source.original;
    source.text = QString::fromUtf8(utf8);
    if (source.text.toUtf8() != utf8) return fail(QStringLiteral("%1 is not UTF-8 text.").arg(file));
    source.crlf = source.text.contains(QStringLiteral("\r\n"));
    source.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    m_files.insert(file, source);
    m_unreadable.removeAll(file);
    return true;
}

int TrainerProject::lineOf(const QString &file, int offset) const {
    const auto it = m_files.constFind(file);
    if (it == m_files.cend()) return 0;
    return QStringView(it->text).left(offset).count('\n') + 1;
}

void TrainerProject::readConstants() {
    m_classes.clear(); m_pics.clear(); m_musics.clear(); m_species.clear();
    m_nameLimit = 0;
    const auto read = [&](const QString &file) {
        QFile input(m_root + '/' + file);
        return input.open(QIODevice::ReadOnly) ? maskComments(QString::fromUtf8(input.readAll())) : QString();
    };
    const QString constants = read("include/constants/trainers.h");
    // Both #define lists and enum members declare these constants.
    const QRegularExpression declaration(QStringLiteral("^\\s*(?:#define\\s+)?((?:TRAINER_CLASS|TRAINER_PIC|TRAINER_ENCOUNTER_MUSIC)_[A-Z0-9_]+)\\b"),
                                         QRegularExpression::MultilineOption);
    auto matches = declaration.globalMatch(constants);
    while (matches.hasNext()) {
        const QString name = matches.next().captured(1);
        if (name.endsWith("_COUNT")) continue;
        if (name.startsWith("TRAINER_CLASS_")) m_classes << name;
        else if (name.startsWith("TRAINER_PIC_")) m_pics << name;
        else m_musics << name;
    }
    m_classes.removeDuplicates(); m_pics.removeDuplicates(); m_musics.removeDuplicates();
    auto species = QRegularExpression(QStringLiteral("^\\s*#define\\s+(SPECIES_[A-Z0-9_]+)\\b"), QRegularExpression::MultilineOption)
                       .globalMatch(read("include/constants/species.h"));
    while (species.hasNext()) m_species.insert(species.next().captured(1));
    const auto limit = QRegularExpression(QStringLiteral("^\\s*#define\\s+TRAINER_NAME_LENGTH\\s+(\\d+)"), QRegularExpression::MultilineOption)
                           .match(read("include/constants/global.h"));
    if (limit.hasMatch()) m_nameLimit = limit.captured(1).toInt();
}

// ---------------------------------------------------------------- Parsing

void TrainerProject::parseDefines(int setIndex, Parsed &parsed, QVector<TrainerIssue> *issues) {
    const QString file = m_sets[setIndex].idsFile;
    if (!m_files.contains(file)) return;
    const QString mask = maskComments(text(file));
    static const QRegularExpression define(QStringLiteral("^[ \\t]*#[ \\t]*define[ \\t]+([A-Za-z_][A-Za-z0-9_]*)[ \\t]+([^\\n]*?)[ \\t]*$"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression number(QStringLiteral("^(?:0[xX][0-9a-fA-F]+|\\d+)$"));
    static const QStringList countNames = {"TRAINERS_COUNT", "NUM_TRAINERS", "TRAINERS_COUNT_EMERALD", "TRAINERS_COUNT_FRLG"};
    static const QStringList maxNames = {"MAX_TRAINERS_COUNT", "MAX_TRAINERS_COUNT_EMERALD", "MAX_TRAINERS_COUNT_FRLG"};
    QHash<QString, int> seenNames;
    QHash<int, QString> seenValues;
    auto matches = define.globalMatch(mask);
    while (matches.hasNext()) {
        const auto match = matches.next();
        Define item;
        item.name = match.captured(1);
        item.value = match.captured(2).trimmed();
        item.numeric = number.match(item.value).hasMatch();
        item.number = item.numeric ? item.value.toInt(nullptr, 0) : 0;
        item.lineStart = match.capturedStart();
        item.lineEnd = match.capturedEnd();
        item.valueStart = match.capturedStart(2);
        item.valueEnd = match.capturedEnd(2);
        item.line = lineOf(file, item.lineStart);
        if (countNames.contains(item.name) && item.numeric) { parsed.count = item.number; parsed.countDefine = item; continue; }
        if (maxNames.contains(item.name) && item.numeric) { parsed.maximum = item.number; continue; }
        if (!item.name.startsWith("TRAINER_")) continue;
        if (seenNames.contains(item.name)) {
            if (issues) {
                const Define first = parsed.defines[seenNames[item.name]];
                auto issue = makeIssue("define-duplicate|" + item.name, item.name, file, item.line,
                    QStringLiteral("%1 is defined twice").arg(item.name),
                    QStringLiteral("It is also defined on line %1. C will not compile two different values for one name.").arg(first.line));
                if (first.value == item.value) {
                    const int end = qMin(item.lineEnd + 1, text(file).size());
                    withFix(issue, QStringLiteral("Remove the repeated define."), {{file, item.lineStart, end, {}}});
                }
                issues->append(issue);
            }
            continue;
        }
        seenNames.insert(item.name, parsed.defines.size());
        parsed.defines.append(item);
    }
    if (!issues) return;
    for (const auto &item : parsed.defines) {
        if (!item.numeric) continue;
        if (seenValues.contains(item.number)) {
            auto issue = makeIssue("id-duplicate|" + item.name, item.name, file, item.line,
                QStringLiteral("%1 reuses ID %2").arg(item.name).arg(item.number),
                QStringLiteral("%1 already uses this number, so both names would load the same trainer.").arg(seenValues[item.number]));
            if (canAllocate(parsed)) {
                const int id = nextId(parsed);
                QVector<TrainerTextEdit> edits{{file, item.valueStart, item.valueEnd, QString::number(id)}};
                if (parsed.count >= 0 && parsed.count <= id)
                    edits.append({file, parsed.countDefine.valueStart, parsed.countDefine.valueEnd, QString::number(id + 1)});
                withFix(issue, QStringLiteral("Give %1 the unused ID %2.").arg(item.name).arg(id), edits);
            }
            issues->append(issue);
        } else {
            seenValues.insert(item.number, item.name);
        }
    }
}

void TrainerProject::parseTable(int setIndex, Parsed &parsed, QVector<TrainerIssue> *issues) {
    const QString file = m_sets[setIndex].tableFile;
    if (!m_files.contains(file)) return;
    const QString source = text(file), mask = maskC(source);
    static const QRegularExpression header(QStringLiteral("\\bstruct\\s+Trainer\\s+[A-Za-z_][A-Za-z0-9_]*\\s*\\[[^\\]]*\\]\\s*=\\s*\\{"));
    const auto table = header.match(mask);
    if (!table.hasMatch()) {
        if (issues) issues->append(makeIssue("table-header|" + file, {}, file, 1, QStringLiteral("The trainer table declaration is missing or damaged"),
            QStringLiteral("Expected a line like: const struct Trainer gTrainers[] = {")));
        return;
    }
    parsed.tableOpen = table.capturedEnd() - 1;
    parsed.tableClose = -1;
    for (int i = mask.lastIndexOf(';'); i > parsed.tableOpen; i = mask.lastIndexOf(';', i - 1)) {
        const int before = previousToken(mask, i);
        if (before > parsed.tableOpen && mask[before] == '}') { parsed.tableClose = before; break; }
    }
    const int tableLimit = parsed.tableClose >= 0 ? parsed.tableClose : source.size();

    static const QRegularExpression entryHeader(QStringLiteral("\\[\\s*(TRAINER_[A-Za-z0-9_]+)\\s*\\]\\s*="));
    QVector<QRegularExpressionMatch> headers;
    auto matches = entryHeader.globalMatch(mask, parsed.tableOpen);
    while (matches.hasNext()) {
        auto match = matches.next();
        if (match.capturedStart() < tableLimit) headers.append(match);
    }

    // Conditional compilation can legitimately define one trainer twice.
    QVector<int> conditionalDepth(source.size() + 1, 0);
    int depthNow = 0, scan = 0;
    for (const auto &line : source.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith("#if")) ++depthNow;
        for (int i = scan; i <= scan + line.size() && i < conditionalDepth.size(); ++i) conditionalDepth[i] = depthNow;
        if (trimmed.startsWith("#endif")) --depthNow;
        scan += line.size() + 1;
    }

    QHash<QString, int> seen;
    bool previousUnclosed = false;
    for (int h = 0; h < headers.size(); ++h) {
        const auto &match = headers[h];
        Entry entry;
        entry.id = match.captured(1);
        entry.start = match.capturedStart();
        entry.line = lineOf(file, entry.start);
        entry.conditional = conditionalDepth.value(entry.start) > 0;
        const int limit = h + 1 < headers.size() ? headers[h + 1].capturedStart() : tableLimit;
        const int last = previousToken(mask, limit);

        if (issues && !previousUnclosed) {
            const int before = previousToken(mask, entry.start);
            if (before >= 0 && mask[before] != ',' && before != parsed.tableOpen) {
                auto issue = makeIssue("entry-comma|" + entry.id, entry.id, file, lineOf(file, before),
                    QStringLiteral("Missing comma before %1").arg(entry.id),
                    QStringLiteral("Entries in the trainer table must be separated by commas."));
                withFix(issue, QStringLiteral("Add the missing comma."), {{file, before + 1, before + 1, QStringLiteral(",")}});
                issues->append(issue);
            }
        }

        int cursor = nextToken(mask, match.capturedEnd(), limit);
        const bool hasOpen = cursor < limit && mask[cursor] == '{';
        if (issues && !hasOpen) {
            auto issue = makeIssue("entry-open|" + entry.id, entry.id, file, entry.line,
                QStringLiteral("%1 is missing its opening brace").arg(entry.id),
                QStringLiteral("Trainer fields must be wrapped in { }."));
            withFix(issue, QStringLiteral("Add the opening brace."), {{file, int(match.capturedEnd()), int(match.capturedEnd()), QStringLiteral(" {")}});
            issues->append(issue);
        }
        int depth = hasOpen ? 0 : 1;
        QString openers = hasOpen ? QString() : QStringLiteral("{");
        int close = -1;
        Field *field = nullptr;
        QVector<Field> fields;
        for (int i = hasOpen ? cursor : match.capturedEnd(); i < limit; ++i) {
            const QChar c = mask[i];
            if (c == '{' || c == '(' || c == '[') { ++depth; openers += c; }
            else if (c == '}' || c == ')' || c == ']') {
                --depth;
                openers.chop(1);
                if (depth == 0) { close = i; break; }
            } else if (c == '.' && depth >= 1 && i + 1 < limit && (mask[i + 1].isLetter() || mask[i + 1] == '_')) {
                int nameEnd = i + 1;
                while (nameEnd < limit && (mask[nameEnd].isLetterOrNumber() || mask[nameEnd] == '_')) ++nameEnd;
                const int equals = nextToken(mask, nameEnd, limit);
                if (equals < limit && mask[equals] == '=') {
                    const QString name = mask.mid(i + 1, nameEnd - i - 1);
                    // A trainer field on its own line can't be nested, so the field
                    // before it left a bracket open.
                    static const QSet<QString> topLevel = {"partyFlags", "trainerClass", "encounterMusic_gender", "trainerPic", "trainerName",
                                                           "items", "doubleBattle", "aiFlags", "partySize", "party"};
                    const int lineStart = mask.lastIndexOf('\n', i) + 1;
                    if (depth > 1 && topLevel.contains(name) && mask.mid(lineStart, i - lineStart).trimmed().isEmpty()) {
                        QString closers;
                        for (int o = openers.size() - 1; o >= 1; --o)
                            closers += openers[o] == '{' ? '}' : openers[o] == '(' ? ')' : ']';
                        if (issues) {
                            const int before = previousToken(mask, i);
                            const int at = mask[before] == ',' ? before : before + 1;
                            auto issue = makeIssue("field-bracket|" + entry.id + "|" + name, entry.id, file, lineOf(file, before),
                                QStringLiteral("Unclosed bracket before .%1 in %2").arg(name, entry.id),
                                QStringLiteral("The previous field opens a bracket that is never closed."));
                            withFix(issue, QStringLiteral("Add the missing %1.").arg(closers), {{file, at, at, closers}});
                            issues->append(issue);
                        }
                        depth = 1;
                        openers = openers.left(1);
                    }
                    if (depth != 1) continue;
                    if (issues && !fields.isEmpty()) {
                        const int before = previousToken(mask, i);
                        if (before >= 0 && mask[before] != ',' && mask[before] != '{') {
                            auto issue = makeIssue("field-comma|" + entry.id + "|" + name, entry.id, file, lineOf(file, before),
                                QStringLiteral("Missing comma before .%1 in %2").arg(name, entry.id),
                                QStringLiteral("Fields in a trainer entry must be separated by commas."));
                            withFix(issue, QStringLiteral("Add the missing comma."), {{file, before + 1, before + 1, QStringLiteral(",")}});
                            issues->append(issue);
                        }
                    }
                    Field next;
                    next.name = name;
                    next.start = i;
                    next.valueStart = nextToken(mask, equals + 1, limit);
                    fields.append(next);
                    field = &fields.last();
                    i = equals;
                }
            }
            if (field && depth == 1 && (c == ',' || c == '\n')) {
                const int end = previousToken(mask, i) + 1;
                if (c == ',' || end > field->valueStart) { field->valueEnd = qMax(end, field->valueStart); if (c == ',') field = nullptr; }
            }
        }
        if (field && field->valueEnd <= field->valueStart)
            field->valueEnd = previousToken(mask, close >= 0 ? close : limit) + 1;
        entry.fields = fields;

        previousUnclosed = close < 0;
        if (close < 0) {
            entry.end = last + 1;
            if (issues && last >= 0) {
                const QString closing = QString(qMax(depth, 1), '}') + (mask[last] == ',' ? QString() : QString());
                const int at = mask[last] == ',' ? last : last + 1;
                const QString insert = (mask[last] == ',' ? QStringLiteral(",\n    ") : QStringLiteral("\n    ")) + closing + QStringLiteral(",");
                auto issue = makeIssue("entry-close|" + entry.id, entry.id, file, lineOf(file, last),
                    QStringLiteral("%1 is missing its closing brace").arg(entry.id),
                    QStringLiteral("The entry runs into the next trainer because a } is missing."));
                withFix(issue, QStringLiteral("Close the entry."), {{file, at, last + 1, insert}});
                issues->append(issue);
            }
        } else {
            int after = nextToken(mask, close + 1, limit);
            entry.end = close + 1;
            if (after < limit && mask[after] == ',') { entry.end = after + 1; after = nextToken(mask, after + 1, limit); }
            if (issues && after < limit) {
                const bool braces = mask.mid(after, limit - after).remove(QRegularExpression(QStringLiteral("[\\s,;}]"))).isEmpty();
                auto issue = makeIssue("entry-extra|" + entry.id, entry.id, file, lineOf(file, after),
                    QStringLiteral("Unexpected text after %1").arg(entry.id),
                    QStringLiteral("Found extra source between this entry and the next one."));
                if (braces) withFix(issue, QStringLiteral("Remove the stray closing brace."), {{file, entry.end, last + 1, {}}});
                issues->append(issue);
            }
        }

        if (seen.contains(entry.id) && !entry.conditional && !parsed.entries[seen[entry.id]].conditional) {
            if (issues) {
                const Entry first = parsed.entries[seen[entry.id]];
                auto issue = makeIssue("entry-duplicate|" + entry.id, entry.id, file, entry.line,
                    QStringLiteral("%1 has two table entries").arg(entry.id),
                    QStringLiteral("The first entry is on line %1. Only one can take effect.").arg(first.line));
                if (source.mid(first.start, first.end - first.start) == source.mid(entry.start, entry.end - entry.start)) {
                    int removeEnd = entry.end;
                    while (removeEnd < source.size() && source[removeEnd].isSpace()) ++removeEnd;
                    withFix(issue, QStringLiteral("Remove the identical repeated entry."), {{file, entry.start, removeEnd, {}}});
                }
                issues->append(issue);
            }
        } else if (!seen.contains(entry.id)) {
            seen.insert(entry.id, parsed.entries.size());
        }
        parsed.entries.append(entry);
    }
    parsed.insertAt = parsed.entries.isEmpty() ? parsed.tableOpen + 1 : parsed.entries.last().end;
    if (issues && parsed.tableClose < 0) {
        auto issue = makeIssue("table-close|" + file, {}, file, lineOf(file, source.size()),
            QStringLiteral("The trainer table is never closed"), QStringLiteral("The file must end the table with };"));
        const QString ending = source.endsWith('\n') ? QStringLiteral("};\n") : QStringLiteral("\n};\n");
        withFix(issue, QStringLiteral("Add the closing }; at the end of the file."), {{file, int(source.size()), int(source.size()), ending}});
        issues->append(issue);
    }
}

void TrainerProject::parseParties(int setIndex, Parsed &parsed, QVector<TrainerIssue> *issues) {
    const QString file = m_sets[setIndex].partiesFile;
    if (file.isEmpty() || !m_files.contains(file)) return;
    const QString source = text(file), mask = maskC(source);
    static const QRegularExpression declaration(QStringLiteral("\\bstruct\\s+(TrainerMon[A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\[[^\\]]*\\]\\s*="));
    QVector<QRegularExpressionMatch> matches;
    auto found = declaration.globalMatch(mask);
    while (found.hasNext()) matches.append(found.next());
    for (int m = 0; m < matches.size(); ++m) {
        const auto &match = matches[m];
        // A declaration's "static const" belongs to it, not to the previous party.
        int limit = source.size();
        if (m + 1 < matches.size()) {
            limit = matches[m + 1].capturedStart();
            const int lineStart = mask.lastIndexOf('\n', limit) + 1;
            if (mask.mid(lineStart, limit - lineStart).trimmed().remove(QRegularExpression(QStringLiteral("\\b(?:static|const)\\b"))).trimmed().isEmpty())
                limit = lineStart;
        }
        Party party;
        party.type = match.captured(1);
        party.name = match.captured(2);
        party.start = match.capturedStart();
        party.line = lineOf(file, party.start);
        int depth = 0, close = -1, lastMember = -1;
        const int open = nextToken(mask, match.capturedEnd(), limit);
        if (open >= limit || mask[open] != '{') {
            if (issues) issues->append(makeIssue("party-open|" + party.name, {}, file, party.line,
                QStringLiteral("%1 is missing its opening brace").arg(party.name), QStringLiteral("The party must be written as = { ... };")));
            party.sizeKnown = false;
            party.end = previousToken(mask, limit) + 1;
            parsed.parties.append(party);
            continue;
        }
        for (int i = open; i < limit; ++i) {
            const QChar c = mask[i];
            if (c == '{' || c == '(' || c == '[') {
                if (c == '{' && depth == 1) {
                    if (lastMember >= 0 && issues) {
                        const int before = previousToken(mask, i);
                        if (mask[before] == '}') {
                            auto issue = makeIssue(QStringLiteral("party-member-comma|%1|%2").arg(party.name).arg(party.members), {}, file, lineOf(file, before),
                                QStringLiteral("Missing comma between Pokémon in %1").arg(party.name), QStringLiteral("Party members must be separated by commas."));
                            withFix(issue, QStringLiteral("Add the missing comma."), {{file, before + 1, before + 1, QStringLiteral(",")}});
                            issues->append(issue);
                        }
                    }
                    ++party.members;
                    lastMember = i;
                }
                ++depth;
            } else if (c == '}' || c == ')' || c == ']') {
                if (--depth == 0) { close = i; break; }
            } else if (depth == 1 && (c.isLetter() || c == '_')) {
                // A macro such as DUMMY_TRAINER_MON can expand to any number of members.
                party.sizeKnown = false;
                while (i + 1 < limit && (mask[i + 1].isLetterOrNumber() || mask[i + 1] == '_')) ++i;
            }
        }
        const int last = previousToken(mask, limit);
        if (close < 0) {
            party.end = last + 1;
            if (issues) {
                const QString closing = (depth > 1 ? QStringLiteral("\n    }") : QString()) + QStringLiteral("\n};");
                auto issue = makeIssue("party-close|" + party.name, {}, file, lineOf(file, last),
                    QStringLiteral("%1 is missing its closing brace").arg(party.name),
                    QStringLiteral("The party runs into the next declaration because a } is missing."));
                if (depth <= 2) withFix(issue, QStringLiteral("Close the party."), {{file, last + 1, last + 1, closing}});
                issues->append(issue);
            }
        } else {
            const int semicolon = nextToken(mask, close + 1, limit);
            party.end = close + 1;
            if (semicolon < limit && mask[semicolon] == ';') party.end = semicolon + 1;
            else if (issues) {
                auto issue = makeIssue("party-semicolon|" + party.name, {}, file, lineOf(file, close),
                    QStringLiteral("Missing semicolon after %1").arg(party.name), QStringLiteral("Each party declaration must end with };"));
                withFix(issue, QStringLiteral("Add the missing semicolon."), {{file, close + 1, close + 1, QStringLiteral(";")}});
                issues->append(issue);
            }
        }
        parsed.parties.append(party);
    }
}

void TrainerProject::parsePartyFile(int setIndex, Parsed &parsed, QVector<TrainerIssue> *issues) {
    const QString file = m_sets[setIndex].tableFile;
    if (!m_files.contains(file)) return;
    const QString source = text(file), mask = maskBlockComments(source);
    static const QRegularExpression strict(QStringLiteral("^[ \\t]*===[ \\t]+(TRAINER_[A-Za-z0-9_]+)[ \\t]+===[ \\t]*$"));
    static const QRegularExpression loose(QStringLiteral("^[ \\t]*=+[ \\t]*(TRAINER_[A-Za-z0-9_]+)[ \\t]*=*[ \\t]*$"));
    struct Line { int start, end; QString text; };
    QVector<Line> lines;
    for (int start = 0; start <= mask.size();) {
        int end = mask.indexOf('\n', start);
        if (end < 0) end = mask.size();
        lines.append({start, end, mask.mid(start, end - start).trimmed()});
        if (end >= mask.size()) break;
        start = end + 1;
    }
    QVector<int> headers;
    for (int i = 0; i < lines.size(); ++i) {
        const QString raw = mask.mid(lines[i].start, lines[i].end - lines[i].start);
        if (strict.match(raw).hasMatch()) { headers.append(i); continue; }
        const auto bad = loose.match(raw);
        if (bad.hasMatch()) {
            headers.append(i);
            if (issues) {
                auto issue = makeIssue("party-header|" + bad.captured(1), bad.captured(1), file, i + 1,
                    QStringLiteral("Malformed section header for %1").arg(bad.captured(1)),
                    QStringLiteral("Sections must be written exactly as === %1 ===").arg(bad.captured(1)));
                withFix(issue, QStringLiteral("Rewrite the header."), {{file, lines[i].start, lines[i].end, QStringLiteral("=== %1 ===").arg(bad.captured(1))}});
                issues->append(issue);
            }
        }
    }
    QHash<QString, int> seen;
    static const QRegularExpression idPattern(QStringLiteral("(TRAINER_[A-Za-z0-9_]+)"));
    for (int h = 0; h < headers.size(); ++h) {
        const int first = headers[h], limit = h + 1 < headers.size() ? headers[h + 1] : lines.size();
        Entry entry;
        entry.id = idPattern.match(lines[first].text).captured(1);
        entry.start = lines[first].start;
        entry.line = first + 1;
        int lastContent = first;
        for (int i = first + 1; i < limit; ++i) if (!lines[i].text.isEmpty()) lastContent = i;
        entry.end = lines[lastContent].end;

        QHash<QString, int> attributes;
        int mons = 0, i = first + 1;
        bool partySize = false;
        for (; i < limit && !lines[i].text.isEmpty(); ++i) {
            const QString line = lines[i].text;
            if (line.startsWith('#')) continue;
            const int colon = line.indexOf(':');
            if (colon < 0) {
                if (issues) {
                    auto issue = makeIssue("party-blank|" + entry.id, entry.id, file, i + 1,
                        QStringLiteral("%1 needs a blank line before its Pokémon").arg(entry.id),
                        QStringLiteral("trainerproc requires a blank line between trainer fields and the party."));
                    withFix(issue, QStringLiteral("Insert the blank line."), {{file, lines[i].start, lines[i].start, QStringLiteral("\n")}});
                    issues->append(issue);
                }
                ++mons;
                while (i < limit && !lines[i].text.isEmpty()) ++i;
                break;
            }
            const QString key = line.left(colon).trimmed();
            attributes.insert(key, i);
            if (key == "Party Size") partySize = true;
        }
        for (bool inMon = false; i < limit; ++i) {
            if (lines[i].text.isEmpty()) { inMon = false; continue; }
            if (lines[i].text.startsWith('#')) continue;
            if (!inMon) { ++mons; inMon = true; }
        }
        if (issues && entry.id != "TRAINER_NONE") {
            for (const QString &required : {QStringLiteral("Name"), QStringLiteral("Pic")}) {
                if (attributes.contains(required)) continue;
                const QString value = required == "Name" ? camelName(entry.id).toUpper().left(m_nameLimit ? m_nameLimit : 10)
                                                         : humanName(m_pics.value(1, "TRAINER_PIC_HIKER"), "TRAINER_PIC_");
                auto issue = makeIssue("party-required|" + entry.id + "|" + required, entry.id, file, entry.line,
                    QStringLiteral("%1 has no %2 field").arg(entry.id, required),
                    QStringLiteral("trainerproc stops the build when a trainer has no %1.").arg(required));
                withFix(issue, QStringLiteral("Add \"%1: %2\".").arg(required, value), {{file, lines[first].end, lines[first].end, QStringLiteral("\n%1: %2").arg(required, value)}});
                issues->append(issue);
            }
            if (mons == 0) {
                auto issue = makeIssue("party-empty|" + entry.id, entry.id, file, entry.line,
                    QStringLiteral("%1 has no Pokémon").arg(entry.id),
                    QStringLiteral("A trainer needs at least one Pokémon to battle."));
                const QString species = humanName(placeholderSpecies(), "SPECIES_");
                withFix(issue, QStringLiteral("Add a level 5 %1 as a placeholder.").arg(species),
                        {{file, entry.end, entry.end, QStringLiteral("\n\n%1\nLevel: 5\nIVs: 0 HP / 0 Atk / 0 Def / 0 SpA / 0 SpD / 0 Spe").arg(species)}});
                issues->append(issue);
            } else if (mons > kPartySize && !partySize) {
                issues->append(makeIssue("party-large|" + entry.id, entry.id, file, entry.line,
                    QStringLiteral("%1 has %2 Pokémon").arg(entry.id).arg(mons),
                    QStringLiteral("Parties hold at most %1. Remove some in the Battle tab, or add a Party Size field if this is a pool.").arg(kPartySize)));
            }
            const auto checkConstant = [&](const QString &key, const QString &prefix, const QStringList &known) {
                if (!attributes.contains(key) || known.isEmpty()) return;
                const QString line = lines[attributes[key]].text;
                const QString value = line.mid(line.indexOf(':') + 1).trimmed();
                if (value.isEmpty() || known.contains(trainerproc(value, prefix))) return;
                issues->append(makeIssue("constant|" + entry.id + "|" + key, entry.id, file, attributes[key] + 1,
                    QStringLiteral("%1 uses an unknown %2: %3").arg(entry.id, key, value),
                    QStringLiteral("%1 is not defined in include/constants/trainers.h.").arg(trainerproc(value, prefix))));
            };
            checkConstant("Class", "TRAINER_CLASS_", m_classes);
            checkConstant("Pic", "TRAINER_PIC_", m_pics);
            checkConstant("Music", "TRAINER_ENCOUNTER_MUSIC_", m_musics);
            if (m_nameLimit > 0 && attributes.contains("Name")) {
                const QString line = lines[attributes["Name"]].text;
                const QString name = line.mid(line.indexOf(':') + 1).trimmed();
                if (name.size() > m_nameLimit && !name.contains('{'))
                    issues->append(makeIssue("name-long|" + entry.id, entry.id, file, attributes["Name"] + 1,
                        QStringLiteral("%1's name is too long").arg(entry.id),
                        QStringLiteral("\"%1\" has %2 characters, and the limit is %3.").arg(name).arg(name.size()).arg(m_nameLimit),
                        TrainerIssue::Warning));
            }
        }
        if (seen.contains(entry.id)) {
            if (issues) {
                const Entry original = parsed.entries[seen[entry.id]];
                auto issue = makeIssue("entry-duplicate|" + entry.id, entry.id, file, entry.line,
                    QStringLiteral("%1 has two sections").arg(entry.id),
                    QStringLiteral("The first section is on line %1. trainerproc stops on duplicates.").arg(original.line));
                if (source.mid(original.start, original.end - original.start) == source.mid(entry.start, entry.end - entry.start)) {
                    int removeEnd = entry.end;
                    while (removeEnd < source.size() && source[removeEnd].isSpace()) ++removeEnd;
                    withFix(issue, QStringLiteral("Remove the identical repeated section."), {{file, entry.start, removeEnd, {}}});
                }
                issues->append(issue);
            }
        } else {
            seen.insert(entry.id, parsed.entries.size());
        }
        parsed.entries.append(entry);
    }
    parsed.insertAt = source.size();
}

TrainerProject::Parsed TrainerProject::parseSet(int setIndex, QVector<TrainerIssue> *issues) {
    Parsed parsed;
    parseDefines(setIndex, parsed, issues);
    if (m_sets[setIndex].partySource) {
        parsePartyFile(setIndex, parsed, issues);
    } else {
        parseTable(setIndex, parsed, issues);
        parseParties(setIndex, parsed, issues);
    }
    return parsed;
}

// ---------------------------------------------------------------- Cross-file checks

int TrainerProject::nextId(const Parsed &parsed) const {
    int id = qMax(parsed.count, 1);
    for (const auto &define : parsed.defines) if (define.numeric) id = qMax(id, define.number + 1);
    return id;
}

bool TrainerProject::canAllocate(const Parsed &parsed) const {
    return parsed.maximum < 0 || nextId(parsed) < parsed.maximum;
}

void TrainerProject::checkLinks(int setIndex, const Parsed &parsed, QVector<TrainerIssue> *issues) {
    const Set &set = m_sets[setIndex];
    QSet<QString> defined, tabled;
    for (const auto &define : parsed.defines) defined.insert(define.name);
    for (const auto &entry : parsed.entries) tabled.insert(entry.id);

    // Numbering.
    int highest = -1;
    for (const auto &define : parsed.defines) if (define.numeric) highest = qMax(highest, define.number);
    if (parsed.count >= 0 && highest >= 0 && parsed.count != highest + 1) {
        auto issue = makeIssue("count|" + set.idsFile, {}, set.idsFile, parsed.countDefine.line,
            QStringLiteral("%1 is %2, but the highest trainer ID is %3").arg(parsed.countDefine.name).arg(parsed.count).arg(highest),
            QStringLiteral("The count must be one more than the highest trainer ID."));
        withFix(issue, QStringLiteral("Set %1 to %2.").arg(parsed.countDefine.name).arg(highest + 1),
                {{set.idsFile, parsed.countDefine.valueStart, parsed.countDefine.valueEnd, QString::number(highest + 1)}});
        issues->append(issue);
    }
    if (parsed.maximum >= 0 && highest >= parsed.maximum)
        issues->append(makeIssue("max|" + set.idsFile, {}, set.idsFile, parsed.countDefine.line,
            QStringLiteral("Trainer ID %1 is beyond the limit of %2").arg(highest).arg(parsed.maximum),
            QStringLiteral("Each trainer uses a defeat flag. Raise MAX_TRAINERS_COUNT and free matching flag space in include/constants/flags.h, then rebuild. Raising the limit changes save data.")));

    // IDs and table entries must match one to one.
    for (const auto &entry : parsed.entries) {
        if (defined.contains(entry.id)) continue;
        auto issue = makeIssue("define-missing|" + entry.id, entry.id, set.tableFile, entry.line,
            QStringLiteral("%1 has no ID").arg(entry.id),
            QStringLiteral("It has trainer data, but no #define in %1.").arg(set.idsFile));
        if (canAllocate(parsed))
            withFix(issue, QStringLiteral("Define %1 as ID %2.").arg(entry.id).arg(nextId(parsed)), defineEdits(setIndex, parsed, entry.id));
        issues->append(issue);
    }
    for (const auto &define : parsed.defines) {
        if (!define.numeric || tabled.contains(define.name)) continue;
        if (parsed.tableOpen < 0 && !set.partySource) continue;
        auto issue = makeIssue("entry-missing|" + define.name, define.name, set.idsFile, define.line,
            QStringLiteral("%1 has no trainer data").arg(define.name),
            QStringLiteral("It has an ID, but no entry in %1.").arg(set.tableFile));
        withFix(issue, QStringLiteral("Add a placeholder trainer with a level 5 %1.").arg(humanName(placeholderSpecies(), "SPECIES_")),
                placeholderEdits(setIndex, parsed, define.name));
        issues->append(issue);
    }

    if (set.partySource) return;

    // Parties and the fields that reference them.
    QHash<QString, Party> parties;
    for (const auto &party : parsed.parties) {
        if (parties.contains(party.name))
            issues->append(makeIssue("party-duplicate|" + party.name, {}, set.partiesFile, party.line,
                QStringLiteral("%1 is declared twice").arg(party.name), QStringLiteral("Rename or remove one declaration.")));
        parties.insert(party.name, party);
    }
    static const QRegularExpression macroRef(QStringLiteral("^([A-Z_]+)\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)$"));
    static const QRegularExpression unionRef(QStringLiteral("^\\{\\s*\\.([A-Za-z]+)\\s*=\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*,?\\s*\\}$"));
    static const QRegularExpression arrayCount(QStringLiteral("^ARRAY_COUNT\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)$"));
    static const QRegularExpression stringName(QStringLiteral("_\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)"));
    const QString tableText = text(set.tableFile);
    QSet<QString> referenced, claimed;
    for (const auto &entry : parsed.entries) {
        for (const auto &field : entry.fields) {
            if (field.name != "party") continue;
            const QString value = tableText.mid(field.valueStart, field.valueEnd - field.valueStart).trimmed();
            const auto macro = macroRef.match(value);
            claimed.insert(macro.hasMatch() ? macro.captured(2) : unionRef.match(value).captured(2));
        }
    }
    for (const auto &entry : parsed.entries) {
        if (entry.id == "TRAINER_NONE") {
            for (const auto &field : entry.fields)
                if (field.name == "party") referenced.insert(tableText.mid(field.valueStart, field.valueEnd - field.valueStart));
            continue;
        }
        QHash<QString, Field> fields;
        for (const auto &field : entry.fields) fields.insert(field.name, field);
        const auto valueOf = [&](const QString &name) {
            const Field field = fields.value(name);
            return tableText.mid(field.valueStart, field.valueEnd - field.valueStart).trimmed();
        };
        const auto lineOfField = [&](const QString &name) { return lineOf(set.tableFile, fields.value(name).start); };

        const auto checkConstant = [&](const QString &field, const QString &prefix, const QStringList &known) {
            if (!fields.contains(field) || known.isEmpty()) return;
            auto tokens = QRegularExpression(QStringLiteral("\\b") + prefix + QStringLiteral("[A-Za-z0-9_]+")).globalMatch(valueOf(field));
            while (tokens.hasNext()) {
                const QString token = tokens.next().captured();
                if (known.contains(token)) continue;
                issues->append(makeIssue("constant|" + entry.id + "|" + field, entry.id, set.tableFile, lineOfField(field),
                    QStringLiteral("%1 uses an unknown constant: %2").arg(entry.id, token),
                    QStringLiteral("%1 is not defined in include/constants/trainers.h.").arg(token)));
            }
        };
        checkConstant("trainerClass", "TRAINER_CLASS_", m_classes);
        checkConstant("trainerPic", "TRAINER_PIC_", m_pics);
        checkConstant("encounterMusic_gender", "TRAINER_ENCOUNTER_MUSIC_", m_musics);

        if (m_nameLimit > 0 && fields.contains("trainerName")) {
            const QString name = stringName.match(valueOf("trainerName")).captured(1);
            if (name.size() > m_nameLimit && !name.contains('{'))
                issues->append(makeIssue("name-long|" + entry.id, entry.id, set.tableFile, lineOfField("trainerName"),
                    QStringLiteral("%1's name is too long").arg(entry.id),
                    QStringLiteral("\"%1\" has %2 characters, and the limit is %3.").arg(name).arg(name.size()).arg(m_nameLimit)));
        }

        if (!fields.contains("party")) {
            issues->append(makeIssue("party-field|" + entry.id, entry.id, set.tableFile, entry.line,
                QStringLiteral("%1 has no .party field").arg(entry.id),
                QStringLiteral("Without one, this trainer has no Pokémon to battle with.")));
            continue;
        }
        const QString partyValue = valueOf("party");
        const auto macro = macroRef.match(partyValue);
        const auto unionValue = unionRef.match(partyValue);
        const QString partyName = macro.hasMatch() ? macro.captured(2) : unionValue.captured(2);
        if (partyName.isEmpty() || partyName == "NULL") continue;
        referenced.insert(partyName);
        const Field partyField = fields.value("party");
        if (!parties.contains(partyName)) {
            auto issue = makeIssue("party-missing|" + entry.id, entry.id, set.tableFile, lineOfField("party"),
                QStringLiteral("%1 points to a party that does not exist: %2").arg(entry.id, partyName),
                QStringLiteral("%1 is not declared in %2.").arg(partyName, set.partiesFile));
            // A mistyped reference usually leaves the trainer's real party unused.
            const QString likely = QStringLiteral("sParty_") + camelName(entry.id);
            const QString likelyMacro = macroForPartyType(parties.value(likely).type);
            if (parties.contains(likely) && !claimed.contains(likely) && !likelyMacro.isEmpty()) {
                const QString value = QStringLiteral("%1(%2)").arg(likelyMacro, likely);
                withFix(issue, QStringLiteral("Point %1 at its unused party, %2.").arg(entry.id, likely),
                        {{set.tableFile, partyField.valueStart, partyField.valueEnd, value}});
                issues->append(issue);
                claimed.insert(likely);
                continue;
            }
            const QString type = macro.hasMatch() ? partyTypeForMacro(macro.captured(1)) : QStringLiteral("TrainerMon") + unionValue.captured(1);
            int at = 0;
            const QString block = appendText(set.partiesFile, renderParty(partyName, type.isEmpty() ? QStringLiteral("TrainerMonNoItemDefaultMoves") : type, false), &at);
            withFix(issue, QStringLiteral("Create %1 with a level 5 %2 as a placeholder.").arg(partyName, humanName(placeholderSpecies(), "SPECIES_")),
                    {{set.partiesFile, at, at, block}});
            issues->append(issue);
            continue;
        }
        const Party party = parties.value(partyName);
        const QString expected = macro.hasMatch() ? partyTypeForMacro(macro.captured(1)) : QStringLiteral("TrainerMon") + unionValue.captured(1);
        if (!expected.isEmpty() && expected != party.type) {
            auto issue = makeIssue("party-type|" + entry.id, entry.id, set.tableFile, lineOfField("party"),
                QStringLiteral("%1 reads %2 with the wrong party type").arg(entry.id, partyName),
                QStringLiteral("The trainer expects %1, but the party is declared as %2. The game would read its Pokémon data incorrectly.").arg(expected, party.type));
            const QString macroName = macroForPartyType(party.type);
            if (macro.hasMatch() && !macroName.isEmpty())
                withFix(issue, QStringLiteral("Change the trainer to %1(%2).").arg(macroName, partyName),
                        {{set.tableFile, partyField.valueStart, partyField.valueEnd, QStringLiteral("%1(%2)").arg(macroName, partyName)}});
            issues->append(issue);
        }
        if (party.sizeKnown && (party.members == 0 || party.members > kPartySize))
            issues->append(makeIssue("party-size|" + partyName, entry.id, set.partiesFile, party.line,
                QStringLiteral("%1 has %2 Pokémon").arg(partyName).arg(party.members),
                QStringLiteral("Parties need between 1 and %1 Pokémon. Edit this party in the Battle tab.").arg(kPartySize)));
        if (fields.contains("partySize")) {
            const QString sizeValue = valueOf("partySize");
            const auto counted = arrayCount.match(sizeValue);
            const bool wrongArray = counted.hasMatch() && counted.captured(1) != partyName;
            bool literal = false;
            const int size = sizeValue.toInt(&literal);
            if (wrongArray || (literal && party.sizeKnown && size != party.members)) {
                const Field sizeField = fields.value("partySize");
                auto issue = makeIssue("party-count|" + entry.id, entry.id, set.tableFile, lineOfField("partySize"),
                    QStringLiteral("%1 has the wrong party size").arg(entry.id),
                    QStringLiteral(".partySize is %1, but %2 has %3 Pokémon.").arg(sizeValue, partyName).arg(party.members));
                withFix(issue, QStringLiteral("Count the party automatically with ARRAY_COUNT(%1).").arg(partyName),
                        {{set.tableFile, sizeField.valueStart, sizeField.valueEnd, QStringLiteral("ARRAY_COUNT(%1)").arg(partyName)}});
                issues->append(issue);
            }
        }
    }
    if (parsed.tableOpen < 0) return;
    for (const auto &party : parsed.parties) {
        if (referenced.contains(party.name)) continue;
        issues->append(makeIssue("party-unused|" + party.name, {}, set.partiesFile, party.line,
            QStringLiteral("%1 is not used by any trainer").arg(party.name),
            QStringLiteral("Nothing battles with this party. Connect it to a trainer or delete it in the Battle tab."),
            TrainerIssue::Warning));
    }
}

void TrainerProject::checkScripts(QVector<TrainerIssue> *issues) {
    const ScriptIndex &index = scripts();
    QSet<QString> defined;
    for (int s = 0; s < m_sets.size(); ++s)
        for (const auto &define : parseSet(s, nullptr).defines) defined.insert(define.name);
    const Parsed target = parseSet(m_defaultSet, nullptr);
    QSet<QString> reported;
    for (const auto &call : index.calls) {
        for (const auto &trainer : call.trainers) {
            if (defined.contains(trainer) || reported.contains(trainer)) continue;
            reported.insert(trainer);
            auto issue = makeIssue("script-trainer|" + trainer, trainer, call.file, call.line,
                QStringLiteral("A script battles %1, which does not exist").arg(trainer),
                QStringLiteral("%1 is used by %2 but is not defined anywhere.").arg(trainer, call.command));
            if (canAllocate(target)) {
                auto edits = defineEdits(m_defaultSet, target, trainer);
                edits += placeholderEdits(m_defaultSet, target, trainer);
                withFix(issue, QStringLiteral("Create %1 as a placeholder trainer.").arg(trainer), edits);
            }
            issues->append(issue);
        }
        for (const auto &label : call.texts) {
            if (index.labels.contains(label) || reported.contains(label)) continue;
            reported.insert(label);
            auto issue = makeIssue("script-text|" + label, call.trainers.value(0), call.file, call.line,
                QStringLiteral("Battle text %1 does not exist").arg(label),
                QStringLiteral("%1 refers to this text label, but it is not defined in any script file.").arg(call.command));
            if (ensureFile(call.file)) {
                const bool pory = call.file.endsWith(".pory");
                const QString block = pory ? QStringLiteral("text %1 {\n    \"...\"\n}\n").arg(label)
                                           : QStringLiteral("%1:\n\t.string \"...$\"\n").arg(label);
                int at = 0;
                const QString insert = appendText(call.file, block, &at);
                withFix(issue, QStringLiteral("Add placeholder text to %1.").arg(call.file), {{call.file, at, at, insert}});
            }
            issues->append(issue);
        }
    }
}

const TrainerProject::ScriptIndex &TrainerProject::scripts() {
    if (m_scripts.valid) return m_scripts;
    m_scripts = {};
    m_scripts.valid = true;
    static const QRegularExpression battle(QStringLiteral("^\\s*(trainerbattle[a-z_]*)\\b\\s*\\(?(.*)$"));
    static const QRegularExpression poryLabel(QStringLiteral("^\\s*(?:script|text|movement|mart|mapscripts|raw)\\s+(?:\\([^)]*\\)\\s*)?([A-Za-z_][A-Za-z0-9_]*)"));
    static const QRegularExpression jump(QStringLiteral("^\\s*(?:goto|call|goto_if_[a-z]+|call_if_[a-z]+)\\b\\s*\\(?\\s*(.*)$"));
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QDirIterator it(m_root + "/data", {"*.inc", "*.s", "*.pory"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.suffix() == "inc" && QFileInfo::exists(info.path() + '/' + info.completeBaseName() + ".pory")) continue;
        const QString relative = QDir(m_root).relativeFilePath(path);
        QString content;
        if (m_files.contains(relative)) {
            content = m_files[relative].text;
        } else {
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly)) continue;
            content = QString::fromUtf8(input.readAll());
        }
        const bool pory = info.suffix() == "pory";
        QString label;
        int lineNumber = 0;
        for (const auto &rawView : QStringView(content).split('\n')) {
            ++lineNumber;
            const QStringView trimmed = rawView.trimmed();
            if (trimmed.isEmpty()) continue;
            const QChar first = trimmed.front();
            if (pory) {
                const auto match = poryLabel.matchView(rawView);
                if (match.hasMatch()) { label = match.captured(1); m_scripts.labels.insert(label); continue; }
            } else if ((first.isLetter() || first == '_')) {
                const int colon = trimmed.indexOf(':');
                if (colon > 0 && identifier.matchView(trimmed.left(colon)).hasMatch()) {
                    label = trimmed.left(colon).toString();
                    m_scripts.labels.insert(label);
                    continue;
                }
            }
            if (trimmed.contains(QLatin1String("trainerbattle"))) {
                const QString line = stripScriptComment(rawView.toString());
                const auto match = battle.match(line);
                if (!match.hasMatch()) continue;
                ScriptCall call;
                call.file = relative;
                call.command = match.captured(1);
                call.line = lineNumber;
                for (const auto &argument : splitArguments(match.captured(2))) {
                    if (argument.startsWith("TRAINER_") && !argument.startsWith("TRAINER_BATTLE_")) {
                        if (identifier.match(argument).hasMatch()) call.trainers << argument;
                    } else if (argument.contains("_Text_") && identifier.match(argument).hasMatch()) {
                        call.texts << argument;
                    }
                }
                if (!label.isEmpty()) for (const auto &trainer : call.trainers) m_scripts.trainers[label].insert(trainer);
                m_scripts.calls.append(call);
            } else if (!label.isEmpty() && (first == 'g' || first == 'c')) {
                const auto match = jump.match(stripScriptComment(rawView.toString()));
                if (!match.hasMatch()) continue;
                const auto arguments = splitArguments(match.captured(1));
                if (!arguments.isEmpty() && identifier.match(arguments.constLast()).hasMatch())
                    m_scripts.jumps[label].insert(arguments.constLast());
            }
        }
    }
    return m_scripts;
}

QStringList TrainerProject::trainersForScript(const QString &label) {
    const ScriptIndex &index = scripts();
    QSet<QString> visited, found;
    QStringList pending{label};
    for (int depth = 0; depth < 4 && !pending.isEmpty(); ++depth) {
        QStringList next;
        for (const auto &current : pending) {
            if (visited.contains(current)) continue;
            visited.insert(current);
            const auto trainers = index.trainers.value(current);
            found.unite(trainers);
            for (const auto &target : index.jumps.value(current)) next << target;
        }
        // Direct battles win over battles reached through shared helpers.
        if (!found.isEmpty()) break;
        pending = next;
    }
    QStringList result(found.cbegin(), found.cend());
    result.sort();
    return result;
}

bool TrainerProject::trainerExists(const QString &id) {
    for (int s = 0; s < m_sets.size(); ++s) {
        Parsed parsed;
        parseDefines(s, parsed, nullptr);
        for (const auto &define : parsed.defines) if (define.name == id) return true;
    }
    return false;
}

QStringList TrainerProject::problemsForTrainer(const QString &id) {
    QStringList problems;
    for (const auto &issue : analyze()) if (issue.trainer == id) problems << issue.summary;
    return problems;
}

QVector<TrainerIssue> TrainerProject::analyze() {
    QVector<TrainerIssue> issues;
    for (const auto &file : m_unreadable)
        issues.append(makeIssue("file|" + file, {}, file, 0, QStringLiteral("%1 could not be read").arg(file),
            QStringLiteral("The file is missing, is not UTF-8 text, or is outside the project.")));
    for (int s = 0; s < m_sets.size(); ++s) {
        const Parsed parsed = parseSet(s, &issues);
        checkLinks(s, parsed, &issues);
    }
    checkScripts(&issues);
    for (auto &issue : issues) {
        for (const auto &edit : issue.edits) {
            if (m_files.contains(edit.file)) continue;
            issue.edits.clear();
            issue.fix.clear();
            break;
        }
    }
    std::stable_sort(issues.begin(), issues.end(), [](const TrainerIssue &a, const TrainerIssue &b) {
        if (a.severity != b.severity) return a.severity < b.severity;
        if (a.file != b.file) return a.file < b.file;
        return a.line < b.line;
    });
    return issues;
}

// ---------------------------------------------------------------- Editing

void TrainerProject::applyEdits(const QVector<TrainerTextEdit> &edits) {
    QHash<QString, QVector<TrainerTextEdit>> byFile;
    for (const auto &edit : edits) byFile[edit.file].append(edit);
    for (auto it = byFile.begin(); it != byFile.end(); ++it) {
        auto &list = it.value();
        // Later offsets first. Reversing equal offsets keeps insertions in request order.
        std::reverse(list.begin(), list.end());
        std::stable_sort(list.begin(), list.end(), [](const TrainerTextEdit &a, const TrainerTextEdit &b) { return a.start > b.start; });
        QString &content = m_files[it.key()].text;
        for (const auto &edit : list) content.replace(edit.start, edit.end - edit.start, edit.text);
        if (it.key().startsWith("data/")) m_scripts.valid = false;
    }
}

int TrainerProject::applyFixes(const QSet<QString> &keys) {
    QSet<QString> done;
    // Fixes that touch the same text (each new ID updates the count) wait for a
    // later pass, where the rescan gives them fresh offsets and unused IDs.
    for (int pass = 0; pass <= keys.size(); ++pass) {
        QVector<TrainerTextEdit> batch;
        QSet<QString> batched;
        for (const auto &issue : analyze()) {
            if (!keys.contains(issue.key) || done.contains(issue.key) || !issue.fixable()) continue;
            bool overlaps = false;
            for (const auto &edit : issue.edits) {
                for (const auto &other : batch) {
                    if (edit.file != other.file) continue;
                    const bool sameInsert = edit.start == edit.end && other.start == other.end;
                    if (!sameInsert && edit.start <= other.end && other.start <= edit.end) overlaps = true;
                }
            }
            if (overlaps) continue;
            batch += issue.edits;
            batched.insert(issue.key);
        }
        if (batch.isEmpty()) break;
        applyEdits(batch);
        done.unite(batched);
    }
    return done.size();
}

QString TrainerProject::previewEdits(const TrainerIssue &issue) const {
    QStringList output;
    for (const auto &edit : issue.edits) {
        const QString content = text(edit.file);
        const int lineStart = edit.start > 0 ? content.lastIndexOf('\n', edit.start - 1) + 1 : 0;
        int lineEnd = content.indexOf('\n', edit.end);
        if (lineEnd < 0) lineEnd = content.size();
        const QString before = content.mid(lineStart, lineEnd - lineStart);
        const QString after = content.mid(lineStart, edit.start - lineStart) + edit.text + content.mid(edit.end, lineEnd - edit.end);
        output << QStringLiteral("%1:%2").arg(edit.file).arg(lineOf(edit.file, edit.start));
        if (edit.start != edit.end || !before.trimmed().isEmpty())
            for (const auto &line : before.split('\n')) output << "- " + line;
        for (const auto &line : after.split('\n')) output << "+ " + line;
        output << QString();
    }
    return output.join('\n').trimmed();
}

QVector<TrainerTextEdit> TrainerProject::defineEdits(int setIndex, const Parsed &parsed, const QString &id) const {
    const QString file = m_sets[setIndex].idsFile;
    const int number = nextId(parsed);
    QVector<TrainerTextEdit> edits;
    Define last;
    bool found = false;
    for (const auto &define : parsed.defines) if (define.numeric && (!found || define.lineStart > last.lineStart)) { last = define; found = true; }
    int column = found ? last.valueStart - last.lineStart : 0;
    QString line = QStringLiteral("#define ") + id + ' ';
    while (line.size() < column) line += ' ';
    line += QString::number(number);
    int at = found ? last.lineEnd : text(file).size();
    edits.append({file, at, at, '\n' + line});
    if (parsed.count >= 0 && parsed.count <= number)
        edits.append({file, parsed.countDefine.valueStart, parsed.countDefine.valueEnd, QString::number(number + 1)});
    return edits;
}

QString TrainerProject::placeholderSpecies() const {
    if (m_species.isEmpty() || m_species.contains(kPlaceholderSpecies)) return kPlaceholderSpecies;
    QStringList names(m_species.cbegin(), m_species.cend());
    names.removeAll("SPECIES_NONE");
    names.sort();
    return names.value(0, kPlaceholderSpecies);
}

QString TrainerProject::partyName(const Parsed &parsed, const QString &id) const {
    QSet<QString> used;
    for (const auto &party : parsed.parties) used.insert(party.name);
    const QString base = QStringLiteral("sParty_") + camelName(id);
    QString name = base;
    for (int suffix = 2; used.contains(name); ++suffix) name = base + QString::number(suffix);
    return name;
}

QString TrainerProject::renderParty(const QString &name, const QString &type, bool twoMembers) const {
    QString member = QStringLiteral("    {\n    .iv = 0,\n    .lvl = 5,\n    .species = %1,\n").arg(placeholderSpecies());
    if (type.startsWith("TrainerMonItem")) member += QStringLiteral("    .heldItem = ITEM_NONE,\n");
    if (type.endsWith("CustomMoves")) member += QStringLiteral("    .moves = {MOVE_TACKLE, MOVE_NONE, MOVE_NONE, MOVE_NONE},\n");
    member += QStringLiteral("    }");
    return QStringLiteral("static const struct %1 %2[] = {\n%3\n};\n").arg(type, name, twoMembers ? member + ",\n" + member : member);
}

// Returns block with the spacing it needs when appended, and where it belongs.
QString TrainerProject::appendText(const QString &file, const QString &block, int *offset) const {
    const QString content = text(file);
    int at = content.size();
    // Stay inside an include guard if the file ends with one.
    static const QRegularExpression guard(QStringLiteral("\\n[ \\t]*#endif[^\\n]*\\s*$"));
    const auto match = guard.match(content);
    if (match.hasMatch()) at = match.capturedStart() + 1;
    *offset = at;
    const QString before = content.left(at);
    QString prefix;
    if (!before.isEmpty() && !before.endsWith('\n')) prefix = QStringLiteral("\n\n");
    else if (!before.isEmpty() && !before.endsWith(QStringLiteral("\n\n"))) prefix = QStringLiteral("\n");
    return prefix + block + (at < content.size() ? QStringLiteral("\n") : QString());
}

NewTrainerSpec TrainerProject::placeholderSpec(int setIndex, const QString &id) const {
    NewTrainerSpec spec;
    spec.set = setIndex;
    spec.id = id;
    QString name = id.mid(QStringLiteral("TRAINER_").size());
    name.remove(QRegularExpression(QStringLiteral("_\\d+$")));
    spec.name = name.replace('_', ' ').left(m_nameLimit > 0 ? m_nameLimit : 10).trimmed();
    spec.trainerClass = m_classes.value(1, m_classes.value(0, QStringLiteral("TRAINER_CLASS_PKMN_TRAINER_1")));
    spec.pic = m_pics.value(1, m_pics.value(0, QStringLiteral("TRAINER_PIC_HIKER")));
    spec.music = m_musics.value(0, QStringLiteral("TRAINER_ENCOUNTER_MUSIC_MALE"));
    return spec;
}

QVector<TrainerTextEdit> TrainerProject::placeholderEdits(int setIndex, const Parsed &parsed, const QString &id) const {
    return entryEdits(setIndex, parsed, placeholderSpec(setIndex, id));
}

QVector<TrainerTextEdit> TrainerProject::entryEdits(int setIndex, const Parsed &parsed, const NewTrainerSpec &spec) const {
    const Set &set = m_sets[setIndex];
    const QString species = placeholderSpecies();
    if (set.partySource) {
        const QString mon = QStringLiteral("%1\nLevel: 5\nIVs: 0 HP / 0 Atk / 0 Def / 0 SpA / 0 SpD / 0 Spe\n").arg(humanName(species, "SPECIES_"));
        QString block = QStringLiteral("=== %1 ===\nName: %2\nClass: %3\nPic: %4\nGender: %5\nMusic: %6\nDouble Battle: %7\n\n")
            .arg(spec.id, spec.name, humanName(spec.trainerClass, "TRAINER_CLASS_"), humanName(spec.pic, "TRAINER_PIC_"),
                 spec.female ? "Female" : "Male", humanName(spec.music, "TRAINER_ENCOUNTER_MUSIC_"), spec.doubleBattle ? "Yes" : "No");
        block += mon;
        if (spec.doubleBattle) block += '\n' + mon;
        const QString content = text(set.tableFile);
        QString prefix = content.endsWith(QStringLiteral("\n\n")) ? QString() : content.endsWith('\n') ? QStringLiteral("\n") : QStringLiteral("\n\n");
        return {{set.tableFile, int(content.size()), int(content.size()), prefix + block}};
    }

    const QString party = partyName(parsed, spec.id);
    const QString music = spec.female ? QStringLiteral("F_TRAINER_FEMALE | ") + spec.music : spec.music;
    const QString escapedName = QString(spec.name).replace('\\', "\\\\").replace('"', "\\\"");
    const QHash<QString, QString> values = {
        {"trainerClass", spec.trainerClass},
        {"encounterMusic_gender", music},
        {"trainerPic", spec.pic},
        {"trainerName", QStringLiteral("_(\"%1\")").arg(escapedName)},
        {"items", QStringLiteral("{}")},
        {"doubleBattle", spec.doubleBattle ? QStringLiteral("TRUE") : QStringLiteral("FALSE")},
        {"aiFlags", QStringLiteral("AI_SCRIPT_CHECK_BAD_MOVE")},
        {"party", QStringLiteral("NO_ITEM_DEFAULT_MOVES(%1)").arg(party)},
    };
    const QString table = text(set.tableFile);
    QString entryText = QStringLiteral("    [%1] =\n    {\n        .trainerClass = %2,\n        .encounterMusic_gender = %3,\n        .trainerPic = %4,\n        .trainerName = %5,\n        .items = {},\n        .doubleBattle = %6,\n        .aiFlags = AI_SCRIPT_CHECK_BAD_MOVE,\n        .party = %7,\n    },")
        .arg(spec.id, spec.trainerClass, music, spec.pic, values["trainerName"], values["doubleBattle"], values["party"]);
    QString separator = QStringLiteral("\n\n");
    // Copy the layout of an existing trainer so new entries match the project's style.
    for (int i = parsed.entries.size() - 1; i >= 0; --i) {
        const Entry &entry = parsed.entries[i];
        QSet<QString> names;
        for (const auto &field : entry.fields) names.insert(field.name);
        bool complete = true;
        for (auto it = values.cbegin(); it != values.cend(); ++it) complete = complete && names.contains(it.key());
        if (!complete || !table.mid(entry.start, entry.end - entry.start).contains("NO_ITEM_DEFAULT_MOVES")) continue;
        QString copy = table.mid(entry.start, entry.end - entry.start);
        auto fields = entry.fields;
        std::sort(fields.begin(), fields.end(), [](const Field &a, const Field &b) { return a.valueStart > b.valueStart; });
        for (const auto &field : fields)
            if (values.contains(field.name))
                copy.replace(field.valueStart - entry.start, field.valueEnd - field.valueStart, values[field.name]);
        copy.replace(QRegularExpression(QStringLiteral("^\\[\\s*TRAINER_[A-Za-z0-9_]+\\s*\\]")), QStringLiteral("[%1]").arg(spec.id));
        const int lineStart = table.lastIndexOf('\n', entry.start) + 1;
        entryText = table.mid(lineStart, entry.start - lineStart) + copy;
        if (i > 0) {
            const int gapStart = parsed.entries[i - 1].end;
            const QString gap = table.mid(gapStart, lineStart - gapStart);
            if (gap.trimmed().isEmpty()) separator = gap;
        }
        break;
    }

    QVector<TrainerTextEdit> edits;
    int at = parsed.insertAt >= 0 ? parsed.insertAt : table.size();
    const bool needsComma = !parsed.entries.isEmpty() && !table.mid(parsed.entries.last().start, at - parsed.entries.last().start).trimmed().endsWith(',');
    edits.append({set.tableFile, at, at, (needsComma ? QStringLiteral(",") : QString()) + separator + entryText});
    int partyAt = 0;
    const QString partyBlock = appendText(set.partiesFile, renderParty(party, QStringLiteral("TrainerMonNoItemDefaultMoves"), spec.doubleBattle), &partyAt);
    edits.append({set.partiesFile, partyAt, partyAt, partyBlock});
    return edits;
}

QString TrainerProject::suggestedId(const QString &name) {
    QString base = name.toUpper();
    base.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]+")), QStringLiteral("_"));
    base.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (base.isEmpty()) base = QStringLiteral("NEW");
    base.prepend(QStringLiteral("TRAINER_"));
    QSet<QString> used;
    for (int s = 0; s < m_sets.size(); ++s) {
        Parsed parsed;
        parseDefines(s, parsed, nullptr);
        for (const auto &define : parsed.defines) used.insert(define.name);
    }
    QString id = base;
    for (int suffix = 2; used.contains(id); ++suffix) id = QStringLiteral("%1_%2").arg(base).arg(suffix);
    return id;
}

QString TrainerProject::suggestedScriptLabel(const QString &mapName, const QString &name) {
    QString part;
    for (const auto &word : QString(name).split(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), Qt::SkipEmptyParts))
        part += word.left(1).toUpper() + word.mid(1).toLower();
    if (part.isEmpty()) part = QStringLiteral("Trainer");
    const QString base = QStringLiteral("%1_EventScript_%2").arg(mapName, part);
    const auto &labels = scripts().labels;
    QString label = base;
    for (int suffix = 2; labels.contains(label); ++suffix) label = base + QString::number(suffix);
    return label;
}

QString TrainerProject::validate(const NewTrainerSpec &spec) {
    if (spec.set < 0 || spec.set >= m_sets.size()) return QStringLiteral("Choose which trainer list to add to.");
    const Set &set = m_sets[spec.set];
    for (const QString &file : {set.idsFile, set.tableFile, set.partiesFile})
        if (!file.isEmpty() && !m_files.contains(file)) return QStringLiteral("%1 could not be read. Run Check Trainer Files first.").arg(file);
    static const QRegularExpression idPattern(QStringLiteral("^TRAINER_[A-Z0-9_]+$"));
    if (!idPattern.match(spec.id).hasMatch()) return QStringLiteral("The ID must look like TRAINER_NAME, using capitals, digits, and underscores.");
    static const QStringList reserved = {"TRAINER_BATTLE_", "TRAINER_CLASS_", "TRAINER_PIC_", "TRAINER_TYPE_", "TRAINER_ENCOUNTER_MUSIC_", "TRAINER_BACK_PIC_"};
    for (const auto &prefix : reserved) if (spec.id.startsWith(prefix)) return QStringLiteral("IDs cannot start with %1.").arg(prefix);
    if (trainerExists(spec.id)) return QStringLiteral("%1 already exists.").arg(spec.id);
    const QString name = spec.name.trimmed();
    if (name.isEmpty()) return QStringLiteral("Enter the trainer's in-game name.");
    if (name.contains('\n') || name.contains('"') || name.contains('\\')) return QStringLiteral("The name cannot contain quotes, backslashes, or line breaks.");
    if (set.partySource && name.contains(QStringLiteral("==="))) return QStringLiteral("The name cannot contain ===.");
    if (m_nameLimit > 0 && name.size() > m_nameLimit) return QStringLiteral("The name can have at most %1 characters.").arg(m_nameLimit);
    if (spec.trainerClass.isEmpty() || spec.pic.isEmpty() || spec.music.isEmpty()) return QStringLiteral("Choose a class, picture, and music.");
    Parsed parsed;
    parseDefines(spec.set, parsed, nullptr);
    if (!canAllocate(parsed))
        return QStringLiteral("All %1 trainer IDs are used. Each trainer needs a defeat flag, so raising MAX_TRAINERS_COUNT also needs free flag space and changes save data.").arg(parsed.maximum);
    if (!spec.scriptsFile.isEmpty()) {
        static const QRegularExpression labelPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (!labelPattern.match(spec.scriptLabel).hasMatch()) return QStringLiteral("The script label can only use letters, digits, and underscores.");
        if (scripts().labels.contains(spec.scriptLabel)) return QStringLiteral("The script label %1 is already used.").arg(spec.scriptLabel);
        QString error;
        if (!ensureFile(spec.scriptsFile, &error)) return error;
    }
    return {};
}

QStringList TrainerProject::encodeDialogue(const QString &input, bool terminate) {
    const int width = 36;
    QStringList paragraphs;
    for (const auto &paragraph : QString(input).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts)) {
        QStringList lines;
        QString current;
        for (const auto &word : paragraph.simplified().split(' ', Qt::SkipEmptyParts)) {
            if (!current.isEmpty() && current.size() + 1 + word.size() > width) { lines << current; current.clear(); }
            current += (current.isEmpty() ? QString() : QStringLiteral(" ")) + word;
        }
        if (!current.isEmpty()) lines << current;
        if (!lines.isEmpty()) paragraphs << lines.join('\n');
    }
    if (paragraphs.isEmpty()) paragraphs << QStringLiteral("...");
    QStringList operands;
    for (int p = 0; p < paragraphs.size(); ++p) {
        const QStringList lines = paragraphs[p].split('\n');
        for (int l = 0; l < lines.size(); ++l) {
            QString line = lines[l];
            line.replace(QRegularExpression(QStringLiteral("(?<!\\\\)\"")), QStringLiteral("\\\""));
            while (line.endsWith('\\') && !line.endsWith(QStringLiteral("\\\\"))) line.chop(1);
            if (l + 1 < lines.size()) line += l == 0 ? QStringLiteral("\\n") : QStringLiteral("\\l");
            else if (p + 1 < paragraphs.size()) line += QStringLiteral("\\p");
            else if (terminate) line += '$';
            operands << line;
        }
    }
    return operands;
}

bool TrainerProject::create(const NewTrainerSpec &input, QString *error) {
    const QString problem = validate(input);
    if (!problem.isEmpty()) { if (error) *error = problem; return false; }
    NewTrainerSpec spec = input;
    spec.name = spec.name.trimmed();
    const Parsed parsed = parseSet(spec.set, nullptr);
    if (!m_sets[spec.set].partySource && parsed.tableOpen < 0) {
        if (error) *error = QStringLiteral("%1 is damaged. Run Check Trainer Files first.").arg(m_sets[spec.set].tableFile);
        return false;
    }
    QVector<TrainerTextEdit> edits = defineEdits(spec.set, parsed, spec.id);
    edits += entryEdits(spec.set, parsed, spec);
    if (!spec.scriptsFile.isEmpty()) {
        const bool pory = spec.scriptsFile.endsWith(".pory");
        QString stem = spec.scriptLabel;
        stem.replace(QStringLiteral("_EventScript_"), QStringLiteral("_Text_"));
        if (stem == spec.scriptLabel) stem += QStringLiteral("_Text");
        const QString intro = stem + "Intro", defeat = stem + "Defeat", post = stem + "PostBattle", notEnough = stem + "NotEnoughMons";
        const auto textBlock = [&](const QString &label, const QString &body) {
            const QStringList operands = encodeDialogue(body, !pory);
            QString block = pory ? QStringLiteral("text %1 {\n").arg(label) : QStringLiteral("%1:\n").arg(label);
            for (const auto &operand : operands)
                block += pory ? QStringLiteral("    \"%1\"\n").arg(operand) : QStringLiteral("\t.string \"%1\"\n").arg(operand);
            return block + (pory ? QStringLiteral("}\n") : QString());
        };
        QString block;
        if (pory) {
            block = QStringLiteral("script %1 {\n").arg(spec.scriptLabel);
            block += spec.doubleBattle ? QStringLiteral("    trainerbattle_double(%1, %2, %3, %4)\n").arg(spec.id, intro, defeat, notEnough)
                                       : QStringLiteral("    trainerbattle_single(%1, %2, %3)\n").arg(spec.id, intro, defeat);
            block += QStringLiteral("    msgbox(%1, MSGBOX_AUTOCLOSE)\n    end\n}\n\n").arg(post);
        } else {
            block = QStringLiteral("%1::\n").arg(spec.scriptLabel);
            block += spec.doubleBattle ? QStringLiteral("\ttrainerbattle_double %1, %2, %3, %4\n").arg(spec.id, intro, defeat, notEnough)
                                       : QStringLiteral("\ttrainerbattle_single %1, %2, %3\n").arg(spec.id, intro, defeat);
            block += QStringLiteral("\tmsgbox %1, MSGBOX_AUTOCLOSE\n\tend\n\n").arg(post);
        }
        block += textBlock(intro, spec.intro) + '\n' + textBlock(defeat, spec.defeat) + '\n' + textBlock(post, spec.postBattle);
        if (spec.doubleBattle) block += '\n' + textBlock(notEnough, QStringLiteral("You need at least two POKéMON to battle me!"));
        const QString content = text(spec.scriptsFile);
        QString prefix = content.isEmpty() || content.endsWith(QStringLiteral("\n\n")) ? QString() : content.endsWith('\n') ? QStringLiteral("\n") : QStringLiteral("\n\n");
        edits.append({spec.scriptsFile, int(content.size()), int(content.size()), prefix + block});
    }
    applyEdits(edits);
    return true;
}

QStringList TrainerProject::changedFiles() const {
    QStringList changed;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        QString current = it->text;
        if (it->crlf) current.replace('\n', QStringLiteral("\r\n"));
        QByteArray bytes = current.toUtf8();
        if (it->bom) bytes.prepend("\xEF\xBB\xBF");
        if (bytes != it->original) changed << it.key();
    }
    return changed;
}

bool TrainerProject::save(QString *error) {
    const QStringList changed = changedFiles();
    QHash<QString, QByteArray> output;
    for (const auto &file : changed) {
        const SourceFile &source = m_files[file];
        QFile disk(m_root + '/' + file);
        if (!disk.open(QIODevice::ReadOnly) || disk.readAll() != source.original) {
            if (error) *error = QStringLiteral("%1 changed outside this window. Nothing was saved. Reopen this dialog and try again.").arg(file);
            return false;
        }
        QString current = source.text;
        if (source.crlf) current.replace('\n', QStringLiteral("\r\n"));
        QByteArray bytes = current.toUtf8();
        if (source.bom) bytes.prepend("\xEF\xBB\xBF");
        output.insert(file, bytes);
    }
    QStringList written;
    const auto write = [&](const QString &file, const QByteArray &bytes, QString *message) {
        QSaveFile target(m_root + '/' + file);
        target.setDirectWriteFallback(false);
        if (target.open(QIODevice::WriteOnly) && target.write(bytes) == bytes.size() && target.commit()) return true;
        if (message) *message = QStringLiteral("%1: %2").arg(file, target.errorString());
        return false;
    };
    for (const auto &file : changed) {
        QString message;
        if (write(file, output[file], &message)) { written << file; continue; }
        QStringList stranded;
        for (const auto &done : written) if (!write(done, m_files[done].original, nullptr)) stranded << done;
        if (error) {
            *error = stranded.isEmpty() ? QStringLiteral("%1. No files were changed.").arg(message)
                                        : QStringLiteral("%1. These files could not be restored: %2").arg(message, stranded.join(", "));
        }
        return false;
    }
    for (const auto &file : changed) m_files[file].original = output[file];
    m_scripts.valid = false;
    return true;
}

}
