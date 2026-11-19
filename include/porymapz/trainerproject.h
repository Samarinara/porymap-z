#pragma once
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PorymapZ {

// One replacement in a project file. Offsets index the LF-normalized text.
struct TrainerTextEdit {
    QString file;
    int start = 0;
    int end = 0;
    QString text;
};

struct TrainerIssue {
    enum Severity { Error, Warning };
    QString key;      // Stable across rescans, so a selection survives other fixes.
    Severity severity = Error;
    QString trainer;
    QString file;
    int line = 0;
    QString summary;
    QString detail;
    QString fix;      // What the automatic fix does. Empty when it must be fixed by hand.
    QVector<TrainerTextEdit> edits;
    bool fixable() const { return !edits.isEmpty(); }
};

struct NewTrainerSpec {
    int set = 0;
    QString id;
    QString name;
    QString trainerClass;
    QString pic;
    QString music;
    bool female = false;
    bool doubleBattle = false;
    // Project-relative map script file. Leave empty to create only the trainer data.
    QString scriptsFile;
    QString scriptLabel;
    QString intro;
    QString defeat;
    QString postBattle;
};

// The loosely coupled files that together define a project's trainers. Parsing
// tolerates damaged source so problems can be reported and repaired. All edits
// stay in memory until save(), which refuses externally modified files.
class TrainerProject {
public:
    struct Set {
        QString label;
        QString idsFile;
        QString tableFile;
        QString partiesFile; // Empty when trainers and parties share a .party file.
        bool partySource = false;
    };

    bool load(const QString &root, bool preferFrlg, QString *error);
    const QVector<Set> &sets() const { return m_sets; }
    int defaultSet() const { return m_defaultSet; }

    QVector<TrainerIssue> analyze();
    // Applies the selected fixes in memory and returns how many were applied.
    int applyFixes(const QSet<QString> &keys);
    QString previewEdits(const TrainerIssue &issue) const;

    QString validate(const NewTrainerSpec &spec);
    bool create(const NewTrainerSpec &spec, QString *error);
    QString suggestedId(const QString &name);
    QString suggestedScriptLabel(const QString &mapName, const QString &name);

    QStringList changedFiles() const;
    bool save(QString *error);

    QStringList classes() const { return m_classes; }
    QStringList pics() const { return m_pics; }
    QStringList musics() const { return m_musics; }
    int nameLimit() const { return m_nameLimit; }

    // Trainer IDs battled by a script label, following goto/call into shared scripts.
    QStringList trainersForScript(const QString &label);
    bool trainerExists(const QString &id);
    QStringList problemsForTrainer(const QString &id);

    // Converts typed text to game text: wrapped lines joined with \n and \l, and
    // blank-line-separated paragraphs joined with \p. Returns one operand per line.
    static QStringList encodeDialogue(const QString &text, bool terminate);

private:
    struct SourceFile {
        QByteArray original;
        QString text;
        bool crlf = false;
        bool bom = false;
    };
    struct Define {
        QString name, value;
        bool numeric = false;
        int number = 0, line = 0, lineStart = 0, lineEnd = 0, valueStart = 0, valueEnd = 0;
    };
    struct Field { QString name; int start = 0, valueStart = 0, valueEnd = 0; };
    struct Entry {
        QString id;
        int start = 0, end = 0, line = 0;
        bool conditional = false;
        QVector<Field> fields;
    };
    struct Party {
        QString name, type;
        int start = 0, end = 0, line = 0, members = 0;
        bool sizeKnown = true;
    };
    struct Parsed {
        QVector<Define> defines;
        int count = -1, maximum = -1;
        Define countDefine;
        QVector<Entry> entries;
        QVector<Party> parties;
        int tableOpen = -1, tableClose = -1, insertAt = -1;
    };
    struct ScriptCall { QString file, command; int line = 0; QStringList trainers, texts; };
    struct ScriptIndex {
        QSet<QString> labels;
        QVector<ScriptCall> calls;
        QHash<QString, QSet<QString>> trainers, jumps;
        bool valid = false;
    };

    bool ensureFile(const QString &file, QString *error = nullptr);
    QString text(const QString &file) const { return m_files.value(file).text; }
    int lineOf(const QString &file, int offset) const;
    void applyEdits(const QVector<TrainerTextEdit> &edits);
    void readConstants();
    const ScriptIndex &scripts();

    Parsed parseSet(int set, QVector<TrainerIssue> *issues);
    void parseDefines(int set, Parsed &parsed, QVector<TrainerIssue> *issues);
    void parseTable(int set, Parsed &parsed, QVector<TrainerIssue> *issues);
    void parseParties(int set, Parsed &parsed, QVector<TrainerIssue> *issues);
    void parsePartyFile(int set, Parsed &parsed, QVector<TrainerIssue> *issues);
    void checkLinks(int set, const Parsed &parsed, QVector<TrainerIssue> *issues);
    void checkScripts(QVector<TrainerIssue> *issues);

    int nextId(const Parsed &parsed) const;
    bool canAllocate(const Parsed &parsed) const;
    QVector<TrainerTextEdit> defineEdits(int set, const Parsed &parsed, const QString &id) const;
    QVector<TrainerTextEdit> entryEdits(int set, const Parsed &parsed, const NewTrainerSpec &spec) const;
    QVector<TrainerTextEdit> placeholderEdits(int set, const Parsed &parsed, const QString &id) const;
    QString partyName(const Parsed &parsed, const QString &id) const;
    QString renderParty(const QString &name, const QString &type, bool twoMembers) const;
    QString placeholderSpecies() const;
    NewTrainerSpec placeholderSpec(int set, const QString &id) const;
    QString appendText(const QString &file, const QString &block, int *offset) const;

    QString m_root;
    QVector<Set> m_sets;
    int m_defaultSet = 0;
    QMap<QString, SourceFile> m_files;
    QStringList m_unreadable;
    QStringList m_classes, m_pics, m_musics;
    QSet<QString> m_species;
    int m_nameLimit = 0;
    ScriptIndex m_scripts;
};

}
