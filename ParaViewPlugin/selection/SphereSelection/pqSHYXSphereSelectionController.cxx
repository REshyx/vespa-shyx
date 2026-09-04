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
#include "vtkDataObject.h"
#include "vtkDataObjectTreeIterator.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInteractorObserver.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPointSet.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSession.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"
#include "vtkStaticCellLocator.h"
#include "vtkStaticPointLocator.h"
// Qt defines emit as a macro; TBB profiling.h has methods named emit().
#ifdef emit
#  undef emit
#endif
#include "vtkSphereSource.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QToolBar>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
constexpr double kWheelScaleUp = 1.1;
constexpr double kWheelScaleDown = 1.0 / 1.1;

struct LeafRef
{
  vtkDataSet* DataSet = nullptr;
  unsigned int FlatIndex = 0;
};

void CollectLeaves(vtkDataObject* obj, std::vector<LeafRef>& out, bool& isComposite)
{
  out.clear();
  isComposite = false;
  if (!obj)
  {
    return;
  }
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    if (ds->GetNumberOfPoints() > 0)
    {
      out.push_back({ ds, 0 });
    }
    return;
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(obj);
  if (!cds)
  {
    return;
  }
  isComposite = true;
  vtkSmartPointer<vtkCompositeDataIterator> it;
  it.TakeReference(cds->NewIterator());
  it->SkipEmptyNodesOn();
  if (auto* treeIt = vtkDataObjectTreeIterator::SafeDownCast(it))
  {
    treeIt->VisitOnlyLeavesOn();
    treeIt->TraverseSubTreeOn();
  }
  for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    if (auto* leaf = vtkDataSet::SafeDownCast(it->GetCurrentDataObject()))
    {
      if (leaf->GetNumberOfPoints() > 0)
      {
        out.push_back({ leaf, it->GetCurrentFlatIndex() });
      }
    }
  }
}

bool GetDataBounds(vtkDataObject* obj, double b[6])
{
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    ds->GetBounds(b);
    return true;
  }
  if (auto* cds = vtkCompositeDataSet::SafeDownCast(obj))
  {
    cds->GetBounds(b);
    return true;
  }
  return false;
}

bool AabbOverlapsSphere(const double b[6], const double c[3], double radius)
{
  const double closest[3] = { std::max(b[0], std::min(c[0], b[1])),
    std::max(b[2], std::min(c[1], b[3])), std::max(b[4], std::min(c[2], b[5])) };
  return vtkMath::Distance2BetweenPoints(closest, c) <= radius * radius;
}

void AppendCellsWithVertexInSphere(vtkDataSet* ds, vtkStaticCellLocator* locator,
  const double center[3], double radius, std::vector<vtkIdType>& ids)
{
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return;
  }

  const double r2 = radius * radius;
  const vtkIdType nCells = ds->GetNumberOfCells();
  vtkNew<vtkIdList> ptIds;

  auto acceptCell = [&](vtkIdType cid) {
    if (cid < 0 || cid >= nCells)
    {
      return;
    }
    ds->GetCellPoints(cid, ptIds);
    const vtkIdType npts = ptIds->GetNumberOfIds();
    for (vtkIdType p = 0; p < npts; ++p)
    {
      double xyz[3];
      ds->GetPoint(ptIds->GetId(p), xyz);
      if (vtkMath::Distance2BetweenPoints(xyz, center) <= r2)
      {
        ids.push_back(cid);
        return;
      }
    }
  };

  if (locator)
  {
    double bbox[6] = { center[0] - radius, center[0] + radius, center[1] - radius,
      center[1] + radius, center[2] - radius, center[2] + radius };
    vtkNew<vtkIdList> candidateCells;
    locator->FindCellsWithinBounds(bbox, candidateCells);
    const vtkIdType nCand = candidateCells->GetNumberOfIds();
    for (vtkIdType i = 0; i < nCand; ++i)
    {
      acceptCell(candidateCells->GetId(i));
    }
  }
  else
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      acceptCell(cid);
    }
  }
}

bool FindClosestPointOnLeaves(
  const std::vector<LeafRef>& leaves, const double world[3], double out[3])
{
  vtkDataSet* bestDs = nullptr;
  double bestBoxDist2 = std::numeric_limits<double>::max();
  for (const LeafRef& leaf : leaves)
  {
    if (!leaf.DataSet)
    {
      continue;
    }
    double b[6];
    leaf.DataSet->GetBounds(b);
    const double closest[3] = { std::max(b[0], std::min(world[0], b[1])),
      std::max(b[2], std::min(world[1], b[3])), std::max(b[4], std::min(world[2], b[5])) };
    const double d2 = vtkMath::Distance2BetweenPoints(closest, world);
    if (d2 < bestBoxDist2)
    {
      bestBoxDist2 = d2;
      bestDs = leaf.DataSet;
    }
  }
  if (!bestDs || bestDs->GetNumberOfPoints() == 0)
  {
    return false;
  }

  if (auto* ps = vtkPointSet::SafeDownCast(bestDs))
  {
    vtkNew<vtkStaticPointLocator> loc;
    loc->SetDataSet(ps);
    loc->BuildLocator();
    const vtkIdType id = loc->FindClosestPoint(world[0], world[1], world[2]);
    if (id >= 0)
    {
      bestDs->GetPoint(id, out);
      return true;
    }
  }

  const vtkIdType npts = bestDs->GetNumberOfPoints();
  vtkIdType bestId = 0;
  double bestDist2 = std::numeric_limits<double>::max();
  for (vtkIdType i = 0; i < npts; ++i)
  {
    double p[3];
    bestDs->GetPoint(i, p);
    const double dist2 = vtkMath::Distance2BetweenPoints(world, p);
    if (dist2 < bestDist2)
    {
      bestDist2 = dist2;
      bestId = i;
    }
  }
  bestDs->GetPoint(bestId, out);
  return true;
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
vtkDataObject* pqSHYXSphereSelectionController::resolveDataObject(pqDataRepresentation* repr) const
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
  return alg->GetOutputDataObject(port->getPortNumber());
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::collectVisibleTargets()
{
  this->Targets.clear();
  if (!this->View)
  {
    return false;
  }

  const QList<pqRepresentation*> reprs = this->View->getRepresentations();
  for (pqRepresentation* r : reprs)
  {
    auto* dr = qobject_cast<pqDataRepresentation*>(r);
    if (!dr || !dr->isVisible())
    {
      continue;
    }
    pqOutputPort* port = dr->getOutputPortFromInput();
    vtkDataObject* root = this->resolveDataObject(dr);
    if (!port || !root)
    {
      continue;
    }

    Target target;
    target.Repr = dr;
    target.Port = port;
    target.Root = root;
    this->Targets.push_back(std::move(target));
  }
  return !this->Targets.empty();
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::ensureTargetCaches(Target& target)
{
  vtkDataObject* obj = target.Root;
  if (!obj && target.Repr)
  {
    obj = this->resolveDataObject(target.Repr);
    target.Root = obj;
  }
  if (!obj)
  {
    return false;
  }

  const vtkMTimeType mtime = obj->GetMTime();
  if (target.Root == obj && target.RootMTime == mtime && !target.Leaves.empty())
  {
    return true;
  }

  std::vector<LeafRef> leaves;
  bool isComposite = false;
  CollectLeaves(obj, leaves, isComposite);
  if (leaves.empty())
  {
    target.Leaves.clear();
    target.RootMTime = 0;
    target.IsComposite = false;
    return false;
  }

  target.Leaves.clear();
  target.Leaves.reserve(leaves.size());
  for (const LeafRef& leaf : leaves)
  {
    LeafCache cache;
    cache.FlatIndex = leaf.FlatIndex;
    cache.DataSet = leaf.DataSet;
    if (auto* pd = vtkPolyData::SafeDownCast(leaf.DataSet))
    {
      pd->BuildCells();
    }
    target.Leaves.push_back(std::move(cache));
  }
  target.Root = obj;
  target.RootMTime = mtime;
  target.IsComposite = isComposite;
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::ensureSpatialCaches()
{
  bool any = false;
  for (Target& target : this->Targets)
  {
    if (this->ensureTargetCaches(target))
    {
      any = true;
    }
  }
  return any;
}

//-----------------------------------------------------------------------------
bool pqSHYXSphereSelectionController::enableSphere()
{
  this->disableSphere();

  vtkRenderer* ren = this->renderer();
  vtkSMRenderViewProxy* rmp = this->View ? this->View->getRenderViewProxy() : nullptr;
  vtkRenderWindowInteractor* iren = rmp ? rmp->GetInteractor() : nullptr;
  if (!ren || !iren || !this->collectVisibleTargets())
  {
    qWarning("SHYX sphere selection: no visible mesh in the active RenderView.");
    return false;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool prepared = this->ensureSpatialCaches();
  const bool snapped = prepared && this->snapToCenterVertex();
  const bool radiusOk = snapped && this->computeInitialRadius();
  if (!radiusOk)
  {
    QApplication::restoreOverrideCursor();
    qWarning("SHYX sphere selection: failed to initialize sphere at view center.");
    this->Targets.clear();
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
  QApplication::restoreOverrideCursor();
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
  this->Targets.clear();
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
  vtkSMRenderViewProxy* rmp = this->View ? this->View->getRenderViewProxy() : nullptr;
  if (!ren)
  {
    return false;
  }

  int* size = ren->GetSize();
  if (!size || size[0] <= 0 || size[1] <= 0)
  {
    return false;
  }

  const int displayPos[2] = { size[0] / 2, size[1] / 2 };
  double world[3] = { 0.0, 0.0, 0.0 };
  double normal[3] = { 0.0, 0.0, 0.0 };

  // Hardware/surface pick at view center — hits any visible representation/block.
  if (rmp &&
    rmp->ConvertDisplayToPointOnSurface(displayPos, world, normal, /*snapOnMeshPoint=*/true))
  {
    if (std::isfinite(world[0]) && std::isfinite(world[1]) && std::isfinite(world[2]))
    {
      this->Center[0] = world[0];
      this->Center[1] = world[1];
      this->Center[2] = world[2];
      return true;
    }
  }

  std::vector<LeafRef> leaves;
  double b[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  bool haveBounds = false;
  for (const Target& target : this->Targets)
  {
    double tb[6];
    if (GetDataBounds(target.Root, tb))
    {
      if (!haveBounds)
      {
        std::copy(tb, tb + 6, b);
        haveBounds = true;
      }
      else
      {
        b[0] = std::min(b[0], tb[0]);
        b[1] = std::max(b[1], tb[1]);
        b[2] = std::min(b[2], tb[2]);
        b[3] = std::max(b[3], tb[3]);
        b[4] = std::min(b[4], tb[4]);
        b[5] = std::max(b[5], tb[5]);
      }
    }
    for (const LeafCache& cache : target.Leaves)
    {
      leaves.push_back({ cache.DataSet, cache.FlatIndex });
    }
  }
  if (haveBounds)
  {
    world[0] = 0.5 * (b[0] + b[1]);
    world[1] = 0.5 * (b[2] + b[3]);
    world[2] = 0.5 * (b[4] + b[5]);
  }
  return FindClosestPointOnLeaves(leaves, world, this->Center);
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
    double b[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    bool haveBounds = false;
    for (const Target& target : this->Targets)
    {
      double tb[6];
      if (GetDataBounds(target.Root, tb))
      {
        if (!haveBounds)
        {
          std::copy(tb, tb + 6, b);
          haveBounds = true;
        }
        else
        {
          b[0] = std::min(b[0], tb[0]);
          b[1] = std::max(b[1], tb[1]);
          b[2] = std::min(b[2], tb[2]);
          b[3] = std::max(b[3], tb[3]);
          b[4] = std::min(b[4], tb[4]);
          b[5] = std::max(b[5], tb[5]);
        }
      }
    }
    if (haveBounds)
    {
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
  for (Target& target : this->Targets)
  {
    target.BaselineAppendSelections = nullptr;
    if (!target.Port)
    {
      continue;
    }
    vtkSMSourceProxy* cur = target.Port->getSelectionInput();
    if (cur)
    {
      target.BaselineAppendSelections = cur;
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::applySelectionToTarget(Target& target)
{
  if (!target.Port || !this->ensureTargetCaches(target) || target.Leaves.empty())
  {
    return;
  }

  const double center[3] = { this->Center[0], this->Center[1], this->Center[2] };
  vtkNew<vtkSelection> selection;

  for (LeafCache& cache : target.Leaves)
  {
    vtkDataSet* ds = cache.DataSet;
    if (!ds || ds->GetNumberOfCells() == 0)
    {
      continue;
    }

    double b[6];
    ds->GetBounds(b);
    if (!AabbOverlapsSphere(b, center, this->Radius))
    {
      continue;
    }

    if (!cache.CellLocator)
    {
      if (auto* ps = vtkPointSet::SafeDownCast(ds))
      {
        cache.CellLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
        cache.CellLocator->SetDataSet(ps);
        cache.CellLocator->BuildLocator();
      }
    }

    std::vector<vtkIdType> localIds;
    AppendCellsWithVertexInSphere(ds, cache.CellLocator, center, this->Radius, localIds);
    if (localIds.empty())
    {
      continue;
    }

    vtkNew<vtkSelectionNode> node;
    node->SetFieldType(vtkSelectionNode::CELL);
    node->SetContentType(vtkSelectionNode::INDICES);
    if (target.IsComposite)
    {
      node->GetProperties()->Set(
        vtkSelectionNode::COMPOSITE_INDEX(), static_cast<vtkIdType>(cache.FlatIndex));
    }
    vtkNew<vtkIdTypeArray> ids;
    ids->SetNumberOfComponents(1);
    ids->SetNumberOfTuples(static_cast<vtkIdType>(localIds.size()));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(localIds.size()); ++i)
    {
      ids->SetValue(i, localIds[static_cast<std::size_t>(i)]);
    }
    node->SetSelectionList(ids);
    selection->AddNode(node);
  }

  vtkSMProxy* producer = target.Port->getSource() ? target.Port->getSource()->getProxy() : nullptr;
  vtkSMSessionProxyManager* pxm = target.Port->getSource() ? target.Port->getSource()->proxyManager() : nullptr;
  if (!producer || !pxm)
  {
    return;
  }

  vtkSmartPointer<vtkSMSourceProxy> selectionSource;
  if (selection->GetNumberOfNodes() > 0)
  {
    selectionSource.TakeReference(vtkSMSourceProxy::SafeDownCast(
      vtkSMSelectionHelper::NewSelectionSourceFromSelection(
        producer->GetSession(), selection, /*ignore_composite_keys=*/ !target.IsComposite)));
  }
  else
  {
    const char* sourceType =
      target.IsComposite ? "CompositeDataIDSelectionSource" : "IDSelectionSource";
    selectionSource.TakeReference(
      vtkSMSourceProxy::SafeDownCast(pxm->NewProxy("sources", sourceType)));
    if (selectionSource)
    {
      vtkSMPropertyHelper(selectionSource, "FieldType").Set(vtkSelectionNode::CELL);
      vtkSMPropertyHelper(selectionSource, "IDs").SetNumberOfElements(0);
      selectionSource->UpdateVTKObjects();
    }
  }
  if (!selectionSource)
  {
    return;
  }

  vtkSmartPointer<vtkSMSourceProxy> newAppendSelections;
  newAppendSelections.TakeReference(vtkSMSourceProxy::SafeDownCast(
    vtkSMSelectionHelper::NewAppendSelectionsFromSelectionSource(selectionSource)));
  if (!newAppendSelections)
  {
    return;
  }

  const int modifier = this->currentSelectionModifier();
  vtkSMSourceProxy* baseline = target.BaselineAppendSelections;
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

  target.Port->setSelectionInput(newAppendSelections, 0);
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionController::applySelection()
{
  if (this->Targets.empty())
  {
    return;
  }

  std::vector<std::pair<pqOutputPort*, vtkSmartPointer<vtkSMSourceProxy>>> saved;
  saved.reserve(this->Targets.size());
  for (Target& target : this->Targets)
  {
    this->applySelectionToTarget(target);
    if (target.Port)
    {
      saved.emplace_back(target.Port, target.Port->getSelectionInput());
      target.Port->renderAllViews(false);
    }
  }

  pqOutputPort* primary = nullptr;
  if (pqOutputPort* activePort = pqActiveObjects::instance().activePort())
  {
    for (const auto& item : saved)
    {
      if (item.first == activePort)
      {
        primary = activePort;
        break;
      }
    }
  }
  if (!primary && !saved.empty())
  {
    primary = saved.front().first;
  }

  // Copy Active Selection / get_selection_ids read the selection manager. select() may
  // CleanSelectionInputs on ports that were previously registered, so restore the others.
  if (primary)
  {
    if (pqPVApplicationCore* core = pqPVApplicationCore::instance())
    {
      if (pqSelectionManager* selMgr = core->selectionManager())
      {
        selMgr->select(primary);
      }
    }
  }
  for (const auto& item : saved)
  {
    if (item.first && item.first != primary && item.second)
    {
      item.first->setSelectionInput(item.second, 0);
      item.first->renderAllViews(false);
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
    "(drag to move, hover+wheel to resize). Every visible pipeline node in the view "
    "is considered, including all blocks of a composite parent. Right-click for options.");
  if (this->DeferSelectionUntilRelease)
  {
    tip += QLatin1Char('\n');
    tip += tr("Selection is applied when the drag is released.");
  }
  this->ToggleAction->setToolTip(tip);
}
