#include "porymapz/battledocument.h"
#include <QRegularExpression>

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
    m_mask = maskSource(source);
    m_commentsMasked = maskSource(source, false);
    m_nodes.clear(); m_records.clear();
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
    if (index < 0 || index >= m_nodes.size() || m_nodes[index].group || value.trimmed().isEmpty() || value.contains(QChar::Null)) return false;
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
    if (isPokemon(index) && m_nodes[node.parent].children.size() >= 6) return false;
    QString copy = m_source.mid(node.start, node.end-node.start);
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
    if (isPokemon(index) && m_nodes[node.parent].children.size() <= 1) return false;
    int start = node.start, end = node.end;
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
    if (!m_records.contains(party) || m_nodes[party].parent >= 0 || m_nodes[party].children.size() >= 6 || !canResize(party)) return false;
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
    const auto node = m_nodes.at(index);
    return node.group && node.parent >= 0 && m_records.contains(node.parent) && m_nodes[node.parent].parent < 0;
}
bool BattleDocument::canResize(int index) const {
    if (index < 0 || index >= m_nodes.size()) return false;
    // Conditional layouts and explicit array bounds need deliberate source edits.
    int parent = m_nodes[index].parent;
    const int container = parent < 0 ? index : parent;
    const auto node = m_nodes[container];
    const QString source = m_source.mid(node.start, node.end-node.start);
    if (QRegularExpression("^\\s*#", QRegularExpression::MultilineOption).match(source).hasMatch()) return false;
    if (node.parent < 0 && !QRegularExpression("\\[\\s*\\]\\s*=\\s*$").match(m_mask.mid(node.start, node.valueStart-node.start)).hasMatch()) return false;
    return true;
}

}
