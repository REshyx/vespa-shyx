#ifndef pqSHYXRecoveryAutoStart_h
#define pqSHYXRecoveryAutoStart_h

#include <QObject>
#include <QPointer>
#include <QString>

class QMenu;

/**
 * Adds File -> Recovery, a submenu listing *.pvsm files in the OS temp folder
 * (newest first). %TEMP%/shyx_checkpoint.pvsm (kept up to date by
 * pqSHYXCheckpointAutoStart) can be reloaded with one click instead of going
 * through File -> Load State.
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
