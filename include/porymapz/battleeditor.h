#pragma once
#include "porymapz/battledocument.h"
#include "porymapz/dialoguedocument.h"
#include <QWidget>
#include <QSet>
class QComboBox;
class QListWidget;
class QLineEdit;
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
    bool setScene(const QString &root, const QString &map, const QString &scripts, bool supported = true, bool scriptsSupported = true);
    bool confirmChanges();
    bool saveChanges();
    bool hasUnsavedChanges() const;
    void refreshScene();
    void reloadFromDisk();
    bool showTrainer(const QString &id);
protected:
    bool event(QEvent *event) override;
signals:
    void dirtyChanged(bool dirty);
    void sourceSaved(const QString &path);
private:
    void loadFile(int index);
    void render(int row = 0);
    void showRecord(int row);
    void setDirty(bool dirty);
    void filterRecords();
    void refreshSceneIndex();
    int sceneFile() const;
    QString baseline() const;
    bool editable(const QString &path) const;
    QString resolveDataPath(int kind) const;
    bool containsData(const QString &path, int kind) const;
    QString m_root;
    QString m_path;
    QString m_mapName;
    QString m_scriptsPath;
    QSet<QString> m_sceneTrainers;
    QSet<QString> m_sceneParties;
    bool m_sceneIndexed = false;
    bool m_dirty = false;
    int m_fileIndex = -1;
    DialogueDocument m_file;
    BattleDocument m_document;
    QComboBox *m_files;
    QComboBox *m_scope;
    QLabel *m_scene;
    QListWidget *m_records;
    QLineEdit *m_search;
    QPlainTextEdit *m_source;
    QTabWidget *m_views = nullptr;
    QLabel *m_status;
    QPushButton *m_save;
    QWidget *m_fileControls;
    QPushButton *m_locate;
    bool m_updatingSource = false;
    bool m_loaded = false;
    DialogueEditor *m_scripts;
};
}
