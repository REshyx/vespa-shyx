#ifndef pqSHYXGrowSelectionWithSimilarController_h
#define pqSHYXGrowSelectionWithSimilarController_h

#include <QObject>
#include <QPointer>
#include <QString>

#include "vtkType.h"

#include <vector>

class pqDataRepresentation;
class pqOutputPort;
class pqRenderView;
class pqView;
class pqViewFrame;
class QAction;
class QEvent;
class QTimer;
class QWidget;
class vtkDataSet;
class vtkPolyData;

/**
 * Title-bar action: grow the current cell selection by one ring of edge-adjacent
 * faces whose normal–normal angle is at most a dihedral threshold (degrees).
 * Click once for a single ring; press-and-hold to keep growing until no more
 * similar neighbors. Right-click the button to edit the threshold. Reports a
 * Warning to the Output Window when the selection does not grow.
 */
class pqSHYXGrowSelectionWithSimilarController : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXGrowSelectionWithSimilarController(
    pqRenderView* view, pqViewFrame* frame, QAction* action, QObject* parent = nullptr);
  ~pqSHYXGrowSelectionWithSimilarController() override;

  /** Shared dihedral threshold in degrees (angle between face normals). */
  static double DihedralThresholdDegrees();
  static void SetDihedralThresholdDegrees(double degrees);

  struct GrowToCompletionResult
  {
    bool ok = false;
    bool grew = false;
    vtkIdType added = 0;
    int rings = 0;
    vtkIdType total = 0;
    QString message;
  };

  /** True when the active (or hinted) port has a resolvable cell selection. */
  static bool HasActiveCellSelection(pqDataRepresentation* hintRepresentation = nullptr);

  /**
   * Grow the current cell selection across every ring of edge-adjacent faces
   * whose normal–normal angle is at most the shared dihedral threshold.
   * Applies the selection once at the end (context-menu "Select Similar / By Normal").
   */
  static GrowToCompletionResult GrowUntilCompleteByNormal(
    pqDataRepresentation* hintRepresentation = nullptr);

  /**
   * Add unselected faces that form holes completely enclosed by the current
   * cell selection (context-menu "Fill Interior"). On an open surface, any
   * unselected component that still touches a mesh-boundary edge is left
   * unselected. On a closed surface, the largest enclosed complement is
   * treated as the exterior unless it is no larger than the current selection.
   */
  static GrowToCompletionResult FillUnselectedInterior(
    pqDataRepresentation* hintRepresentation = nullptr);

  /**
   * Select every face in the edge-connected component(s) that contain the
   * current cell selection (context-menu "Select All"). Disconnected shells
   * that do not touch the selection are left unselected.
   */
  static GrowToCompletionResult SelectConnectedRegion(
    pqDataRepresentation* hintRepresentation = nullptr);

  /**
   * Replace the current cell selection with its complement on the active
   * dataset (context-menu "Invert Selection").
   */
  static GrowToCompletionResult InvertSelection(
    pqDataRepresentation* hintRepresentation = nullptr);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
  void onTriggered();
  void onHoldRepeat();

private:
  Q_DISABLE_COPY(pqSHYXGrowSelectionWithSimilarController)

  enum class GrowStatus
  {
    Grew,
    NoGrowth,
    Error
  };

  void installButtonExtras();
  void updateActionTooltip();
  void promptDihedralThreshold();
  static void reportToOutputWindow(const QString& message);
  void stopHoldRepeat();
  GrowStatus growOnce(bool quietSuccess);

  static bool resolveActiveSelection(pqOutputPort*& portOut, vtkDataSet*& dsOut,
    pqDataRepresentation* hintRepresentation, pqView* hintView);
  static bool collectSelectedCellIds(
    pqOutputPort* port, vtkDataSet* ds, std::vector<vtkIdType>& ids);
  static bool growSimilar(
    vtkPolyData* pd, const std::vector<vtkIdType>& seed, std::vector<vtkIdType>& grown);
  static void applyCellSelection(pqOutputPort* port, const std::vector<vtkIdType>& ids);

  QPointer<pqRenderView> View;
  QPointer<pqViewFrame> Frame;
  QPointer<QAction> Action;
  QPointer<QWidget> Button;
  QTimer* HoldTimer = nullptr;

  bool HoldActive = false;
  bool HoldBlocked = false;
  bool HandledByMousePress = false;
  int HoldGrewSteps = 0;
  vtkIdType HoldAddedCells = 0;

  static double SharedDihedralThresholdDegrees;
};

#endif
