#pragma once
#include "porymapz/scriptdocument.h"
#include <QDialog>
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
namespace PorymapZ {
class ScriptEditor : public QDialog {
public:
    ScriptEditor(const QString &root, const QStringList &sources, const QStringList &maps,
                 const QString &currentMap, const QString &label, QWidget *parent);
protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;
private:
    void rebuild(const QString &label = QString());
    void select(QTreeWidgetItem *item);
    void filter();
    bool confirmChanges();
    bool save();
    ScriptDocument m_document;
    QString m_root;
    QStringList m_sources, m_maps;
    QComboBox *m_scope;
    QTreeWidget *m_tree;
    QPlainTextEdit *m_text;
    QLineEdit *m_search;
    QLabel *m_location;
    QPushButton *m_save;
    int m_index = -1, m_scopeIndex = 0;
    QTreeWidgetItem *m_selected = nullptr;
};
}
