#include "playtest.h"
#include "editor.h"
#include "mapview.h"
#include "layoutpixmapitem.h"
#include "project.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>
#include <QVBoxLayout>
#include <cmath>

Playtest::Playtest(QMainWindow *window, Editor *editor, MapView *view, std::function<bool()> save)
    : QObject(window), m_window(window), m_editor(editor), m_view(view), m_save(std::move(save)),
      m_action(new QAction(window->style()->standardIcon(QStyle::SP_MediaPlay), tr("Play"), this)),
      m_process(new QProcess(this)) {
    m_action->setObjectName("actionPlayMap");
    m_action->setToolTip(tr("Configure a demo character and choose a starting tile"));
    auto *toolbar = new QToolBar(tr("Playtest"), window);
    toolbar->setObjectName("playtestToolBar");
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_action);
    window->addToolBar(toolbar);
    connect(m_action, &QAction::triggered, this, &Playtest::configure);
    qApp->installEventFilter(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
}

Playtest::~Playtest() {
    qApp->removeEventFilter(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        // The helper forwards termination to its build/emulator process group.
        m_process->waitForFinished(3000);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished();
        }
    }
    delete m_files;
}

void Playtest::configure() {
    cancelSelection();
    if (m_process->state() != QProcess::NotRunning) {
        if (m_log) { m_log->show(); m_log->raise(); }
        return;
    }
    if (!m_editor->project || !m_editor->map) {
        QMessageBox::information(m_window, tr("Play map"), tr("Open a project and map first."));
        return;
    }
    QSettings settings;
    const QString prefix = "playtest/" + m_editor->project->root + "/";
    QDialog dialog(m_window);
    dialog.setObjectName("playtestConfiguration");
    dialog.setWindowTitle(tr("Demo character"));
    auto *form = new QFormLayout(&dialog);
    auto *help = new QLabel(tr("Choose your character, then click OK and select a tile on the map.\n"
                               "Current edits are saved before building a separate demo ROM.\n"
                               "Requires Python 3, make, and the project's GBA build toolchain."));
    help->setWordWrap(true);
    form->addRow(help);
    auto *name = new QLineEdit(settings.value(prefix + "name", "DEMO").toString());
    name->setObjectName("demoTrainerName");
    name->setMaxLength(7);
    name->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9 ]{1,7}"), name));
    form->addRow(tr("Trainer name"), name);
    auto *gender = new QComboBox;
    gender->setObjectName("demoGender");
    gender->addItems({tr("Boy"), tr("Girl")});
    gender->setCurrentIndex(settings.value(prefix + "gender", 0).toInt() == 1 ? 1 : 0);
    form->addRow(tr("Character"), gender);
    auto *species = new QComboBox;
    species->setObjectName("demoSpecies");
    for (const QString &value : m_editor->project->speciesNames)
        if (value != "SPECIES_NONE") species->addItem(value);
    species->setCurrentIndex(qMax(0, species->findText(settings.value(prefix + "species", "SPECIES_TREECKO").toString())));
    form->addRow(tr("Partner Pokémon"), species);
    auto *level = new QSpinBox;
    level->setObjectName("demoLevel");
    level->setRange(1, 100);
    level->setValue(settings.value(prefix + "level", 5).toInt());
    form->addRow(tr("Level"), level);
    QString detected;
    for (const QString &candidate : {QString("mgba-qt"), QString("mgba"), QString("visualboyadvance-m"), QString("vbam")}) {
        detected = QStandardPaths::findExecutable(candidate);
        if (!detected.isEmpty()) break;
    }
    auto *emulator = new QLineEdit(settings.value(prefix + "emulator", detected).toString());
    emulator->setObjectName("demoEmulator");
    emulator->setPlaceholderText(tr("Path to mGBA or another GBA emulator"));
    auto *browse = new QPushButton(tr("Browse…"));
    auto *row = new QHBoxLayout;
    row->addWidget(emulator);
    row->addWidget(browse);
    form->addRow(tr("Emulator"), row);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog, tr("Choose GBA emulator"), emulator->text());
        if (!path.isEmpty()) emulator->setText(path);
    });
    auto *makeArgs = new QLineEdit(settings.value(prefix + "makeArgs").toString());
    makeArgs->setObjectName("demoMakeArguments");
    makeArgs->setPlaceholderText(tr("Optional, e.g. modern or release"));
    form->addRow(tr("Build arguments"), makeArgs);
    auto *buildTarget = new QComboBox;
    buildTarget->setObjectName("demoBuildTarget");
    buildTarget->addItem(tr("Project default"), "");
    // Expansion's FRLG builds retain Emerald's data format. Do not change
    // baseGameVersion when selecting a ROM target.
    QFile general(m_editor->project->root + "/include/config/general.h");
    QFile makefile(m_editor->project->root + "/Makefile");
    const bool expansion = general.open(QIODevice::ReadOnly)
        && QRegularExpression("(?m)^\\s*#\\s*define\\s+RHH_EXPANSION\\b").match(QString::fromUtf8(general.readAll())).hasMatch();
    const QString makeSource = makefile.open(QIODevice::ReadOnly) ? QString::fromUtf8(makefile.readAll()) : QString();
    if (expansion && QRegularExpression("(?m)^firered\\s*:").match(makeSource).hasMatch()
                  && QRegularExpression("(?m)^leafgreen\\s*:").match(makeSource).hasMatch()) {
        buildTarget->addItem(tr("Emerald"), "emerald");
        buildTarget->addItem(tr("FireRed"), "firered");
        buildTarget->addItem(tr("LeafGreen"), "leafgreen");
    }
    buildTarget->setCurrentIndex(qMax(0, buildTarget->findData(settings.value(prefix + "buildTarget", ""))));
    buildTarget->setToolTip(tr("Expansion: Emerald uses Hoenn maps; FireRed and LeafGreen use Kanto maps."));
    form->addRow(tr("Build target"), buildTarget);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString executable = QStandardPaths::findExecutable(emulator->text().trimmed());
        if (!name->hasAcceptableInput() || species->currentText().isEmpty() || executable.isEmpty()) {
            QMessageBox::warning(&dialog, tr("Demo character"), tr("Enter a trainer name, choose a Pokémon, and select an installed emulator executable."));
            return;
        }
        if (QStandardPaths::findExecutable("python3").isEmpty() || QStandardPaths::findExecutable("make").isEmpty()) {
            QMessageBox::warning(&dialog, tr("Missing build tools"), tr("Install Python 3 and make, and set up this project's normal GBA build toolchain first."));
            return;
        }
        m_config = {{"root", m_editor->project->root}, {"map", m_editor->map->constantName()},
                    {"name", name->text()}, {"gender", gender->currentIndex()},
                    {"species", species->currentText()}, {"level", level->value()}, {"emulator", executable}};
        QJsonArray args;
        for (const auto &arg : QProcess::splitCommand(makeArgs->text())) args.append(arg);
        m_config.insert("make_args", args);
        m_config.insert("build_target", buildTarget->currentData().toString());
        settings.setValue(prefix + "name", name->text());
        settings.setValue(prefix + "gender", gender->currentIndex());
        settings.setValue(prefix + "species", species->currentText());
        settings.setValue(prefix + "level", level->value());
        settings.setValue(prefix + "emulator", executable);
        settings.setValue(prefix + "makeArgs", makeArgs->text());
        settings.setValue(prefix + "buildTarget", buildTarget->currentData());
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    // The normal Map page uses the map view; other main pages may hide it.
    emit choosingTile();
    m_selecting = true;
    m_view->viewport()->setCursor(Qt::CrossCursor);
    m_view->setFocus();
    m_window->statusBar()->showMessage(tr("Click a starting tile for the demo. Press Escape or right-click to cancel."));
}

void Playtest::cancelSelection() {
    if (!m_selecting) return;
    m_selecting = false;
    m_view->viewport()->unsetCursor();
    m_window->statusBar()->clearMessage();
}

bool Playtest::eventFilter(QObject *object, QEvent *event) {
    if (object == m_view->viewport() && m_swallowRelease && event->type() == QEvent::MouseButtonRelease) {
        m_swallowRelease = false;
        return true;
    }
    if (!m_selecting) return false;
    if (!m_editor->project || !m_editor->map || m_editor->project->root != m_config["root"].toString()
        || m_editor->map->constantName() != m_config["map"].toString()) {
        cancelSelection();
        return false;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        cancelSelection();
        return true;
    }
    if (object != m_view->viewport()) return false;
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::RightButton) {
            cancelSelection();
            m_swallowRelease = true;
        } else if (mouse->button() == Qt::LeftButton && m_editor->map_item) {
            const QPointF pos = m_editor->map_item->mapFromScene(m_view->mapToScene(mouse->pos()));
            const int x = std::floor(pos.x() / 16), y = std::floor(pos.y() / 16);
            if (x >= 0 && y >= 0 && x < m_editor->map->getWidth() && y < m_editor->map->getHeight()) {
                cancelSelection();
                m_swallowRelease = true;
                launch(x, y);
            }
        }
        return true;
    }
    return event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseButtonDblClick;
}

void Playtest::launch(int x, int y) {
    if (!m_save()) {
        QMessageBox::warning(m_window, tr("Play map"), tr("Could not save the project. The demo was not started."));
        return;
    }
    delete m_files;
    m_files = new QTemporaryDir;
    m_config.insert("x", x);
    m_config.insert("y", y);
    QFile script(":/demo/playtest.py");
    QFile target(m_files->filePath("playtest.py"));
    QFile config(m_files->filePath("config.json"));
    const QByteArray json = QJsonDocument(m_config).toJson();
    if (!m_files->isValid() || !script.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)
        || target.write(script.readAll()) < 0 || !config.open(QIODevice::WriteOnly) || config.write(json) != json.size()) {
        QMessageBox::warning(m_window, tr("Play map"), tr("Could not create temporary demo files."));
        return;
    }
    target.close();
    config.close();
    if (m_log) delete m_log.data();
    auto *dialog = new QDialog(m_window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Map demo"));
    dialog->resize(740, 420);
    auto *layout = new QVBoxLayout(dialog);
    auto *output = new QPlainTextEdit;
    output->setReadOnly(true);
    output->setMaximumBlockCount(5000);
    layout->addWidget(output);
    auto *stop = new QPushButton(tr("Stop"));
    layout->addWidget(stop);
    m_log = dialog;
    connect(m_process, &QProcess::readyReadStandardOutput, dialog, [=] {
        output->moveCursor(QTextCursor::End);
        output->insertPlainText(QString::fromLocal8Bit(m_process->readAllStandardOutput()));
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), dialog, [=](int code, QProcess::ExitStatus status) {
        output->appendPlainText(code == 0 && status == QProcess::NormalExit ? tr("Demo finished.") : tr("Demo stopped or failed. See output above."));
        stop->setText(tr("Close"));
    });
    connect(m_process, &QProcess::errorOccurred, dialog, [=](QProcess::ProcessError) {
        output->appendPlainText(m_process->errorString());
        stop->setText(tr("Close"));
    });
    connect(stop, &QPushButton::clicked, dialog, [=] {
        if (m_process->state() != QProcess::NotRunning) m_process->terminate();
        else dialog->close();
    });
    connect(dialog, &QDialog::finished, this, [this] {
        if (m_process->state() != QProcess::NotRunning) m_process->terminate();
    });
    m_process->start(QStandardPaths::findExecutable("python3"), {"-u", target.fileName(), config.fileName()});
    dialog->show();
}
