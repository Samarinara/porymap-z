#pragma once

#include "porymapz/dialoguedocument.h"
#include <QWidget>

class QComboBox;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QTabWidget;

namespace PorymapZ {
class DialogueEditor : public QWidget {
    Q_OBJECT
public:
    explicit DialogueEditor(QWidget *parent = nullptr);
    bool setScene(const QString &root, const QString &mapName, const QString &scriptsPath, bool supported = true);
    bool confirmChanges();
    bool saveChanges();
    bool hasUnsavedChanges() const { return m_dirty; }
signals:
    void dirtyChanged(bool dirty);
    void sourceSaved(const QString &path);
private:
    void scan();
    void loadFile(int index);
    void selectEntry(int row);
    void filter();
    void setDirty(bool dirty);
    void displayDocument();
    QString currentPath() const;
    bool editablePath(const QString &path) const;
    QString m_root;
    QString m_mapName;
    QString m_scriptsPath;
    bool m_supported = true;
    DialogueDocument m_document;
    QLabel *m_scene;
    QComboBox *m_scope;
    QComboBox *m_files;
    QListWidget *m_entries;
    QLineEdit *m_search;
    QPlainTextEdit *m_text;
    QPlainTextEdit *m_script;
    QTabWidget *m_views;
    QLabel *m_status;
    QPushButton *m_save;
    int m_fileIndex = -1;
    int m_entryIndex = -1;
    int m_mode = 0;
    bool m_dirty = false;
    bool m_loaded = false;
};
}
