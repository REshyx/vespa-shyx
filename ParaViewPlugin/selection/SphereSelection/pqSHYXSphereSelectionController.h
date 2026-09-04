#ifndef pqSHYXSphereSelectionController_h
#define pqSHYXSphereSelectionController_h

#include <QObject>
#include <QPointer>

#include "vtkSmartPointer.h"
#include "vtkType.h"

#include <vector>

class pqDataRepresentation;
class pqOutputPort;
class pqRenderView;
class pqViewFrame;
class QAction;
class vtkActor;
class vtkCallbackCommand;
class vtkDataObject;
class vtkDataSet;
class vtkObject;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkSMSourceProxy;
class vtkSphereSource;
class vtkStaticCellLocator;

/**
 * Per-view interactive sphere used to select cells whose vertices fall inside the ball.
 * Toggle via title-bar action: snap to nearest vertex at view center, radius ~30% of viewport
 * short edge (as diameter). Left-drag moves the center; wheel while hovering scales radius.
 *
 * Applies to every visible pipeline representation in the view (not only the active node).
 * Composite parents (multiblock / PDC) select every intersecting leaf.
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

  struct LeafCache
  {
    unsigned int FlatIndex = 0;
    vtkDataSet* DataSet = nullptr;
    vtkSmartPointer<vtkStaticCellLocator> CellLocator;
  };

  struct Target
  {
    QPointer<pqDataRepresentation> Repr;
    QPointer<pqOutputPort> Port;
    vtkDataObject* Root = nullptr;
    vtkMTimeType RootMTime = 0;
    bool IsComposite = false;
    std::vector<LeafCache> Leaves;
    vtkSmartPointer<vtkSMSourceProxy> BaselineAppendSelections;
  };

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
  void applySelectionToTarget(Target& target);
  void captureBaselineSelection();
  void installToggleActionContextMenu();
  void updateToggleActionTooltip();
  int currentSelectionModifier() const;
  vtkDataObject* resolveDataObject(pqDataRepresentation* repr) const;
  vtkRenderer* renderer() const;
  bool collectVisibleTargets();
  bool ensureTargetCaches(Target& target);
  bool ensureSpatialCaches();

  QPointer<pqRenderView> View;
  QPointer<pqViewFrame> Frame;
  QPointer<QAction> ToggleAction;
  std::vector<Target> Targets;

  vtkSmartPointer<vtkSphereSource> Sphere;
  vtkSmartPointer<vtkPolyDataMapper> Mapper;
  vtkSmartPointer<vtkActor> Actor;
  vtkSmartPointer<vtkCallbackCommand> Observer;

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
