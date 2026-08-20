#include "pqSHYXSelectionPlaneClipperWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqProxyWidget.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqSelectionInputWidget.h"
#include "pqSelectionManager.h"
#include "pqSMProxy.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkBoundingBox.h"
#include "vtkCommand.h"
#include "vtkDataSet.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPVDataInformation.h"
#include "vtkPolyData.h"
#include "vtkSelection.h"
#include "vtkSMInputProperty.h"
#include "vtkSMParaViewPipelineController.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMProperty.h"

#include "vtkSHYXSelectionPlaneClipper.h"

#include <QCheckBox>
#include <QLabel>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace
{

void AdjustBounds(vtkBoundingBox& bbox, double scaleFactor)
{
  double max_length = bbox.GetMaxLength();
  max_length = max_length > 0 ? max_length * 0.05 : 1;
  double min_point[3], max_point[3];
  bbox.GetMinPoint(min_point[0], min_point[1], min_point[2]);
  bbox.GetMaxPoint(max_point[0], max_point[1], max_point[2]);
  for (int cc = 0; cc < 3; ++cc)
  {
    if (bbox.GetLength(cc) == 0)
    {
      min_point[cc] -= max_length;
      max_point[cc] += max_length;
    }
    const double mid = (min_point[cc] + max_point[cc]) / 2.0;
    min_point[cc] = mid + scaleFactor * (min_point[cc] - mid);
    max_point[cc] = mid + scaleFactor * (max_point[cc] - mid);
  }
  bbox.SetMinPoint(min_point);
  bbox.SetMaxPoint(max_point);
}

bool ParsePackedDoubles(const QString& s, std::vector<double>& out)
{
  out.clear();
  if (s.isEmpty())
  {
    return false;
  }
  std::istringstream iss(s.toStdString());
  double v = 0.0;
  while (iss >> v)
  {
    out.push_back(v);
  }
  return out.size() == 6u;
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXSelectionPlaneClipperWidget::pqSHYXSelectionPlaneClipperWidget(
  vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
  : pqPropertyGroupWidget(proxy, smgroup, parent)
{
  this->setChangeAvailableAsChangeFinished(false);

  auto* vbox = new QVBoxLayout(this);
  this->InfoLabel = new QLabel(
    tr("Select triangles on any pipeline node, then Copy Active Selection into the Selection box "
       "(that copied selection is the source of truth). Show interactive cut plane is on by default "
       "and places the yellow plane at that patch without clipping. Apply to clip. "
       "The view selection is kept."),
    this);
  this->InfoLabel->setWordWrap(true);
  vbox->addWidget(this->InfoLabel);

  this->UseInteractiveCheckbox = new QCheckBox(tr("Show interactive cut plane"), this);
  vbox->addWidget(this->UseInteractiveCheckbox);

  this->SuppressSelectionRecompute = true;
  this->addPropertyLink(this->UseInteractiveCheckbox, "UseInteractiveCutPlanes");
  QObject::connect(this->UseInteractiveCheckbox, &QCheckBox::toggled, this,
    &pqSHYXSelectionPlaneClipperWidget::onUseInteractiveToggled);
  this->SuppressSelectionRecompute = false;

  this->rememberCopiedSelectionIdentity();
  if (vtkSMProperty* selProp = proxy->GetProperty("Selection"))
  {
    this->SelectionModifiedTag =
      pqCoreUtilities::connect(selProp, vtkCommand::ModifiedEvent, this, SLOT(onCopiedSelectionChanged()));
  }
  QTimer::singleShot(0, this, [this]() {
    this->connectCopiedSelectionWidget();
    if (this->UseInteractiveCheckbox && this->UseInteractiveCheckbox->isChecked())
    {
      this->computePlaneFromCopiedSelection();
      this->rebuildPlaneWidgetsIfNeeded();
    }
  });

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(proxy);
    if (this->PipelineSource)
    {
      auto* srcObj = this->PipelineSource.data();
      QObject::connect(srcObj,
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) { this->onPipelineDataUpdated(); });
      this->rememberCopiedGeometryProducerFromInput();
    }
  }
}

//-----------------------------------------------------------------------------
pqSHYXSelectionPlaneClipperWidget::~pqSHYXSelectionPlaneClipperWidget()
{
  this->disconnectViewVisibilityLinks();
  this->tearDownPlaneWidgets();
  if (this->SelectionModifiedTag != 0)
  {
    if (vtkSMProxy* px = this->proxy())
    {
      if (vtkSMProperty* selProp = px->GetProperty("Selection"))
      {
        selProp->RemoveObserver(this->SelectionModifiedTag);
      }
    }
    this->SelectionModifiedTag = 0;
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::select()
{
  this->Superclass::select();
  this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::deselect()
{
  this->Superclass::deselect();
  this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::showEvent(QShowEvent* event)
{
  this->Superclass::showEvent(event);
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::setView(pqView* view)
{
  this->disconnectViewVisibilityLinks();
  if (view)
  {
    this->ViewVisibilityConnections.push_back(QObject::connect(view,
      &pqView::representationVisibilityChanged, this,
      [this](pqRepresentation* /*repr*/, bool /*visible*/) { this->updatePlaneWidgetsVisibility(); }));
  }
  this->detachPlaneWidgetsFromView();
  this->Superclass::setView(view);
  this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::onUseInteractiveToggled(bool on)
{
  this->connectCopiedSelectionWidget();
  if (on && !this->SuppressSelectionRecompute)
  {
    // View selection is kept after Copy; refresh the producer if it is still active.
    this->rememberCopiedGeometryProducerFromView();
    this->computePlaneFromCopiedSelection();
  }
  this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::connectCopiedSelectionWidget()
{
  if (this->CopiedSelectionWidgetConnected && this->CopiedSelectionWidget)
  {
    return;
  }
  for (QWidget* p = this->parentWidget(); p; p = p->parentWidget())
  {
    if (auto* siw = p->findChild<pqSelectionInputWidget*>())
    {
      this->CopiedSelectionWidget = siw;
      if (!this->CopiedSelectionWidgetConnected)
      {
        QObject::connect(siw, &pqSelectionInputWidget::selectionChanged, this,
          [this](pqSMProxy) { this->onCopiedSelectionChanged(); });
        this->CopiedSelectionWidgetConnected = true;
      }
      return;
    }
    if (qobject_cast<pqProxyWidget*>(p))
    {
      break;
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::rememberCopiedSelectionIdentity()
{
  vtkSMProxy* sel = this->copiedSelectionProxy();
  this->LastCopiedSelectionProxy = sel;
  this->LastCopiedSelectionMTime = sel ? sel->GetMTime() : 0;
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::onCopiedSelectionChanged()
{
  if (this->RecomputeFromSelectionGuard || this->SuppressSelectionRecompute)
  {
    this->rememberCopiedSelectionIdentity();
    return;
  }
  vtkSMProxy* sel = this->copiedSelectionProxy();
  const vtkMTimeType mt = sel ? sel->GetMTime() : 0;
  if (sel == this->LastCopiedSelectionProxy.GetPointer() && mt == this->LastCopiedSelectionMTime)
  {
    return;
  }
  this->LastCopiedSelectionProxy = sel;
  this->LastCopiedSelectionMTime = mt;
  this->rememberCopiedGeometryProducerFromView();
  if (this->UseInteractiveCheckbox && this->UseInteractiveCheckbox->isChecked())
  {
    this->computePlaneFromCopiedSelection();
    this->rebuildPlaneWidgetsIfNeeded();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::rememberCopiedGeometryProducerFromView()
{
  auto* core = pqPVApplicationCore::instance();
  pqSelectionManager* selMgr = core ? core->selectionManager() : nullptr;
  pqOutputPort* port = selMgr ? selMgr->getSelectedPort() : nullptr;
  if (port)
  {
    this->LastCopiedGeometryPort = port;
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::rememberCopiedGeometryProducerFromInput()
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return;
  }
  auto* inProp = vtkSMInputProperty::SafeDownCast(src->GetProperty("Input"));
  vtkSMProxy* input = inProp ? inProp->GetProxy(0) : nullptr;
  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  auto* inSrc = (smm && input) ? smm->findItem<pqPipelineSource*>(input) : nullptr;
  if (!inSrc)
  {
    return;
  }
  const unsigned int port = inProp ? inProp->GetOutputPortForConnection(0) : 0;
  this->LastCopiedGeometryPort = inSrc->getOutputPort(static_cast<int>(port));
}

//-----------------------------------------------------------------------------
bool pqSHYXSelectionPlaneClipperWidget::hasCopiedSelection() const
{
  return this->copiedSelectionProxy() != nullptr;
}

//-----------------------------------------------------------------------------
vtkSMProxy* pqSHYXSelectionPlaneClipperWidget::copiedSelectionProxy() const
{
  // Copy Active Selection lives on pqSelectionInputWidget until Apply; that widget (and the
  // unchecked SM value) is the source of truth for Show. InitializationHelper writes the checked
  // Selection, which is the fallback for create-time auto-copy.
  if (this->CopiedSelectionWidget)
  {
    if (vtkSMProxy* wsel = this->CopiedSelectionWidget->selection())
    {
      return wsel;
    }
  }
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  vtkSMProperty* selProp = src ? src->GetProperty("Selection") : nullptr;
  if (!selProp)
  {
    return nullptr;
  }
  vtkSMPropertyHelper unchecked(selProp);
  unchecked.SetUseUnchecked(true);
  if (vtkSMProxy* sel = unchecked.GetAsProxy())
  {
    return sel;
  }
  return vtkSMPropertyHelper(selProp).GetAsProxy();
}

//-----------------------------------------------------------------------------
vtkDataSet* pqSHYXSelectionPlaneClipperWidget::datasetFromCopiedGeometryPort() const
{
  auto fetchFromProxy = [this](vtkSMProxy* px, int port) -> vtkDataSet* {
    auto* src = vtkSMSourceProxy::SafeDownCast(px);
    if (!src)
    {
      return nullptr;
    }
    // Show must not execute this clipper (that would clip). Other producers are already in the
    // pipeline and UpdatePipeline is a no-op if they are up to date.
    if (src != this->proxy())
    {
      src->UpdatePipeline();
    }
    auto* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    return alg ? vtkDataSet::SafeDownCast(alg->GetOutputDataObject(port)) : nullptr;
  };

  if (pqOutputPort* port = this->LastCopiedGeometryPort.data())
  {
    pqPipelineSource* prod = port->getSource();
    if (vtkDataSet* ds = fetchFromProxy(prod ? prod->getProxy() : nullptr, port->getPortNumber()))
    {
      if (ds->GetNumberOfCells() > 0 || ds->GetNumberOfPoints() > 0)
      {
        return ds;
      }
    }
  }

  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  auto* inProp = src ? vtkSMInputProperty::SafeDownCast(src->GetProperty("Input")) : nullptr;
  vtkSMProxy* input = inProp ? inProp->GetProxy(0) : nullptr;
  const unsigned int port = inProp ? inProp->GetOutputPortForConnection(0) : 0;
  return fetchFromProxy(input, static_cast<int>(port));
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::fillPlanePlaceBounds(double bounds[6]) const
{
  bounds[0] = bounds[2] = bounds[4] = 0.0;
  bounds[1] = bounds[3] = bounds[5] = 1.0;
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return;
  }
  if (vtkPVDataInformation* di = src->GetDataInformation(0))
  {
    di->GetBounds(bounds);
    if (bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5] &&
      (bounds[1] - bounds[0] + bounds[3] - bounds[2] + bounds[5] - bounds[4]) > 1e-12)
    {
      return;
    }
  }
  auto* inProp = vtkSMInputProperty::SafeDownCast(src->GetProperty("Input"));
  vtkSMProxy* input = inProp ? inProp->GetProxy(0) : nullptr;
  auto* inSrc = vtkSMSourceProxy::SafeDownCast(input);
  if (inSrc)
  {
    const unsigned int port = inProp->GetOutputPortForConnection(0);
    if (vtkPVDataInformation* di = inSrc->GetDataInformation(port))
    {
      di->GetBounds(bounds);
    }
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXSelectionPlaneClipperWidget::writePackedFromOriginNormal(
  const double origin[3], const double normalIn[3])
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return false;
  }
  double normal[3] = { normalIn[0], normalIn[1], normalIn[2] };
  if (vtkMath::Normalize(normal) < 1e-15)
  {
    normal[0] = 0.0;
    normal[1] = 0.0;
    normal[2] = 1.0;
  }
  double b[6] = { 0, 1, 0, 1, 0, 1 };
  this->fillPlanePlaceBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double arm = std::max(1.0, diag * 0.04);
  const double dh[3] = { origin[0] + arm * normal[0], origin[1] + arm * normal[1],
    origin[2] + arm * normal[2] };

  QString packed;
  for (int k = 0; k < 3; ++k)
  {
    if (!packed.isEmpty())
    {
      packed += QLatin1Char(' ');
    }
    packed += pqCoreUtilities::number(origin[k]);
  }
  for (int k = 0; k < 3; ++k)
  {
    packed += QLatin1Char(' ');
    packed += pqCoreUtilities::number(dh[k]);
  }
  if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
  {
    vtkSMPropertyHelper hp(p);
    const QByteArray utf = packed.toUtf8();
    hp.Set(0, utf.constData());
    p->Modified();
  }
  Q_EMIT this->changeAvailable();
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXSelectionPlaneClipperWidget::computePlaneFromCopiedSelection()
{
  if (!this->hasCopiedSelection())
  {
    return false;
  }
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return false;
  }

  vtkSMProxy* selProxy = this->copiedSelectionProxy();
  auto* selSrc = vtkSMSourceProxy::SafeDownCast(selProxy);
  if (!selSrc)
  {
    return false;
  }
  selSrc->UpdatePipeline();
  auto* selAlg = vtkAlgorithm::SafeDownCast(selSrc->GetClientSideObject());
  vtkSelection* selection =
    selAlg ? vtkSelection::SafeDownCast(selAlg->GetOutputDataObject(0)) : nullptr;

  vtkDataSet* dataset = this->datasetFromCopiedGeometryPort();
  if (!selection || !dataset)
  {
    return false;
  }

  double origin[3] = { 0.0, 0.0, 0.0 };
  double normal[3] = { 0.0, 0.0, 1.0 };
  if (!vtkSHYXSelectionPlaneClipper::ComputePlaneFromDatasetSelection(dataset, selection, origin, normal))
  {
    return false;
  }
  const int invert = vtkSMPropertyHelper(src, "InvertPlane").GetAsInt();
  if (invert)
  {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
  const double offset = vtkSMPropertyHelper(src, "ClipOffset").GetAsDouble();
  origin[0] += offset * normal[0];
  origin[1] += offset * normal[1];
  origin[2] += offset * normal[2];
  return this->writePackedFromOriginNormal(origin, normal);
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::onPipelineDataUpdated()
{
  this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::onPlaneInteraction()
{
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "DrawPlane", true).Set(1);
    this->PlaneWidget->UpdateVTKObjects();
  }
  this->alignPickCenterToVisiblePlane();
  Q_EMIT this->changeAvailable();
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::onPlaneEndInteraction()
{
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "DrawPlane", true).Set(1);
    this->PlaneWidget->UpdateVTKObjects();
  }
  this->pushPackedFromWidgetsToFilter();
  this->alignPickCenterToVisiblePlane();
  Q_EMIT this->changeFinished();
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::tearDownPlaneWidgets()
{
  this->detachPlaneWidgetsFromView();
  vtkSMNewWidgetRepresentationProxy* w = this->PlaneWidget.GetPointer();
  if (w)
  {
    if (this->PlaneEndInteractionTag != 0)
    {
      w->RemoveObserver(this->PlaneEndInteractionTag);
    }
    if (this->PlaneInteractionTag != 0)
    {
      w->RemoveObserver(this->PlaneInteractionTag);
    }
  }
  this->PlaneEndInteractionTag = 0;
  this->PlaneInteractionTag   = 0;
  this->PlaneWidget            = nullptr;
  this->LastPlaneHostRenderView.clear();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::detachPlaneWidgetsFromView()
{
  pqRenderView* rv = this->LastPlaneHostRenderView.data();
  if (!rv)
  {
    rv = qobject_cast<pqRenderView*>(this->view());
  }
  if (!rv)
  {
    return;
  }
  vtkSMRenderViewProxy* rvpx = rv->getRenderViewProxy();
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "Visibility", true).Set(0);
    vtkSMPropertyHelper(this->PlaneWidget, "Enabled", true).Set(0);
    this->PlaneWidget->UpdateVTKObjects();
    vtkSMPropertyHelper(rvpx, "HiddenRepresentations", true).Remove(this->PlaneWidget);
  }
  rvpx->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::disconnectViewVisibilityLinks()
{
  for (const QMetaObject::Connection& c : this->ViewVisibilityConnections)
  {
    QObject::disconnect(c);
  }
  this->ViewVisibilityConnections.clear();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::attachPlaneWidgetsToView()
{
  pqView* view = this->view();
  auto* rv     = qobject_cast<pqRenderView*>(view);
  if (!rv || !this->PlaneWidget)
  {
    return;
  }
  this->LastPlaneHostRenderView = rv;
  vtkSMRenderViewProxy* rvpx = rv->getRenderViewProxy();
  vtkSMPropertyHelper(rvpx, "HiddenRepresentations", true).Add(this->PlaneWidget);
  rvpx->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::updatePlaneWidgetsVisibility()
{
  const bool use = this->UseInteractiveCheckbox && this->UseInteractiveCheckbox->isChecked();
  const bool base =
    use && this->isSelected() && qobject_cast<pqRenderView*>(this->view()) != nullptr && this->PlaneWidget;
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "Visibility", true).Set(base ? 1 : 0);
    vtkSMPropertyHelper(this->PlaneWidget, "Enabled", true).Set(base ? 1 : 0);
    this->PlaneWidget->UpdateVTKObjects();
  }
  this->alignPickCenterToVisiblePlane();
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::alignPickCenterToVisiblePlane()
{
  if (!this->PlaneWidget || vtkSMPropertyHelper(this->PlaneWidget, "Visibility", true).GetAsInt() == 0)
  {
    return;
  }
  auto* rv = qobject_cast<pqRenderView*>(this->view());
  if (!rv)
  {
    return;
  }
  double origin[3] = { 0.0, 0.0, 0.0 };
  vtkSMPropertyHelper(this->PlaneWidget, "Origin", true).Get(origin, 3);
  rv->setCenterOfRotation(origin);
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::placePlaneBounds(
  vtkSMNewWidgetRepresentationProxy* wdg, const double bounds[6])
{
  if (!wdg)
  {
    return;
  }
  vtkBoundingBox bbox;
  bbox.SetBounds(bounds);
  AdjustBounds(bbox, vtkSMPropertyHelper(wdg, "PlaceFactor", true).GetAsDouble());
  double bds[6];
  bbox.GetBounds(bds);
  vtkSMPropertyHelper(wdg, "WidgetBounds", true).Set(bds, 6);
  wdg->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const
{
  if (!wdg)
  {
    return;
  }
  const double yellow[3] = { 1.0, 1.0, 0.0 };
  vtkSMPropertyHelper(wdg, "DrawPlane", true).Set(1);
  vtkSMPropertyHelper(wdg, "DrawOutline", true).Set(0);
  vtkSMPropertyHelper(wdg, "DrawIntersectionEdges", true).Set(0);
  vtkSMPropertyHelper(wdg, "WidgetColor", true).Set(yellow, 3);
  vtkSMPropertyHelper(wdg, "ForegroundWidgetColor", true).Set(yellow, 3);
  vtkSMPropertyHelper(wdg, "InteractiveWidgetColor", true).Set(yellow, 3);
  wdg->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
int pqSHYXSelectionPlaneClipperWidget::planeHintCountFromOutput(vtkAlgorithm* alg, vtkPolyData* /*out*/) const
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  std::vector<double> parsed;
  if (src)
  {
    if (vtkSMProperty* prop = src->GetProperty("InteractiveCutPacked"))
    {
      vtkSMPropertyHelper hp(prop);
      const char* cs = hp.GetAsString(0);
      if (ParsePackedDoubles(cs ? QString::fromUtf8(cs) : QString(), parsed))
      {
        return 1;
      }
    }
  }
  auto* clip = vtkSHYXSelectionPlaneClipper::SafeDownCast(alg);
  if (clip && clip->GetClipPlaneHintPackedString())
  {
    if (ParsePackedDoubles(QString::fromUtf8(clip->GetClipPlaneHintPackedString()), parsed))
    {
      return 1;
    }
  }
  return 0;
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::syncWidgetsFromFilterState()
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src || !this->PlaneWidget)
  {
    return;
  }

  // InteractiveCutPacked is the pending plane from Copy/Show or a widget drag; prefer it so Show
  // can place the plane before Apply. Fall back to ClipPlaneHint after a completed clip.
  std::vector<double> parsed;
  bool haveSix = false;
  if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
  {
    vtkSMPropertyHelper hp(p);
    const char* cs = hp.GetAsString(0);
    haveSix = ParsePackedDoubles(cs ? QString::fromUtf8(cs) : QString(), parsed);
  }
  if (!haveSix)
  {
    vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    auto* clip = vtkSHYXSelectionPlaneClipper::SafeDownCast(alg);
    const char* hint = clip ? clip->GetClipPlaneHintPackedString() : nullptr;
    if (hint && hint[0] != '\0')
    {
      haveSix = ParsePackedDoubles(QString::fromUtf8(hint), parsed);
    }
  }
  if (!haveSix || parsed.size() != 6u)
  {
    return;
  }

  const double o[3] = { parsed[0], parsed[1], parsed[2] };
  const double d[3] = { parsed[3], parsed[4], parsed[5] };
  double nrm[3] = { d[0] - o[0], d[1] - o[1], d[2] - o[2] };
  if (vtkMath::Normalize(nrm) < 1e-15)
  {
    nrm[0] = 0.0;
    nrm[1] = 0.0;
    nrm[2] = 1.0;
  }
  vtkSMPropertyHelper(this->PlaneWidget, "Origin", true).Set(o, 3);
  vtkSMPropertyHelper(this->PlaneWidget, "Normal", true).Set(nrm, 3);
  this->PlaneWidget->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::pushPackedFromWidgetsToFilter()
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src || !this->PlaneWidget)
  {
    return;
  }
  double b[6] = { 0, 1, 0, 1, 0, 1 };
  if (vtkPVDataInformation* di = src->GetDataInformation(0))
  {
    di->GetBounds(b);
  }
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double arm = std::max(1.0, diag * 0.04);

  double o[3];
  double nrm[3];
  vtkSMPropertyHelper ho(this->PlaneWidget, "Origin", true);
  vtkSMPropertyHelper hn(this->PlaneWidget, "Normal", true);
  for (int k = 0; k < 3; ++k)
  {
    o[k]   = ho.GetAsDouble(k);
    nrm[k] = hn.GetAsDouble(k);
  }
  if (vtkMath::Normalize(nrm) < 1e-15)
  {
    nrm[0] = 0.0;
    nrm[1] = 0.0;
    nrm[2] = 1.0;
  }
  const double dh[3] = { o[0] + arm * nrm[0], o[1] + arm * nrm[1], o[2] + arm * nrm[2] };

  QString packed;
  for (int k = 0; k < 3; ++k)
  {
    if (!packed.isEmpty())
    {
      packed += QLatin1Char(' ');
    }
    packed += pqCoreUtilities::number(o[k]);
  }
  for (int k = 0; k < 3; ++k)
  {
    packed += QLatin1Char(' ');
    packed += pqCoreUtilities::number(dh[k]);
  }
  if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
  {
    vtkSMPropertyHelper hp(p);
    const QByteArray utf = packed.toUtf8();
    hp.Set(0, utf.constData());
  }
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::writeClipHintToInteractivePackedIfLocked()
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return;
  }
  vtkSMProperty* packedProp = src->GetProperty("InteractiveCutPacked");
  if (!packedProp)
  {
    return;
  }
  vtkSMPropertyHelper hp(packedProp);
  const char* current = hp.GetAsString(0);
  const bool packedAlreadySet = current && current[0] != '\0';
  if (!packedAlreadySet && !this->PlaneWidget)
  {
    return;
  }
  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  auto* clip = vtkSHYXSelectionPlaneClipper::SafeDownCast(alg);
  const char* hint = clip ? clip->GetClipPlaneHintPackedString() : nullptr;
  if (!hint || hint[0] == '\0')
  {
    return;
  }
  if (current && std::strcmp(current, hint) == 0)
  {
    return;
  }
  hp.Set(0, hint);
}

//-----------------------------------------------------------------------------
void pqSHYXSelectionPlaneClipperWidget::rebuildPlaneWidgetsIfNeeded()
{
  if (!this->UseInteractiveCheckbox || !this->UseInteractiveCheckbox->isChecked())
  {
    // Hide the widget only; keep the representation proxy and packed string so Apply still clips
    // with the last interactive plane (server no longer gates on UseInteractiveCutPlanes).
    if (this->PlaneWidget)
    {
      this->detachPlaneWidgetsFromView();
    }
    this->writeClipHintToInteractivePackedIfLocked();
    if (pqView* v = this->view())
    {
      v->render();
    }
    if (this->LastPlaneHostRenderView)
    {
      this->LastPlaneHostRenderView->render();
    }
    return;
  }

  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return;
  }

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  vtkPolyData* outPd = alg ? vtkPolyData::SafeDownCast(alg->GetOutputDataObject(0)) : nullptr;
  const int n = this->planeHintCountFromOutput(alg, outPd);

  if (n <= 0)
  {
    this->tearDownPlaneWidgets();
    return;
  }

  double bounds[6] = { 0, 1, 0, 1, 0, 1 };
  this->fillPlanePlaceBounds(bounds);

  if (this->PlaneWidget)
  {
    this->placePlaneBounds(this->PlaneWidget, bounds);
    this->syncWidgetsFromFilterState();
    this->attachPlaneWidgetsToView();
    this->updatePlaneWidgetsVisibility();
    return;
  }

  this->tearDownPlaneWidgets();

  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  pqServer* server = (smm && src->GetSession()) ? smm->findServer(src->GetSession()) : nullptr;
  if (!server)
  {
    return;
  }
  vtkSMSessionProxyManager* pxm = server->proxyManager();
  if (!pxm)
  {
    return;
  }

  vtkNew<vtkSMParaViewPipelineController> controller;

  vtkSmartPointer<vtkSMProxy> aProxy;
  aProxy.TakeReference(pxm->NewProxy("representations", "DisplaySizedImplicitPlaneWidgetRepresentation"));
  auto* wdg = vtkSMNewWidgetRepresentationProxy::SafeDownCast(aProxy);
  if (!wdg)
  {
    return;
  }
  controller->InitializeProxy(wdg);
  wdg->PrototypeOn();
  this->stylePlaneWidget(wdg);
  this->placePlaneBounds(wdg, bounds);

  this->PlaneEndInteractionTag =
    pqCoreUtilities::connect(wdg, vtkCommand::EndInteractionEvent, this, SLOT(onPlaneEndInteraction()));
  this->PlaneInteractionTag =
    pqCoreUtilities::connect(wdg, vtkCommand::InteractionEvent, this, SLOT(onPlaneInteraction()));

  this->PlaneWidget = wdg;

  this->attachPlaneWidgetsToView();
  this->syncWidgetsFromFilterState();
  this->updatePlaneWidgetsVisibility();
}
