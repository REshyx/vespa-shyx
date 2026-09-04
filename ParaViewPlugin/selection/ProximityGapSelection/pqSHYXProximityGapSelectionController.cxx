#include "pqSHYXProximityGapSelectionController.h"

#include "pqActiveObjects.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqRepresentation.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqSelectionManager.h"
#include "pqView.h"
#include "pqViewFrame.h"

#include "vtkAlgorithm.h"
#include "vtkCellType.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkOutputWindow.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"
#include "vtkStaticPointLocator.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QInputDialog>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
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

bool IsSurfaceCell(vtkPolyData* pd, vtkIdType cellId)
{
  const int t = pd->GetCellType(cellId);
  return t == VTK_TRIANGLE || t == VTK_QUAD || t == VTK_POLYGON;
}

double MeanSurfaceEdgeLength(vtkPolyData* pd, vtkIdList* ptIds)
{
  double sum = 0.0;
  vtkIdType n = 0;
  const vtkIdType nCells = pd->GetNumberOfCells();
  vtkPoints* pts = pd->GetPoints();
  if (!pts)
  {
    return 0.0;
  }
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    if (!IsSurfaceCell(pd, c))
    {
      continue;
    }
    pd->GetCellPoints(c, ptIds);
    const vtkIdType npts = ptIds->GetNumberOfIds();
    if (npts < 3)
    {
      continue;
    }
    for (vtkIdType e = 0; e < npts; ++e)
    {
      const vtkIdType a = ptIds->GetId(e);
      const vtkIdType b = ptIds->GetId((e + 1) % npts);
      double pa[3], pb[3];
      pts->GetPoint(a, pa);
      pts->GetPoint(b, pb);
      sum += std::sqrt(vtkMath::Distance2BetweenPoints(pa, pb));
      ++n;
    }
  }
  return n > 0 ? (sum / static_cast<double>(n)) : 0.0;
}

struct LocatorHit
{
  vtkIdType PointId = -1;
  int Comp = -1;
  int Loop = -1;
  int LoopIndex = -1;
  int LoopSize = 0;
};

bool AcceptPair(const LocatorHit& a, const LocatorHit& b, int hopThreshold)
{
  if (a.PointId == b.PointId)
  {
    return false;
  }
  if (a.Comp >= 0 && b.Comp >= 0 && a.Comp != b.Comp)
  {
    return true;
  }
  if (a.Loop >= 0 && b.Loop >= 0 && a.Loop != b.Loop)
  {
    return true;
  }
  if (a.Loop >= 0 && a.Loop == b.Loop && a.LoopSize > 0)
  {
    int d = std::abs(a.LoopIndex - b.LoopIndex);
    d = std::min(d, a.LoopSize - d);
    return d > hopThreshold;
  }
  return false;
}

class UnionFind
{
public:
  explicit UnionFind(vtkIdType n)
    : Parent(static_cast<size_t>(n))
    , Rank(static_cast<size_t>(n), 0)
  {
    for (vtkIdType i = 0; i < n; ++i)
    {
      Parent[static_cast<size_t>(i)] = i;
    }
  }
  vtkIdType find(vtkIdType x)
  {
    if (Parent[static_cast<size_t>(x)] != x)
    {
      Parent[static_cast<size_t>(x)] = find(Parent[static_cast<size_t>(x)]);
    }
    return Parent[static_cast<size_t>(x)];
  }
  void unite(vtkIdType x, vtkIdType y)
  {
    vtkIdType px = find(x);
    vtkIdType py = find(y);
    if (px == py)
    {
      return;
    }
    if (Rank[static_cast<size_t>(px)] < Rank[static_cast<size_t>(py)])
    {
      std::swap(px, py);
    }
    Parent[static_cast<size_t>(py)] = px;
    if (Rank[static_cast<size_t>(px)] == Rank[static_cast<size_t>(py)])
    {
      ++Rank[static_cast<size_t>(px)];
    }
  }

private:
  std::vector<vtkIdType> Parent;
  std::vector<int> Rank;
};

int LabelSurfaceComponents(
  vtkPolyData* pd, std::vector<int>& cellComp, std::vector<int>& pointComp)
{
  const vtkIdType nCells = pd->GetNumberOfCells();
  const vtkIdType nPoints = pd->GetNumberOfPoints();
  cellComp.assign(static_cast<size_t>(nCells), -1);
  pointComp.assign(static_cast<size_t>(nPoints), -1);
  vtkNew<vtkIdList> ptIds;
  vtkNew<vtkIdList> nbrs;
  vtkNew<vtkIdList> cellOnPt;
  int nComp = 0;
  std::vector<vtkIdType> stack;
  stack.reserve(64);
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    if (cellComp[static_cast<size_t>(c)] >= 0 || !IsSurfaceCell(pd, c))
    {
      continue;
    }
    stack.clear();
    stack.push_back(c);
    cellComp[static_cast<size_t>(c)] = nComp;
    while (!stack.empty())
    {
      const vtkIdType cur = stack.back();
      stack.pop_back();
      pd->GetCellPoints(cur, ptIds);
      const vtkIdType npts = ptIds->GetNumberOfIds();
      if (npts < 3)
      {
        continue;
      }
      for (vtkIdType e = 0; e < npts; ++e)
      {
        nbrs->Reset();
        pd->GetCellEdgeNeighbors(
          cur, ptIds->GetId(e), ptIds->GetId((e + 1) % npts), nbrs);
        const vtkIdType nNb = nbrs->GetNumberOfIds();
        for (vtkIdType i = 0; i < nNb; ++i)
        {
          const vtkIdType nb = nbrs->GetId(i);
          if (cellComp[static_cast<size_t>(nb)] < 0)
          {
            cellComp[static_cast<size_t>(nb)] = nComp;
            stack.push_back(nb);
          }
        }
      }
    }
    ++nComp;
  }
  for (vtkIdType p = 0; p < nPoints; ++p)
  {
    pd->GetPointCells(p, cellOnPt);
    for (vtkIdType i = 0; i < cellOnPt->GetNumberOfIds(); ++i)
    {
      const int cc = cellComp[static_cast<size_t>(cellOnPt->GetId(i))];
      if (cc >= 0)
      {
        pointComp[static_cast<size_t>(p)] = cc;
        break;
      }
    }
  }
  return nComp;
}

struct ClosestPair
{
  double Distance = 0.0;
  vtkIdType IdA = -1;
  vtkIdType IdB = -1;
  int CompA = -1;
  int CompB = -1;
};

ClosestPair FindClosestCrossComponentPair(
  vtkPolyData* pd, const std::vector<int>& pointComp, int nComp)
{
  ClosestPair pair;
  if (!pd || !pd->GetPoints() || nComp < 2)
  {
    return pair;
  }

  const vtkIdType nPoints = pd->GetNumberOfPoints();
  std::vector<vtkSmartPointer<vtkPoints>> compPts(static_cast<size_t>(nComp));
  std::vector<std::vector<vtkIdType>> compIds(static_cast<size_t>(nComp));
  for (int c = 0; c < nComp; ++c)
  {
    compPts[static_cast<size_t>(c)] = vtkSmartPointer<vtkPoints>::New();
    compPts[static_cast<size_t>(c)]->SetDataTypeToDouble();
  }
  for (vtkIdType p = 0; p < nPoints; ++p)
  {
    const int cc = pointComp[static_cast<size_t>(p)];
    if (cc < 0)
    {
      continue;
    }
    compIds[static_cast<size_t>(cc)].push_back(p);
    compPts[static_cast<size_t>(cc)]->InsertNextPoint(pd->GetPoint(p));
  }

  std::vector<vtkSmartPointer<vtkPolyData>> locPds(static_cast<size_t>(nComp));
  std::vector<vtkSmartPointer<vtkStaticPointLocator>> locators(static_cast<size_t>(nComp));
  for (int c = 0; c < nComp; ++c)
  {
    if (compPts[static_cast<size_t>(c)]->GetNumberOfPoints() == 0)
    {
      continue;
    }
    locPds[static_cast<size_t>(c)] = vtkSmartPointer<vtkPolyData>::New();
    locPds[static_cast<size_t>(c)]->SetPoints(compPts[static_cast<size_t>(c)]);
    locators[static_cast<size_t>(c)] = vtkSmartPointer<vtkStaticPointLocator>::New();
    locators[static_cast<size_t>(c)]->SetDataSet(locPds[static_cast<size_t>(c)]);
    locators[static_cast<size_t>(c)]->BuildLocator();
  }

  double dmin2 = std::numeric_limits<double>::infinity();
  for (int c = 0; c < nComp; ++c)
  {
    if (!locators[static_cast<size_t>(c)])
    {
      continue;
    }
    for (int d = c + 1; d < nComp; ++d)
    {
      if (!locators[static_cast<size_t>(d)])
      {
        continue;
      }
      const vtkIdType nC = static_cast<vtkIdType>(compIds[static_cast<size_t>(c)].size());
      for (vtkIdType i = 0; i < nC; ++i)
      {
        double pbuf[3];
        compPts[static_cast<size_t>(c)]->GetPoint(i, pbuf);
        const vtkIdType hit =
          locators[static_cast<size_t>(d)]->FindClosestPoint(pbuf);
        if (hit < 0)
        {
          continue;
        }
        double q[3];
        compPts[static_cast<size_t>(d)]->GetPoint(hit, q);
        const double dist2 = vtkMath::Distance2BetweenPoints(pbuf, q);
        if (dist2 < dmin2)
        {
          dmin2 = dist2;
          pair.IdA = compIds[static_cast<size_t>(c)][static_cast<size_t>(i)];
          pair.IdB = compIds[static_cast<size_t>(d)][static_cast<size_t>(hit)];
          pair.CompA = c;
          pair.CompB = d;
        }
      }
    }
  }
  if (std::isfinite(dmin2))
  {
    pair.Distance = std::sqrt(dmin2);
  }
  return pair;
}

void CollectPointsNearCenter(vtkPolyData* pd, const std::vector<int>& pointComp, int comp,
  const double center[3], double epsilon, std::vector<vtkIdType>& outIds)
{
  outIds.clear();
  const double eps2 = epsilon * epsilon;
  const vtkIdType nPoints = pd->GetNumberOfPoints();
  for (vtkIdType p = 0; p < nPoints; ++p)
  {
    if (pointComp[static_cast<size_t>(p)] != comp)
    {
      continue;
    }
    double x[3];
    pd->GetPoint(p, x);
    if (vtkMath::Distance2BetweenPoints(x, center) <= eps2)
    {
      outIds.push_back(p);
    }
  }
}

bool CentroidOfPoints(
  vtkPolyData* pd, const std::vector<vtkIdType>& ids, double center[3])
{
  center[0] = center[1] = center[2] = 0.0;
  if (ids.empty())
  {
    return false;
  }
  for (vtkIdType id : ids)
  {
    double x[3];
    pd->GetPoint(id, x);
    center[0] += x[0];
    center[1] += x[1];
    center[2] += x[2];
  }
  const double inv = 1.0 / static_cast<double>(ids.size());
  center[0] *= inv;
  center[1] *= inv;
  center[2] *= inv;
  return true;
}
}

double pqSHYXProximityGapSelectionController::SharedEpsilon = 0.0;
bool pqSHYXProximityGapSelectionController::SharedSingleNearestRegion = true;
bool pqSHYXProximityGapSelectionController::SharedTowardOppositeCenter = false;

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionController::pqSHYXProximityGapSelectionController(
  pqRenderView* view, pqViewFrame* frame, QAction* action, QObject* parent)
  : Superclass(parent)
  , View(view)
  , Frame(frame)
  , Action(action)
{
  QObject::connect(action, &QAction::toggled, this,
    &pqSHYXProximityGapSelectionController::onToggled);
  QTimer::singleShot(0, this, [this]() { this->installButtonExtras(); });
  this->updateActionTooltip();
}

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionController::~pqSHYXProximityGapSelectionController()
{
  if (this->Button)
  {
    this->Button->removeEventFilter(this);
  }
}

//-----------------------------------------------------------------------------
double pqSHYXProximityGapSelectionController::Epsilon()
{
  return SharedEpsilon;
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::SetEpsilon(double epsilon)
{
  if (!(epsilon > 0.0) || !std::isfinite(epsilon))
  {
    SharedEpsilon = 0.0;
    return;
  }
  SharedEpsilon = epsilon;
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::SingleNearestRegion()
{
  return SharedSingleNearestRegion;
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::SetSingleNearestRegion(bool enabled)
{
  SharedSingleNearestRegion = enabled;
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::TowardOppositeCenter()
{
  return SharedTowardOppositeCenter;
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::SetTowardOppositeCenter(bool enabled)
{
  SharedTowardOppositeCenter = enabled;
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::updateActionTooltip()
{
  if (!this->Action)
  {
    return;
  }
  if (SharedEpsilon > 0.0)
  {
    const QString mode = SharedTowardOppositeCenter
      ? tr("opposite centers")
      : (SharedSingleNearestRegion ? tr("single nearest region") : tr("all regions"));
    this->Action->setToolTip(
      tr("Proximity gap selection (ε = %1, %2).\n"
         "Toggle on to compute. While on, wheel on this button scales ε. "
         "Right-click for ε and modes.")
        .arg(SharedEpsilon, 0, 'g', 6)
        .arg(mode));
  }
  else
  {
    const QString mode = SharedTowardOppositeCenter
      ? tr("opposite centers")
      : (SharedSingleNearestRegion ? tr("single nearest region") : tr("all regions"));
    this->Action->setToolTip(
      tr("Proximity gap selection (ε = auto, %1).\n"
         "Toggle on to compute. While on, wheel on this button scales ε. "
         "Right-click for ε and modes.")
        .arg(mode));
  }
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::installButtonExtras()
{
  if (!this->Frame || !this->Action)
  {
    return;
  }

  QToolBar* toolbar = this->Frame->findChild<QToolBar*>();
  if (!toolbar)
  {
    return;
  }

  QWidget* button = toolbar->widgetForAction(this->Action);
  if (!button)
  {
    return;
  }

  if (button->property("shyxProximityGapExtrasInstalled").toBool())
  {
    return;
  }
  button->setProperty("shyxProximityGapExtrasInstalled", true);
  this->Button = button;
  button->installEventFilter(this);

  if (auto* toolButton = qobject_cast<QToolButton*>(button))
  {
    toolButton->setAutoRepeat(false);
  }

  button->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(button, &QWidget::customContextMenuRequested, this,
    [this, button](const QPoint& pos) {
      QMenu menu(button);
      const QString current = SharedEpsilon > 0.0
        ? QString::number(SharedEpsilon, 'g', 6)
        : tr("auto");
      QAction* setEps = menu.addAction(tr("Set gap distance ε… (current %1)").arg(current));
      QObject::connect(setEps, &QAction::triggered, this,
        &pqSHYXProximityGapSelectionController::promptEpsilon);
      QAction* single = menu.addAction(tr("Single nearest region"));
      single->setCheckable(true);
      single->setChecked(SharedSingleNearestRegion);
      QObject::connect(single, &QAction::toggled, this, [this](bool on) {
        SetSingleNearestRegion(on);
        this->updateActionTooltip();
        this->computeIfArmed();
      });
      QAction* opposite = menu.addAction(
        tr("Toward opposite center (limit parallel growth)"));
      opposite->setCheckable(true);
      opposite->setChecked(SharedTowardOppositeCenter);
      QObject::connect(opposite, &QAction::toggled, this, [this](bool on) {
        SetTowardOppositeCenter(on);
        this->updateActionTooltip();
        this->computeIfArmed();
      });
      menu.exec(button->mapToGlobal(pos));
    });
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == this->Button && event && event->type() == QEvent::Wheel)
  {
    if (!this->isArmed())
    {
      return false;
    }
    auto* we = static_cast<QWheelEvent*>(event);
    const int delta = we->angleDelta().y();
    if (delta != 0)
    {
      const double factor = (delta > 0) ? 1.15 : (1.0 / 1.15);
      this->scaleEpsilonAndSelect(factor);
      we->accept();
      return true;
    }
  }
  return this->Superclass::eventFilter(watched, event);
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::isArmed() const
{
  return this->Action && this->Action->isChecked();
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::computeIfArmed()
{
  if (this->isArmed())
  {
    this->computeAndApply(/*quietSuccess=*/false);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::onToggled(bool checked)
{
  if (checked)
  {
    this->computeAndApply(/*quietSuccess=*/false);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::scaleEpsilonAndSelect(double factor)
{
  pqOutputPort* port = nullptr;
  vtkPolyData* pd = nullptr;
  if (SharedEpsilon <= 0.0)
  {
    if (this->resolveActivePolyData(port, pd))
    {
      SetEpsilon(suggestEpsilon(pd));
    }
  }
  if (SharedEpsilon > 0.0)
  {
    SetEpsilon(SharedEpsilon * factor);
  }
  this->updateActionTooltip();
  this->computeAndApply(/*quietSuccess=*/false);
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::promptEpsilon()
{
  pqOutputPort* port = nullptr;
  vtkPolyData* pd = nullptr;
  double suggested = SharedEpsilon;
  if (suggested <= 0.0 && this->resolveActivePolyData(port, pd))
  {
    suggested = suggestEpsilon(pd);
  }
  if (suggested <= 0.0)
  {
    suggested = 1.0;
  }

  bool ok = false;
  const double value = QInputDialog::getDouble(this->Frame,
    tr("Proximity gap selection"),
    tr("Gap distance ε (world units). 0 = auto (2 × mean surface edge):"),
    suggested, 0.0, 1.0e12, 6, &ok);
  if (!ok)
  {
    return;
  }
  SetEpsilon(value);
  this->updateActionTooltip();
  this->computeIfArmed();
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::reportToOutputWindow(const QString& message)
{
  const QByteArray utf8 = message.toUtf8();
  if (vtkOutputWindow* win = vtkOutputWindow::GetInstance())
  {
    win->DisplayWarningText((utf8 + "\n").constData());
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::resolveActivePolyData(
  pqOutputPort*& portOut, vtkPolyData*& pdOut)
{
  portOut = nullptr;
  pdOut = nullptr;

  pqDataRepresentation* active = pqActiveObjects::instance().activeRepresentation();
  pqOutputPort* port = nullptr;
  if (active && active->isVisible() && (!this->View || active->getView() == this->View))
  {
    port = active->getOutputPortFromInput();
  }
  if (!port && this->View)
  {
    const QList<pqRepresentation*> reprs = this->View->getRepresentations();
    for (pqRepresentation* r : reprs)
    {
      auto* dr = qobject_cast<pqDataRepresentation*>(r);
      if (!dr || !dr->isVisible())
      {
        continue;
      }
      port = dr->getOutputPortFromInput();
      if (port)
      {
        break;
      }
    }
  }
  if (!port || !port->getSource())
  {
    return false;
  }

  vtkSMSourceProxy* src = vtkSMSourceProxy::SafeDownCast(port->getSource()->getProxy());
  if (!src)
  {
    return false;
  }
  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  if (!alg)
  {
    return false;
  }
  vtkDataSet* ds = FirstLeafDataSet(alg->GetOutputDataObject(port->getPortNumber()));
  auto* pd = vtkPolyData::SafeDownCast(ds);
  if (!pd || pd->GetNumberOfCells() == 0 || !pd->GetPoints())
  {
    return false;
  }

  portOut = port;
  pdOut = pd;
  return true;
}

//-----------------------------------------------------------------------------
double pqSHYXProximityGapSelectionController::suggestEpsilon(vtkPolyData* pd)
{
  if (!pd)
  {
    return 0.0;
  }
  vtkNew<vtkIdList> ptIds;
  const double mean = MeanSurfaceEdgeLength(pd, ptIds);
  if (mean > 0.0)
  {
    return 2.0 * mean;
  }
  double b[6];
  pd->GetBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  return diag > 0.0 ? (0.01 * diag) : 0.0;
}

//-----------------------------------------------------------------------------
double pqSHYXProximityGapSelectionController::closestCrossComponentDistance(vtkPolyData* pd)
{
  if (!pd || !pd->GetPoints() || pd->GetNumberOfCells() == 0)
  {
    return 0.0;
  }
  pd->BuildLinks();
  std::vector<int> cellComp;
  std::vector<int> pointComp;
  const int nComp = LabelSurfaceComponents(pd, cellComp, pointComp);
  return FindClosestCrossComponentPair(pd, pointComp, nComp).Distance;
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::selectProximityGap(vtkPolyData* pd, double epsilon,
  bool singleRegion, std::vector<vtkIdType>& cellIds, vtkIdType& nContactPts, int& nCompOut,
  double& closestDist)
{
  cellIds.clear();
  nContactPts = 0;
  nCompOut = 0;
  closestDist = -1.0;
  if (!pd || !(epsilon > 0.0) || !pd->GetPoints())
  {
    return false;
  }

  pd->BuildLinks();
  const vtkIdType nCells = pd->GetNumberOfCells();
  const vtkIdType nPoints = pd->GetNumberOfPoints();
  if (nCells == 0 || nPoints == 0)
  {
    return false;
  }

  vtkNew<vtkIdList> ptIds;
  vtkNew<vtkIdList> nbrs;
  vtkNew<vtkIdList> cellOnPt;

  std::vector<int> cellComp;
  std::vector<int> pointComp;
  const int nComp = LabelSurfaceComponents(pd, cellComp, pointComp);
  nCompOut = nComp;

  std::vector<std::vector<vtkIdType>> adj(static_cast<size_t>(nPoints));
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    if (!IsSurfaceCell(pd, c))
    {
      continue;
    }
    pd->GetCellPoints(c, ptIds);
    const vtkIdType npts = ptIds->GetNumberOfIds();
    if (npts < 3)
    {
      continue;
    }
    for (vtkIdType e = 0; e < npts; ++e)
    {
      const vtkIdType a = ptIds->GetId(e);
      const vtkIdType b = ptIds->GetId((e + 1) % npts);
      nbrs->Reset();
      pd->GetCellEdgeNeighbors(c, a, b, nbrs);
      if (nbrs->GetNumberOfIds() != 0)
      {
        continue;
      }
      adj[static_cast<size_t>(a)].push_back(b);
      adj[static_cast<size_t>(b)].push_back(a);
    }
  }
  for (auto& nbrsOfPt : adj)
  {
    std::sort(nbrsOfPt.begin(), nbrsOfPt.end());
    nbrsOfPt.erase(std::unique(nbrsOfPt.begin(), nbrsOfPt.end()), nbrsOfPt.end());
  }

  auto undirectedKey = [](vtkIdType a, vtkIdType b) -> std::uint64_t {
    const vtkIdType lo = std::min(a, b);
    const vtkIdType hi = std::max(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo)) << 32) |
      static_cast<std::uint32_t>(hi);
  };

  std::unordered_set<std::uint64_t> usedEdges;
  std::vector<int> loopId(static_cast<size_t>(nPoints), -1);
  std::vector<int> loopIndex(static_cast<size_t>(nPoints), -1);
  std::vector<int> loopSize;
  int nLoops = 0;

  for (vtkIdType start = 0; start < nPoints; ++start)
  {
    const auto& nbrStart = adj[static_cast<size_t>(start)];
    if (nbrStart.empty())
    {
      continue;
    }
    for (vtkIdType first : nbrStart)
    {
      const std::uint64_t k0 = undirectedKey(start, first);
      if (usedEdges.count(k0))
      {
        continue;
      }
      std::vector<vtkIdType> chain;
      chain.push_back(start);
      chain.push_back(first);
      usedEdges.insert(k0);
      vtkIdType prev = start;
      vtkIdType cur = first;
      while (true)
      {
        vtkIdType next = -1;
        for (vtkIdType cand : adj[static_cast<size_t>(cur)])
        {
          if (cand == prev)
          {
            continue;
          }
          if (!usedEdges.count(undirectedKey(cur, cand)))
          {
            next = cand;
            break;
          }
        }
        if (next < 0)
        {
          break;
        }
        usedEdges.insert(undirectedKey(cur, next));
        if (next == start)
        {
          break;
        }
        chain.push_back(next);
        prev = cur;
        cur = next;
      }

      const int sz = static_cast<int>(chain.size());
      loopSize.push_back(sz);
      for (int i = 0; i < sz; ++i)
      {
        const vtkIdType pid = chain[static_cast<size_t>(i)];
        if (loopId[static_cast<size_t>(pid)] < 0)
        {
          loopId[static_cast<size_t>(pid)] = nLoops;
          loopIndex[static_cast<size_t>(pid)] = i;
        }
      }
      ++nLoops;
    }
  }

  std::vector<LocatorHit> hits;
  hits.reserve(static_cast<size_t>(nPoints));
  vtkNew<vtkPoints> locPts;
  locPts->SetDataTypeToDouble();
  for (vtkIdType p = 0; p < nPoints; ++p)
  {
    if (pointComp[static_cast<size_t>(p)] < 0)
    {
      continue;
    }
    LocatorHit h;
    h.PointId = p;
    h.Comp = pointComp[static_cast<size_t>(p)];
    h.Loop = loopId[static_cast<size_t>(p)];
    h.LoopIndex = loopIndex[static_cast<size_t>(p)];
    h.LoopSize = (h.Loop >= 0) ? loopSize[static_cast<size_t>(h.Loop)] : 0;
    hits.push_back(h);
    locPts->InsertNextPoint(pd->GetPoint(p));
  }

  if (hits.size() < 2)
  {
    return false;
  }

  vtkNew<vtkPolyData> locPd;
  locPd->SetPoints(locPts);
  vtkNew<vtkStaticPointLocator> locator;
  locator->SetDataSet(locPd);
  locator->BuildLocator();

  const double meanEdge = MeanSurfaceEdgeLength(pd, ptIds);
  int hopThreshold = 4;
  if (meanEdge > 0.0)
  {
    hopThreshold = std::max(4, static_cast<int>(std::ceil(2.0 * epsilon / meanEdge)));
  }

  std::vector<char> contactPt(static_cast<size_t>(nPoints), 0);
  UnionFind uf(nPoints);
  vtkIdType seedPt = -1;
  double closestDist2 = std::numeric_limits<double>::infinity();
  vtkNew<vtkIdList> found;
  const vtkIdType nHit = static_cast<vtkIdType>(hits.size());
  for (vtkIdType i = 0; i < nHit; ++i)
  {
    found->Reset();
    locator->FindPointsWithinRadius(epsilon, locPts->GetPoint(i), found);
    const vtkIdType nFound = found->GetNumberOfIds();
    for (vtkIdType k = 0; k < nFound; ++k)
    {
      const vtkIdType j = found->GetId(k);
      if (j == i || j < 0 || j >= nHit)
      {
        continue;
      }
      if (!AcceptPair(hits[static_cast<size_t>(i)], hits[static_cast<size_t>(j)], hopThreshold))
      {
        continue;
      }
      const vtkIdType pa = hits[static_cast<size_t>(i)].PointId;
      const vtkIdType pb = hits[static_cast<size_t>(j)].PointId;
      contactPt[static_cast<size_t>(pa)] = 1;
      contactPt[static_cast<size_t>(pb)] = 1;
      if (j <= i)
      {
        continue;
      }
      uf.unite(pa, pb);
      double xi[3];
      double xj[3];
      locPts->GetPoint(i, xi);
      locPts->GetPoint(j, xj);
      const double dist2 = vtkMath::Distance2BetweenPoints(xi, xj);
      if (dist2 < closestDist2)
      {
        closestDist2 = dist2;
        seedPt = pa;
      }
    }
  }

  if (std::isfinite(closestDist2))
  {
    closestDist = std::sqrt(closestDist2);
  }

  if (singleRegion && seedPt >= 0)
  {
    for (vtkIdType c = 0; c < nCells; ++c)
    {
      if (!IsSurfaceCell(pd, c))
      {
        continue;
      }
      pd->GetCellPoints(c, ptIds);
      const vtkIdType npts = ptIds->GetNumberOfIds();
      if (npts < 3)
      {
        continue;
      }
      for (vtkIdType e = 0; e < npts; ++e)
      {
        const vtkIdType a = ptIds->GetId(e);
        const vtkIdType b = ptIds->GetId((e + 1) % npts);
        if (contactPt[static_cast<size_t>(a)] && contactPt[static_cast<size_t>(b)])
        {
          uf.unite(a, b);
        }
      }
    }
    const vtkIdType seedRoot = uf.find(seedPt);
    for (vtkIdType p = 0; p < nPoints; ++p)
    {
      if (contactPt[static_cast<size_t>(p)] && uf.find(p) != seedRoot)
      {
        contactPt[static_cast<size_t>(p)] = 0;
      }
    }
  }

  std::unordered_set<vtkIdType> selected;
  for (vtkIdType p = 0; p < nPoints; ++p)
  {
    if (!contactPt[static_cast<size_t>(p)])
    {
      continue;
    }
    ++nContactPts;
    pd->GetPointCells(p, cellOnPt);
    for (vtkIdType i = 0; i < cellOnPt->GetNumberOfIds(); ++i)
    {
      const vtkIdType cid = cellOnPt->GetId(i);
      if (IsSurfaceCell(pd, cid))
      {
        selected.insert(cid);
      }
    }
  }

  cellIds.assign(selected.begin(), selected.end());
  std::sort(cellIds.begin(), cellIds.end());
  return !cellIds.empty();
}

//-----------------------------------------------------------------------------
bool pqSHYXProximityGapSelectionController::selectOppositeCenterGap(vtkPolyData* pd, double epsilon,
  std::vector<vtkIdType>& cellIds, vtkIdType& nContactPts, int& nCompOut, double& closestDist)
{
  cellIds.clear();
  nContactPts = 0;
  nCompOut = 0;
  closestDist = -1.0;
  if (!pd || !(epsilon > 0.0) || !pd->GetPoints())
  {
    return false;
  }

  pd->BuildLinks();
  std::vector<int> cellComp;
  std::vector<int> pointComp;
  const int nComp = LabelSurfaceComponents(pd, cellComp, pointComp);
  nCompOut = nComp;
  const ClosestPair seed = FindClosestCrossComponentPair(pd, pointComp, nComp);
  if (seed.IdA < 0 || seed.IdB < 0 || seed.CompA < 0 || seed.CompB < 0)
  {
    return false;
  }
  closestDist = seed.Distance;

  double cA[3];
  double cB[3];
  pd->GetPoint(seed.IdA, cA);
  pd->GetPoint(seed.IdB, cB);

  std::vector<vtkIdType> setA;
  std::vector<vtkIdType> setB;
  constexpr int kMaxIters = 24;
  const double moveTol2 = std::max(1e-24, 1e-8 * epsilon * epsilon);

  for (int iter = 0; iter < kMaxIters; ++iter)
  {
    CollectPointsNearCenter(pd, pointComp, seed.CompA, cB, epsilon, setA);
    CollectPointsNearCenter(pd, pointComp, seed.CompB, cA, epsilon, setB);
    if (setA.empty())
    {
      setA.push_back(seed.IdA);
    }
    if (setB.empty())
    {
      setB.push_back(seed.IdB);
    }

    double nA[3];
    double nB[3];
    CentroidOfPoints(pd, setA, nA);
    CentroidOfPoints(pd, setB, nB);
    const double dA2 = vtkMath::Distance2BetweenPoints(cA, nA);
    const double dB2 = vtkMath::Distance2BetweenPoints(cB, nB);
    cA[0] = nA[0];
    cA[1] = nA[1];
    cA[2] = nA[2];
    cB[0] = nB[0];
    cB[1] = nB[1];
    cB[2] = nB[2];
    if (dA2 <= moveTol2 && dB2 <= moveTol2)
    {
      break;
    }
  }

  std::unordered_set<vtkIdType> contact;
  contact.insert(setA.begin(), setA.end());
  contact.insert(setB.begin(), setB.end());
  nContactPts = static_cast<vtkIdType>(contact.size());

  vtkNew<vtkIdList> cellOnPt;
  std::unordered_set<vtkIdType> selected;
  for (vtkIdType p : contact)
  {
    pd->GetPointCells(p, cellOnPt);
    for (vtkIdType i = 0; i < cellOnPt->GetNumberOfIds(); ++i)
    {
      const vtkIdType cid = cellOnPt->GetId(i);
      if (IsSurfaceCell(pd, cid))
      {
        selected.insert(cid);
      }
    }
  }
  cellIds.assign(selected.begin(), selected.end());
  std::sort(cellIds.begin(), cellIds.end());
  return !cellIds.empty();
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::applyCellSelection(
  pqOutputPort* port, const std::vector<vtkIdType>& ids)
{
  if (!port || !port->getSource())
  {
    return;
  }
  vtkSMSessionProxyManager* pxm = port->getSource()->proxyManager();
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
  if (selectionSource->GetProperty("NumberOfLayers"))
  {
    vtkSMPropertyHelper(selectionSource, "NumberOfLayers").Set(0);
  }

  std::vector<vtkIdType> idPairs;
  idPairs.reserve(ids.size() * 2);
  for (vtkIdType id : ids)
  {
    idPairs.push_back(-1);
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

  port->setSelectionInput(newAppendSelections, 0);

  if (pqPVApplicationCore* core = pqPVApplicationCore::instance())
  {
    if (pqSelectionManager* selMgr = core->selectionManager())
    {
      selMgr->select(port);
    }
  }
  port->renderAllViews();
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionController::computeAndApply(bool quietSuccess)
{
  pqOutputPort* port = nullptr;
  vtkPolyData* pd = nullptr;
  if (!this->resolveActivePolyData(port, pd))
  {
    this->reportToOutputWindow(
      tr("SHYX Proximity Gap Selection: no visible vtkPolyData in the view "
         "(select a surface representation)."));
    return;
  }

  double eps = SharedEpsilon;
  if (eps <= 0.0)
  {
    if (SharedTowardOppositeCenter || SharedSingleNearestRegion)
    {
      const double dmin = closestCrossComponentDistance(pd);
      if (dmin > 0.0)
      {
        eps = dmin * 1.05;
      }
    }
    if (!(eps > 0.0))
    {
      eps = suggestEpsilon(pd);
    }
    SetEpsilon(eps);
    this->updateActionTooltip();
  }
  if (!(eps > 0.0))
  {
    this->reportToOutputWindow(
      tr("SHYX Proximity Gap Selection: could not determine a gap distance ε."));
    return;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  std::vector<vtkIdType> cells;
  vtkIdType nContactPts = 0;
  int nComp = 0;
  double closestDist = -1.0;
  const bool ok = SharedTowardOppositeCenter
    ? selectOppositeCenterGap(pd, eps, cells, nContactPts, nComp, closestDist)
    : selectProximityGap(
        pd, eps, SharedSingleNearestRegion, cells, nContactPts, nComp, closestDist);
  QApplication::restoreOverrideCursor();

  if (!ok)
  {
    if (nComp < 2)
    {
      this->reportToOutputWindow(
        tr("SHYX Proximity Gap Selection: only %1 connected component(s). "
           "Closed shells that do not share vertices should count as separate "
           "components; if this is a single closed surface there is no gap to select "
           "(ε = %2).")
          .arg(nComp)
          .arg(eps, 0, 'g', 6));
    }
    else
    {
      this->reportToOutputWindow(
        tr("SHYX Proximity Gap Selection: %1 connected components, but none "
           "approach within ε = %2. Increase ε (wheel on the button).")
          .arg(nComp)
          .arg(eps, 0, 'g', 6));
    }
    return;
  }

  this->applyCellSelection(port, cells);
  if (!quietSuccess)
  {
    this->reportToOutputWindow(
      tr("SHYX Proximity Gap Selection: selected %1 cell(s) from %2 contact "
         "vertices (ε = %3%4%5).")
        .arg(cells.size())
        .arg(nContactPts)
        .arg(eps, 0, 'g', 6)
        .arg(SharedTowardOppositeCenter
            ? tr(", opposite centers")
            : (SharedSingleNearestRegion ? tr(", single nearest region") : QString()))
        .arg(closestDist > 0.0
            ? tr(", closest pair %1").arg(closestDist, 0, 'g', 6)
            : QString()));
  }
}
