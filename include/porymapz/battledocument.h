#pragma once
#include <QString>
#include <QVector>

namespace PorymapZ {
// Source spans are QString indices. The lexer masks comments and strings before
// finding C delimiters. Expansion .party files use line spans and block-comment
// masking, preserving Showdown syntax without converting it to generated C.
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
    bool isPartySource() const { return m_partySource; }
    bool isTrainer(int node) const;
    bool isPokemon(int node) const;
    QVector<int> pokemon(int record) const;
private:
    int addNode(const QString &name, int start, int valueStart, int end, int parent);
    int matching(int start) const;
    void parseParty();
    bool m_partySource = false;
    QString m_source, m_mask, m_commentsMasked;
    QVector<Node> m_nodes;
    QVector<int> m_records;
};
}
