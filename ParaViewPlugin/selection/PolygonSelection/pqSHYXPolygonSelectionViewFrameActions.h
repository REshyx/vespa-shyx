#ifndef pqSHYXPolygonSelectionViewFrameActions_h
#define pqSHYXPolygonSelectionViewFrameActions_h

#include "pqViewFrameActionsInterface.h"

#include <QObject>

class pqViewFrame;
class pqView;

/**
 * Adds a checkable polygon cell-selection button to RenderView title bars.
 * Click-to-add vertices; independent of the rectangle rubber band and of
 * ParaView's freehand polygon lasso.
 */
class pqSHYXPolygonSelectionViewFrameActions : public QObject, public pqViewFrameActionsInterface
{
  Q_OBJECT
  Q_INTERFACES(pqViewFrameActionsInterface)
  typedef QObject Superclass;

public:
  pqSHYXPolygonSelectionViewFrameActions(QObject* parent = nullptr);
  ~pqSHYXPolygonSelectionViewFrameActions() override;

  void frameConnected(pqViewFrame* frame, pqView* view) override;

  /** Patch RenderViews whose frames were created before this interface was registered. */
  void installOnExistingViews();

private:
  Q_DISABLE_COPY(pqSHYXPolygonSelectionViewFrameActions)
};

#endif
