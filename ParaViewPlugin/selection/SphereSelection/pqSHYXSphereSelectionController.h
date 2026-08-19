#ifndef pqSHYXSphereSelectionController_h
#define pqSHYXSphereSelectionController_h

#include <QObject>
#include <QPointer>

#include "vtkSmartPointer.h"
#include "vtkType.h"

class pqDataRepresentation;
class pqOutputPort;
class pqRenderView;
class pqViewFrame;
class QAction;
class vtkActor;
class vtkCallbackCommand;
class vtkDataSet;
class vtkObject;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkSMSourceProxy;
class vtkSphereSource;
class vtkStaticCellLocator;
class vtkStaticPointLocator;

/**
 * Per-view interactive sphere used to select cells whose vertices fall inside the ball.
 * Toggle via title-bar action: snap to nearest vertex at view center, radius ~30% of viewport
 * short edge (as diameter). Left-drag moves the center; wheel while hovering scales radius.
 */
class pqSHYXSphereSelectionController : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXSphereSelectionController(
    pqRenderView* view, pqViewFrame* frame, QAction* toggleAction, QObject* parent = nullptr);
  ~pqSHYXSphereSelectionController() override;

private Q_SLOTS:
  void onToggled(bool checked);

private:
  Q_DISABLE_COPY(pqSHYXSphereSelectionController)

  static void ProcessEvents(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  void handleInteractorEvent(unsigned long eid);
  bool enableSphere();
  void disableSphere();
  void updateSphereGeometry();
  void renderView();
  bool snapToCenterVertex();
  bool computeInitialRadius();
  bool pickSphere(int displayX, int displayY) const;
  void dragToDisplay(int displayX, int displayY);
  void applySelection();
  void captureBaselineSelection();
  void installToggleActionContextMenu();
  void updateToggleActionTooltip();
  int currentSelectionModifier() const;
  pqDataRepresentation* resolveRepresentation() const;
  vtkDataSet* resolveDataSet(pqDataRepresentation* repr) const;
  vtkRenderer* renderer() const;
  /** BuildCells (main thread) + cache point/cell locators; skip if data unchanged. */
  bool ensureSpatialCaches(vtkDataSet* ds);

  QPointer<pqRenderView> View;
  QPointer<pqViewFrame> Frame;
  QPointer<QAction> ToggleAction;
  QPointer<pqDataRepresentation> TargetRepresentation;
  QPointer<pqOutputPort> TargetPort;

  vtkSmartPointer<vtkSphereSource> Sphere;
  vtkSmartPointer<vtkPolyDataMapper> Mapper;
  vtkSmartPointer<vtkActor> Actor;
  vtkSmartPointer<vtkCallbackCommand> Observer;
  vtkSmartPointer<vtkSMSourceProxy> BaselineAppendSelections;
  vtkSmartPointer<vtkStaticPointLocator> PointLocator;
  vtkSmartPointer<vtkStaticCellLocator> CellLocator;
  vtkDataSet* CachedDataSet = nullptr;
  vtkMTimeType CachedDataMTime = 0;

  double Center[3] = { 0.0, 0.0, 0.0 };
  double Radius = 1.0;
  double DragDepth = 0.5;
  // Center - clickWorld at press, so drag keeps the grab point under the cursor.
  double DragGrabOffset[3] = { 0.0, 0.0, 0.0 };

  bool Enabled = false;
  bool Hovering = false;
  bool Dragging = false;
  bool DeferSelectionUntilRelease = false;
  bool PendingSelectionApply = false;
};

#endif
