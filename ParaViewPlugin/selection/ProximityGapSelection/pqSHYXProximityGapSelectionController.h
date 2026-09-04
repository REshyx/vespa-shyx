#ifndef pqSHYXProximityGapSelectionController_h
#define pqSHYXProximityGapSelectionController_h

#include <QObject>
#include <QPointer>
#include <QString>

#include "vtkType.h"

#include <vector>

class pqOutputPort;
class pqRenderView;
class pqViewFrame;
class QAction;
class QEvent;
class QWidget;
class vtkPolyData;

/**
 * Title-bar action: select cells at near-disconnected contacts.
 *
 * Vertices of different edge-connected components (closed shells included)
 * within Euclidean distance ε form A' and B'. Open-mesh slits on the same
 * component still use boundary-loop pairing. Cells incident to A' ∪ B'
 * become the selection.
 *
 * Toggle the title-bar button on to compute. While it is on, wheel on the
 * button scales ε and recomputes. Wheel while the tool is off does nothing.
 *
 * Right-click: set ε; "Single nearest region" keeps the closest contact cluster;
 * "Toward opposite center" (default off) grows A/B as balls around each other's
 * centroids so parallel surfaces do not flood the selection.
 */
class pqSHYXProximityGapSelectionController : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXProximityGapSelectionController(
    pqRenderView* view, pqViewFrame* frame, QAction* action, QObject* parent = nullptr);
  ~pqSHYXProximityGapSelectionController() override;

  /** Shared gap distance. 0 means auto on next compute. */
  static double Epsilon();
  static void SetEpsilon(double epsilon);

  /**
   * When on (default), only the contact cluster containing the globally closest
   * pair is selected. Raising ε grows that region instead of adding other gaps.
   */
  static bool SingleNearestRegion();
  static void SetSingleNearestRegion(bool enabled);

  /**
   * When on (default off), A is part1 near the centroid of B and B is part2 near the
   * centroid of A, iterated from the closest pair. Limits expansion along
   * parallel nearby surfaces.
   */
  static bool TowardOppositeCenter();
  static void SetTowardOppositeCenter(bool enabled);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
  void onToggled(bool checked);

private:
  Q_DISABLE_COPY(pqSHYXProximityGapSelectionController)

  bool isArmed() const;
  void computeIfArmed();

  void installButtonExtras();
  void updateActionTooltip();
  void promptEpsilon();
  void scaleEpsilonAndSelect(double factor);
  static void reportToOutputWindow(const QString& message);

  bool resolveActivePolyData(pqOutputPort*& portOut, vtkPolyData*& pdOut);
  static double suggestEpsilon(vtkPolyData* pd);
  static bool selectProximityGap(vtkPolyData* pd, double epsilon, bool singleRegion,
    std::vector<vtkIdType>& cellIds, vtkIdType& nContactPts, int& nComp, double& closestDist);
  static bool selectOppositeCenterGap(vtkPolyData* pd, double epsilon,
    std::vector<vtkIdType>& cellIds, vtkIdType& nContactPts, int& nComp, double& closestDist);
  static double closestCrossComponentDistance(vtkPolyData* pd);
  static void applyCellSelection(pqOutputPort* port, const std::vector<vtkIdType>& ids);
  void computeAndApply(bool quietSuccess);

  QPointer<pqRenderView> View;
  QPointer<pqViewFrame> Frame;
  QPointer<QAction> Action;
  QPointer<QWidget> Button;

  static double SharedEpsilon;
  static bool SharedSingleNearestRegion;
  static bool SharedTowardOppositeCenter;
};

#endif
