#pragma once
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace PorymapZ {
class ScriptDocument {
public:
    struct Block {
        QString label, path;
        int offset, length, line;
        QStringList references;
    };
    struct MapScripts {
        QString name;
        QMap<QString, QStringList> groups;
    };
    void scan(const QString &root, const QStringList &sources, const QStringList &mapJsonPaths);
    const QVector<Block> &blocks() const { return m_blocks; }
    const QVector<MapScripts> &maps() const { return m_maps; }
    QVector<int> find(const QString &label) const;
    QString text(int index) const;
    bool save(int index, const QString &text, QString *error);
    QStringList warnings() const { return m_warnings; }
private:
    QVector<Block> m_blocks;
    QVector<MapScripts> m_maps;
    QMap<QString, QByteArray> m_sources;
    QMap<QString, QVector<int>> m_labels;
    QStringList m_warnings;
};
}
