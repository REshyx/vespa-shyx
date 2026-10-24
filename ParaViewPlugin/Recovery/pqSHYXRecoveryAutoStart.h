#ifndef pqSHYXRecoveryAutoStart_h
#define pqSHYXRecoveryAutoStart_h

#include <QByteArray>
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
 * The submenu also has a checkable "Safely Open" trigger, on by default. While
 * it is on, loading a state patches the .pvsm before it is loaded (and restores
 * the file right after): every representation's Visibility becomes 0, and the
 * SHYX remesh filters get Enable remesh / wall / cap remesh switched off. That
 * way neither the view nor the properties panel can pull data through a CGAL
 * remesh while the state is being rebuilt. Existing representations are hidden
 * as well.
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
  void hideAllRepresentations();
  static bool patchStateForSafeOpen(const QByteArray& stateXml, QByteArray* patchedXml,
    int* hiddenRepresentations, int* disabledRemeshFilters);

  QPointer<QMenu> RecoveryMenu;
  bool SafelyOpen = true;
};

#endif
