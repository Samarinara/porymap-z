#pragma once
#include <QString>
#include <QVector>

namespace PorymapZ {
// Source spans are QString indices. The lexer masks comments and strings before
// finding delimiters, so commas/braces inside them never become structural edits.
class BattleDocument {
public:
    struct Node {
        QString name;
        int start, valueStart, end, parent;
        bool group;
        QVector<int> children;
    };
    bool parse(const QString &source);
    const QString &source() const { return m_source; }
    const QVector<Node> &nodes() const { return m_nodes; }
    const QVector<int> &records() const { return m_records; }
    bool setValue(int node, const QString &value);
    bool duplicate(int node, const QString &name = {});
    bool remove(int node);
    bool appendPokemon(int party);
    bool canResize(int node) const;
private:
    int addNode(const QString &name, int start, int valueStart, int end, int parent);
    int matching(int start) const;
    bool isPokemon(int node) const;
    QString m_source, m_mask, m_commentsMasked;
    QVector<Node> m_nodes;
    QVector<int> m_records;
};
}
