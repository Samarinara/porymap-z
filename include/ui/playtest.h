#ifndef PLAYTEST_H
#define PLAYTEST_H

#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <functional>

class Editor;
class MapView;
class QMainWindow;
class QAction;
class QProcess;
class QTemporaryDir;
class QDialog;

class Playtest : public QObject {
    Q_OBJECT
public:
    Playtest(QMainWindow *window, Editor *editor, MapView *view, std::function<bool()> save);
    ~Playtest() override;
    void configure();
signals:
    void choosingTile();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void cancelSelection();
    void launch(int x, int y);
    QMainWindow *m_window;
    Editor *m_editor;
    MapView *m_view;
    std::function<bool()> m_save;
    QAction *m_action;
    QProcess *m_process;
    QTemporaryDir *m_files = nullptr;
    QPointer<QDialog> m_log;
    QJsonObject m_config;
    bool m_selecting = false;
    bool m_swallowRelease = false;
};
#endif
