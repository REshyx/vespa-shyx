#ifndef pqSHYXProximityGapSelectionViewFrameActions_h
#define pqSHYXProximityGapSelectionViewFrameActions_h

#include "pqViewFrameActionsInterface.h"

#include <QObject>

class pqViewFrame;
class pqView;

/**
 * Adds a "proximity gap selection" button to RenderView title bars.
 */
class pqSHYXProximityGapSelectionViewFrameActions : public QObject,
                                                    public pqViewFrameActionsInterface
{
  Q_OBJECT
  Q_INTERFACES(pqViewFrameActionsInterface)
  typedef QObject Superclass;

public:
  pqSHYXProximityGapSelectionViewFrameActions(QObject* parent = nullptr);
  ~pqSHYXProximityGapSelectionViewFrameActions() override;

  void frameConnected(pqViewFrame* frame, pqView* view) override;

  /** Patch RenderViews whose frames were created before this interface was registered. */
  void installOnExistingViews();

private:
  Q_DISABLE_COPY(pqSHYXProximityGapSelectionViewFrameActions)
};

#endif
