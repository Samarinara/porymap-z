#pragma once

#include "porymapz/dialoguedocument.h"
#include <QDialog>

class QComboBox;
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QCloseEvent;

namespace PorymapZ {
class DialogueEditor : public QDialog {
public:
    explicit DialogueEditor(const QString &root, QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    bool confirmDiscard();
    void scan();
    void loadFile(int index);
    void selectEntry(int row);
    void save();
    void filter();
    QString m_root;
    DialogueDocument m_document;
    QComboBox *m_files;
    QListWidget *m_entries;
    QLineEdit *m_search;
    QPlainTextEdit *m_text;
    QLabel *m_status;
    QPushButton *m_save;
    int m_fileIndex = -1;
    int m_entryIndex = -1;
    bool m_dirty = false;
};
}
