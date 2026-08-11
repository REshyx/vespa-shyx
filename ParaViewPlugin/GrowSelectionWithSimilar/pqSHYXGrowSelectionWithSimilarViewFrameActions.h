#ifndef pqSHYXGrowSelectionWithSimilarViewFrameActions_h
#define pqSHYXGrowSelectionWithSimilarViewFrameActions_h

#include "pqViewFrameActionsInterface.h"

#include <QObject>

class pqViewFrame;
class pqView;

/**
 * Adds a "grow selection with similar normals" button to RenderView title bars.
 */
class pqSHYXGrowSelectionWithSimilarViewFrameActions : public QObject,
                                                       public pqViewFrameActionsInterface
{
  Q_OBJECT
  Q_INTERFACES(pqViewFrameActionsInterface)
  typedef QObject Superclass;

public:
  pqSHYXGrowSelectionWithSimilarViewFrameActions(QObject* parent = nullptr);
  ~pqSHYXGrowSelectionWithSimilarViewFrameActions() override;

  void frameConnected(pqViewFrame* frame, pqView* view) override;

  /** Patch RenderViews whose frames were created before this interface was registered. */
  void installOnExistingViews();

private:
  Q_DISABLE_COPY(pqSHYXGrowSelectionWithSimilarViewFrameActions)
};

#endif
