#include "pqSHYXPolygonSelectionController.h"

#include "pqSHYXStatusNotifier.h"

#include "pqActiveObjects.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqSelectionManager.h"
#include "pqView.h"
#include "pqViewFrame.h"

#include "vtkAlgorithm.h"
#include "vtkActor2D.h"
#include "vtkCallbackCommand.h"
#include "vtkCellArray.h"
#include "vtkCommand.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkCoordinate.h"
#include "vtkDataObjectTreeIterator.h"
#include "vtkDataSet.h"
#include "vtkCamera.h"
#include "vtkIdList.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkIntArray.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkNew.h"
#include "vtkObject.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkRenderWindow.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QToolBar>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
// Double-click jitter still counts as the same vertex, so closing on the last
// point does not leave a short spur.
constexpr int kDuplicatePixelDistance2 = 64;

struct ScreenPt
{
  double X = 0.0;
  double Y = 0.0;
};

struct ScreenBox
{
  double X0 = 0.0;
  double Y0 = 0.0;
  double X1 = 0.0;
  double Y1 = 0.0;
  bool Valid = false;

  void add(double x, double y)
  {
    if (!this->Valid)
    {
      this->X0 = this->X1 = x;
      this->Y0 = this->Y1 = y;
      this->Valid = true;
      return;
    }
    this->X0 = std::min(this->X0, x);
    this->Y0 = std::min(this->Y0, y);
    this->X1 = std::max(this->X1, x);
    this->Y1 = std::max(this->Y1, y);
  }

  bool overlaps(const ScreenBox& other) const
  {
    return this->Valid && other.Valid && this->X0 <= other.X1 && this->X1 >= other.X0 &&
      this->Y0 <= other.Y1 && this->Y1 >= other.Y0;
  }
};

struct Projector
{
  std::array<double, 16> Mat = {};
  double Viewport[4] = { 0.0, 0.0, 1.0, 1.0 };
  int SizeX = 0;
  int SizeY = 0;

  bool init(vtkRenderer* ren)
  {
    if (!ren || !ren->GetActiveCamera() || !ren->GetVTKWindow())
    {
      return false;
    }
    // Same matrix vtkRenderer::WorldToView uses. That getter is protected.
    vtkMatrix4x4::DeepCopy(this->Mat.data(),
      ren->GetActiveCamera()->GetCompositeProjectionTransformMatrix(ren->GetTiledAspectRatio(), 0, 1));
    const double* vp = ren->GetViewport();
    std::copy(vp, vp + 4, this->Viewport);
    const int* size = ren->GetVTKWindow()->GetSize();
    this->SizeX = size ? size[0] : 0;
    this->SizeY = size ? size[1] : 0;
    return this->SizeX > 0 && this->SizeY > 0;
  }

  // Window pixels, same space as interactor GetEventPosition. Points behind the camera fail.
  bool toDisplay(const double world[3], double& x, double& y) const
  {
    const double w = world[0] * this->Mat[12] + world[1] * this->Mat[13] + world[2] * this->Mat[14] +
      this->Mat[15];
    if (w <= 1e-8)
    {
      return false;
    }
    const double cx = world[0] * this->Mat[0] + world[1] * this->Mat[1] + world[2] * this->Mat[2] +
      this->Mat[3];
    const double cy = world[0] * this->Mat[4] + world[1] * this->Mat[5] + world[2] * this->Mat[6] +
      this->Mat[7];
    const double nx = cx / w;
    const double ny = cy / w;
    x = (nx + 1.0) * (this->SizeX * (this->Viewport[2] - this->Viewport[0])) / 2.0 +
      this->SizeX * this->Viewport[0];
    y = (ny + 1.0) * (this->SizeY * (this->Viewport[3] - this->Viewport[1])) / 2.0 +
      this->SizeY * this->Viewport[1];
    return true;
  }
};

double cross2(const ScreenPt& o, const ScreenPt& a, const ScreenPt& b)
{
  return (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
}

bool pointInPolygon(const ScreenPt& p, const std::vector<ScreenPt>& poly)
{
  bool inside = false;
  const std::size_t n = poly.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++)
  {
    const double yi = poly[i].Y;
    const double yj = poly[j].Y;
    const bool straddles = (yi > p.Y) != (yj > p.Y);
    if (!straddles)
    {
      continue;
    }
    const double xi = poly[i].X;
    const double xj = poly[j].X;
    const double xCross = (xj - xi) * (p.Y - yi) / (yj - yi) + xi;
    if (p.X < xCross)
    {
      inside = !inside;
    }
  }
  return inside;
}

bool segmentsIntersect(const ScreenPt& a, const ScreenPt& b, const ScreenPt& c, const ScreenPt& d)
{
  constexpr double kEps = 1e-8;
  const double d1 = cross2(c, d, a);
  const double d2 = cross2(c, d, b);
  const double d3 = cross2(a, b, c);
  const double d4 = cross2(a, b, d);
  const bool ab = (d1 > kEps && d2 < -kEps) || (d1 < -kEps && d2 > kEps);
  const bool cd = (d3 > kEps && d4 < -kEps) || (d3 < -kEps && d4 > kEps);
  return ab && cd;
}

std::vector<ScreenPt> convexHull(std::vector<ScreenPt> pts)
{
  std::sort(pts.begin(), pts.end(), [](const ScreenPt& a, const ScreenPt& b) {
    return a.X < b.X || (a.X == b.X && a.Y < b.Y);
  });
  pts.erase(std::unique(pts.begin(), pts.end(),
              [](const ScreenPt& a, const ScreenPt& b) { return a.X == b.X && a.Y == b.Y; }),
    pts.end());
  if (pts.size() <= 2)
  {
    return pts;
  }
  std::vector<ScreenPt> hull;
  for (const ScreenPt& p : pts)
  {
    while (hull.size() >= 2 && cross2(hull[hull.size() - 2], hull.back(), p) <= 0.0)
    {
      hull.pop_back();
    }
    hull.push_back(p);
  }
  const std::size_t lower = hull.size();
  // Upper chain: pts[n-2] down to pts[0]. The closing duplicate is popped below.
  for (std::size_t rev = pts.size() - 1; rev > 0; --rev)
  {
    const ScreenPt& p = pts[rev - 1];
    while (hull.size() > lower && cross2(hull[hull.size() - 2], hull.back(), p) <= 0.0)
    {
      hull.pop_back();
    }
    hull.push_back(p);
  }
  if (!hull.empty())
  {
    hull.pop_back();
  }
  return hull;
}

bool polygonsOverlap(const std::vector<ScreenPt>& cellPts, const std::vector<ScreenPt>& poly)
{
  if (cellPts.empty() || poly.size() < 3)
  {
    return false;
  }
  for (const ScreenPt& p : cellPts)
  {
    if (pointInPolygon(p, poly))
    {
      return true;
    }
  }
  if (cellPts.size() == 1)
  {
    return false;
  }
  const std::vector<ScreenPt> hull = convexHull(cellPts);
  if (hull.size() < 2)
  {
    return false;
  }
  if (hull.size() == 2)
  {
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    {
      if (segmentsIntersect(hull[0], hull[1], poly[j], poly[i]))
      {
        return true;
      }
    }
    return false;
  }
  for (const ScreenPt& p : poly)
  {
    if (pointInPolygon(p, hull))
    {
      return true;
    }
  }
  for (std::size_t i = 0, j = hull.size() - 1; i < hull.size(); j = i++)
  {
    for (std::size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
    {
      if (segmentsIntersect(hull[j], hull[i], poly[b], poly[a]))
      {
        return true;
      }
    }
  }
  return false;
}

bool datasetMightHit(
  vtkDataSet* ds, const Projector& proj, const ScreenBox& polyBox)
{
  double bounds[6];
  ds->GetBounds(bounds);
  if (!vtkMath::AreBoundsInitialized(bounds))
  {
    return false;
  }
  bool anyFront = false;
  bool allFront = true;
  ScreenBox box;
  for (int iz = 0; iz < 2; ++iz)
  {
    for (int iy = 0; iy < 2; ++iy)
    {
      for (int ix = 0; ix < 2; ++ix)
      {
        const double world[3] = { bounds[ix], bounds[2 + iy], bounds[4 + iz] };
        double x = 0.0;
        double y = 0.0;
        if (!proj.toDisplay(world, x, y))
        {
          allFront = false;
          continue;
        }
        anyFront = true;
        box.add(x, y);
      }
    }
  }
  if (!anyFront)
  {
    return false;
  }
  if (allFront && !box.overlaps(polyBox))
  {
    return false;
  }
  return true;
}

struct LeafRef
{
  vtkDataSet* DataSet = nullptr;
  unsigned int FlatIndex = 0;
};

void collectLeaves(vtkDataObject* obj, std::vector<LeafRef>& out, bool& isComposite)
{
  out.clear();
  isComposite = false;
  if (!obj)
  {
    return;
  }
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    if (ds->GetNumberOfCells() > 0)
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
      if (leaf->GetNumberOfCells() > 0)
      {
        out.push_back({ leaf, it->GetCurrentFlatIndex() });
      }
    }
  }
}

void appendCellsHitByPolygon(vtkDataSet* ds, const Projector& proj,
  const std::vector<ScreenPt>& poly, const ScreenBox& polyBox, std::vector<vtkIdType>& ids)
{
  if (!ds || !datasetMightHit(ds, proj, polyBox))
  {
    return;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    pd->BuildCells();
  }
  vtkNew<vtkIdList> ptIds;
  const vtkIdType nCells = ds->GetNumberOfCells();
  std::vector<ScreenPt> projected;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    ds->GetCellPoints(cid, ptIds);
    const vtkIdType npts = ptIds->GetNumberOfIds();
    if (npts <= 0)
    {
      continue;
    }
    projected.clear();
    ScreenBox cellBox;
    for (vtkIdType i = 0; i < npts; ++i)
    {
      double world[3];
      ds->GetPoint(ptIds->GetId(i), world);
      double x = 0.0;
      double y = 0.0;
      if (!proj.toDisplay(world, x, y))
      {
        continue;
      }
      projected.push_back({ x, y });
      cellBox.add(x, y);
    }
    if (projected.empty() || (cellBox.Valid && !cellBox.overlaps(polyBox)))
    {
      continue;
    }
    if (polygonsOverlap(projected, poly))
    {
      ids.push_back(cid);
    }
  }
}

const char* kOtherModeActions[] = {
  "actionSelectSurfaceCells",
  "actionSelectSurfacePoints",
  "actionSelectFrustumCells",
  "actionSelectFrustumPoints",
  "actionPolygonSelectionCells",
  "actionPolygonSelectionPoints",
  "actionSelectBlock",
  "actionSelectFrustumBlocks",
  "actionInteractiveSelectSurfaceCellData",
  "actionInteractiveSelectSurfacePointData",
  "actionInteractiveSelectSurfaceCells",
  "actionInteractiveSelectSurfacePoints",
  "actionHoveringSurfaceCells",
  "actionHoveringSurfacePoints",
  "actionSHYXSphereSelection",
};
}

//-----------------------------------------------------------------------------
pqSHYXPolygonSelectionController::pqSHYXPolygonSelectionController(
  pqRenderView* view, pqViewFrame* frame, QAction* toggleAction, QObject* parent)
  : Superclass(parent)
  , View(view)
  , Frame(frame)
  , ToggleAction(toggleAction)
{
  QObject::connect(
    toggleAction, &QAction::toggled, this, &pqSHYXPolygonSelectionController::onToggled);
  this->installToggleActionContextMenu();
}

//-----------------------------------------------------------------------------
pqSHYXPolygonSelectionController::~pqSHYXPolygonSelectionController()
{
  this->disablePolygon();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::ProcessEvents(
  vtkObject* caller, unsigned long eid, void* clientdata, void* /*calldata*/)
{
  Q_UNUSED(caller);
  auto* self = reinterpret_cast<pqSHYXPolygonSelectionController*>(clientdata);
  if (self && self->Enabled)
  {
    self->handleInteractorEvent(eid);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::onToggled(bool checked)
{
  if (checked)
  {
    if (!this->enablePolygon())
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
    this->disablePolygon();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::onOtherSelectionToggled(bool checked)
{
  if (checked && this->ToggleAction && this->ToggleAction->isChecked())
  {
    this->ToggleAction->setChecked(false);
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXPolygonSelectionController::eventFilter(QObject* watched, QEvent* event)
{
  if (!this->Enabled || !event || watched != this->FilteredWidget)
  {
    return false;
  }

  const QEvent::Type type = event->type();
  if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease ||
    type == QEvent::MouseButtonDblClick || type == QEvent::ContextMenu)
  {
    if (type == QEvent::ContextMenu)
    {
      return true;
    }
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::RightButton)
    {
      if (type == QEvent::MouseButtonPress)
      {
        this->undoLastPoint();
      }
      return true;
    }
    return false;
  }

  if (type == QEvent::KeyPress || type == QEvent::ShortcutOverride)
  {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape &&
      !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    {
      if (type == QEvent::ShortcutOverride)
      {
        event->accept();
        return false;
      }
      this->onEscape();
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::handleInteractorEvent(unsigned long eid)
{
  vtkSMRenderViewProxy* rmp = this->View ? this->View->getRenderViewProxy() : nullptr;
  vtkRenderWindowInteractor* iren = rmp ? rmp->GetInteractor() : nullptr;
  if (!iren || !this->Observer)
  {
    return;
  }
  const int* pos = iren->GetEventPosition();

  switch (eid)
  {
    case vtkCommand::LeftButtonPressEvent:
      this->addPoint(pos[0], pos[1]);
      this->Observer->AbortFlagOn();
      break;
    case vtkCommand::LeftButtonReleaseEvent:
      this->Observer->AbortFlagOn();
      break;
    case vtkCommand::LeftButtonDoubleClickEvent:
      this->closePolygon();
      this->Observer->AbortFlagOn();
      break;
    case vtkCommand::MouseMoveEvent:
      this->Cursor[0] = pos[0];
      this->Cursor[1] = pos[1];
      this->CursorValid = true;
      if (!this->Points.empty())
      {
        this->rebuildPreview();
        this->renderView();
      }
      break;
    default:
      break;
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXPolygonSelectionController::enablePolygon()
{
  vtkSMRenderViewProxy* rmp = this->View ? this->View->getRenderViewProxy() : nullptr;
  vtkRenderer* ren = rmp ? rmp->GetRenderer() : nullptr;
  vtkRenderWindowInteractor* iren = rmp ? rmp->GetInteractor() : nullptr;
  QWidget* widget = this->View ? this->View->widget() : nullptr;
  if (!ren || !iren || !widget)
  {
    return false;
  }

  this->releaseOtherSelectionModes();

  this->Poly = vtkSmartPointer<vtkPolyData>::New();
  this->Mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->Mapper->SetInputData(this->Poly);
  this->Mapper->ScalarVisibilityOff();
  this->DisplayCoordinate = vtkSmartPointer<vtkCoordinate>::New();
  this->DisplayCoordinate->SetCoordinateSystemToDisplay();
  this->Mapper->SetTransformCoordinate(this->DisplayCoordinate);

  this->Actor = vtkSmartPointer<vtkActor2D>::New();
  this->Actor->SetMapper(this->Mapper);
  this->Actor->GetPositionCoordinate()->SetCoordinateSystemToViewport();
  this->Actor->SetPosition(0.0, 0.0);
  this->Actor->GetProperty()->SetColor(1.0, 0.86, 0.15);
  this->Actor->GetProperty()->SetLineWidth(2.0);
  this->Actor->GetProperty()->SetPointSize(8.0);
  this->Actor->GetProperty()->SetDisplayLocationToForeground();
  this->Actor->PickableOff();
  this->Actor->VisibilityOff();
  ren->AddViewProp(this->Actor);

  this->Observer = vtkSmartPointer<vtkCallbackCommand>::New();
  this->Observer->SetClientData(this);
  this->Observer->SetCallback(&pqSHYXPolygonSelectionController::ProcessEvents);
  iren->AddObserver(vtkCommand::LeftButtonPressEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::LeftButtonReleaseEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::LeftButtonDoubleClickEvent, this->Observer, 1.0);
  iren->AddObserver(vtkCommand::MouseMoveEvent, this->Observer, 1.0);

  widget->removeEventFilter(this);
  widget->installEventFilter(this);
  this->FilteredWidget = widget;

  this->Points.clear();
  this->CursorValid = false;
  this->Enabled = true;
  this->View->setCursor(Qt::CrossCursor);
  pqSHYXStatusNotifier::info(tr(
    "Polygon selection: left-click adds a vertex, right-click undoes, double-click closes. "
    "Right-click the button to toggle Select through."));
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::disablePolygon()
{
  this->Enabled = false;

  if (this->View && this->View->getRenderViewProxy() && this->Observer)
  {
    if (vtkRenderWindowInteractor* iren = this->View->getRenderViewProxy()->GetInteractor())
    {
      iren->RemoveObserver(this->Observer);
    }
  }
  this->Observer = nullptr;

  if (this->View && this->View->getRenderViewProxy() && this->Actor)
  {
    if (vtkRenderer* ren = this->View->getRenderViewProxy()->GetRenderer())
    {
      ren->RemoveViewProp(this->Actor);
    }
  }
  this->Actor = nullptr;
  this->Mapper = nullptr;
  this->Poly = nullptr;
  this->DisplayCoordinate = nullptr;

  if (this->FilteredWidget)
  {
    this->FilteredWidget->removeEventFilter(this);
  }
  this->FilteredWidget = nullptr;

  this->Points.clear();
  this->CursorValid = false;

  if (this->View)
  {
    // Another selection mode may already have installed its own cursor.
    if (!this->otherSelectionModeChecked())
    {
      this->View->setCursor(QCursor());
    }
    this->renderView();
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXPolygonSelectionController::otherSelectionModeChecked() const
{
  if (!this->Frame)
  {
    return false;
  }
  for (const char* name : kOtherModeActions)
  {
    QAction* action = this->Frame->findChild<QAction*>(QLatin1String(name));
    if (action && action->isChecked())
    {
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::releaseOtherSelectionModes()
{
  if (!this->Frame)
  {
    return;
  }
  for (const char* name : kOtherModeActions)
  {
    QAction* action = this->Frame->findChild<QAction*>(QLatin1String(name));
    if (!action)
    {
      continue;
    }
    QObject::connect(action, &QAction::toggled, this,
      &pqSHYXPolygonSelectionController::onOtherSelectionToggled, Qt::UniqueConnection);
    if (action->isCheckable() && action->isChecked())
    {
      action->setChecked(false);
    }
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXPolygonSelectionController::nearLastPoint(int x, int y) const
{
  if (this->Points.empty())
  {
    return false;
  }
  const vtkVector2i& last = this->Points.back();
  const int dx = x - last[0];
  const int dy = y - last[1];
  return dx * dx + dy * dy <= kDuplicatePixelDistance2;
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::addPoint(int x, int y)
{
  if (!this->Enabled || this->nearLastPoint(x, y))
  {
    return;
  }
  this->Points.emplace_back(x, y);
  this->Cursor[0] = x;
  this->Cursor[1] = y;
  this->CursorValid = true;
  this->rebuildPreview();
  this->renderView();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::undoLastPoint()
{
  if (!this->Enabled || this->Points.empty())
  {
    return;
  }
  this->Points.pop_back();
  this->rebuildPreview();
  this->renderView();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::clearDraft()
{
  this->Points.clear();
  this->CursorValid = false;
  this->rebuildPreview();
  this->renderView();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::onEscape()
{
  if (!this->Points.empty())
  {
    this->clearDraft();
    return;
  }
  if (this->ToggleAction)
  {
    this->ToggleAction->setChecked(false);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::installToggleActionContextMenu()
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
      QAction* throughAction = menu.addAction(tr("Select through"));
      throughAction->setCheckable(true);
      throughAction->setChecked(this->SelectThrough);
      throughAction->setToolTip(tr(
        "Select cells along the view direction, including cells hidden behind the front surface. "
        "Off: visible surface cells only."));
      QObject::connect(throughAction, &QAction::toggled, this, [this](bool on) {
        this->SelectThrough = on;
        this->updateToggleActionTooltip();
        if (on)
        {
          pqSHYXStatusNotifier::info(tr(
            "Polygon selection: through. Hidden cells along the view are included."));
        }
        else
        {
          pqSHYXStatusNotifier::info(tr("Polygon selection: visible surface cells only."));
        }
      });
      menu.exec(button->mapToGlobal(pos));
    });
  this->updateToggleActionTooltip();
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::updateToggleActionTooltip()
{
  if (!this->ToggleAction)
  {
    return;
  }
  QString tip = tr(
    "Polygon cell selection.\n"
    "Left-click: add a vertex. Right-click: undo the last vertex. Double-click: close and select.\n"
    "Right-click this button to toggle Select through.\n"
    "Stays on after a selection. Ctrl adds, Shift subtracts, Ctrl+Shift toggles. "
    "Esc clears the draft, or leaves the mode when the draft is empty.");
  tip += QLatin1Char('\n');
  if (this->SelectThrough)
  {
    tip += tr("Select through is on: cells hidden behind the front surface are included.");
  }
  else
  {
    tip += tr("Select through is off: only visible surface cells are selected.");
  }
  this->ToggleAction->setToolTip(tip);
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::selectThroughCells(
  const std::vector<vtkVector2i>& polygonPts, int modifier)
{
  vtkRenderer* ren = this->View && this->View->getRenderViewProxy()
    ? this->View->getRenderViewProxy()->GetRenderer()
    : nullptr;
  Projector proj;
  if (!ren || !proj.init(ren) || polygonPts.size() < 3)
  {
    pqSHYXStatusNotifier::warning(tr("Polygon through selection could not read the view."));
    return;
  }

  std::vector<ScreenPt> poly;
  poly.reserve(polygonPts.size());
  ScreenBox polyBox;
  for (const vtkVector2i& p : polygonPts)
  {
    poly.push_back({ static_cast<double>(p[0]), static_cast<double>(p[1]) });
    polyBox.add(poly.back().X, poly.back().Y);
  }

  const bool multiple = this->View->getUseMultipleRepresentationSelection();
  struct Applied
  {
    pqOutputPort* Port = nullptr;
    vtkSmartPointer<vtkSMSourceProxy> Selection;
  };
  std::vector<Applied> applied;

  QApplication::setOverrideCursor(Qt::WaitCursor);
  const QList<pqRepresentation*> reprs = this->View->getRepresentations();
  for (pqRepresentation* repr : reprs)
  {
    auto* dataRepr = qobject_cast<pqDataRepresentation*>(repr);
    if (!dataRepr || !dataRepr->isVisible() || !dataRepr->getProxy())
    {
      continue;
    }
    if (vtkSMProperty* pickable = dataRepr->getProxy()->GetProperty("Pickable"))
    {
      if (vtkSMPropertyHelper(pickable).GetAsInt() == 0)
      {
        continue;
      }
    }

    pqOutputPort* port = dataRepr->getOutputPortFromInput();
    if (!port || !port->getSource())
    {
      continue;
    }
    vtkSMSourceProxy* src = vtkSMSourceProxy::SafeDownCast(port->getSource()->getProxy());
    vtkAlgorithm* alg = src ? vtkAlgorithm::SafeDownCast(src->GetClientSideObject()) : nullptr;
    vtkDataObject* root = alg ? alg->GetOutputDataObject(port->getPortNumber()) : nullptr;
    if (!root)
    {
      continue;
    }

    std::vector<LeafRef> leaves;
    bool isComposite = false;
    collectLeaves(root, leaves, isComposite);
    if (leaves.empty())
    {
      continue;
    }

    vtkNew<vtkSelection> selection;
    for (const LeafRef& leaf : leaves)
    {
      std::vector<vtkIdType> ids;
      appendCellsHitByPolygon(leaf.DataSet, proj, poly, polyBox, ids);
      if (ids.empty())
      {
        continue;
      }
      vtkNew<vtkSelectionNode> node;
      node->SetFieldType(vtkSelectionNode::CELL);
      node->SetContentType(vtkSelectionNode::INDICES);
      if (isComposite)
      {
        node->GetProperties()->Set(
          vtkSelectionNode::COMPOSITE_INDEX(), static_cast<vtkIdType>(leaf.FlatIndex));
      }
      vtkNew<vtkIdTypeArray> idArray;
      idArray->SetNumberOfComponents(1);
      idArray->SetNumberOfTuples(static_cast<vtkIdType>(ids.size()));
      for (vtkIdType i = 0; i < static_cast<vtkIdType>(ids.size()); ++i)
      {
        idArray->SetValue(i, ids[static_cast<std::size_t>(i)]);
      }
      node->SetSelectionList(idArray);
      selection->AddNode(node);
    }
    if (selection->GetNumberOfNodes() == 0)
    {
      continue;
    }

    vtkSMProxy* producer = port->getSource()->getProxy();
    vtkSMSessionProxyManager* pxm = port->getSource()->proxyManager();
    if (!producer || !pxm)
    {
      continue;
    }
    vtkSmartPointer<vtkSMSourceProxy> selectionSource;
    selectionSource.TakeReference(vtkSMSourceProxy::SafeDownCast(
      vtkSMSelectionHelper::NewSelectionSourceFromSelection(
        producer->GetSession(), selection, /*ignore_composite_keys=*/ !isComposite)));
    if (!selectionSource)
    {
      continue;
    }
    vtkSmartPointer<vtkSMSourceProxy> appendSelections;
    appendSelections.TakeReference(vtkSMSourceProxy::SafeDownCast(
      vtkSMSelectionHelper::NewAppendSelectionsFromSelectionSource(selectionSource)));
    if (!appendSelections)
    {
      continue;
    }

    vtkSMSourceProxy* baseline = port->getSelectionInput();
    switch (modifier)
    {
      case pqView::PV_SELECTION_ADDITION:
        vtkSMSelectionHelper::AddSelection(baseline, appendSelections);
        break;
      case pqView::PV_SELECTION_SUBTRACTION:
        vtkSMSelectionHelper::SubtractSelection(baseline, appendSelections);
        break;
      case pqView::PV_SELECTION_TOGGLE:
        vtkSMSelectionHelper::ToggleSelection(baseline, appendSelections);
        break;
      case pqView::PV_SELECTION_DEFAULT:
      default:
        vtkSMSelectionHelper::IgnoreSelection(baseline, appendSelections);
        break;
    }
    port->setSelectionInput(appendSelections, 0);
    applied.push_back({ port, appendSelections });
    if (!multiple)
    {
      break;
    }
  }
  QApplication::restoreOverrideCursor();

  if (applied.empty())
  {
    pqSHYXStatusNotifier::warning(tr("Polygon through selection did not hit any cells."));
    return;
  }

  for (const Applied& item : applied)
  {
    if (item.Port)
    {
      item.Port->renderAllViews(false);
    }
  }

  pqOutputPort* primary = nullptr;
  if (pqOutputPort* activePort = pqActiveObjects::instance().activePort())
  {
    for (const Applied& item : applied)
    {
      if (item.Port == activePort)
      {
        primary = activePort;
        break;
      }
    }
  }
  if (!primary)
  {
    primary = applied.front().Port;
  }
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
  for (const Applied& item : applied)
  {
    if (item.Port && item.Port != primary && item.Selection)
    {
      item.Port->setSelectionInput(item.Selection, 0);
      item.Port->renderAllViews(false);
    }
  }
  this->renderView();
  pqSHYXStatusNotifier::info(
    tr("Polygon through selection applied. Draw another, or toggle the button off."));
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::closePolygon()
{
  if (!this->Enabled || !this->View)
  {
    return;
  }
  if (this->Points.size() < 3)
  {
    pqSHYXStatusNotifier::warning(
      tr("Polygon selection needs at least 3 vertices. Double-click again after adding more."));
    return;
  }

  const std::vector<vtkVector2i> polygonPts = this->Points;
  const int modifier = this->currentSelectionModifier();
  const bool through = this->SelectThrough;

  // Hide the draft before selection so the stroke is not in the pick buffer.
  this->Points.clear();
  this->CursorValid = false;
  this->rebuildPreview();

  if (through)
  {
    this->selectThroughCells(polygonPts, modifier);
    return;
  }

  vtkNew<vtkIntArray> polygon;
  polygon->SetNumberOfComponents(2);
  polygon->SetNumberOfTuples(static_cast<vtkIdType>(polygonPts.size()));
  for (vtkIdType i = 0; i < polygon->GetNumberOfTuples(); ++i)
  {
    const vtkVector2i& p = polygonPts[static_cast<std::size_t>(i)];
    const int xy[2] = { p[0], p[1] };
    polygon->SetTypedTuple(i, xy);
  }
  this->View->selectPolygonCells(polygon, modifier);
  pqSHYXStatusNotifier::info(tr("Polygon selection applied. Draw another, or toggle the button off."));
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::rebuildPreview()
{
  if (!this->Poly || !this->Actor)
  {
    return;
  }

  const vtkIdType n = static_cast<vtkIdType>(this->Points.size());
  const bool rubber = this->CursorValid && n > 0;

  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(n + (rubber ? 1 : 0));
  for (vtkIdType i = 0; i < n; ++i)
  {
    const vtkVector2i& p = this->Points[static_cast<size_t>(i)];
    pts->SetPoint(i, p[0], p[1], 0.0);
  }
  if (rubber)
  {
    pts->SetPoint(n, this->Cursor[0], this->Cursor[1], 0.0);
  }

  vtkNew<vtkCellArray> lines;
  if (n >= 2)
  {
    std::vector<vtkIdType> ids(static_cast<size_t>(n));
    for (vtkIdType i = 0; i < n; ++i)
    {
      ids[static_cast<size_t>(i)] = i;
    }
    lines->InsertNextCell(n, ids.data());
  }
  if (rubber)
  {
    const vtkIdType seg[2] = { n - 1, n };
    lines->InsertNextCell(2, seg);
    if (n >= 2)
    {
      const vtkIdType closeSeg[2] = { n, 0 };
      lines->InsertNextCell(2, closeSeg);
    }
  }

  vtkNew<vtkCellArray> verts;
  for (vtkIdType i = 0; i < n; ++i)
  {
    const vtkIdType id = i;
    verts->InsertNextCell(1, &id);
  }

  this->Poly->SetPoints(pts);
  this->Poly->SetLines(lines);
  this->Poly->SetVerts(verts);
  this->Actor->SetVisibility(n > 0 ? 1 : 0);
}

//-----------------------------------------------------------------------------
void pqSHYXPolygonSelectionController::renderView()
{
  if (this->View)
  {
    this->View->render();
  }
}

//-----------------------------------------------------------------------------
int pqSHYXPolygonSelectionController::currentSelectionModifier() const
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
  if (QAction* action = checkedModifier("actionAddSelection"))
  {
    if (action->isChecked())
    {
      return pqView::PV_SELECTION_ADDITION;
    }
  }
  if (QAction* action = checkedModifier("actionSubtractSelection"))
  {
    if (action->isChecked())
    {
      return pqView::PV_SELECTION_SUBTRACTION;
    }
  }
  if (QAction* action = checkedModifier("actionToggleSelection"))
  {
    if (action->isChecked())
    {
      return pqView::PV_SELECTION_TOGGLE;
    }
  }
  return pqView::PV_SELECTION_DEFAULT;
}
