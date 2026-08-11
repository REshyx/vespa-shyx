#ifndef pqSHYXGrowSelectionWithSimilarController_h
#define pqSHYXGrowSelectionWithSimilarController_h

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
  void reportToOutputWindow(const QString& message) const;
  void stopHoldRepeat();
  GrowStatus growOnce(bool quietSuccess);

  bool resolveActiveSelection(pqOutputPort*& portOut, vtkDataSet*& dsOut) const;
  bool collectSelectedCellIds(pqOutputPort* port, vtkDataSet* ds, std::vector<vtkIdType>& ids) const;
  bool growSimilar(vtkPolyData* pd, const std::vector<vtkIdType>& seed,
    std::vector<vtkIdType>& grown) const;
  void applyCellSelection(pqOutputPort* port, const std::vector<vtkIdType>& ids);

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
