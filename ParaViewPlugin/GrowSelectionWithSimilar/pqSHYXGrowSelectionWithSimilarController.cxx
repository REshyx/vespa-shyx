#include "pqSHYXGrowSelectionWithSimilarController.h"

#include "pqActiveObjects.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqSelectionManager.h"
#include "pqView.h"
#include "pqViewFrame.h"

#include "vtkAlgorithm.h"
#include "vtkCell.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkConvertSelection.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkIdTypeArray.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkOutputWindow.h"
#include "vtkPolygon.h"
#include "vtkPolyData.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"

#include <QAction>
#include <QEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <unordered_set>
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

bool CellNormal(vtkDataSet* ds, vtkIdType cellId, double n[3])
{
  vtkCell* cell = ds->GetCell(cellId);
  if (!cell || cell->GetNumberOfPoints() < 3)
  {
    return false;
  }
  vtkPolygon::ComputeNormal(cell->GetPoints(), n);
  return vtkMath::Normalize(n) > 0.0;
}

double NormalAngleDegrees(const double n0[3], const double n1[3])
{
  double d = vtkMath::Dot(n0, n1);
  d = std::max(-1.0, std::min(1.0, d));
  return vtkMath::DegreesFromRadians(std::acos(d));
}
}

double pqSHYXGrowSelectionWithSimilarController::SharedDihedralThresholdDegrees = 15.0;

namespace
{
constexpr int kHoldInitialDelayMs = 350;
constexpr int kHoldRepeatIntervalMs = 10;
}

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarController::pqSHYXGrowSelectionWithSimilarController(
  pqRenderView* view, pqViewFrame* frame, QAction* action, QObject* parent)
  : Superclass(parent)
  , View(view)
  , Frame(frame)
  , Action(action)
{
  this->HoldTimer = new QTimer(this);
  this->HoldTimer->setSingleShot(false);
  QObject::connect(
    this->HoldTimer, &QTimer::timeout, this, &pqSHYXGrowSelectionWithSimilarController::onHoldRepeat);

  QObject::connect(action, &QAction::triggered, this,
    &pqSHYXGrowSelectionWithSimilarController::onTriggered);
  // Title-bar button widget may appear after the action is added.
  QTimer::singleShot(0, this, [this]() { this->installButtonExtras(); });
  this->updateActionTooltip();
}

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarController::~pqSHYXGrowSelectionWithSimilarController()
{
  this->stopHoldRepeat();
  if (this->Button)
  {
    this->Button->removeEventFilter(this);
  }
}

//-----------------------------------------------------------------------------
double pqSHYXGrowSelectionWithSimilarController::DihedralThresholdDegrees()
{
  return SharedDihedralThresholdDegrees;
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::SetDihedralThresholdDegrees(double degrees)
{
  SharedDihedralThresholdDegrees = std::max(0.0, std::min(180.0, degrees));
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::updateActionTooltip()
{
  if (!this->Action)
  {
    return;
  }
  this->Action->setToolTip(
    tr("Grow selection with similar normals (dihedral ≤ %1°).\n"
       "Click once for one ring; press and hold to keep growing.\n"
       "Right-click to set the angle threshold.")
      .arg(SharedDihedralThresholdDegrees, 0, 'g', 4));
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::installButtonExtras()
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

  // Avoid duplicate connections if install is retried.
  if (button->property("shyxGrowSimilarExtrasInstalled").toBool())
  {
    return;
  }
  button->setProperty("shyxGrowSimilarExtrasInstalled", true);
  this->Button = button;
  button->installEventFilter(this);

  // Prefer Qt auto-repeat when available; we still drive growth via press/hold
  // timers so we can stop cleanly when expansion stalls.
  if (auto* toolButton = qobject_cast<QToolButton*>(button))
  {
    toolButton->setAutoRepeat(false);
  }

  button->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(button, &QWidget::customContextMenuRequested, this,
    [this, button](const QPoint& pos) {
      this->stopHoldRepeat();
      QMenu menu(button);
      QAction* setAngle = menu.addAction(
        tr("Set dihedral threshold… (current %1°)")
          .arg(SharedDihedralThresholdDegrees, 0, 'g', 4));
      QObject::connect(setAngle, &QAction::triggered, this,
        &pqSHYXGrowSelectionWithSimilarController::promptDihedralThreshold);
      menu.exec(button->mapToGlobal(pos));
    });
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::stopHoldRepeat()
{
  if (this->HoldTimer)
  {
    this->HoldTimer->stop();
  }
  this->HoldActive = false;
  this->HoldBlocked = false;
}

//-----------------------------------------------------------------------------
bool pqSHYXGrowSelectionWithSimilarController::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == this->Button && event)
  {
    switch (event->type())
    {
      case QEvent::MouseButtonPress:
      {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton)
        {
          this->HoldActive = true;
          this->HoldBlocked = false;
          this->HoldGrewSteps = 0;
          this->HoldAddedCells = 0;
          this->HandledByMousePress = true;
          const GrowStatus status = this->growOnce(/*quietSuccess=*/false);
          if (status == GrowStatus::Grew)
          {
            this->HoldTimer->start(kHoldInitialDelayMs);
          }
          else
          {
            this->HoldBlocked = true;
          }
        }
        break;
      }
      case QEvent::MouseButtonRelease:
      {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton)
        {
          const bool wasHolding = this->HoldTimer && this->HoldTimer->isActive();
          const int steps = this->HoldGrewSteps;
          const vtkIdType added = this->HoldAddedCells;
          this->stopHoldRepeat();
          if (wasHolding && steps > 1)
          {
            this->reportToOutputWindow(
              tr("SHYX Grow Selection With Similar: hold finished "
                 "(%1 steps, +%2 cells, threshold %3°).")
                .arg(steps)
                .arg(added)
                .arg(SharedDihedralThresholdDegrees, 0, 'g', 4));
          }
          // Clear after QAction::triggered from the click has a chance to run.
          QTimer::singleShot(0, this, [this]() { this->HandledByMousePress = false; });
        }
        break;
      }
      default:
        break;
    }
  }
  return this->Superclass::eventFilter(watched, event);
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::onHoldRepeat()
{
  if (!this->HoldActive || this->HoldBlocked)
  {
    this->stopHoldRepeat();
    return;
  }

  // After the initial delay, switch to a faster repeat cadence.
  if (this->HoldTimer->interval() != kHoldRepeatIntervalMs)
  {
    this->HoldTimer->setInterval(kHoldRepeatIntervalMs);
  }

  const GrowStatus status = this->growOnce(/*quietSuccess=*/true);
  if (status != GrowStatus::Grew)
  {
    this->HoldBlocked = true;
    this->stopHoldRepeat();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::promptDihedralThreshold()
{
  bool ok = false;
  const double value = QInputDialog::getDouble(this->Frame,
    tr("Grow selection with similar normals"),
    tr("Maximum angle between face normals (degrees):"),
    SharedDihedralThresholdDegrees, 0.0, 180.0, 2, &ok);
  if (!ok)
  {
    return;
  }
  SetDihedralThresholdDegrees(value);
  this->updateActionTooltip();
  this->reportToOutputWindow(tr("SHYX Grow Selection With Similar: dihedral threshold set to %1°.")
                               .arg(SharedDihedralThresholdDegrees, 0, 'g', 4));
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::reportToOutputWindow(const QString& message)
{
  const QByteArray utf8 = message.toUtf8();
  if (vtkOutputWindow* win = vtkOutputWindow::GetInstance())
  {
    win->DisplayWarningText((utf8 + "\n").constData());
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXGrowSelectionWithSimilarController::resolveActiveSelection(pqOutputPort*& portOut,
  vtkDataSet*& dsOut, pqDataRepresentation* hintRepresentation, pqView* hintView)
{
  portOut = nullptr;
  dsOut = nullptr;

  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  if (!core || !core->selectionManager())
  {
    return false;
  }

  pqOutputPort* port = core->selectionManager()->getSelectedPort();
  if (!port)
  {
    if (hintRepresentation)
    {
      port = hintRepresentation->getOutputPortFromInput();
    }
    else
    {
      pqDataRepresentation* active = pqActiveObjects::instance().activeRepresentation();
      if (active && (!hintView || active->getView() == hintView))
      {
        port = active->getOutputPortFromInput();
      }
    }
  }
  if (!port || !port->getSelectionInput())
  {
    return false;
  }

  vtkSMSourceProxy* src = port->getSourceProxy();
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
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return false;
  }

  portOut = port;
  dsOut = ds;
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXGrowSelectionWithSimilarController::collectSelectedCellIds(
  pqOutputPort* port, vtkDataSet* ds, std::vector<vtkIdType>& ids)
{
  ids.clear();
  vtkSMSourceProxy* appendSel = port->getSelectionInput();
  if (!appendSel)
  {
    return false;
  }

  appendSel->UpdatePipeline();
  vtkAlgorithm* selAlg = vtkAlgorithm::SafeDownCast(appendSel->GetClientSideObject());
  if (!selAlg)
  {
    return false;
  }
  vtkSelection* selection = vtkSelection::SafeDownCast(selAlg->GetOutputDataObject(0));
  if (!selection)
  {
    return false;
  }

  vtkNew<vtkIdTypeArray> selected;
  vtkConvertSelection::GetSelectedCells(selection, ds, selected);
  const vtkIdType n = selected->GetNumberOfTuples();
  ids.reserve(static_cast<size_t>(n));
  for (vtkIdType i = 0; i < n; ++i)
  {
    const vtkIdType cid = selected->GetValue(i);
    if (cid >= 0 && cid < ds->GetNumberOfCells())
    {
      ids.push_back(cid);
    }
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return !ids.empty();
}

//-----------------------------------------------------------------------------
bool pqSHYXGrowSelectionWithSimilarController::growSimilar(
  vtkPolyData* pd, const std::vector<vtkIdType>& seed, std::vector<vtkIdType>& grown)
{
  grown = seed;
  if (!pd || seed.empty())
  {
    return false;
  }

  pd->BuildLinks();

  const double threshold = SharedDihedralThresholdDegrees;
  std::unordered_set<vtkIdType> selected(seed.begin(), seed.end());
  std::unordered_set<vtkIdType> added;

  vtkNew<vtkIdList> ptIds;
  vtkNew<vtkIdList> neighbors;

  for (vtkIdType cid : seed)
  {
    double n0[3];
    if (!CellNormal(pd, cid, n0))
    {
      continue;
    }

    pd->GetCellPoints(cid, ptIds);
    const vtkIdType npts = ptIds->GetNumberOfIds();
    if (npts < 3)
    {
      continue;
    }

    for (vtkIdType e = 0; e < npts; ++e)
    {
      const vtkIdType p0 = ptIds->GetId(e);
      const vtkIdType p1 = ptIds->GetId((e + 1) % npts);
      neighbors->Reset();
      pd->GetCellEdgeNeighbors(cid, p0, p1, neighbors);
      const vtkIdType nNb = neighbors->GetNumberOfIds();
      for (vtkIdType i = 0; i < nNb; ++i)
      {
        const vtkIdType nid = neighbors->GetId(i);
        if (selected.count(nid) || added.count(nid))
        {
          continue;
        }
        double n1[3];
        if (!CellNormal(pd, nid, n1))
        {
          continue;
        }
        if (NormalAngleDegrees(n0, n1) <= threshold)
        {
          added.insert(nid);
        }
      }
    }
  }

  if (added.empty())
  {
    return false;
  }

  grown.reserve(grown.size() + added.size());
  for (vtkIdType nid : added)
  {
    grown.push_back(nid);
  }
  std::sort(grown.begin(), grown.end());
  grown.erase(std::unique(grown.begin(), grown.end()), grown.end());
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::applyCellSelection(
  pqOutputPort* port, const std::vector<vtkIdType>& ids)
{
  if (!port)
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
pqSHYXGrowSelectionWithSimilarController::GrowStatus
pqSHYXGrowSelectionWithSimilarController::growOnce(bool quietSuccess)
{
  pqOutputPort* port = nullptr;
  vtkDataSet* ds = nullptr;
  if (!resolveActiveSelection(port, ds, nullptr, this->View))
  {
    this->reportToOutputWindow(
      tr("SHYX Grow Selection With Similar: no active cell selection to grow."));
    return GrowStatus::Error;
  }

  auto* pd = vtkPolyData::SafeDownCast(ds);
  if (!pd)
  {
    this->reportToOutputWindow(
      tr("SHYX Grow Selection With Similar: active data is not vtkPolyData "
         "(surface mesh required)."));
    return GrowStatus::Error;
  }

  std::vector<vtkIdType> seed;
  if (!this->collectSelectedCellIds(port, ds, seed))
  {
    this->reportToOutputWindow(
      tr("SHYX Grow Selection With Similar: could not resolve selected cell IDs "
         "(need a cell selection)."));
    return GrowStatus::Error;
  }

  std::vector<vtkIdType> grown;
  if (!this->growSimilar(pd, seed, grown))
  {
    this->reportToOutputWindow(
      tr("SHYX Grow Selection With Similar: selection did not grow "
         "(no adjacent faces within %1° normal angle).")
        .arg(SharedDihedralThresholdDegrees, 0, 'g', 4));
    return GrowStatus::NoGrowth;
  }

  const vtkIdType added = static_cast<vtkIdType>(grown.size() - seed.size());
  this->applyCellSelection(port, grown);
  ++this->HoldGrewSteps;
  this->HoldAddedCells += added;

  if (!quietSuccess)
  {
    this->reportToOutputWindow(
      tr("SHYX Grow Selection With Similar: grew by %1 cell(s) (threshold %2°, total %3).")
        .arg(added)
        .arg(SharedDihedralThresholdDegrees, 0, 'g', 4)
        .arg(grown.size()));
  }
  return GrowStatus::Grew;
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarController::onTriggered()
{
  // Mouse press already performed the grow; ignore the click's triggered().
  if (this->HandledByMousePress)
  {
    return;
  }
  this->growOnce(/*quietSuccess=*/false);
}

//-----------------------------------------------------------------------------
bool pqSHYXGrowSelectionWithSimilarController::HasActiveCellSelection(
  pqDataRepresentation* hintRepresentation)
{
  pqOutputPort* port = nullptr;
  vtkDataSet* ds = nullptr;
  if (!resolveActiveSelection(port, ds, hintRepresentation, nullptr))
  {
    return false;
  }
  std::vector<vtkIdType> ids;
  return collectSelectedCellIds(port, ds, ids);
}

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarController::GrowToCompletionResult
pqSHYXGrowSelectionWithSimilarController::GrowUntilCompleteByNormal(
  pqDataRepresentation* hintRepresentation)
{
  GrowToCompletionResult result;

  pqOutputPort* port = nullptr;
  vtkDataSet* ds = nullptr;
  if (!resolveActiveSelection(port, ds, hintRepresentation, nullptr))
  {
    result.message =
      tr("SHYX Select Similar / By Normal: no active cell selection to grow.");
    reportToOutputWindow(result.message);
    return result;
  }

  auto* pd = vtkPolyData::SafeDownCast(ds);
  if (!pd)
  {
    result.message = tr("SHYX Select Similar / By Normal: active data is not vtkPolyData "
                        "(surface mesh required).");
    reportToOutputWindow(result.message);
    return result;
  }

  std::vector<vtkIdType> seed;
  if (!collectSelectedCellIds(port, ds, seed))
  {
    result.message = tr("SHYX Select Similar / By Normal: could not resolve selected cell IDs "
                        "(need a cell selection).");
    reportToOutputWindow(result.message);
    return result;
  }

  pd->BuildLinks();
  const vtkIdType nCells = pd->GetNumberOfCells();
  const double threshold = SharedDihedralThresholdDegrees;

  std::unordered_set<vtkIdType> selected(seed.begin(), seed.end());
  std::vector<vtkIdType> frontier = seed;

  vtkNew<vtkIdList> ptIds;
  vtkNew<vtkIdList> neighbors;
  int rings = 0;
  vtkIdType addedTotal = 0;

  while (!frontier.empty() && static_cast<vtkIdType>(selected.size()) < nCells)
  {
    std::vector<vtkIdType> next;
    for (vtkIdType cid : frontier)
    {
      double n0[3];
      if (!CellNormal(pd, cid, n0))
      {
        continue;
      }
      pd->GetCellPoints(cid, ptIds);
      const vtkIdType npts = ptIds->GetNumberOfIds();
      if (npts < 3)
      {
        continue;
      }
      for (vtkIdType e = 0; e < npts; ++e)
      {
        const vtkIdType p0 = ptIds->GetId(e);
        const vtkIdType p1 = ptIds->GetId((e + 1) % npts);
        neighbors->Reset();
        pd->GetCellEdgeNeighbors(cid, p0, p1, neighbors);
        const vtkIdType nNb = neighbors->GetNumberOfIds();
        for (vtkIdType i = 0; i < nNb; ++i)
        {
          const vtkIdType nid = neighbors->GetId(i);
          if (selected.count(nid) != 0)
          {
            continue;
          }
          double n1[3];
          if (!CellNormal(pd, nid, n1))
          {
            continue;
          }
          if (NormalAngleDegrees(n0, n1) <= threshold)
          {
            selected.insert(nid);
            next.push_back(nid);
          }
        }
      }
    }
    if (next.empty())
    {
      break;
    }
    addedTotal += static_cast<vtkIdType>(next.size());
    frontier.swap(next);
    ++rings;
  }

  result.ok = true;
  result.added = addedTotal;
  result.rings = rings;
  result.total = static_cast<vtkIdType>(selected.size());
  if (addedTotal == 0)
  {
    result.message = tr("SHYX Select Similar / By Normal: selection did not grow "
                        "(no adjacent faces within %1° normal angle).")
                       .arg(SharedDihedralThresholdDegrees, 0, 'g', 4);
    reportToOutputWindow(result.message);
    return result;
  }

  std::vector<vtkIdType> grown(selected.begin(), selected.end());
  std::sort(grown.begin(), grown.end());
  applyCellSelection(port, grown);

  result.grew = true;
  result.message =
    tr("SHYX Select Similar / By Normal: grew by %1 cell(s) in %2 ring(s) "
       "(threshold %3°, total %4).")
      .arg(addedTotal)
      .arg(rings)
      .arg(SharedDihedralThresholdDegrees, 0, 'g', 4)
      .arg(result.total);
  reportToOutputWindow(result.message);
  return result;
}
