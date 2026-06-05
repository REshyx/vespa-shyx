#include "pqSHYXEnhancedRulerWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqInteractivePropertyWidgetAbstract.h"
#include "pqPropertyWidget.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqServerManagerModel.h"

#include "vtkAlgorithm.h"
#include "vtkAxisActor2D.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDijkstraGraphGeodesicPath.h"
#include "vtkDistanceRepresentation2D.h"
#include "vtkDistanceWidget.h"
#include "vtkDoubleArray.h"
#include "vtkGeometryFilter.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMUncheckedPropertyHelper.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMProperty.h"

#include "vtkTextProperty.h"
#include "vtkTriangleFilter.h"

#include "vtkSmartPointer.h"

#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

#include <algorithm>

namespace
{
constexpr double kVertexMatchTol2 = 1e-12;
constexpr int kUninitializedVertexId = -1;
vtkSmartPointer<vtkPolyData> gEnhancedRulerSurfaceCache;

void applyRulerAxisTitleStyle(vtkAxisActor2D* axis)
{
    if (!axis)
    {
        return;
    }
    axis->SetTitlePosition(0.5);
    if (vtkTextProperty* tp = axis->GetTitleTextProperty())
    {
        tp->SetJustificationToCentered();
        tp->SetVerticalJustificationToCentered();
    }
}
}

//-----------------------------------------------------------------------------
pqSHYXEnhancedRulerWidget::pqSHYXEnhancedRulerWidget(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : pqInteractivePropertyWidget(
          "representations", "DistanceWidgetRepresentation", smproxy, smgroup, parent)
{
    auto* vbox = new QVBoxLayout(this);
    auto* info = new QLabel(
        tr("Drag the two handles to measure; the 3D label shows chord length while dragging and "
           "Distance / Path on two centered lines after release. On release, each handle snaps via a "
           "screen-ray pick to the nearest visible mesh point (same method as ParaView Select "
           "Points). Apply (or auto-apply) to refresh output polylines and panel distances."),
        this);
    info->setWordWrap(true);
    vbox->addWidget(info);

    auto* showWidget = new QCheckBox(tr("Show interactive ruler"), this);
    showWidget->setChecked(false);
    vbox->addWidget(showWidget);
    this->connect(showWidget, SIGNAL(toggled(bool)), SLOT(setWidgetVisible(bool)));
    showWidget->connect(this, SIGNAL(widgetVisibilityToggled(bool)), SLOT(setChecked(bool)));

    QObject::disconnect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this, nullptr);
    QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this,
        &pqSHYXEnhancedRulerWidget::onInputDataUpdated);

    QObject::connect(static_cast<pqInteractivePropertyWidgetAbstract*>(this),
        &pqInteractivePropertyWidgetAbstract::endInteraction, this,
        &pqSHYXEnhancedRulerWidget::onRulerEndInteraction);
    QObject::connect(this, &pqPropertyWidget::changeAvailable, this,
        &pqSHYXEnhancedRulerWidget::onRulerInteraction);

    if (auto* smm = pqApplicationCore::instance()->getServerManagerModel())
    {
        this->PipelineSource = smm->findItem<pqPipelineSource*>(smproxy);
        if (this->PipelineSource)
        {
            QObject::connect(this->PipelineSource.data(),
                static_cast<void (pqPipelineSource::*)(pqPipelineSource*)>(
                    &pqPipelineSource::dataUpdated),
                this, &pqSHYXEnhancedRulerWidget::onPipelineSourceUpdated);
        }
    }
}

//-----------------------------------------------------------------------------
pqSHYXEnhancedRulerWidget::~pqSHYXEnhancedRulerWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::select()
{
    this->initializeEndpointsIfNeeded();
    this->syncWidgetFromFilterOnSelect();
    this->Superclass::select();
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::onInputDataUpdated()
{
    if (this->initializeEndpointsIfNeeded())
    {
        this->syncWidgetFromFilterOnSelect();
    }
    this->placeWidget();
}

//-----------------------------------------------------------------------------
bool pqSHYXEnhancedRulerWidget::initializeEndpointsIfNeeded()
{
    vtkSMProxy* filter = this->proxy();
    vtkPoints* pts = this->inputPoints();
    if (!filter || !pts)
    {
        return false;
    }

    const vtkIdType n = pts->GetNumberOfPoints();
    if (n <= 0)
    {
        return false;
    }

    vtkSMPropertyHelper id2Helper(filter, "Point2VertexId");
    id2Helper.SetUseUnchecked(true);
    if (id2Helper.GetAsInt() != kUninitializedVertexId)
    {
        return false;
    }

    const vtkIdType id1 = 0;
    const vtkIdType id2 = std::max<vtkIdType>(0, n - 1);
    double p1[3], p2[3];
    pts->GetPoint(id1, p1);
    pts->GetPoint(id2, p2);

    vtkSMUncheckedPropertyHelper(filter, "Point1VertexId").Set(static_cast<int>(id1));
    vtkSMUncheckedPropertyHelper(filter, "Point2VertexId").Set(static_cast<int>(id2));
    vtkSMUncheckedPropertyHelper(filter, "Point1").Set(p1, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point2").Set(p2, 3);

    for (const char* propName : { "Point1", "Point2", "Point1VertexId", "Point2VertexId" })
    {
        if (vtkSMProperty* prop = filter->GetProperty(propName))
        {
            prop->Modified();
        }
    }

    if (vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy())
    {
        vtkSMPropertyHelper(wdg, "Point1WorldPosition").Set(p1, 3);
        vtkSMPropertyHelper(wdg, "Point2WorldPosition").Set(p2, 3);
        wdg->UpdateVTKObjects();
    }

    this->refreshRulerLabel(true);
    return true;
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::placeWidget()
{
    this->initializeEndpointsIfNeeded();
    vtkSMNewWidgetRepresentationProxy* w = this->widgetProxy();
    if (!w)
    {
        return;
    }
    double bds[6] = { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 };
    if (vtkPoints* pts = this->inputPoints())
    {
        pts->GetBounds(bds);
    }
    else if (vtkSMProxy* filter = this->proxy())
    {
        vtkSMPropertyHelper(filter, "Point1").Get(bds, 3);
        vtkSMPropertyHelper(filter, "Point2").Get(bds + 3, 3);
    }
    const double span[3] = { bds[1] - bds[0], bds[3] - bds[2], bds[5] - bds[4] };
    const double pad = std::max(1e-3, 0.05 * vtkMath::Norm(span));
    for (int i = 0; i < 3; ++i)
    {
        bds[2 * i] -= pad;
        bds[2 * i + 1] += pad;
    }
    vtkSMPropertyHelper(w, "PlaceWidget").Set(bds, 6);
    w->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
vtkPoints* pqSHYXEnhancedRulerWidget::inputPoints() const
{
    vtkSMProxy* filter = this->proxy();
    if (!filter || !filter->GetProperty("Input"))
    {
        return nullptr;
    }
    vtkSMSourceProxy* src =
        vtkSMSourceProxy::SafeDownCast(vtkSMPropertyHelper(filter, "Input").GetAsProxy());
    if (!src)
    {
        return nullptr;
    }
    vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    if (!alg)
    {
        return nullptr;
    }
    vtkDataObject* data = alg->GetOutputDataObject(0);
    if (auto* ps = vtkPointSet::SafeDownCast(data))
    {
        return ps->GetPoints();
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
vtkPolyData* pqSHYXEnhancedRulerWidget::inputSurfacePoly() const
{
    vtkSMProxy* filter = this->proxy();
    if (!filter || !filter->GetProperty("Input"))
    {
        return nullptr;
    }
    vtkSMSourceProxy* src =
        vtkSMSourceProxy::SafeDownCast(vtkSMPropertyHelper(filter, "Input").GetAsProxy());
    if (!src)
    {
        return nullptr;
    }
    vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    if (!alg)
    {
        return nullptr;
    }
    vtkDataObject* data = alg->GetOutputDataObject(0);
    if (auto* pd = vtkPolyData::SafeDownCast(data))
    {
        if (pd->GetNumberOfPolys() > 0)
        {
            return pd;
        }
    }
    if (auto* ds = vtkDataSet::SafeDownCast(data))
    {
        if (!gEnhancedRulerSurfaceCache)
        {
            gEnhancedRulerSurfaceCache = vtkSmartPointer<vtkPolyData>::New();
        }
        vtkNew<vtkGeometryFilter> geometry;
        geometry->SetInputData(ds);
        geometry->Update();
        gEnhancedRulerSurfaceCache->ShallowCopy(geometry->GetOutput());
        if (gEnhancedRulerSurfaceCache->GetNumberOfPolys() > 0)
        {
            return gEnhancedRulerSurfaceCache;
        }
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
bool pqSHYXEnhancedRulerWidget::pickMeshVertexAtDisplay(
    const int displayPos[2], double worldOut[3], vtkIdType& vertexIdOut) const
{
    pqRenderView* rv = qobject_cast<pqRenderView*>(this->view());
    if (!rv || !rv->getRenderViewProxy())
    {
        return false;
    }

    double normal[3] = { 0.0, 0.0, 0.0 };
    if (!rv->getRenderViewProxy()->ConvertDisplayToPointOnSurface(
            displayPos, worldOut, normal, /*snapOnMeshPoint=*/true))
    {
        return false;
    }
    if (std::isnan(worldOut[0]) || std::isnan(worldOut[1]) || std::isnan(worldOut[2]))
    {
        return false;
    }

    vtkPoints* pts = this->inputPoints();
    if (!pts || pts->GetNumberOfPoints() == 0)
    {
        return false;
    }

    vertexIdOut = pqSHYXEnhancedRulerWidget::vertexIdFromPosition(pts, worldOut);
    pts->GetPoint(vertexIdOut, worldOut);
    return true;
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXEnhancedRulerWidget::vertexIdFromPosition(vtkPoints* pts, const double world[3])
{
    if (!pts)
    {
        return 0;
    }
    const vtkIdType n = pts->GetNumberOfPoints();
    if (n <= 0)
    {
        return 0;
    }
    for (vtkIdType i = 0; i < n; ++i)
    {
        double q[3];
        pts->GetPoint(i, q);
        if (vtkMath::Distance2BetweenPoints(world, q) <= kVertexMatchTol2)
        {
            return i;
        }
    }
    return pqSHYXEnhancedRulerWidget::nearestPointId(pts, world);
}

//-----------------------------------------------------------------------------
bool pqSHYXEnhancedRulerWidget::snapEndpointFromHandle(
    vtkDistanceRepresentation2D* rep, bool point1, double worldOut[3], vtkIdType& vertexIdOut) const
{
    if (!rep)
    {
        return false;
    }

    double display3[3];
    if (point1)
    {
        rep->GetPoint1DisplayPosition(display3);
    }
    else
    {
        rep->GetPoint2DisplayPosition(display3);
    }

    const int displayPos[2] = { static_cast<int>(std::lround(display3[0])),
        static_cast<int>(std::lround(display3[1])) };
    if (this->pickMeshVertexAtDisplay(displayPos, worldOut, vertexIdOut))
    {
        return true;
    }

    // Fallback when nothing is pickable under the handle (e.g. empty view pick).
    if (point1)
    {
        rep->GetPoint1WorldPosition(worldOut);
    }
    else
    {
        rep->GetPoint2WorldPosition(worldOut);
    }
    vtkPoints* pts = this->inputPoints();
    if (!pts || pts->GetNumberOfPoints() == 0)
    {
        return false;
    }
    vertexIdOut = pqSHYXEnhancedRulerWidget::vertexIdFromPosition(pts, worldOut);
    pts->GetPoint(vertexIdOut, worldOut);
    return true;
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXEnhancedRulerWidget::nearestPointId(vtkPoints* pts, const double p[3])
{
    if (!pts)
    {
        return 0;
    }
    const vtkIdType n = pts->GetNumberOfPoints();
    if (n <= 0)
    {
        return 0;
    }
    vtkIdType best = 0;
    double bestD2 = VTK_DOUBLE_MAX;
    for (vtkIdType i = 0; i < n; ++i)
    {
        double q[3];
        pts->GetPoint(i, q);
        const double d2 = vtkMath::Distance2BetweenPoints(p, q);
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = i;
        }
    }
    return best;
}

//-----------------------------------------------------------------------------
double pqSHYXEnhancedRulerWidget::computeGeodesicDistance(
    vtkPolyData* surface, vtkIdType startId, vtkIdType endId)
{
    if (!surface || startId < 0 || endId < 0 || startId == endId)
    {
        return -1.0;
    }
    vtkNew<vtkTriangleFilter> triangles;
    triangles->SetInputData(surface);
    triangles->Update();
    vtkPolyData* triMesh = triangles->GetOutput();
    if (startId >= triMesh->GetNumberOfPoints() || endId >= triMesh->GetNumberOfPoints())
    {
        return -1.0;
    }

    vtkNew<vtkDijkstraGraphGeodesicPath> dijkstra;
    dijkstra->SetInputData(triMesh);
    dijkstra->SetStartVertex(startId);
    dijkstra->SetEndVertex(endId);
    dijkstra->StopWhenEndReachedOn();
    dijkstra->Update();

    vtkNew<vtkDoubleArray> weights;
    dijkstra->GetCumulativeWeights(weights);
    if (endId >= weights->GetNumberOfTuples())
    {
        return -1.0;
    }
    const double w = weights->GetValue(endId);
    if (w >= std::numeric_limits<double>::max() * 0.5)
    {
        return -1.0;
    }
    return w;
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::refreshRulerLabel(bool includeGeodesic)
{
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!wdg)
    {
        return;
    }
    vtkDistanceWidget* distW = vtkDistanceWidget::SafeDownCast(wdg->GetWidget());
    if (!distW)
    {
        return;
    }
    auto* rep = vtkDistanceRepresentation2D::SafeDownCast(distW->GetRepresentation());
    if (!rep)
    {
        return;
    }

    double p1[3], p2[3];
    rep->GetPoint1WorldPosition(p1);
    rep->GetPoint2WorldPosition(p2);
    const double straight = std::sqrt(vtkMath::Distance2BetweenPoints(p1, p2));

    double geodesic = -1.0;
    if (includeGeodesic)
    {
        if (vtkPoints* pts = this->inputPoints())
        {
            const vtkIdType id1 = pqSHYXEnhancedRulerWidget::vertexIdFromPosition(pts, p1);
            const vtkIdType id2 = pqSHYXEnhancedRulerWidget::vertexIdFromPosition(pts, p2);
            if (vtkPolyData* surface = this->inputSurfacePoly())
            {
                geodesic = pqSHYXEnhancedRulerWidget::computeGeodesicDistance(surface, id1, id2);
            }
        }
    }

    // LabelFormat without '%' is shown as-is (see vtkDistanceRepresentation). This survives
    // vtkDistanceRepresentation2D::BuildRepresentation() on each render, unlike SetTitle().
    char label[512];
    if (includeGeodesic)
    {
        if (geodesic >= 0.0 && std::isfinite(geodesic))
        {
            snprintf(label, sizeof(label), "Distance: %.4g\nPath: %.4g", straight, geodesic);
        }
        else
        {
            snprintf(label, sizeof(label), "Distance: %.4g\nPath: n/a", straight);
        }
    }
    else
    {
        snprintf(label, sizeof(label), "Distance: %.4g", straight);
    }
    rep->SetLabelFormat(label);
    rep->BuildRepresentation();
    applyRulerAxisTitleStyle(rep->GetAxis());
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::syncWidgetFromFilterOnSelect()
{
    vtkSMProxy* filter = this->proxy();
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!filter || !wdg)
    {
        return;
    }
    double p1[3], p2[3];
    vtkSMPropertyHelper p1h(filter, "Point1");
    vtkSMPropertyHelper p2h(filter, "Point2");
    p1h.SetUseUnchecked(true);
    p2h.SetUseUnchecked(true);
    p1h.Get(p1, 3);
    p2h.Get(p2, 3);
    vtkSMPropertyHelper(wdg, "Point1WorldPosition").Set(p1, 3);
    vtkSMPropertyHelper(wdg, "Point2WorldPosition").Set(p2, 3);
    wdg->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::snapEndpointsAndUpdateLabel()
{
    vtkSMProxy* filter = this->proxy();
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!filter || !wdg)
    {
        return;
    }

    vtkDistanceWidget* distW = vtkDistanceWidget::SafeDownCast(wdg->GetWidget());
    auto* rep = distW ? vtkDistanceRepresentation2D::SafeDownCast(distW->GetRepresentation()) : nullptr;
    if (!rep)
    {
        return;
    }

    double snapped1[3], snapped2[3];
    vtkIdType id1 = 0;
    vtkIdType id2 = 0;
    if (!this->snapEndpointFromHandle(rep, true, snapped1, id1) ||
        !this->snapEndpointFromHandle(rep, false, snapped2, id2))
    {
        return;
    }

    vtkSMPropertyHelper(wdg, "Point1WorldPosition").Set(snapped1, 3);
    vtkSMPropertyHelper(wdg, "Point2WorldPosition").Set(snapped2, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point1").Set(snapped1, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point2").Set(snapped2, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point1VertexId").Set(static_cast<int>(id1));
    vtkSMUncheckedPropertyHelper(filter, "Point2VertexId").Set(static_cast<int>(id2));

    for (const char* propName : { "Point1", "Point2", "Point1VertexId", "Point2VertexId" })
    {
        if (vtkSMProperty* prop = filter->GetProperty(propName))
        {
            prop->Modified();
        }
    }

    this->refreshRulerLabel(true);

    wdg->UpdateVTKObjects();
    this->placeWidget();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::onRulerInteraction()
{
    this->refreshRulerLabel(false);
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::onPipelineSourceUpdated()
{
    this->syncGeodesicDistancePanel();
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::syncGeodesicDistancePanel()
{
    vtkSMProxy* filter = this->proxy();
    if (!filter)
    {
        return;
    }
    vtkSMProperty* geoProp = filter->GetProperty("GeodesicDistance");
    if (!geoProp)
    {
        return;
    }

    if (auto* source = vtkSMSourceProxy::SafeDownCast(filter))
    {
        source->UpdatePropertyInformation(geoProp);
    }
    else
    {
        filter->UpdatePropertyInformation(geoProp);
    }

    for (QWidget* ancestor = this->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
    {
        for (pqPropertyWidget* pw : ancestor->findChildren<pqPropertyWidget*>())
        {
            if (pw->property() == geoProp)
            {
                pw->updateWidget(true);
                return;
            }
        }
    }
}

//-----------------------------------------------------------------------------
void pqSHYXEnhancedRulerWidget::onRulerEndInteraction()
{
    this->snapEndpointsAndUpdateLabel();
}
