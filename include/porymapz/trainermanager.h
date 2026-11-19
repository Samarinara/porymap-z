#pragma once
#include "porymapz/trainerproject.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace PorymapZ {

// What the map event should become once its trainer exists.
struct TrainerEventOptions {
    QStringList sprites;
    QStringList trainerTypes;
    QString sprite;
    QString trainerType;
    int sightRadius = 3;
    QString replacedScript; // The object's current script, when a new one will replace it.
};

// Creates the trainer's ID, data, placeholder party, and map script in one step.
class NewTrainerDialog : public QDialog {
    Q_OBJECT
public:
    NewTrainerDialog(TrainerProject *project, const QString &mapName, const QString &scriptsFile,
                     const TrainerEventOptions &options, QWidget *parent = nullptr);
    NewTrainerSpec spec() const;
    QString sprite() const;
    QString trainerType() const;
    int sightRadius() const;
    void accept() override;
private:
    void nameEdited();
    void refresh();
    TrainerProject *m_project;
    QString m_mapName, m_scriptsFile;
    bool m_idEdited = false, m_labelEdited = false;
    QComboBox *m_set;
    QLineEdit *m_name, *m_id, *m_label;
    QComboBox *m_class, *m_pic, *m_music, *m_gender, *m_battle, *m_sprite, *m_type;
    QSpinBox *m_sight;
    QPlainTextEdit *m_intro, *m_defeat, *m_post;
    QLabel *m_message;
    QPushButton *m_create;
};

// Lists problems across the trainer files, previews each repair, and applies them.
class TrainerRepairDialog : public QDialog {
    Q_OBJECT
public:
    explicit TrainerRepairDialog(TrainerProject *project, QWidget *parent = nullptr);
signals:
    void filesChanged();
    void openTrainer(const QString &id);
private:
    void rescan();
    void showDetails();
    void applySelected();
    void updateButtons();
    TrainerProject *m_project;
    QVector<TrainerIssue> m_issues;
    QTreeWidget *m_list;
    QPlainTextEdit *m_details;
    QLabel *m_summary;
    QPushButton *m_apply, *m_battle, *m_selectAll;
};

}
