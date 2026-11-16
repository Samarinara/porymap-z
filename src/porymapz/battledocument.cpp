#include "porymapz/battledocument.h"
#include <QRegularExpression>
#include <QMap>

namespace PorymapZ {
static QString maskSource(QString text, bool maskLiterals = true, bool *valid = nullptr) {
    enum { Code, String, Character, LineComment, BlockComment } state = Code;
    bool escape = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i], next = i + 1 < text.size() ? text[i + 1] : QChar();
        if (state == Code) {
            if (c == '"') state = String;
            else if (c == '\'') state = Character;
            else if (c == '/' && next == '/') { state = LineComment; text[i++] = ' '; }
            else if (c == '/' && next == '*') { state = BlockComment; text[i++] = ' '; }
            else continue;
        } else if (state == LineComment && c == '\n') { state = Code; continue; }
        else if (state == BlockComment && c == '*' && next == '/') {
            text[i++] = ' '; text[i] = ' '; state = Code; continue;
        } else if (state == String || state == Character) {
            if (!escape && ((state == String && c == '"') || (state == Character && c == '\''))) state = Code;
            if (!escape && c == '\\') escape = true;
            else escape = false;
        }
        if ((maskLiterals || state == LineComment || state == BlockComment || (c == '/' && (next == '/' || next == '*'))) && c != '\n' && c != '\r') text[i] = ' ';
    }
    if (valid) *valid = state == Code;
    return text;
}
int BattleDocument::matching(int start) const {
    int depth = 0;
    for (int i = start; i < m_mask.size(); ++i) {
        if (m_mask[i] == '{') ++depth;
        if (m_mask[i] == '}' && --depth == 0) return i;
    }
    return -1;
}
bool BattleDocument::parse(const QString &source) {
    m_source = source;
    m_partySource = false;
    m_mask = maskSource(source);
    m_commentsMasked = maskSource(source, false);
    m_nodes.clear(); m_records.clear();
    // .party files are not C: apostrophes and quotes are ordinary text. Mask
    // block comments only before detecting trainerproc's section markers.
    QString partyMask = source;
    auto comments = QRegularExpression("/\\*[\\s\\S]*?\\*/").globalMatch(source);
    while (comments.hasNext()) {
        const auto match = comments.next();
        for (int i = match.capturedStart(); i < match.capturedEnd(); ++i)
            if (partyMask[i] != '\n' && partyMask[i] != '\r') partyMask[i] = ' ';
    }
    if (QRegularExpression("^[ \\t]*===[ \\t]+TRAINER_[A-Za-z0-9_]+[ \\t]+===[ \\t\\r]*$", QRegularExpression::MultilineOption).match(partyMask).hasMatch()) {
        m_partySource = true; m_mask = partyMask; parseParty(); return !m_records.isEmpty();
    }
    // Both stock trainer tables and the four stock TrainerMon party types.
    const QRegularExpression declarations(QStringLiteral("\\b(?:static\\s+)?(?:const\\s+)?struct\\s+(Trainer|TrainerMon[A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\[[^\\]]*\\]\\s*=\\s*\\{"));
    auto matches = declarations.globalMatch(m_mask);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int open = match.capturedEnd() - 1, close = matching(open);
        if (close < 0) continue;
        if (match.captured(1) == "Trainer") {
            const int root = addNode(match.captured(2), match.capturedStart(), open, close + 1, -1);
            for (int child : m_nodes[root].children) {
                if (m_nodes[child].group && m_nodes[child].name.startsWith("[TRAINER_")) m_records.append(child);
            }
        } else {
            m_records.append(addNode(match.captured(2), match.capturedStart(), open, close + 1, -1));
        }
    }
    return !m_records.isEmpty();
}
int BattleDocument::addNode(const QString &name, int start, int valueStart, int end, int parent) {
    const int index = m_nodes.size();
    const bool group = m_mask[valueStart] == '{' && matching(valueStart) == end - 1;
    m_nodes.append({name, start, valueStart, end, parent, group, {}});
    if (!group) return index;
    int part = valueStart + 1, depth = 0, ordinal = 0;
    for (int i = part; i < end; ++i) {
        const auto c = m_mask[i];
        if (depth == 0 && (c == ',' || i == end - 1)) {
            int first = part, last = i;
            while (first < last && m_commentsMasked[first].isSpace()) ++first;
            while (last > first && m_commentsMasked[last-1].isSpace()) --last;
            if (first < last) {
                const auto text = m_commentsMasked.mid(first, last-first);
                const QRegularExpression field(QStringLiteral("^(\\.[A-Za-z_][A-Za-z0-9_.]*|\\[[^\\]]+\\])\\s*=\\s*"));
                const auto match = field.match(text);
                const QString childName = match.hasMatch() ? match.captured(1) : QString("[%1]").arg(ordinal);
                const int value = match.hasMatch() ? first + match.capturedEnd() : first;
                if (value < last) {
                    const int child = addNode(childName, first, value, last, index);
                    m_nodes[index].children.append(child);
                }
                ++ordinal;
            }
            part = i + 1;
        } else if (c == '{' || c == '(' || c == '[') ++depth;
        else if (c == '}' || c == ')' || c == ']') --depth;
    }
    return index;
}
bool BattleDocument::setValue(int index, const QString &value) {
    if (index < 0 || index >= m_nodes.size() || m_nodes[index].group || (!m_partySource && value.trimmed().isEmpty()) || value.contains(QChar::Null)) return false;
    if (m_partySource) {
        if (value.contains('\n') || value.contains('\r') || value.contains("/*") || value.contains("*/")
            || value.trimmed().startsWith('#') || value.contains("===")) return false;
        const auto node = m_nodes[index];
        if (node.name == ".species" && (value.trimmed().isEmpty() || value.contains(':') || value.trimmed().startsWith('-'))) return false;
        const QString original = m_source;
        const auto topology = m_nodes;
        QString updated = original; updated.replace(node.valueStart, node.end-node.valueStart, value);
        parse(updated);
        bool sameStructure = m_nodes.size() == topology.size() && m_partySource;
        if (sameStructure) for (int i = 0; i < topology.size(); ++i) {
            if (m_nodes[i].name != topology[i].name || m_nodes[i].parent != topology[i].parent || m_nodes[i].group != topology[i].group) {
                sameStructure = false; break;
            }
        }
        if (!sameStructure) { parse(original); return false; }
        return true;
    }
    // Values may contain nested expressions but must remain one initializer.
    bool valid;
    const auto mask = maskSource(value, true, &valid);
    if (!valid) return false;
    QString delimiters;
    for (const auto c : mask) {
        if (c == '{' || c == '(' || c == '[') delimiters.append(c);
        else if (c == '}' || c == ')' || c == ']') {
            const QChar expected = c == '}' ? '{' : c == ')' ? '(' : '[';
            if (delimiters.isEmpty() || delimiters.back() != expected) return false;
            delimiters.chop(1);
        } else if ((c == ',' && delimiters.isEmpty()) || c == ';' || c == '#') return false;
    }
    if (!delimiters.isEmpty()) return false;
    const auto node = m_nodes[index];
    QString updated = m_source;
    updated.replace(node.valueStart, node.end-node.valueStart, value);
    const QString original = m_source;
    const int count = m_nodes.size();
    parse(updated);
    if (m_nodes.size() != count || m_nodes[index].group) { parse(original); return false; }
    return true;
}
bool BattleDocument::duplicate(int index, const QString &name) {
    if (index < 0 || index >= m_nodes.size()) return false;
    const auto node = m_nodes[index];
    if ((!m_records.contains(index) && !isPokemon(index)) || !canResize(index)) return false;
    if (isPokemon(index) && pokemon(node.parent).size() >= 6) return false;
    QString copy = m_source.mid(node.start, node.end-node.start);
    if (m_partySource) {
        if (m_records.contains(index)) {
            if (!QRegularExpression("^TRAINER_[A-Za-z0-9_]+$").match(name).hasMatch()) return false;
            for (int record : m_records) if (m_nodes[record].name == name) return false;
            copy.replace(copy.indexOf(node.name), node.name.size(), name);
        }
        const QString newline = m_source.contains("\r\n") ? "\r\n" : "\n";
        QString updated = m_source; updated.insert(node.end, newline + newline + copy);
        parse(updated); return true;
    }
    if (node.parent < 0) {
        if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch() || m_mask.contains(QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\b"))) return false;
        copy.replace(QRegularExpression("\\b" + QRegularExpression::escape(node.name) + "\\b"), name);
        const int semicolon = m_mask.indexOf(';', node.end);
        if (semicolon < 0) return false;
        QString updated = m_source; updated.insert(semicolon + 1, (m_source.contains("\r\n") ? "\r\n\r\n" : "\n\n") + copy + ";"); parse(updated);
    } else {
        if (node.name.startsWith("[TRAINER_")) {
            if (!QRegularExpression("^TRAINER_[A-Za-z0-9_]+$").match(name).hasMatch() || m_mask.contains(QRegularExpression("\\[\\s*" + QRegularExpression::escape(name) + "\\s*\\]"))) return false;
            copy.replace(0, node.name.size(), "[" + name + "]");
        }
        QString updated = m_source;
        int after = node.end;
        while (after < m_mask.size() && m_mask[after].isSpace()) ++after;
        if (after < m_mask.size() && m_mask[after] == ',') updated.insert(after+1, (m_source.contains("\r\n") ? "\r\n    " : "\n    ") + copy + ",");
        else updated.insert(node.end, (m_source.contains("\r\n") ? ",\r\n    " : ",\n    ") + copy);
        parse(updated);
    }
    return true;
}
bool BattleDocument::remove(int index) {
    if (index < 0 || index >= m_nodes.size()) return false;
    const auto node = m_nodes[index];
    if ((!m_records.contains(index) && !isPokemon(index)) || !canResize(index)) return false;
    if (isPokemon(index) && pokemon(node.parent).size() <= 1) return false;
    int start = node.start, end = node.end;
    if (m_partySource) {
        QString updated = m_source; updated.remove(start, end-start); parse(updated); return true;
    }
    if (node.parent < 0) {
        end = m_mask.indexOf(';', end);
        if (end < 0) return false;
        ++end;
    } else {
        while (end < m_mask.size() && m_mask[end].isSpace()) ++end;
        if (end < m_mask.size() && m_mask[end] == ',') ++end;
        else {
            start = node.start - 1;
            while (start >= 0 && m_mask[start].isSpace()) --start;
            if (start < 0 || m_mask[start] != ',') start = node.start;
        }
    }
    QString updated = m_source; updated.remove(start, end-start); parse(updated); return true;
}
bool BattleDocument::appendPokemon(int party) {
    if (!m_records.contains(party) || (!m_partySource && m_nodes[party].parent >= 0) || pokemon(party).size() >= 6 || !canResize(party)) return false;
    if (m_partySource) {
        const QString newline = m_source.contains("\r\n") ? "\r\n" : "\n";
        QString updated = m_source;
        updated.insert(m_nodes[party].end, newline + newline + "Treecko" + newline + "Level: 5" + newline + newline);
        parse(updated); return true;
    }
    const auto node = m_nodes[party];
    const QString declaration = m_source.mid(node.start, node.valueStart-node.start);
    QString pokemon = "{ .iv = 0, .lvl = 5, .species = SPECIES_TREECKO";
    if (declaration.contains("TrainerMonItem")) pokemon += ", .heldItem = ITEM_NONE";
    if (declaration.contains("CustomMoves")) pokemon += ", .moves = {MOVE_POUND, MOVE_NONE, MOVE_NONE, MOVE_NONE}";
    pokemon += " }";
    int last = node.end - 2;
    while (last > node.valueStart && m_mask[last].isSpace()) --last;
    const QString comma = m_mask[last] != '{' && m_mask[last] != ',' ? "," : "";
    QString updated = m_source;
    const QString newline = m_source.contains("\r\n") ? "\r\n" : "\n";
    updated.insert(last+1, comma + newline + "    " + pokemon + "," + newline);
    parse(updated);
    return true;
}

bool BattleDocument::isPokemon(int index) const {
    if (index < 0 || index >= m_nodes.size()) return false;
    const auto node = m_nodes.at(index);
    return node.group && node.parent >= 0 && m_records.contains(node.parent) && (m_partySource || m_nodes[node.parent].parent < 0);
}
bool BattleDocument::canResize(int index) const {
    if (index < 0 || index >= m_nodes.size()) return false;
    if (m_partySource) {
        const auto node = m_nodes[isPokemon(index) ? m_nodes[index].parent : index];
        const auto block = m_mask.mid(node.start, node.end-node.start);
        return !QRegularExpression("^[ \\t]*(?:#|(?:Pool|Copy Pool|Copy Party):)", QRegularExpression::MultilineOption).match(block).hasMatch();
    }
    // Conditional layouts and explicit array bounds need deliberate source edits.
    int parent = m_nodes[index].parent;
    const int container = parent < 0 ? index : parent;
    const auto node = m_nodes[container];
    // A macro may expand to one or several team members. Its size is unknown;
    // do not resize or duplicate that declaration through the structured UI.
    if (m_records.contains(container) && !isTrainer(container))
        for (int child : node.children) if (!m_nodes[child].group) return false;
    const QString source = m_source.mid(node.start, node.end-node.start);
    if (QRegularExpression("^\\s*#", QRegularExpression::MultilineOption).match(source).hasMatch()) return false;
    if (node.parent < 0 && !QRegularExpression("\\[\\s*\\]\\s*=\\s*$").match(m_mask.mid(node.start, node.valueStart-node.start)).hasMatch()) return false;
    return true;
}

bool BattleDocument::isTrainer(int index) const {
    return m_records.contains(index) && (m_partySource || m_nodes[index].parent >= 0);
}
QVector<int> BattleDocument::pokemon(int record) const {
    QVector<int> result;
    if (m_records.contains(record)) for (int child : m_nodes[record].children) if (isPokemon(child)) result.append(child);
    return result;
}
void BattleDocument::parseParty() {
    const QRegularExpression header("^[ \\t]*===[ \\t]+(TRAINER_[A-Za-z0-9_]+)[ \\t]+===[ \\t\\r]*$", QRegularExpression::MultilineOption);
    QVector<QRegularExpressionMatch> headers;
    auto matches = header.globalMatch(m_mask);
    while (matches.hasNext()) headers.append(matches.next());
    static const QMap<QString, QString> names = {
        {"Name", ".trainerName"}, {"Class", ".trainerClass"}, {"Pic", ".trainerPic"},
        {"Double Battle", ".doubleBattle"}, {"Battle Type", ".battleType"}, {"AI", ".aiFlags"}, {"Items", ".items"},
        {"Level", ".lvl"}, {"Ability", ".ability"}, {"Nature", ".nature"}, {"IVs", ".ivs"}, {"EVs", ".evs"}
    };
    for (int h = 0; h < headers.size(); ++h) {
        const auto match = headers[h];
        const int end = h+1 < headers.size() ? headers[h+1].capturedStart() : m_source.size();
        const int record = m_nodes.size();
        m_nodes.append({match.captured(1), int(match.capturedStart()), int(match.capturedEnd()), end, -1, true, {}});
        m_records.append(record);
        bool inParty = false, trainerField = false;
        int mon = -1, moves = -1, ordinal = 0;
        int start = m_source.indexOf('\n', match.capturedEnd());
        if (start < 0) continue;
        ++start;
        while (start < end) {
            int lineEnd = m_source.indexOf('\n', start);
            if (lineEnd < 0 || lineEnd > end) lineEnd = end;
            int first = start, last = lineEnd;
            while (first < last && m_mask[first].isSpace()) ++first;
            while (last > first && m_mask[last-1].isSpace()) --last;
            const auto line = m_mask.mid(first, last-first);
            if (line.isEmpty()) {
                if (trainerField && m_source.mid(start, lineEnd-start).trimmed().isEmpty()) { inParty = true; mon = -1; moves = -1; }
            } else if (!line.startsWith('#')) {
                const int colon = line.indexOf(':');
                if (inParty && mon < 0) {
                    mon = m_nodes.size();
                    m_nodes.append({QString("[%1]").arg(ordinal++), first, first, last, record, true, {}});
                    m_nodes[record].children.append(mon);
                    const int species = m_nodes.size();
                    m_nodes.append({".species", first, first, last, mon, false, {}});
                    m_nodes[mon].children.append(species);
                } else if (mon >= 0 && line.startsWith("- ")) {
                    if (moves < 0) {
                        moves = m_nodes.size(); m_nodes.append({".moves", first, first, last, mon, true, {}});
                        m_nodes[mon].children.append(moves);
                    }
                    const int move = m_nodes.size();
                    m_nodes.append({QString("[%1]").arg(m_nodes[moves].children.size()), first, first+2, last, moves, false, {}});
                    m_nodes[moves].children.append(move); m_nodes[moves].end = last;
                } else if (colon >= 0) {
                    int value = first + colon + 1;
                    while (value < last && m_mask[value].isSpace()) ++value;
                    const QString key = line.left(colon).trimmed();
                    const int parent = mon < 0 ? record : mon, field = m_nodes.size();
                    m_nodes.append({names.value(key, key), first, value, last, parent, false, {}});
                    m_nodes[parent].children.append(field); trainerField = true;
                } else if (mon >= 0) {
                    const int extra = m_nodes.size();
                    m_nodes.append({"Other", first, first, last, mon, false, {}}); m_nodes[mon].children.append(extra);
                }
                if (mon >= 0) m_nodes[mon].end = last;
            }
            start = lineEnd + 1;
        }
    }
}

}
