#include "pqVESPAEndClipperPlaneHandlesWidget.h"

#include "pqApplicationCore.h"
#include "pqArraySelectionWidget.h"
#include "pqCoreUtilities.h"
#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPipelineSource.h"
#include "pqProxyWidget.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkBoundingBox.h"
#include "vtkCommand.h"
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
#include <QItemSelectionModel>
#include <QLabel>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>
#include <string>

namespace
{
constexpr int kMaxEndpoints = 64;

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
    double             v = 0.0;
    while (iss >> v)
    {
        out.push_back(v);
    }
    return !out.empty() && (out.size() % 6u) == 0u;
}

} // namespace

//-----------------------------------------------------------------------------
pqVESPAEndClipperPlaneHandlesWidget::pqVESPAEndClipperPlaneHandlesWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : pqPropertyGroupWidget(proxy, smgroup, parent)
{
    this->setChangeAvailableAsChangeFinished(false);

    auto* vbox = new QVBoxLayout(this);
    this->InfoLabel = new QLabel(
        tr("Enable interactive planes, then click a single row under \"Endpoints to Clip\" "
           "(blue selection, not the clip checkbox) to show the yellow plane for that endpoint. "
           "Dragging updates origin and normal in the panel only — use Apply (or turn on auto-apply) "
           "to run clipping with the edited planes."),
        this);
    this->InfoLabel->setWordWrap(true);
    vbox->addWidget(this->InfoLabel);

    this->UseInteractiveCheckbox = new QCheckBox(tr("Use interactive cut planes"), this);
    vbox->addWidget(this->UseInteractiveCheckbox);

    this->addPropertyLink(this->UseInteractiveCheckbox, "UseInteractiveCutPlanes");
    QObject::connect(this->UseInteractiveCheckbox, &QCheckBox::toggled, this,
        &pqVESPAEndClipperPlaneHandlesWidget::onUseInteractiveToggled);

    if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
    {
        this->PipelineSource = smm->findItem<pqPipelineSource*>(proxy);
        if (this->PipelineSource)
        {
            // Disambiguate public signal dataUpdated(pqPipelineSource*) from private slot dataUpdated().
            auto* srcObj = this->PipelineSource.data();
            QObject::connect(srcObj,
                static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(
                    &pqPipelineSource::dataUpdated),
                this, [this](pqPipelineSource*) { this->onPipelineDataUpdated(); });
        }
    }

    QTimer::singleShot(0, this, [this]() { this->tryConnectEndpointListSelection(); });
}

//-----------------------------------------------------------------------------
pqVESPAEndClipperPlaneHandlesWidget::~pqVESPAEndClipperPlaneHandlesWidget()
{
    this->disconnectViewVisibilityLinks();
    this->disconnectEndpointListSelection();
    this->tearDownPlaneWidgets();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::select()
{
    this->Superclass::select();
    QTimer::singleShot(0, this, [this]() { this->tryConnectEndpointListSelection(); });
    this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::deselect()
{
    this->Superclass::deselect();
    this->ActiveRowEndpointIndex = -1;
    this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::showEvent(QShowEvent* event)
{
    this->Superclass::showEvent(event);
    QTimer::singleShot(0, this, [this]() { this->tryConnectEndpointListSelection(); });
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::setView(pqView* view)
{
    this->disconnectViewVisibilityLinks();
    if (view)
    {
        this->ViewVisibilityConnections.push_back(QObject::connect(view,
            &pqView::representationVisibilityChanged, this,
            [this](pqRepresentation* /*repr*/, bool /*visible*/) {
                this->updatePlaneWidgetsVisibility();
            }));
    }
    this->detachPlaneWidgetsFromView();
    this->Superclass::setView(view);
    this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::onUseInteractiveToggled(bool /*on*/)
{
    this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::onPipelineDataUpdated()
{
    this->rebuildPlaneWidgetsIfNeeded();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::onPlaneInteraction()
{
    for (const auto& wdg : this->PlaneWidgets)
    {
        if (wdg)
        {
            vtkSMPropertyHelper(wdg, "DrawPlane", true).Set(1);
            wdg->UpdateVTKObjects();
        }
    }
    Q_EMIT this->changeAvailable();
    if (pqView* v = this->view())
    {
        v->render();
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::onPlaneEndInteraction()
{
    for (const auto& wdg : this->PlaneWidgets)
    {
        if (wdg)
        {
            vtkSMPropertyHelper(wdg, "DrawPlane", true).Set(1);
            wdg->UpdateVTKObjects();
        }
    }
    this->pushPackedFromWidgetsToFilter();
    Q_EMIT this->changeFinished();
    if (pqView* v = this->view())
    {
        v->render();
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::tearDownPlaneWidgets()
{
    this->detachPlaneWidgetsFromView();
    for (size_t i = 0; i < this->PlaneWidgets.size(); ++i)
    {
        vtkSMNewWidgetRepresentationProxy* w = this->PlaneWidgets[i].GetPointer();
        if (!w)
        {
            continue;
        }
        if (i < this->PlaneEndInteractionTags.size() && this->PlaneEndInteractionTags[i] != 0)
        {
            w->RemoveObserver(this->PlaneEndInteractionTags[i]);
        }
        if (i < this->PlaneInteractionTags.size() && this->PlaneInteractionTags[i] != 0)
        {
            w->RemoveObserver(this->PlaneInteractionTags[i]);
        }
    }
    this->PlaneWidgets.clear();
    this->PlaneEndInteractionTags.clear();
    this->PlaneInteractionTags.clear();
    this->LastPlaneHostRenderView.clear();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::detachPlaneWidgetsFromView()
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
    for (const auto& wdg : this->PlaneWidgets)
    {
        if (wdg)
        {
            vtkSMPropertyHelper(wdg, "Visibility", true).Set(0);
            vtkSMPropertyHelper(wdg, "Enabled", true).Set(0);
            wdg->UpdateVTKObjects();
            vtkSMPropertyHelper(rvpx, "HiddenRepresentations", true).Remove(wdg);
        }
    }
    rvpx->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::disconnectViewVisibilityLinks()
{
    for (const QMetaObject::Connection& c : this->ViewVisibilityConnections)
    {
        QObject::disconnect(c);
    }
    this->ViewVisibilityConnections.clear();
}

//-----------------------------------------------------------------------------
bool pqVESPAEndClipperPlaneHandlesWidget::isClipPreviewPort1VisibleInView(pqView* view) const
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
    bool foundPort1Repr = false;
    for (pqRepresentation* repr : view->getRepresentations())
    {
        auto* dr = qobject_cast<pqDataRepresentation*>(repr);
        if (!dr || dr->getInput() != src)
        {
            continue;
        }
        pqOutputPort* op = dr->getOutputPortFromInput();
        if (!op || op->getPortNumber() != 1)
        {
            continue;
        }
        foundPort1Repr = true;
        if (dr->isVisible())
        {
            return true;
        }
    }
    if (!foundPort1Repr)
    {
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::attachPlaneWidgetsToView()
{
    pqView* view = this->view();
    auto*   rv   = qobject_cast<pqRenderView*>(view);
    if (!rv)
    {
        return;
    }
    this->LastPlaneHostRenderView = rv;
    vtkSMRenderViewProxy* rvpx = rv->getRenderViewProxy();
    for (const auto& wdg : this->PlaneWidgets)
    {
        if (wdg)
        {
            vtkSMPropertyHelper(rvpx, "HiddenRepresentations", true).Add(wdg);
        }
    }
    rvpx->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::updatePlaneWidgetsVisibility()
{
    const bool use = this->UseInteractiveCheckbox && this->UseInteractiveCheckbox->isChecked();
    bool base =
        use && this->isSelected() && qobject_cast<pqRenderView*>(this->view()) != nullptr;
    if (base && this->view() && !this->isClipPreviewPort1VisibleInView(this->view()))
    {
        base = false;
    }
    const int focus = this->ActiveRowEndpointIndex;
    for (size_t i = 0; i < this->PlaneWidgets.size(); ++i)
    {
        const auto& wdg = this->PlaneWidgets[i];
        if (!wdg)
        {
            continue;
        }
        const bool showOne = base && focus >= 0 &&
            static_cast<size_t>(focus) < this->PlaneWidgets.size() &&
            static_cast<int>(i) == focus;
        vtkSMPropertyHelper(wdg, "Visibility", true).Set(showOne ? 1 : 0);
        vtkSMPropertyHelper(wdg, "Enabled", true).Set(showOne ? 1 : 0);
        wdg->UpdateVTKObjects();
    }
    if (pqView* v = this->view())
    {
        v->render();
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::disconnectEndpointListSelection()
{
    for (const QMetaObject::Connection& c : this->EndpointListConnections)
    {
        QObject::disconnect(c);
    }
    this->EndpointListConnections.clear();
    this->EndpointArrayListWidget = nullptr;
}

//-----------------------------------------------------------------------------
int pqVESPAEndClipperPlaneHandlesWidget::ParseEndpointIndexFromArrayRowLabel(const QString& label)
{
    static const QRegularExpression re(QStringLiteral("^Endpoint\\s+(\\d+)"));
    const QRegularExpressionMatch    m = re.match(label);
    if (m.hasMatch())
    {
        return m.captured(1).toInt();
    }
    return -1;
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::tryConnectEndpointListSelection()
{
    this->disconnectEndpointListSelection();

    vtkSMProxy*    myProxy = this->proxy();
    pqProxyWidget* panel   = nullptr;
    for (QWidget* w = this->parentWidget(); w; w = w->parentWidget())
    {
        if (auto* pw = qobject_cast<pqProxyWidget*>(w))
        {
            if (pw->proxy() == myProxy)
            {
                panel = pw;
                break;
            }
        }
    }
    if (!panel)
    {
        return;
    }

    QList<pqArraySelectionWidget*> lists = panel->findChildren<pqArraySelectionWidget*>(
        QStringLiteral("ArraySelectionWidget"), Qt::FindChildrenRecursively);

    pqArraySelectionWidget*          best    = nullptr;
    const int                        nPlanes = static_cast<int>(this->PlaneWidgets.size());
    for (pqArraySelectionWidget* list : lists)
    {
        if (!list || !list->model())
        {
            continue;
        }
        if (nPlanes > 0 && list->model()->rowCount() == nPlanes)
        {
            best = list;
            break;
        }
    }
    if (!best && !lists.isEmpty())
    {
        best = lists.first();
    }
    if (!best)
    {
        return;
    }

    this->EndpointArrayListWidget = best;
    if (QItemSelectionModel* sm = best->selectionModel())
    {
        this->EndpointListConnections.push_back(QObject::connect(sm,
            &QItemSelectionModel::currentChanged, this,
            &pqVESPAEndClipperPlaneHandlesWidget::onEndpointListCurrentChanged));

        this->onEndpointListCurrentChanged(sm->currentIndex(), QModelIndex());
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::onEndpointListCurrentChanged(
    const QModelIndex& current, const QModelIndex& /*previous*/)
{
    if (!current.isValid())
    {
        this->ActiveRowEndpointIndex = -1;
    }
    else
    {
        QModelIndex src = current;
        if (auto* proxyModel = qobject_cast<const QSortFilterProxyModel*>(current.model()))
        {
            src = proxyModel->mapToSource(current);
        }
        const QModelIndex nameIdx = src.siblingAtColumn(0);
        const QString     text    = nameIdx.data(Qt::DisplayRole).toString();
        this->ActiveRowEndpointIndex =
            pqVESPAEndClipperPlaneHandlesWidget::ParseEndpointIndexFromArrayRowLabel(text);
    }
    this->updatePlaneWidgetsVisibility();
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::placePlaneBounds(
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
void pqVESPAEndClipperPlaneHandlesWidget::stylePlaneWidget(vtkSMNewWidgetRepresentationProxy* wdg) const
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
int pqVESPAEndClipperPlaneHandlesWidget::endpointCountFromClipViz(vtkPolyData* viz) const
{
    if (!viz || viz->GetNumberOfPoints() < 2)
    {
        return 0;
    }
    return viz->GetNumberOfPoints() / 2;
}

//-----------------------------------------------------------------------------
bool pqVESPAEndClipperPlaneHandlesWidget::readEndpointHandlesFromViz(
    vtkPolyData* viz, int idx, double origin[3], double dirHandle[3]) const
{
    if (!viz || idx < 0 || 2 * idx + 1 >= viz->GetNumberOfPoints())
    {
        return false;
    }
    viz->GetPoint(2 * idx, origin);
    viz->GetPoint(2 * idx + 1, dirHandle);
    return true;
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::syncWidgetsFromFilterState()
{
    auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
    if (!src || static_cast<int>(this->PlaneWidgets.size()) == 0)
    {
        return;
    }
    const int n = static_cast<int>(this->PlaneWidgets.size());

    QString packedStr;
    if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
    {
        vtkSMPropertyHelper hp(p);
        const char*         cs = hp.GetAsString(0);
        packedStr                = cs ? QString::fromUtf8(cs) : QString();
    }
    std::vector<double> parsed;
    const bool          havePacked = ParsePackedDoubles(packedStr, parsed) &&
        static_cast<int>(parsed.size()) == 6 * n;

    vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    vtkPolyData*  viz = alg ? vtkPolyData::SafeDownCast(alg->GetOutputDataObject(1)) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        vtkSMNewWidgetRepresentationProxy* wdg = this->PlaneWidgets[static_cast<size_t>(i)];
        if (!wdg)
        {
            continue;
        }
        double o[3] = { 0, 0, 0 };
        double d[3] = { 0, 0, 0 };
        if (havePacked)
        {
            o[0] = parsed[static_cast<size_t>(6 * i + 0)];
            o[1] = parsed[static_cast<size_t>(6 * i + 1)];
            o[2] = parsed[static_cast<size_t>(6 * i + 2)];
            d[0] = parsed[static_cast<size_t>(6 * i + 3)];
            d[1] = parsed[static_cast<size_t>(6 * i + 4)];
            d[2] = parsed[static_cast<size_t>(6 * i + 5)];
        }
        else if (!this->readEndpointHandlesFromViz(viz, i, o, d))
        {
            continue;
        }
        double nrm[3] = { d[0] - o[0], d[1] - o[1], d[2] - o[2] };
        if (vtkMath::Normalize(nrm) < 1e-15)
        {
            nrm[0] = 0.0;
            nrm[1] = 0.0;
            nrm[2] = 1.0;
        }
        vtkSMPropertyHelper(wdg, "Origin", true).Set(o, 3);
        vtkSMPropertyHelper(wdg, "Normal", true).Set(nrm, 3);
        wdg->UpdateVTKObjects();
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::pushPackedFromWidgetsToFilter()
{
    auto* src = vtkSMSourceProxy::SafeDownCast(this->proxy());
    if (!src)
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

    QString packed;
    for (const auto& wdg : this->PlaneWidgets)
    {
        if (!wdg)
        {
            continue;
        }
        double o[3];
        double nrm[3];
        vtkSMPropertyHelper ho(wdg, "Origin", true);
        vtkSMPropertyHelper hn(wdg, "Normal", true);
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
        const double d[3] = { o[0] + arm * nrm[0], o[1] + arm * nrm[1], o[2] + arm * nrm[2] };
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
            packed += pqCoreUtilities::number(d[k]);
        }
    }
    // Checked SM state is what Apply pushes to the VTK object; skip UpdateVTKObjects here so
    // the filter does not run until Apply (or auto-apply), unlike the old unchecked-only path.
    if (vtkSMProperty* p = src->GetProperty("InteractiveCutPacked"))
    {
        vtkSMPropertyHelper hp(p);
        const QByteArray   utf = packed.toUtf8();
        hp.Set(0, utf.constData());
    }
}

//-----------------------------------------------------------------------------
void pqVESPAEndClipperPlaneHandlesWidget::rebuildPlaneWidgetsIfNeeded()
{
    if (!this->UseInteractiveCheckbox || !this->UseInteractiveCheckbox->isChecked())
    {
        this->ActiveRowEndpointIndex = -1;
        this->disconnectEndpointListSelection();
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
    vtkPolyData*  viz = alg ? vtkPolyData::SafeDownCast(alg->GetOutputDataObject(1)) : nullptr;
    int           n   = this->endpointCountFromClipViz(viz);
    if (n > kMaxEndpoints)
    {
        if (this->InfoLabel)
        {
            this->InfoLabel->setText(tr("More than %1 endpoints; only the first %1 are shown for "
                                         "interactive editing.")
                    .arg(kMaxEndpoints));
        }
        n = kMaxEndpoints;
    }
    else if (this->InfoLabel)
    {
        this->InfoLabel->setText(
            tr("Enable interactive planes, then click a row under \"Endpoints to Clip\" "
               "(blue selection, not the checkbox) to edit that endpoint's yellow plane. "
               "Apply (or auto-apply) runs clipping; dragging alone does not."));
    }

    if (n <= 0)
    {
        this->tearDownPlaneWidgets();
        return;
    }

    if (n > 0 && static_cast<int>(this->PlaneWidgets.size()) == n && this->PlaneWidgets[0])
    {
        this->syncWidgetsFromFilterState();
        this->pushPackedFromWidgetsToFilter();
        this->attachPlaneWidgetsToView();
        this->updatePlaneWidgetsVisibility();
        this->tryConnectEndpointListSelection();
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

    this->PlaneWidgets.resize(static_cast<size_t>(n));
    this->PlaneEndInteractionTags.resize(static_cast<size_t>(n), 0);
    this->PlaneInteractionTags.resize(static_cast<size_t>(n), 0);

    for (int i = 0; i < n; ++i)
    {
        vtkSmartPointer<vtkSMProxy> aProxy;
        aProxy.TakeReference(
            pxm->NewProxy("representations", "DisplaySizedImplicitPlaneWidgetRepresentation"));
        auto* wdg = vtkSMNewWidgetRepresentationProxy::SafeDownCast(aProxy);
        if (!wdg)
        {
            continue;
        }
        controller->InitializeProxy(wdg);
        wdg->PrototypeOn();
        this->stylePlaneWidget(wdg);
        this->placePlaneBounds(wdg, bounds);

        this->PlaneEndInteractionTags[static_cast<size_t>(i)] = pqCoreUtilities::connect(wdg,
            vtkCommand::EndInteractionEvent, this, SLOT(onPlaneEndInteraction()));
        this->PlaneInteractionTags[static_cast<size_t>(i)] = pqCoreUtilities::connect(wdg,
            vtkCommand::InteractionEvent, this, SLOT(onPlaneInteraction()));

        this->PlaneWidgets[static_cast<size_t>(i)] = wdg;
    }

    this->attachPlaneWidgetsToView();
    this->syncWidgetsFromFilterState();
    this->pushPackedFromWidgetsToFilter();
    this->updatePlaneWidgetsVisibility();
    this->tryConnectEndpointListSelection();
}
