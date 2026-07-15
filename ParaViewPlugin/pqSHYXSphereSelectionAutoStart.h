#ifndef pqSHYXSphereSelectionAutoStart_h
#define pqSHYXSphereSelectionAutoStart_h

#include <QObject>
#include <QPointer>

class pqSHYXSphereSelectionViewFrameActions;

/**
 * Registers pqSHYXSphereSelectionViewFrameActions with the ParaView interface tracker
 * so each RenderView title bar gets a sphere-selection toggle.
 */
class pqSHYXSphereSelectionAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXSphereSelectionAutoStart(QObject* parent = nullptr);
  ~pqSHYXSphereSelectionAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  Q_DISABLE_COPY(pqSHYXSphereSelectionAutoStart)

  QPointer<pqSHYXSphereSelectionViewFrameActions> Interface;
};

#endif
