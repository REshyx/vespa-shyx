#include "pqVESPASelectionPlaneClipperWidget.h"

#include "pqApplicationCore.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkBoundingBox.h"
#include "vtkCommand.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPVDataInformation.h"
#include "vtkPolyData.h"
#include "vtkSMParaViewPipelineController.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMNewWidgetRepresentationProxy.h"

#include <QCheckBox>
#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>
#include <string>

namespace
{
constexpr char kPlanePackedFieldName[] = "SHYX_SelectionPlaneClipper_PlanePacked";

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
pqVESPASelectionPlaneClipperWidget::pqVESPASelectionPlaneClipperWidget(
  vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
  : pqPropertyGroupWidget(proxy, smgroup, parent)
{
  this->setChangeAvailableAsChangeFinished(false);

  auto* vbox = new QVBoxLayout(this);
  this->InfoLabel = new QLabel(
    tr("Enable the interactive plane, Apply once to generate output, then drag the yellow plane. "
       "Apply again (or use auto-apply) to clip with the edited plane."),
    this);
  this->InfoLabel->setWordWrap(true);
  vbox->addWidget(this->InfoLabel);

  this->UseInteractiveCheckbox = new QCheckBox(tr("Use interactive cut plane"), this);
  vbox->addWidget(this->UseInteractiveCheckbox);

  this->addPropertyLink(this->UseInteractiveCheckbox, "UseInteractiveCutPlanes");
  QObject::connect(this->UseInteractiveCheckbox, &QCheckBox::toggled, this,
    &pqVESPASelectionPlaneClipperWidget::onUseInteractiveToggled);

  if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
  {
    this->PipelineSource = smm->findItem<pqPipelineSource*>(proxy);
    if (this->PipelineSource)
    {
      auto* srcObj = this->PipelineSource.data();
      QObject::connect(srcObj,
        static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(&pqPipelineSource::dataUpdated),
        this, [this](pqPipelineSource*) { this->onPipelineDataUpdated(); });
    }
  }
}

//-----------------------------------------------------------------------------
pqVESPASelectionPlaneClipperWidget::~pqVESPASelectionPlaneClipperWidget()
{
  this->disconnectViewVisibilityLinks();
  this->tearDownPlaneWidgets();
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::select()
{
  this->Superclass::select();
  this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::deselect()
{
  this->Superclass::deselect();
  this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::showEvent(QShowEvent* event)
{
  this->Superclass::showEvent(event);
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::setView(pqView* view)
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
void pqVESPASelectionPlaneClipperWidget::onUseInteractiveToggled(bool /*on*/)
{
  this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::onPipelineDataUpdated()
{
  this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::onPlaneInteraction()
{
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "DrawPlane", true).Set(1);
    this->PlaneWidget->UpdateVTKObjects();
  }
  Q_EMIT this->changeAvailable();
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::onPlaneEndInteraction()
{
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "DrawPlane", true).Set(1);
    this->PlaneWidget->UpdateVTKObjects();
  }
  this->pushPackedFromWidgetsToFilter();
  Q_EMIT this->changeFinished();
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::tearDownPlaneWidgets()
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
void pqVESPASelectionPlaneClipperWidget::detachPlaneWidgetsFromView()
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
void pqVESPASelectionPlaneClipperWidget::disconnectViewVisibilityLinks()
{
  for (const QMetaObject::Connection& c : this->ViewVisibilityConnections)
  {
    QObject::disconnect(c);
  }
  this->ViewVisibilityConnections.clear();
}

//-----------------------------------------------------------------------------
bool pqVESPASelectionPlaneClipperWidget::isOutputPort0VisibleInView(pqView* view) const
{
  if (!view)
  {
    return true;
  }
  auto* smm = pqApplicationCore::instance()->getServerManagerModel();
  auto* src = smm->findItem<pqPipelineSource*>(this->proxy());
  if (!src)
  {
    return true;
  }
  bool foundPort0Repr = false;
  for (pqRepresentation* repr : view->getRepresentations())
  {
    auto* dr = qobject_cast<pqDataRepresentation*>(repr);
    if (!dr || dr->getInput() != src)
    {
      continue;
    }
    pqOutputPort* op = dr->getOutputPortFromInput();
    if (!op || op->getPortNumber() != 0)
    {
      continue;
    }
    foundPort0Repr = true;
    if (dr->isVisible())
    {
      return true;
    }
  }
  if (!foundPort0Repr)
  {
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::attachPlaneWidgetsToView()
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
void pqVESPASelectionPlaneClipperWidget::updatePlaneWidgetsVisibility()
{
  const bool use = this->UseInteractiveCheckbox && this->UseInteractiveCheckbox->isChecked();
  bool base =
    use && this->isSelected() && qobject_cast<pqRenderView*>(this->view()) != nullptr && this->PlaneWidget;
  if (base && this->view() && !this->isOutputPort0VisibleInView(this->view()))
  {
    base = false;
  }
  if (this->PlaneWidget)
  {
    vtkSMPropertyHelper(this->PlaneWidget, "Visibility", true).Set(base ? 1 : 0);
    vtkSMPropertyHelper(this->PlaneWidget, "Enabled", true).Set(base ? 1 : 0);
    this->PlaneWidget->UpdateVTKObjects();
  }
  if (pqView* v = this->view())
  {
    v->render();
  }
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::placePlaneBounds(
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
void pqVESPASelectionPlaneClipperWidget::stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const
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
int pqVESPASelectionPlaneClipperWidget::planeHintCountFromOutput(vtkPolyData* out) const
{
  if (!out)
  {
    return 0;
  }
  auto* da = vtkDoubleArray::SafeDownCast(out->GetFieldData()->GetAbstractArray(kPlanePackedFieldName));
  if (!da || da->GetNumberOfComponents() != 6 || da->GetNumberOfTuples() < 1)
  {
    return 0;
  }
  return 1;
}

//-----------------------------------------------------------------------------
bool pqVESPASelectionPlaneClipperWidget::readPlaneHandlesFromOutput(
  vtkPolyData* out, double origin[3], double dirHandle[3]) const
{
  if (!out)
  {
    return false;
  }
  auto* da = vtkDoubleArray::SafeDownCast(out->GetFieldData()->GetAbstractArray(kPlanePackedFieldName));
  if (!da || da->GetNumberOfComponents() != 6 || da->GetNumberOfTuples() < 1)
  {
    return false;
  }
  double buf[6];
  da->GetTuple(0, buf);
  origin[0]   = buf[0];
  origin[1]   = buf[1];
  origin[2]   = buf[2];
  dirHandle[0] = buf[3];
  dirHandle[1] = buf[4];
  dirHandle[2] = buf[5];
  return true;
}

//-----------------------------------------------------------------------------
void pqVESPASelectionPlaneClipperWidget::syncWidgetsFromFilterState()
{
  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src || !this->PlaneWidget)
  {
    return;
  }

  QString packedStr;
  if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
  {
    vtkSMPropertyHelper hp(p);
    const char* cs = hp.GetAsString(0);
    packedStr      = cs ? QString::fromUtf8(cs) : QString();
  }
  std::vector<double> parsed;
  const bool havePacked = ParsePackedDoubles(packedStr, parsed);

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  vtkPolyData*  outPd = alg ? vtkPolyData::SafeDownCast(alg->GetOutputDataObject(0)) : nullptr;

  double o[3] = { 0, 0, 0 };
  double d[3] = { 0, 0, 0 };
  if (havePacked)
  {
    o[0] = parsed[0];
    o[1] = parsed[1];
    o[2] = parsed[2];
    d[0] = parsed[3];
    d[1] = parsed[4];
    d[2] = parsed[5];
  }
  else if (!this->readPlaneHandlesFromOutput(outPd, o, d))
  {
    return;
  }
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
void pqVESPASelectionPlaneClipperWidget::pushPackedFromWidgetsToFilter()
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
void pqVESPASelectionPlaneClipperWidget::rebuildPlaneWidgetsIfNeeded()
{
  if (!this->UseInteractiveCheckbox || !this->UseInteractiveCheckbox->isChecked())
  {
    const QPointer<pqRenderView> hostSnap = this->LastPlaneHostRenderView;
    this->tearDownPlaneWidgets();
    if (pqView* v = this->view())
    {
      v->render();
    }
    if (hostSnap)
    {
      hostSnap->render();
    }
    return;
  }

  auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
  if (!src)
  {
    return;
  }
  src->UpdatePipeline();
  src->UpdatePipelineInformation();

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
  vtkPolyData*  outPd = alg ? vtkPolyData::SafeDownCast(alg->GetOutputDataObject(0)) : nullptr;
  const int n       = this->planeHintCountFromOutput(outPd);

  if (n <= 0)
  {
    this->tearDownPlaneWidgets();
    return;
  }

  if (this->PlaneWidget)
  {
    this->syncWidgetsFromFilterState();
    this->pushPackedFromWidgetsToFilter();
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

  double bounds[6] = { 0, 1, 0, 1, 0, 1 };
  if (vtkPVDataInformation* di = src->GetDataInformation(0))
  {
    di->GetBounds(bounds);
  }

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
  this->pushPackedFromWidgetsToFilter();
  this->updatePlaneWidgetsVisibility();
}
