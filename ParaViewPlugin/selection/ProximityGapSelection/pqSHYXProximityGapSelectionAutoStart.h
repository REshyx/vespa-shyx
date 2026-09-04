#ifndef pqSHYXProximityGapSelectionAutoStart_h
#define pqSHYXProximityGapSelectionAutoStart_h

#include <QObject>
#include <QPointer>

class pqSHYXProximityGapSelectionViewFrameActions;

/**
 * Registers pqSHYXProximityGapSelectionViewFrameActions with the ParaView
 * interface tracker so each RenderView title bar gets the proximity-gap button.
 */
class pqSHYXProximityGapSelectionAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXProximityGapSelectionAutoStart(QObject* parent = nullptr);
  ~pqSHYXProximityGapSelectionAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  Q_DISABLE_COPY(pqSHYXProximityGapSelectionAutoStart)

  QPointer<pqSHYXProximityGapSelectionViewFrameActions> Interface;
};

#endif
