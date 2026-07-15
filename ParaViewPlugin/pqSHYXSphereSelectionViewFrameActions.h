#ifndef pqSHYXSphereSelectionViewFrameActions_h
#define pqSHYXSphereSelectionViewFrameActions_h

#include "pqViewFrameActionsInterface.h"

#include <QObject>

class pqViewFrame;
class pqView;

/**
 * Adds a checkable "sphere cell selection" button to RenderView title bars.
 */
class pqSHYXSphereSelectionViewFrameActions : public QObject, public pqViewFrameActionsInterface
{
  Q_OBJECT
  Q_INTERFACES(pqViewFrameActionsInterface)
  typedef QObject Superclass;

public:
  pqSHYXSphereSelectionViewFrameActions(QObject* parent = nullptr);
  ~pqSHYXSphereSelectionViewFrameActions() override;

  void frameConnected(pqViewFrame* frame, pqView* view) override;

  /** Patch RenderViews whose frames were created before this interface was registered. */
  void installOnExistingViews();

private:
  Q_DISABLE_COPY(pqSHYXSphereSelectionViewFrameActions)
};

#endif
