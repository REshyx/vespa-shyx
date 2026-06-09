#include "pqSHYXEndpointStentWidget.h"

#include "vtkSHYXCoronaryStentCatalog.h"
#include "vtkSHYXEndpointStentPlacement.h"
#include "vtkSHYXEnhancedRuler.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqInteractivePropertyWidgetAbstract.h"
#include "pqRenderView.h"

#include "vtkAlgorithm.h"
#include "vtkAxisActor2D.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDistanceRepresentation2D.h"
#include "vtkDistanceWidget.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMUncheckedPropertyHelper.h"
#include "vtkTextProperty.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>

namespace
{
constexpr double kVertexMatchTol2 = 1e-12;
constexpr int kUninitializedVertexId = -1;

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

std::string TrimAscii(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.erase(0, 1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.pop_back();
    }
    return s;
}

} // namespace

//-----------------------------------------------------------------------------
pqSHYXEndpointStentWidget::pqSHYXEndpointStentWidget(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : pqInteractivePropertyWidget(
          "representations", "DistanceWidgetRepresentation", smproxy, smgroup, parent)
{
    auto* vbox = new QVBoxLayout(this);
    auto* info = new QLabel(
        tr("Drag the two handles along the centerline to measure length and radius, then pick a "
           "nominal coronary stent size from the catalog dropdowns (or use Custom to keep measured "
           "values). Catalog length expands or contracts both endpoints symmetrically about the "
           "segment midpoint."),
        this);
    info->setWordWrap(true);
    vbox->addWidget(info);

    auto* diamRow = new QHBoxLayout();
    diamRow->addWidget(new QLabel(tr("Catalog diameter:"), this));
    this->DiameterCatalogCombo = new QComboBox(this);
    diamRow->addWidget(this->DiameterCatalogCombo, 1);
    vbox->addLayout(diamRow);

    auto* lenRow = new QHBoxLayout();
    lenRow->addWidget(new QLabel(tr("Catalog length:"), this));
    this->LengthCatalogCombo = new QComboBox(this);
    lenRow->addWidget(this->LengthCatalogCombo, 1);
    vbox->addLayout(lenRow);

    this->populateCatalogCombos();
    QObject::connect(this->DiameterCatalogCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        &pqSHYXEndpointStentWidget::onCatalogDiameterChanged);
    QObject::connect(this->LengthCatalogCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        &pqSHYXEndpointStentWidget::onCatalogLengthChanged);

    auto* showWidget = new QCheckBox(tr("Show interactive endpoints"), this);
    showWidget->setChecked(false);
    vbox->addWidget(showWidget);
    this->connect(showWidget, SIGNAL(toggled(bool)), SLOT(setWidgetVisible(bool)));
    showWidget->connect(this, SIGNAL(widgetVisibilityToggled(bool)), SLOT(setChecked(bool)));

    QObject::disconnect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this, nullptr);
    QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this,
        &pqSHYXEndpointStentWidget::onCenterlineDataUpdated);

    QObject::connect(static_cast<pqInteractivePropertyWidgetAbstract*>(this),
        &pqInteractivePropertyWidgetAbstract::endInteraction, this,
        &pqSHYXEndpointStentWidget::onRulerEndInteraction);
    QObject::connect(this, &pqPropertyWidget::changeAvailable, this,
        &pqSHYXEndpointStentWidget::onRulerInteraction);
}

//-----------------------------------------------------------------------------
pqSHYXEndpointStentWidget::~pqSHYXEndpointStentWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::select()
{
    this->initializeEndpointsIfNeeded();
    this->syncWidgetFromFilterOnSelect();
    this->syncCatalogCombosFromFilter();
    this->Superclass::select();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::onCenterlineDataUpdated()
{
    if (this->initializeEndpointsIfNeeded())
    {
        this->syncWidgetFromFilterOnSelect();
    }
    this->placeWidget();
}

//-----------------------------------------------------------------------------
bool pqSHYXEndpointStentWidget::initializeEndpointsIfNeeded()
{
    vtkSMProxy* filter = this->proxy();
    vtkPoints* pts = this->centerlinePoints();
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

    this->updateStentLengthAndRadius(id1, id2);

    for (const char* propName :
        { "Point1", "Point2", "Point1VertexId", "Point2VertexId", "StentLength", "StentRadius" })
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
void pqSHYXEndpointStentWidget::placeWidget()
{
    this->initializeEndpointsIfNeeded();
    vtkSMNewWidgetRepresentationProxy* w = this->widgetProxy();
    if (!w)
    {
        return;
    }
    double bds[6] = { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 };
    if (vtkPoints* pts = this->centerlinePoints())
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
vtkPolyData* pqSHYXEndpointStentWidget::centerlineClientPoly() const
{
    vtkSMProxy* filter = this->proxy();
    if (!filter || !filter->GetProperty("Centerline"))
    {
        return nullptr;
    }
    vtkSMSourceProxy* src =
        vtkSMSourceProxy::SafeDownCast(vtkSMPropertyHelper(filter, "Centerline").GetAsProxy());
    if (!src)
    {
        return nullptr;
    }
    vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(src->GetClientSideObject());
    if (!alg)
    {
        return nullptr;
    }
    return vtkPolyData::SafeDownCast(alg->GetOutputDataObject(0));
}

//-----------------------------------------------------------------------------
vtkPoints* pqSHYXEndpointStentWidget::centerlinePoints() const
{
    vtkPolyData* cl = this->centerlineClientPoly();
    return cl ? cl->GetPoints() : nullptr;
}

//-----------------------------------------------------------------------------
bool pqSHYXEndpointStentWidget::pickCenterlineVertexAtDisplay(
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

    vtkPoints* pts = this->centerlinePoints();
    if (!pts || pts->GetNumberOfPoints() == 0)
    {
        return false;
    }

    vertexIdOut = pqSHYXEndpointStentWidget::vertexIdFromPosition(pts, worldOut);
    pts->GetPoint(vertexIdOut, worldOut);
    return true;
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXEndpointStentWidget::vertexIdFromPosition(vtkPoints* pts, const double world[3])
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
    return pqSHYXEndpointStentWidget::nearestPointId(pts, world);
}

//-----------------------------------------------------------------------------
bool pqSHYXEndpointStentWidget::snapEndpointFromHandle(
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
    if (this->pickCenterlineVertexAtDisplay(displayPos, worldOut, vertexIdOut))
    {
        return true;
    }

    if (point1)
    {
        rep->GetPoint1WorldPosition(worldOut);
    }
    else
    {
        rep->GetPoint2WorldPosition(worldOut);
    }
    vtkPoints* pts = this->centerlinePoints();
    if (!pts || pts->GetNumberOfPoints() == 0)
    {
        return false;
    }
    vertexIdOut = pqSHYXEndpointStentWidget::vertexIdFromPosition(pts, worldOut);
    pts->GetPoint(vertexIdOut, worldOut);
    return true;
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXEndpointStentWidget::nearestPointId(vtkPoints* pts, const double p[3])
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
double pqSHYXEndpointStentWidget::computeCenterlinePathDistance(
    vtkPolyData* centerline, vtkIdType startId, vtkIdType endId)
{
    return vtkSHYXEnhancedRuler::ComputePathDistance(centerline, startId, endId, nullptr);
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXEndpointStentWidget::midpointVertexOnPath(vtkPolyData* pathPoly, vtkPolyData* centerline)
{
    if (!pathPoly || !centerline)
    {
        return 0;
    }
    vtkPoints* pathPts = pathPoly->GetPoints();
    vtkPoints* clPts = centerline->GetPoints();
    if (!pathPts || !clPts || pathPts->GetNumberOfPoints() < 1)
    {
        return 0;
    }
    const vtkIdType nPath = pathPts->GetNumberOfPoints();
    if (nPath == 1)
    {
        double p[3];
        pathPts->GetPoint(0, p);
        return pqSHYXEndpointStentWidget::nearestPointId(clPts, p);
    }

    double total = 0.0;
    for (vtkIdType i = 1; i < nPath; ++i)
    {
        double a[3], b[3];
        pathPts->GetPoint(i - 1, a);
        pathPts->GetPoint(i, b);
        total += std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
    }
    if (total <= 0.0)
    {
        double p[3];
        pathPts->GetPoint(0, p);
        return pqSHYXEndpointStentWidget::nearestPointId(clPts, p);
    }

    const double half = 0.5 * total;
    double acc = 0.0;
    for (vtkIdType i = 1; i < nPath; ++i)
    {
        double a[3], b[3];
        pathPts->GetPoint(i - 1, a);
        pathPts->GetPoint(i, b);
        const double seg = std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
        if (acc + seg >= half)
        {
            double p[3];
            pathPts->GetPoint(i, p);
            return pqSHYXEndpointStentWidget::nearestPointId(clPts, p);
        }
        acc += seg;
    }
    double p[3];
    pathPts->GetPoint(nPath - 1, p);
    return pqSHYXEndpointStentWidget::nearestPointId(clPts, p);
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::populateCatalogCombos()
{
    if (!this->DiameterCatalogCombo || !this->LengthCatalogCombo)
    {
        return;
    }
    this->UpdatingCatalogCombos = true;
    this->DiameterCatalogCombo->clear();
    this->LengthCatalogCombo->clear();
    this->DiameterCatalogCombo->addItem(tr("Custom (measured)"));
    for (int i = 1; i <= vtkSHYXCoronaryStentCatalog::DiameterCatalogCount(); ++i)
    {
        this->DiameterCatalogCombo->addItem(
            QString::number(vtkSHYXCoronaryStentCatalog::DiameterMm(i), 'g', 4));
    }
    this->LengthCatalogCombo->addItem(tr("Custom (measured)"));
    for (int i = 1; i <= vtkSHYXCoronaryStentCatalog::LengthCatalogCount(); ++i)
    {
        this->LengthCatalogCombo->addItem(
            QString::number(vtkSHYXCoronaryStentCatalog::LengthMm(i), 'f', 0));
    }
    this->UpdatingCatalogCombos = false;
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::syncCatalogCombosFromFilter()
{
    vtkSMProxy* filter = this->proxy();
    if (!filter || !this->DiameterCatalogCombo || !this->LengthCatalogCombo)
    {
        return;
    }
    vtkSMPropertyHelper diamHelper(filter, "StentCatalogDiameterIndex");
    vtkSMPropertyHelper lenHelper(filter, "StentCatalogLengthIndex");
    diamHelper.SetUseUnchecked(true);
    lenHelper.SetUseUnchecked(true);
    const int diamIdx = std::clamp(diamHelper.GetAsInt(), 0, vtkSHYXCoronaryStentCatalog::DiameterCatalogCount());
    const int lenIdx = std::clamp(lenHelper.GetAsInt(), 0, vtkSHYXCoronaryStentCatalog::LengthCatalogCount());

    this->UpdatingCatalogCombos = true;
    this->DiameterCatalogCombo->setCurrentIndex(diamIdx);
    this->LengthCatalogCombo->setCurrentIndex(lenIdx);
    this->UpdatingCatalogCombos = false;
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::setCatalogSelectionToCustom()
{
    vtkSMProxy* filter = this->proxy();
    if (!filter)
    {
        return;
    }
    vtkSMUncheckedPropertyHelper(filter, "StentCatalogDiameterIndex").Set(vtkSHYXCoronaryStentCatalog::kCustomIndex);
    vtkSMUncheckedPropertyHelper(filter, "StentCatalogLengthIndex").Set(vtkSHYXCoronaryStentCatalog::kCustomIndex);
    this->markCatalogPropertiesModified();

    this->UpdatingCatalogCombos = true;
    if (this->DiameterCatalogCombo)
    {
        this->DiameterCatalogCombo->setCurrentIndex(0);
    }
    if (this->LengthCatalogCombo)
    {
        this->LengthCatalogCombo->setCurrentIndex(0);
    }
    this->UpdatingCatalogCombos = false;
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::markCatalogPropertiesModified()
{
    vtkSMProxy* filter = this->proxy();
    if (!filter)
    {
        return;
    }
    for (const char* propName :
        { "StentCatalogDiameterIndex", "StentCatalogLengthIndex", "StentLength", "StentRadius" })
    {
        if (vtkSMProperty* prop = filter->GetProperty(propName))
        {
            prop->Modified();
        }
    }
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::applyCatalogDiameter(int catalogIndex)
{
    vtkSMProxy* filter = this->proxy();
    if (!filter || catalogIndex <= vtkSHYXCoronaryStentCatalog::kCustomIndex)
    {
        return;
    }
    const double radius = vtkSHYXCoronaryStentCatalog::RadiusMm(catalogIndex);
    vtkSMUncheckedPropertyHelper(filter, "StentCatalogDiameterIndex").Set(catalogIndex);
    vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(radius);
    this->markCatalogPropertiesModified();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::pushEndpointsToFilter(
    vtkIdType id1, const double pos1[3], vtkIdType id2, const double pos2[3])
{
    vtkSMProxy* filter = this->proxy();
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!filter)
    {
        return;
    }
    vtkSMUncheckedPropertyHelper(filter, "Point1").Set(pos1, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point2").Set(pos2, 3);
    vtkSMUncheckedPropertyHelper(filter, "Point1VertexId").Set(static_cast<int>(id1));
    vtkSMUncheckedPropertyHelper(filter, "Point2VertexId").Set(static_cast<int>(id2));
    if (wdg)
    {
        vtkSMPropertyHelper(wdg, "Point1WorldPosition").Set(pos1, 3);
        vtkSMPropertyHelper(wdg, "Point2WorldPosition").Set(pos2, 3);
        wdg->UpdateVTKObjects();
    }
    for (const char* propName : { "Point1", "Point2", "Point1VertexId", "Point2VertexId" })
    {
        if (vtkSMProperty* prop = filter->GetProperty(propName))
        {
            prop->Modified();
        }
    }
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::applyCatalogLength(int catalogIndex)
{
    vtkSMProxy* filter = this->proxy();
    vtkPolyData* cl = this->centerlineClientPoly();
    if (!filter || !cl || catalogIndex <= vtkSHYXCoronaryStentCatalog::kCustomIndex)
    {
        return;
    }

    vtkSMPropertyHelper id1Helper(filter, "Point1VertexId");
    id1Helper.SetUseUnchecked(true);
    const vtkIdType id1 = static_cast<vtkIdType>(id1Helper.GetAsInt());
    vtkSMPropertyHelper id2Helper(filter, "Point2VertexId");
    id2Helper.SetUseUnchecked(true);
    const vtkIdType hintId2 = static_cast<vtkIdType>(id2Helper.GetAsInt());
    if (id1 < 0 || hintId2 < 0 || id1 == hintId2)
    {
        return;
    }

    const double targetLen = vtkSHYXCoronaryStentCatalog::LengthMm(catalogIndex);
    vtkIdType newId1 = id1;
    vtkIdType newId2 = hintId2;
    double newPos1[3] = { 0.0, 0.0, 0.0 };
    double newPos2[3] = { 0.0, 0.0, 0.0 };
    if (!vtkSHYXEndpointStentPlacement::ComputeSymmetricEndpointsForLength(
            cl, id1, hintId2, targetLen, newId1, newPos1, newId2, newPos2))
    {
        return;
    }

    vtkSMUncheckedPropertyHelper(filter, "StentCatalogLengthIndex").Set(catalogIndex);
    vtkSMUncheckedPropertyHelper(filter, "StentLength").Set(targetLen);
    this->pushEndpointsToFilter(newId1, newPos1, newId2, newPos2);
    this->markCatalogPropertiesModified();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::onCatalogDiameterChanged(int comboIndex)
{
    if (this->UpdatingCatalogCombos || comboIndex < 0)
    {
        return;
    }
    vtkSMProxy* filter = this->proxy();
    if (!filter)
    {
        return;
    }
    if (comboIndex == vtkSHYXCoronaryStentCatalog::kCustomIndex)
    {
        vtkSMUncheckedPropertyHelper(filter, "StentCatalogDiameterIndex").Set(comboIndex);
        this->markCatalogPropertiesModified();
        return;
    }
    this->applyCatalogDiameter(comboIndex);
    this->refreshRulerLabel(true);
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::onCatalogLengthChanged(int comboIndex)
{
    if (this->UpdatingCatalogCombos || comboIndex < 0)
    {
        return;
    }
    vtkSMProxy* filter = this->proxy();
    if (!filter)
    {
        return;
    }
    if (comboIndex == vtkSHYXCoronaryStentCatalog::kCustomIndex)
    {
        vtkSMUncheckedPropertyHelper(filter, "StentCatalogLengthIndex").Set(comboIndex);
        this->markCatalogPropertiesModified();
        return;
    }
    this->applyCatalogLength(comboIndex);
    this->refreshRulerLabel(true);
    this->placeWidget();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::updateStentLengthAndRadius(vtkIdType id1, vtkIdType id2)
{
    vtkSMProxy* filter = this->proxy();
    vtkPolyData* cl = this->centerlineClientPoly();
    if (!filter || !cl || id1 < 0 || id2 < 0 || id1 == id2)
    {
        return;
    }

    vtkNew<vtkPolyData> pathPoly;
    const double pathLen = vtkSHYXEnhancedRuler::ComputePathDistance(cl, id1, id2, pathPoly);
    if (pathLen > 0.0 && std::isfinite(pathLen))
    {
        vtkSMUncheckedPropertyHelper(filter, "StentLength").Set(pathLen);
    }

    vtkSMProperty* radiusArrayProp = filter->GetProperty("CenterlineRadiusArrayName");
    if (!radiusArrayProp)
    {
        return;
    }
    const char* raw = vtkSMPropertyHelper(radiusArrayProp).GetAsString();
    if (!raw)
    {
        return;
    }
    const std::string name = TrimAscii(std::string(raw));
    if (name.empty())
    {
        return;
    }

    const vtkIdType midId = pqSHYXEndpointStentWidget::midpointVertexOnPath(pathPoly, cl);
    vtkDataArray* arr = cl->GetPointData()->GetArray(name.c_str());
    if (!arr || midId < 0 || midId >= arr->GetNumberOfTuples() || arr->GetNumberOfComponents() < 1)
    {
        return;
    }
    const double r = arr->GetComponent(midId, 0);
    if (std::isfinite(r) && r > 0.0)
    {
        vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(r);
    }
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::refreshRulerLabel(bool includePathLength)
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

    double pathLen = -1.0;
    if (includePathLength)
    {
        if (vtkPoints* pts = this->centerlinePoints())
        {
            const vtkIdType id1 = pqSHYXEndpointStentWidget::vertexIdFromPosition(pts, p1);
            const vtkIdType id2 = pqSHYXEndpointStentWidget::vertexIdFromPosition(pts, p2);
            if (vtkPolyData* cl = this->centerlineClientPoly())
            {
                pathLen = pqSHYXEndpointStentWidget::computeCenterlinePathDistance(cl, id1, id2);
            }
        }
    }

    vtkSMProxy* filter = this->proxy();
    double radius = 0.0;
    if (filter)
    {
        vtkSMPropertyHelper rh(filter, "StentRadius");
        rh.SetUseUnchecked(true);
        radius = rh.GetAsDouble();
    }
    const double diameter = 2.0 * radius;
    const int nearDiam = vtkSHYXCoronaryStentCatalog::NearestDiameterIndex(diameter);
    const int nearLen = (pathLen > 0.0 && std::isfinite(pathLen))
        ? vtkSHYXCoronaryStentCatalog::NearestLengthIndex(pathLen)
        : vtkSHYXCoronaryStentCatalog::kCustomIndex;

    char label[640];
    if (includePathLength)
    {
        const char* lenHint = "";
        char lenHintBuf[48] = {};
        if (pathLen > 0.0 && std::isfinite(pathLen) && nearLen > vtkSHYXCoronaryStentCatalog::kCustomIndex)
        {
            snprintf(lenHintBuf, sizeof(lenHintBuf), " (nearest %g)",
                vtkSHYXCoronaryStentCatalog::LengthMm(nearLen));
            lenHint = lenHintBuf;
        }
        const char* diamHint = "";
        char diamHintBuf[48] = {};
        if (radius > 0.0 && nearDiam > vtkSHYXCoronaryStentCatalog::kCustomIndex)
        {
            snprintf(diamHintBuf, sizeof(diamHintBuf), " (nearest %g)",
                vtkSHYXCoronaryStentCatalog::DiameterMm(nearDiam));
            diamHint = diamHintBuf;
        }
        if (pathLen >= 0.0 && std::isfinite(pathLen))
        {
            snprintf(label, sizeof(label), "Chord: %.4g\nPath: %.4g%s\nDiam: %.4g%s", straight, pathLen, lenHint,
                diameter, diamHint);
        }
        else
        {
            snprintf(label, sizeof(label), "Chord: %.4g\nPath: n/a\nDiam: %.4g%s", straight, diameter, diamHint);
        }
    }
    else
    {
        snprintf(label, sizeof(label), "Chord: %.4g", straight);
    }
    rep->SetLabelFormat(label);
    rep->BuildRepresentation();
    applyRulerAxisTitleStyle(rep->GetAxis());
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::syncWidgetFromFilterOnSelect()
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
void pqSHYXEndpointStentWidget::snapEndpointsAndUpdateStentParams()
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

    this->setCatalogSelectionToCustom();
    this->updateStentLengthAndRadius(id1, id2);

    for (const char* propName :
        { "Point1", "Point2", "Point1VertexId", "Point2VertexId", "StentLength", "StentRadius",
            "StentCatalogDiameterIndex", "StentCatalogLengthIndex" })
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
void pqSHYXEndpointStentWidget::onRulerInteraction()
{
    this->refreshRulerLabel(false);
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXEndpointStentWidget::onRulerEndInteraction()
{
    this->snapEndpointsAndUpdateStentParams();
}
