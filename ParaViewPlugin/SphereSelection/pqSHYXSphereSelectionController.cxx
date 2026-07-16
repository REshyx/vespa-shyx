#include "pqSHYXSphereSelectionController.h"

#include "pqActiveObjects.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqSelectionManager.h"
#include "pqView.h"
#include "pqViewFrame.h"

#include "vtkActor.h"
#include "vtkAlgorithm.h"
#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkInteractorObserver.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"
// Qt defines emit as a macro; TBB profiling.h has methods named emit().
#ifdef emit
#  undef emit
#endif
#include "vtkSMPThreadLocalObject.h"
#include "vtkSMPTools.h"
#include "vtkSphereSource.h"

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr double kWheelScaleUp = 1.1;
constexpr double kWheelScaleDown = 1.0 / 1.1;

vtkDataSet* FirstLeafDataSet(vtkDataObject* obj)
{
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    return ds;
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(obj);
  if (!cds)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkCompositeDataIterator> it;
  it.TakeReference(cds->NewIterator());
  for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    if (auto* leaf = vtkDataSet::SafeDownCast(it->GetCurrentDataObject()))
    {
      if (leaf->GetNumberOfPoints() > 0)
      {
        return leaf;
      }
    }
  }
  return nullptr;
}
}

//-----------------------------------------------------------------------------
pqSHYXSphereSelectionController::pqSHYXSphereSelectionController(
  pqRenderView* view, pqViewFrame* frame, QAction* toggleAction, QObject* parent)
  : Superclass(parent)
  , View(view)
  , Frame(frame)
  , ToggleAction(toggleAction)
{
  QObject::connect(toggleAction, &QAction::toggled, this, &pqSHYXSphereSelectionController::onToggled);
  this->installToggleActionContextMenu();
}

//-----------------------------------------------------------------------------
pqSHYXSphereSelectionController::~pqSHYXSphereSelectionController()
{
  this->disableSphere();
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::ProcessEvents(
  vtkObject* caller, unsigned long eid, void* clientdata, void* /*calldata*/)
{
  Q_UNUSED(caller);
  auto* self = reinterpret_cast<pqSHYXSphereSelectionController*>(clientdata);
  if (self)
  {
    self->handleInteractorEvent(eid);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::onToggled(bool checked)
{
  if (checked)
  {
    if (!this->enableSphere())
    {
      if (this->ToggleAction)
      {
        QSignalBlocker blocker(this->ToggleAction);
        this->ToggleAction->setChecked(false);
      }
    }
  }
  else
  {
    this->disableSphere();
  }
}

//-----------------------------------------------------------------------------
vtkRenderer* pqSHYXSphereSelectionController::renderer() const
{
  if (!this->View || !this->View->getRenderViewProxy())
  {
    return nullptr;
  }
  return this->View->getRenderViewProxy()->GetRenderer();
}

//-----------------------------------------------------------------------------
pqDataRepresentation* pqSHYXSphereSelectionController::resolveRepresentation() const
{
  if (!this->View)
  {
    return nullptr;
  }

  pqDataRepresentation* active = pqActiveObjects::instance().activeRepresentation();
  if (active && active->getView() == this->View && active->isVisible())
  {
    return active;
  }

  const QList<pqRepresentation*> reprs = this->View->getRepresentations();
  for (pqRepresentation* r : reprs)
  {
    auto* dr = qobject_cast<pqDataRepresentation*>(r);
    if (dr && dr->isVisible())
    {
      return dr;
    }
  }
  return nullptr;
}

//-----------------------------------------------------------------------------
vtkDataSet* pqSHYXSphereSelectionController::resolveDataSet(pqDataRepresentation* repr) const
{
  if (!repr)
  {
    return nullptr;
  }
  pqOutputPort* port = repr->getOutputPortFromInput();
  if (!port || !port->getSource())
  {
    return nullptr;
  }
  vtkSMSourceProxy* src = vtkSMSourceProxy::SafeDownCast(port->getSource()->getProxy());
  if (!src)
  {
    return nullptr;
  }
  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  if (!alg)
  {
    return nullptr;
  }
  return FirstLeafDataSet(alg->GetOutputDataObject(port->getPortNumber()));
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::enableSphere()
{
  this->disableSphere();

  pqDataRepresentation* repr = this->resolveRepresentation();
  vtkDataSet* ds = this->resolveDataSet(repr);
  vtkRenderer* ren = this->renderer();
  vtkSMRenderViewProxy* rmp = this->View ? this->View->getRenderViewProxy() : nullptr;
  vtkRenderWindowInteractor* iren = rmp ? rmp->GetInteractor() : nullptr;
  if (!repr || !ds || ds->GetNumberOfPoints() == 0 || !ren || !iren)
  {
    qWarning("SHYX sphere selection: no visible mesh in the active RenderView.");
    return false;
  }

  this->TargetRepresentation = repr;
  this->TargetPort = repr->getOutputPortFromInput();

  if (!this->snapToCenterVertex() || !this->computeInitialRadius())
  {
    qWarning("SHYX sphere selection: failed to initialize sphere at view center.");
    this->TargetRepresentation = nullptr;
    this->TargetPort = nullptr;
    return false;
  }

  this->Sphere = vtkSmartPointer<vtkSphereSource>::New();
  this->Sphere->SetThetaResolution(32);
  this->Sphere->SetPhiResolution(24);
  this->Mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->Mapper->SetInputConnection(this->Sphere->GetOutputPort());
  this->Actor = vtkSmartPointer<vtkActor>::New();
  this->Actor->SetMapper(this->Mapper);
  this->Actor->GetProperty()->SetColor(0.2, 0.65, 1.0);
  this->Actor->GetProperty()->SetOpacity(0.35);
  this->Actor->GetProperty()->SetInterpolationToPhong();
  this->Actor->PickableOn();
  this->Actor->DragableOn();

  this->updateSphereGeometry();
  ren->AddActor(this->Actor);

  this->Observer = vtkSmartPointer<vtkCallbackCommand>::New();
  this->Observer->SetClientData(this);
  this->Observer->SetCallback(&pqSHYXSphereSelectionController::ProcessEvents);
  // High priority so wheel can abort camera zoom while hovering the sphere.
  iren->AddObserver(vtkCommand::LeftButtonPressEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::LeftButtonReleaseEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::MouseMoveEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::MouseWheelForwardEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::MouseWheelBackwardEvent, this->Observer, 1.0);

  this->Enabled = true;
  this->captureBaselineSelection();
  this->applySelection();
  this->renderView();
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::disableSphere()
{
  if (this->View && this->View->getRenderViewProxy() && this->Observer)
  {
    if (vtkRenderWindowInteractor* iren = this->View->getRenderViewProxy()->GetInteractor())
    {
      iren->RemoveObserver(this->Observer);
    }
  }
  this->Observer = nullptr;

  if (vtkRenderer* ren = this->renderer())
  {
    if (this->Actor)
    {
      ren->RemoveActor(this->Actor);
    }
  }

  this->Actor = nullptr;
  this->Mapper = nullptr;
  this->Sphere = nullptr;
  this->BaselineAppendSelections = nullptr;
  this->TargetRepresentation = nullptr;
  this->TargetPort = nullptr;
  this->Enabled = false;
  this->Hovering = false;
  this->Dragging = false;
  this->PendingSelectionApply = false;

  if (this->View)
  {
    this->renderView();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::updateSphereGeometry()
{
  if (!this->Sphere)
  {
    return;
  }
  this->Sphere->SetCenter(this->Center);
  this->Sphere->SetRadius(this->Radius);
  this->Sphere->Update();
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::renderView()
{
  if (this->View)
  {
    this->View->render();
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::snapToCenterVertex()
{
  vtkRenderer* ren = this->renderer();
  vtkDataSet* ds = this->resolveDataSet(this->TargetRepresentation);
  if (!ren || !ds || ds->GetNumberOfPoints() == 0)
  {
    return false;
  }

  int* size = ren->GetSize();
  if (!size || size[0] <= 0 || size[1] <= 0)
  {
    return false;
  }

  const double cx = 0.5 * size[0];
  const double cy = 0.5 * size[1];

  vtkIdType bestId = 0;
  double bestDist2 = std::numeric_limits<double>::max();
  double bestWorld[3] = { 0.0, 0.0, 0.0 };

  const vtkIdType npts = ds->GetNumberOfPoints();
  for (vtkIdType i = 0; i < npts; ++i)
  {
    double p[3];
    ds->GetPoint(i, p);
    ren->SetWorldPoint(p[0], p[1], p[2], 1.0);
    ren->WorldToDisplay();
    double* d = ren->GetDisplayPoint();
    if (d[2] < 0.0 || d[2] > 1.0)
    {
      continue;
    }
    const double dx = d[0] - cx;
    const double dy = d[1] - cy;
    const double dist2 = dx * dx + dy * dy;
    if (dist2 < bestDist2)
    {
      bestDist2 = dist2;
      bestId = i;
      bestWorld[0] = p[0];
      bestWorld[1] = p[1];
      bestWorld[2] = p[2];
    }
  }

  if (bestDist2 == std::numeric_limits<double>::max())
  {
    // Fallback: geometric centroid of bounds mid, then nearest point in world space.
    double b[6];
    ds->GetBounds(b);
    const double mid[3] = { 0.5 * (b[0] + b[1]), 0.5 * (b[2] + b[3]), 0.5 * (b[4] + b[5]) };
    bestDist2 = std::numeric_limits<double>::max();
    for (vtkIdType i = 0; i < npts; ++i)
    {
      double p[3];
      ds->GetPoint(i, p);
      const double dist2 = vtkMath::Distance2BetweenPoints(mid, p);
      if (dist2 < bestDist2)
      {
        bestDist2 = dist2;
        bestId = i;
        bestWorld[0] = p[0];
        bestWorld[1] = p[1];
        bestWorld[2] = p[2];
      }
    }
    Q_UNUSED(bestId);
  }

  this->Center[0] = bestWorld[0];
  this->Center[1] = bestWorld[1];
  this->Center[2] = bestWorld[2];
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::computeInitialRadius()
{
  vtkRenderer* ren = this->renderer();
  if (!ren)
  {
    return false;
  }
  int* size = ren->GetSize();
  if (!size || size[0] <= 0 || size[1] <= 0)
  {
    return false;
  }

  // Diameter ~30% of viewport short edge => radius uses 15% display pixels at sphere depth.
  const double pixelRadius = 0.15 * static_cast<double>(std::min(size[0], size[1]));

  ren->SetWorldPoint(this->Center[0], this->Center[1], this->Center[2], 1.0);
  ren->WorldToDisplay();
  double displayCenter[3];
  ren->GetDisplayPoint(displayCenter);

  double worldA[4];
  vtkInteractorObserver::ComputeDisplayToWorld(
    ren, displayCenter[0], displayCenter[1], displayCenter[2], worldA);
  double worldB[4];
  vtkInteractorObserver::ComputeDisplayToWorld(
    ren, displayCenter[0] + pixelRadius, displayCenter[1], displayCenter[2], worldB);

  const double dx = worldA[0] - worldB[0];
  const double dy = worldA[1] - worldB[1];
  const double dz = worldA[2] - worldB[2];
  this->Radius = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(this->Radius > 0.0) || !std::isfinite(this->Radius))
  {
    double b[6];
    if (vtkDataSet* ds = this->resolveDataSet(this->TargetRepresentation))
    {
      ds->GetBounds(b);
      this->Radius = 0.15 * std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 });
    }
    else
    {
      this->Radius = 1.0;
    }
  }
  return this->Radius > 0.0;
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::pickSphere(int displayX, int displayY) const
{
  vtkRenderer* ren = this->renderer();
  if (!ren || this->Radius <= 0.0)
  {
    return false;
  }

  double worldNear[4];
  double worldFar[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, 0.0, worldNear);
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, 1.0, worldFar);

  double dir[3] = { worldFar[0] - worldNear[0], worldFar[1] - worldNear[1], worldFar[2] - worldNear[2] };
  const double dirLen2 = vtkMath::Dot(dir, dir);
  if (dirLen2 <= 0.0)
  {
    return false;
  }
  vtkMath::Normalize(dir);

  double oc[3] = { worldNear[0] - this->Center[0], worldNear[1] - this->Center[1],
    worldNear[2] - this->Center[2] };
  const double b = vtkMath::Dot(oc, dir);
  const double c = vtkMath::Dot(oc, oc) - this->Radius * this->Radius;
  const double disc = b * b - c;
  return disc >= 0.0;
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::dragToDisplay(int displayX, int displayY)
{
  vtkRenderer* ren = this->renderer();
  if (!ren)
  {
    return;
  }

  double world[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, this->DragDepth, world);
  this->Center[0] = world[0] + this->DragGrabOffset[0];
  this->Center[1] = world[1] + this->DragGrabOffset[1];
  this->Center[2] = world[2] + this->DragGrabOffset[2];
  this->updateSphereGeometry();
  if (this->DeferSelectionUntilRelease)
  {
    this->PendingSelectionApply = true;
    this->renderView();
  }
  else
  {
    this->applySelection();
    this->renderView();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::handleInteractorEvent(unsigned long eid)
{
  if (!this->Enabled || !this->View || !this->View->getRenderViewProxy())
  {
    return;
  }
  vtkRenderWindowInteractor* iren = this->View->getRenderViewProxy()->GetInteractor();
  if (!iren)
  {
    return;
  }

  int pos[2];
  iren->GetEventPosition(pos);

  switch (eid)
  {
    case vtkCommand::MouseMoveEvent:
    {
      if (this->Dragging)
      {
        this->dragToDisplay(pos[0], pos[1]);
      }
      else
      {
        this->Hovering = this->pickSphere(pos[0], pos[1]);
      }
      break;
    }
    case vtkCommand::LeftButtonPressEvent:
    {
      if (this->pickSphere(pos[0], pos[1]))
      {
        this->Dragging = true;
        this->Hovering = true;
        vtkRenderer* ren = this->renderer();
        if (ren)
        {
          ren->SetWorldPoint(this->Center[0], this->Center[1], this->Center[2], 1.0);
          ren->WorldToDisplay();
          this->DragDepth = ren->GetDisplayPoint()[2];

          double clickWorld[4];
          vtkInteractorObserver::ComputeDisplayToWorld(
            ren, pos[0], pos[1], this->DragDepth, clickWorld);
          this->DragGrabOffset[0] = this->Center[0] - clickWorld[0];
          this->DragGrabOffset[1] = this->Center[1] - clickWorld[1];
          this->DragGrabOffset[2] = this->Center[2] - clickWorld[2];
        }
        else
        {
          this->DragGrabOffset[0] = this->DragGrabOffset[1] = this->DragGrabOffset[2] = 0.0;
        }
        this->Observer->AbortFlagOn();
      }
      break;
    }
    case vtkCommand::LeftButtonReleaseEvent:
    {
      if (this->Dragging)
      {
        this->Dragging = false;
        if (this->DeferSelectionUntilRelease && this->PendingSelectionApply)
        {
          this->applySelection();
          this->PendingSelectionApply = false;
          this->renderView();
        }
        this->Observer->AbortFlagOn();
      }
      break;
    }
    case vtkCommand::MouseWheelForwardEvent:
    case vtkCommand::MouseWheelBackwardEvent:
    {
      if (!this->Hovering && !this->pickSphere(pos[0], pos[1]))
      {
        break;
      }
      this->Hovering = true;
      const double scale =
        (eid == vtkCommand::MouseWheelForwardEvent) ? kWheelScaleUp : kWheelScaleDown;
      this->Radius *= scale;
      if (this->Radius < 1e-12)
      {
        this->Radius = 1e-12;
      }
      this->updateSphereGeometry();
      this->applySelection();
      this->renderView();
      this->Observer->AbortFlagOn();
      break;
    }
    default:
      break;
  }
}

//-----------------------------------------------------------------------------
int pqSHYXSphereSelectionController::currentSelectionModifier() const
{
  if (!this->Frame)
  {
    return pqView::PV_SELECTION_DEFAULT;
  }

  // Prefer keyboard modifiers when present (same as pqRenderViewSelectionReaction).
  if (this->View && this->View->getRenderViewProxy() &&
    this->View->getRenderViewProxy()->GetInteractor())
  {
    vtkRenderWindowInteractor* iren = this->View->getRenderViewProxy()->GetInteractor();
    const bool ctrl = iren->GetControlKey() == 1;
    const bool shift = iren->GetShiftKey() == 1;
    if (ctrl && shift)
    {
      return pqView::PV_SELECTION_TOGGLE;
    }
    if (ctrl)
    {
      return pqView::PV_SELECTION_ADDITION;
    }
    if (shift)
    {
      return pqView::PV_SELECTION_SUBTRACTION;
    }
  }

  auto checkedModifier = [this](const char* name) -> QAction* {
    return this->Frame->findChild<QAction*>(QLatin1String(name));
  };
  if (QAction* a = checkedModifier("actionAddSelection"))
  {
    if (a->isChecked())
    {
      return pqView::PV_SELECTION_ADDITION;
    }
  }
  if (QAction* a = checkedModifier("actionSubtractSelection"))
  {
    if (a->isChecked())
    {
      return pqView::PV_SELECTION_SUBTRACTION;
    }
  }
  if (QAction* a = checkedModifier("actionToggleSelection"))
  {
    if (a->isChecked())
    {
      return pqView::PV_SELECTION_TOGGLE;
    }
  }
  return pqView::PV_SELECTION_DEFAULT;
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::captureBaselineSelection()
{
  this->BaselineAppendSelections = nullptr;
  if (!this->TargetPort)
  {
    return;
  }
  vtkSMSourceProxy* cur = this->TargetPort->getSelectionInput();
  if (!cur)
  {
    return;
  }
  // Keep a live reference to the current append-selections proxy as baseline.
  this->BaselineAppendSelections = cur;
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::applySelection()
{
  if (!this->TargetPort || !this->TargetRepresentation)
  {
    return;
  }

  vtkDataSet* ds = this->resolveDataSet(this->TargetRepresentation);
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return;
  }

  const double r2 = this->Radius * this->Radius;
  const double center[3] = { this->Center[0], this->Center[1], this->Center[2] };
  const vtkIdType nCells = ds->GetNumberOfCells();

  std::vector<unsigned char> insideMask(static_cast<size_t>(nCells), 0);
  vtkSMPThreadLocalObject<vtkIdList> tlPtIds;
  vtkSMPTools::For(0, nCells, [&](vtkIdType begin, vtkIdType end) {
    vtkIdList* ptIds = tlPtIds.Local();
    for (vtkIdType cid = begin; cid < end; ++cid)
    {
      ds->GetCellPoints(cid, ptIds);
      const vtkIdType npts = ptIds->GetNumberOfIds();
      for (vtkIdType i = 0; i < npts; ++i)
      {
        double p[3];
        ds->GetPoint(ptIds->GetId(i), p);
        if (vtkMath::Distance2BetweenPoints(p, center) <= r2)
        {
          insideMask[static_cast<size_t>(cid)] = 1;
          break;
        }
      }
    }
  });

  std::vector<vtkIdType> ids;
  ids.reserve(static_cast<size_t>(std::min<vtkIdType>(nCells, 1024)));
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (insideMask[static_cast<size_t>(cid)])
    {
      ids.push_back(cid);
    }
  }

  vtkSMSessionProxyManager* pxm = this->TargetPort->getSource()->proxyManager();
  if (!pxm)
  {
    return;
  }

  vtkSmartPointer<vtkSMSourceProxy> selectionSource;
  selectionSource.TakeReference(
    vtkSMSourceProxy::SafeDownCast(pxm->NewProxy("sources", "IDSelectionSource")));
  if (!selectionSource)
  {
    return;
  }

  vtkSMPropertyHelper(selectionSource, "FieldType").Set(vtkSelectionNode::CELL);
  std::vector<vtkIdType> idPairs;
  idPairs.reserve(ids.size() * 2);
  for (vtkIdType id : ids)
  {
    idPairs.push_back(-1); // process id (builtin / local)
    idPairs.push_back(id);
  }
  vtkSMPropertyHelper idsHelper(selectionSource, "IDs");
  if (idPairs.empty())
  {
    idsHelper.SetNumberOfElements(0);
  }
  else
  {
    idsHelper.Set(idPairs.data(), static_cast<unsigned int>(idPairs.size()));
  }
  selectionSource->UpdateVTKObjects();

  vtkSmartPointer<vtkSMSourceProxy> newAppendSelections;
  newAppendSelections.TakeReference(vtkSMSourceProxy::SafeDownCast(
    vtkSMSelectionHelper::NewAppendSelectionsFromSelectionSource(selectionSource)));
  if (!newAppendSelections)
  {
    return;
  }

  const int modifier = this->currentSelectionModifier();
  vtkSMSourceProxy* baseline = this->BaselineAppendSelections;
  switch (modifier)
  {
    case pqView::PV_SELECTION_ADDITION:
      vtkSMSelectionHelper::AddSelection(baseline, newAppendSelections);
      break;
    case pqView::PV_SELECTION_SUBTRACTION:
      vtkSMSelectionHelper::SubtractSelection(baseline, newAppendSelections);
      break;
    case pqView::PV_SELECTION_TOGGLE:
      vtkSMSelectionHelper::ToggleSelection(baseline, newAppendSelections);
      break;
    case pqView::PV_SELECTION_DEFAULT:
    default:
      vtkSMSelectionHelper::IgnoreSelection(baseline, newAppendSelections);
      break;
  }

  this->TargetPort->setSelectionInput(newAppendSelections, 0);

  if (pqPVApplicationCore* core = pqPVApplicationCore::instance())
  {
    if (pqSelectionManager* selMgr = core->selectionManager())
    {
      selMgr->select(this->TargetPort);
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::installToggleActionContextMenu()
{
  if (!this->Frame || !this->ToggleAction)
  {
    return;
  }

  QToolBar* toolbar = this->Frame->findChild<QToolBar*>();
  if (!toolbar)
  {
    return;
  }

  QWidget* button = toolbar->widgetForAction(this->ToggleAction);
  if (!button)
  {
    return;
  }

  button->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(button, &QWidget::customContextMenuRequested, this,
    [this, button](const QPoint& pos) {
      QMenu menu(button);
      QAction* deferAction = menu.addAction(tr("Apply selection on release only"));
      deferAction->setCheckable(true);
      QObject::connect(&menu, &QMenu::aboutToShow, this, [deferAction, this]() {
        deferAction->setChecked(this->DeferSelectionUntilRelease);
      });
      QObject::connect(deferAction, &QAction::triggered, this, [this, deferAction]() {
        this->DeferSelectionUntilRelease = deferAction->isChecked();
        this->updateToggleActionTooltip();
      });
      menu.exec(button->mapToGlobal(pos));
    });
  this->updateToggleActionTooltip();
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::updateToggleActionTooltip()
{
  if (!this->ToggleAction)
  {
    return;
  }

  QString tip = tr(
    "Toggle interactive sphere selection: select cells inside a movable sphere "
    "(drag to move, hover+wheel to resize). Right-click for options.");
  if (this->DeferSelectionUntilRelease)
  {
    tip += QLatin1Char('\n');
    tip += tr("Selection is applied when the drag is released.");
  }
  this->ToggleAction->setToolTip(tip);
}
