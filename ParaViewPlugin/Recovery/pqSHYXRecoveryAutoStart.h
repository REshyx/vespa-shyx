#ifndef pqSHYXRecoveryAutoStart_h
#define pqSHYXRecoveryAutoStart_h

#include <QObject>
#include <QPointer>
#include <QString>

class QMenu;

/**
 * Adds File -> Recovery, a submenu listing *.pvsm files in the OS temp folder
 * (newest first). Recovery states written before destructive operations, such as
 * %TEMP%/shyx_remesh_pre_apply.pvsm from the adaptive remesher, can be reloaded
 * with one click instead of going through File -> Load State.
 *
 * The remesh recovery file contains the pipeline as it was before that Apply:
 * the filter that was about to run CGAL is not in the state, so loading the
 * recovery file cannot restart the remesh.
 *
 * Client-side only: no Server Manager proxy, no VTK module.
 */
class pqSHYXRecoveryAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  explicit pqSHYXRecoveryAutoStart(QObject* parent = nullptr);
  ~pqSHYXRecoveryAutoStart() override;

  void onStartup();
  void onShutdown();

private Q_SLOTS:
  void installMenu();
  void refreshMenu();

private:
  Q_DISABLE_COPY(pqSHYXRecoveryAutoStart)

  void loadStateFile(const QString& path);

  QPointer<QMenu> RecoveryMenu;
};

#endif
