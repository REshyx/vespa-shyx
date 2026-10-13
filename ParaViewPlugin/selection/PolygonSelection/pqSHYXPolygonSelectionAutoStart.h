#ifndef pqSHYXPolygonSelectionAutoStart_h
#define pqSHYXPolygonSelectionAutoStart_h

#include <QObject>
#include <QPointer>

class pqSHYXPolygonSelectionViewFrameActions;

/**
 * Registers pqSHYXPolygonSelectionViewFrameActions with the ParaView interface tracker
 * so each RenderView title bar gets a polygon-selection toggle.
 */
class pqSHYXPolygonSelectionAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXPolygonSelectionAutoStart(QObject* parent = nullptr);
  ~pqSHYXPolygonSelectionAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  Q_DISABLE_COPY(pqSHYXPolygonSelectionAutoStart)

  QPointer<pqSHYXPolygonSelectionViewFrameActions> Interface;
};

#endif
