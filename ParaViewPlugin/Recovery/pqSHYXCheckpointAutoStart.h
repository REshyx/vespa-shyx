#ifndef pqSHYXCheckpointAutoStart_h
#define pqSHYXCheckpointAutoStart_h

#include <QObject>
#include <QString>

class QTimer;
class pqPipelineSource;
class pqServerManagerModelItem;

/**
 * Keeps %TEMP%/shyx_checkpoint.pvsm current so File -> Recovery always has a
 * recent state to fall back to.
 *
 * Writes happen:
 *  - when a pipeline source switches to MODIFIED, i.e. on the first pending
 *    change of an Apply cycle. pqPropertyWidget keeps such edits in the
 *    unchecked SM values; SaveXMLState serializes the checked values, so the
 *    checkpoint contains the parameters of the last successful Apply, not the
 *    ones that are about to run.
 *  - when the GUI creates a source, filter or reader (pqObjectBuilder);
 *  - shortly after an Apply finishes (a proxy becomes UNMODIFIED) and shortly
 *    after a state file is loaded, so the checkpoint follows successful work
 *    and the first Apply of a session is covered as well.
 *
 * One file is kept: every write replaces the previous checkpoint atomically
 * (temporary file + rename). Writes are skipped while a state file is loading
 * and when the user unchecks Auto-save checkpoint in File -> Recovery.
 * Every successful write posts a short "checkpoint saved" notice on the SHYX
 * status chip (pqSHYXStatusNotifier).
 *
 * Client-side only: no Server Manager proxy, no VTK module.
 */
class pqSHYXCheckpointAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  explicit pqSHYXCheckpointAutoStart(QObject* parent = nullptr);
  ~pqSHYXCheckpointAutoStart() override;

  void onStartup();
  void onShutdown();

  /// Checkpoint file name inside the OS temp folder.
  static QString checkpointFileName();

  /// Setting shared with the File -> Recovery toggle (QSettings).
  static bool autoSaveEnabled();
  static void setAutoSaveEnabled(bool enabled);

private Q_SLOTS:
  void install();
  void onModifiedStateChanged(pqServerManagerModelItem* item);
  void onCreated();
  void onStateLoaded();
  void writeCheckpoint();

private:
  Q_DISABLE_COPY(pqSHYXCheckpointAutoStart)

  /// Write after a short delay, so an Apply / state load can finish first.
  void scheduleCheckpoint();

  QTimer* SaveTimer = nullptr;
  bool Writing = false;
};

#endif
