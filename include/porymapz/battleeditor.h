#pragma once
#include "porymapz/battledocument.h"
#include "porymapz/dialoguedocument.h"
#include <QWidget>
class QComboBox;
class QListWidget;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QTabWidget;
class QLabel;
class QPushButton;
namespace PorymapZ {
class DialogueEditor;
class BattleEditor : public QWidget {
    Q_OBJECT
public:
    explicit BattleEditor(QWidget *parent = nullptr);
    bool setScene(const QString &root, const QString &map, const QString &scripts, bool supported = true);
    bool confirmChanges();
    bool saveChanges();
    bool hasUnsavedChanges() const;
signals:
    void dirtyChanged(bool dirty);
    void sourceSaved(const QString &path);
private:
    void loadFile(int index);
    void render(int row = 0);
    void showRecord(int row);
    void addFields(int node, QTreeWidgetItem *parent);
    void setDirty(bool dirty);
    void mutate(bool remove);
    bool editable(const QString &path) const;
    QString m_root;
    QString m_path;
    bool m_dirty = false;
    int m_fileIndex = -1;
    DialogueDocument m_file;
    BattleDocument m_document;
    QComboBox *m_files;
    QListWidget *m_records;
    QLineEdit *m_search;
    QTreeWidget *m_fields;
    QPlainTextEdit *m_source;
    QTabWidget *m_views;
    QLabel *m_status;
    QPushButton *m_save;
    DialogueEditor *m_scripts;
};
}
