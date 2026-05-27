#include "pqSHYXVascularStentCylinderWidget.h"

#include "pqActiveObjects.h"
#include "pqInteractivePropertyWidgetAbstract.h"

#include <QtCore/QCoreApplication>
#include "ui_pqCylinderPropertyWidget.h"

#include "vtkAlgorithm.h"
#include "vtkCellArray.h"
#include "vtkDataArray.h"
#include "vtkImplicitCylinderRepresentation.h"
#include "vtkImplicitCylinderWidget.h"
#include "vtkMath.h"
#include "vtkPoints.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMUncheckedPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSourceProxy.h"

#include "vtkSHYXImplicitCylinderRepresentation.h"

#include "vtkCallbackCommand.h"
#include "vtkCommand.h"

#include <QMetaObject>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr double kEps = 1e-12;
/** Inflate finite-cylinder widget bounds by 10% beyond analytic geometry + pad. */
constexpr double kWidgetBoundsScale = 1.1;

void SHYXOnStentLengthPropertyEvent(vtkObject*, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<pqSHYXVascularStentCylinderWidget*>(clientData);
    if (self)
    {
        QMetaObject::invokeMethod(self, "onStentLengthPropertyModified", Qt::QueuedConnection);
    }
}

/** Prefer SM unchecked StentLength (Properties panel draft before Apply), else checked. */
double SHYXGetStentLengthFromFilter(vtkSMProxy* filter)
{
    if (!filter || !filter->GetProperty("StentLength"))
    {
        return 10.0;
    }
    vtkSMPropertyHelper h(filter, "StentLength");
    h.SetUseUnchecked(true);
    double L = h.GetAsDouble(0);
    if (!std::isfinite(L) || L <= 0.0)
    {
        h.SetUseUnchecked(false);
        L = h.GetAsDouble(0);
    }
    if (!std::isfinite(L) || L <= 0.0)
    {
        return 1e-9;
    }
    return L;
}

/** Prefer SM unchecked StentRadius (draft before Apply), else checked. */
double SHYXGetStentRadiusFromFilter(vtkSMProxy* filter)
{
    if (!filter || !filter->GetProperty("StentRadius"))
    {
        return 0.0;
    }
    vtkSMPropertyHelper h(filter, "StentRadius");
    h.SetUseUnchecked(true);
    double R = h.GetAsDouble(0);
    if (!std::isfinite(R) || R <= 0.0)
    {
        h.SetUseUnchecked(false);
        R = h.GetAsDouble(0);
    }
    return R;
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

/** If CenterlineRadiusArrayName is set on \a filter, copy scalar at \a pid into StentRadius + widget Radius. */
bool ApplyStentRadiusFromCenterlinePointArray(
    vtkPolyData* cl, vtkSMProxy* filter, vtkSMNewWidgetRepresentationProxy* wdg, vtkIdType pid)
{
    if (!cl || !filter || !wdg || pid < 0 || pid >= cl->GetNumberOfPoints())
    {
        return false;
    }
    vtkSMProperty* prop = filter->GetProperty("CenterlineRadiusArrayName");
    if (!prop)
    {
        return false;
    }
    const char* raw = vtkSMPropertyHelper(prop).GetAsString();
    if (!raw)
    {
        return false;
    }
    const std::string name = TrimAscii(std::string(raw));
    if (name.empty())
    {
        return false;
    }
    vtkDataArray* arr = cl->GetPointData()->GetArray(name.c_str());
    if (!arr || arr->GetNumberOfTuples() <= pid || arr->GetNumberOfComponents() < 1)
    {
        return false;
    }
    const double r = arr->GetComponent(pid, 0);
    if (!std::isfinite(r) || r <= 0.0)
    {
        return false;
    }
    vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(r);
    vtkSMPropertyHelper(wdg, "Radius").Set(r);
    return true;
}

void AddEdge(std::unordered_map<vtkIdType, std::vector<vtkIdType>>& adj, vtkIdType a, vtkIdType b)
{
    if (a == b)
    {
        return;
    }
    adj[a].push_back(b);
    adj[b].push_back(a);
}

void BuildAdjacency(vtkPolyData* cl, std::unordered_map<vtkIdType, std::vector<vtkIdType>>& adj)
{
    adj.clear();
    if (!cl)
    {
        return;
    }
    vtkCellArray* lines = cl->GetLines();
    if (!lines)
    {
        return;
    }
    vtkIdType npts = 0;
    const vtkIdType* pts = nullptr;
    lines->InitTraversal();
    while (lines->GetNextCell(npts, pts))
    {
        if (npts < 2)
        {
            continue;
        }
        for (vtkIdType i = 0; i + 1 < npts; ++i)
        {
            AddEdge(adj, pts[i], pts[i + 1]);
        }
    }
    for (auto& kv : adj)
    {
        auto& v = kv.second;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
}

} // namespace

//-----------------------------------------------------------------------------
pqSHYXVascularStentCylinderWidget::pqSHYXVascularStentCylinderWidget(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : pqInteractivePropertyWidget(
          "representations", "SHYXImplicitCylinderWidgetRepresentation", smproxy, smgroup, parent)
{
    Ui::CylinderPropertyWidget ui;
    ui.setupUi(this);

    if (vtkSMProperty* center = smgroup->GetProperty("Center"))
    {
        this->addPropertyLink(ui.centerX, "text2", SIGNAL(textChangedAndEditingFinished()), center, 0);
        this->addPropertyLink(ui.centerY, "text2", SIGNAL(textChangedAndEditingFinished()), center, 1);
        this->addPropertyLink(ui.centerZ, "text2", SIGNAL(textChangedAndEditingFinished()), center, 2);
        ui.centerLabel->setText(QCoreApplication::translate("ServerManagerXML", center->GetXMLLabel()));
        const QString tooltip = this->getTooltip(center);
        ui.centerX->setToolTip(tooltip);
        ui.centerY->setToolTip(tooltip);
        ui.centerZ->setToolTip(tooltip);
        ui.centerLabel->setToolTip(tooltip);
    }
    else
    {
        qCritical("Missing required property for function 'Center'.");
    }

    if (vtkSMProperty* axis = smgroup->GetProperty("Axis"))
    {
        this->addPropertyLink(ui.axisX, "text2", SIGNAL(textChangedAndEditingFinished()), axis, 0);
        this->addPropertyLink(ui.axisY, "text2", SIGNAL(textChangedAndEditingFinished()), axis, 1);
        this->addPropertyLink(ui.axisZ, "text2", SIGNAL(textChangedAndEditingFinished()), axis, 2);
        ui.axisLabel->setText(QCoreApplication::translate("ServerManagerXML", axis->GetXMLLabel()));
        const QString tooltip = this->getTooltip(axis);
        ui.axisX->setToolTip(tooltip);
        ui.axisY->setToolTip(tooltip);
        ui.axisZ->setToolTip(tooltip);
        ui.axisLabel->setToolTip(tooltip);
    }
    else
    {
        qCritical("Missing required property for function 'Axis'.");
    }

    if (vtkSMProperty* radius = smgroup->GetProperty("Radius"))
    {
        this->addPropertyLink(ui.radius, "text2", SIGNAL(textChangedAndEditingFinished()), radius);
        ui.radiusLabel->setText(QCoreApplication::translate("ServerManagerXML", radius->GetXMLLabel()));
        const QString tooltip = this->getTooltip(radius);
        ui.radius->setToolTip(tooltip);
        ui.radiusLabel->setToolTip(tooltip);
    }
    else
    {
        qCritical("Missing required property for function 'Radius'.");
    }

    vtkSMProxy* wdgProxy = this->widgetProxy();
    this->WidgetLinks.addPropertyLink(ui.outlineTranslation, "checked", SIGNAL(toggled(bool)), wdgProxy,
        wdgProxy->GetProperty("OutlineTranslation"));
    this->connect(&this->WidgetLinks, SIGNAL(qtWidgetChanged()), SLOT(render()));

    this->connect(ui.show3DWidget, SIGNAL(toggled(bool)), SLOT(setWidgetVisible(bool)));
    ui.show3DWidget->connect(this, SIGNAL(widgetVisibilityToggled(bool)), SLOT(setChecked(bool)));
    this->setWidgetVisible(ui.show3DWidget->isChecked());

    this->AdvancedPropertyWidgets[0] = ui.outlineTranslation;
    this->AdvancedPropertyWidgets[1] = nullptr;

    QObject::disconnect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this, nullptr);
    QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this,
        &pqSHYXVascularStentCylinderWidget::placeWidget);

    if (vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy())
    {
        vtkSMPropertyHelper(wdg, "ConstrainToWidgetBounds").Set(0);
        vtkSMPropertyHelper(wdg, "Tubing").Set(0);
        vtkSMPropertyHelper(wdg, "ScaleEnabled").Set(0);
        vtkSMPropertyHelper(wdg, "OutlineTranslation").Set(1);
        vtkSMPropertyHelper(wdg, "OutsideBounds").Set(1);
        wdg->UpdateVTKObjects();
    }

    QObject::connect(static_cast<pqInteractivePropertyWidgetAbstract*>(this),
        &pqInteractivePropertyWidgetAbstract::endInteraction, this,
        &pqSHYXVascularStentCylinderWidget::onCylinderEndInteraction);

    if (vtkSMProperty* stentLength = smproxy->GetProperty("StentLength"))
    {
        this->StentLengthPropertyCallback = vtkSmartPointer<vtkCallbackCommand>::New();
        this->StentLengthPropertyCallback->SetClientData(this);
        this->StentLengthPropertyCallback->SetCallback(SHYXOnStentLengthPropertyEvent);
        this->StentLengthUncheckedObserverTag = stentLength->AddObserver(
            vtkCommand::UncheckedPropertyModifiedEvent, this->StentLengthPropertyCallback);
        this->StentLengthModifiedObserverTag =
            stentLength->AddObserver(vtkCommand::ModifiedEvent, this->StentLengthPropertyCallback);
    }
}

//-----------------------------------------------------------------------------
pqSHYXVascularStentCylinderWidget::~pqSHYXVascularStentCylinderWidget()
{
    if (vtkSMProxy* px = this->proxy())
    {
        if (vtkSMProperty* p = px->GetProperty("StentLength"))
        {
            if (this->StentLengthUncheckedObserverTag)
            {
                p->RemoveObserver(this->StentLengthUncheckedObserverTag);
                this->StentLengthUncheckedObserverTag = 0;
            }
            if (this->StentLengthModifiedObserverTag)
            {
                p->RemoveObserver(this->StentLengthModifiedObserverTag);
                this->StentLengthModifiedObserverTag = 0;
            }
        }
    }
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::select()
{
    this->syncStentWidgetFromAnchorOnSelect();
    this->Superclass::select();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::placeWidget()
{
    vtkSMNewWidgetRepresentationProxy* w = this->widgetProxy();
    if (!w)
    {
        return;
    }
    vtkSMProxy* filter = this->proxy();
    double center[3], axis[3];
    vtkSMPropertyHelper(w, "Center").Get(center, 3);
    vtkSMPropertyHelper(w, "Axis").Get(axis, 3);
    if (vtkMath::Normalize(axis) < kEps)
    {
        axis[0] = 0.0;
        axis[1] = 0.0;
        axis[2] = 1.0;
    }
    // Bounds must cover the geometry VTK clips against; use the larger of filter vs widget radius
    // so a brief SM desync cannot make the box tighter than the drawn cylinder.
    const double rF = SHYXGetStentRadiusFromFilter(filter);
    const double rW = vtkSMPropertyHelper(w, "Radius").GetAsDouble();
    const double R = std::max(
        (std::isfinite(rF) && rF > 0.0) ? rF : 0.0, (std::isfinite(rW) && rW > 0.0) ? rW : 0.0);
    const double L = SHYXGetStentLengthFromFilter(filter);
    double bds[6];
    pqSHYXVascularStentCylinderWidget::finiteCylinderWorldAABB(center, axis, R, L, bds);
    vtkSMPropertyHelper(w, "WidgetBounds").Set(bds, 6);
    w->UpdateVTKObjects();
    this->syncFiniteLengthHintFromFilter();
}

//-----------------------------------------------------------------------------
vtkPolyData* pqSHYXVascularStentCylinderWidget::centerlineClientPoly() const
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
void pqSHYXVascularStentCylinderWidget::tangentAtCenterlineVertex(
    vtkPolyData* cl, vtkIdType anchor, double axisOut[3])
{
    axisOut[0] = 0.0;
    axisOut[1] = 0.0;
    axisOut[2] = 1.0;
    if (!cl || anchor < 0 || anchor >= cl->GetNumberOfPoints())
    {
        return;
    }
    std::unordered_map<vtkIdType, std::vector<vtkIdType>> adj;
    BuildAdjacency(cl, adj);
    auto it = adj.find(anchor);
    if (it == adj.end() || it->second.empty())
    {
        return;
    }
    std::vector<vtkIdType> neighbors = it->second;
    std::sort(neighbors.begin(), neighbors.end());

    double ach[3];
    cl->GetPoint(anchor, ach);

    auto setChordAxis = [&](vtkIdType na, vtkIdType nb) -> bool {
        double pa[3], pb[3];
        cl->GetPoint(na, pa);
        cl->GetPoint(nb, pb);
        double chord[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
        if (vtkMath::Normalize(chord) < kEps)
        {
            return false;
        }
        axisOut[0] = chord[0];
        axisOut[1] = chord[1];
        axisOut[2] = chord[2];
        return true;
    };

    if (neighbors.size() == 1)
    {
        double nh[3];
        cl->GetPoint(neighbors[0], nh);
        double e[3] = { nh[0] - ach[0], nh[1] - ach[1], nh[2] - ach[2] };
        if (vtkMath::Normalize(e) < kEps)
        {
            return;
        }
        axisOut[0] = e[0];
        axisOut[1] = e[1];
        axisOut[2] = e[2];
        return;
    }

    if (neighbors.size() == 2)
    {
        if (!setChordAxis(neighbors[0], neighbors[1]))
        {
            return;
        }
        return;
    }

    // Degree >= 3: average outgoing unit directions (bifurcation). If nearly opposite pairs cancel,
    // fall back to chord between lowest and highest neighbor id.
    double tsum[3] = { 0.0, 0.0, 0.0 };
    for (vtkIdType nx : neighbors)
    {
        double nh[3];
        cl->GetPoint(nx, nh);
        double e[3] = { nh[0] - ach[0], nh[1] - ach[1], nh[2] - ach[2] };
        if (vtkMath::Normalize(e) > kEps)
        {
            tsum[0] += e[0];
            tsum[1] += e[1];
            tsum[2] += e[2];
        }
    }
    if (vtkMath::Normalize(tsum) >= kEps)
    {
        axisOut[0] = tsum[0];
        axisOut[1] = tsum[1];
        axisOut[2] = tsum[2];
        return;
    }
    if (!setChordAxis(neighbors.front(), neighbors.back()))
    {
        return;
    }
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::finiteCylinderWorldAABB(
    const double center[3], const double axis[3], double radius, double length, double bds[6])
{
    double a[3] = { axis[0], axis[1], axis[2] };
    if (vtkMath::Normalize(a) < kEps)
    {
        a[0] = 0.0;
        a[1] = 0.0;
        a[2] = 1.0;
    }
    double ref[3] = { 0.0, 0.0, 1.0 };
    if (std::fabs(vtkMath::Dot(a, ref)) > 0.9)
    {
        ref[0] = 1.0;
        ref[1] = 0.0;
        ref[2] = 0.0;
    }
    double u[3], v[3];
    vtkMath::Cross(a, ref, u);
    vtkMath::Normalize(u);
    vtkMath::Cross(a, u, v);
    vtkMath::Normalize(v);

    const double halfL = 0.5 * std::max(length, 0.0) * kWidgetBoundsScale;
    const double R = std::max(radius, 0.0) * kWidgetBoundsScale;
    double minC[3] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MAX, VTK_DOUBLE_MAX };
    double maxC[3] = { -VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX };
    for (int sA = -1; sA <= 1; sA += 2)
    {
        for (int su = -1; su <= 1; su += 2)
        {
            for (int sv = -1; sv <= 1; sv += 2)
            {
                double p[3];
                for (int i = 0; i < 3; ++i)
                {
                    p[i] = center[i] + halfL * sA * a[i] + R * su * u[i] + R * sv * v[i];
                    minC[i] = std::min(minC[i], p[i]);
                    maxC[i] = std::max(maxC[i], p[i]);
                }
            }
        }
    }
    // vtkImplicitCylinderRepresentation clips the cylinder mesh to this box; keep a bit of slack
    // beyond the analytic finite cylinder so tubing / tessellation does not sit on the clip plane.
    const double pad = std::max(1e-3, 0.06 * std::max(R, halfL));
    for (int i = 0; i < 3; ++i)
    {
        minC[i] -= pad;
        maxC[i] += pad;
    }
    bds[0] = minC[0];
    bds[1] = maxC[0];
    bds[2] = minC[1];
    bds[3] = maxC[1];
    bds[4] = minC[2];
    bds[5] = maxC[2];
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXVascularStentCylinderWidget::nearestPointId(vtkPoints* pts, const double p[3])
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
void pqSHYXVascularStentCylinderWidget::syncStentWidgetFromAnchorOnSelect()
{
    vtkSMProxy* filter = this->proxy();
    vtkPolyData* cl = this->centerlineClientPoly();
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!filter || !wdg || !cl || cl->GetNumberOfPoints() == 0)
    {
        return;
    }
    const vtkIdType n = cl->GetNumberOfPoints();
    vtkIdType aid = static_cast<vtkIdType>(vtkSMPropertyHelper(filter, "AnchorCenterlinePointId").GetAsInt());
    if (aid < 0 || aid >= n)
    {
        aid = 0;
    }
    double c[3];
    cl->GetPoint(aid, c);
    double ax[3];
    pqSHYXVascularStentCylinderWidget::tangentAtCenterlineVertex(cl, aid, ax);

    vtkSMUncheckedPropertyHelper(filter, "StentWidgetCenter").Set(c, 3);
    vtkSMUncheckedPropertyHelper(filter, "StentWidgetAxis").Set(ax, 3);

    vtkSMPropertyHelper(wdg, "Center").Set(c, 3);
    vtkSMPropertyHelper(wdg, "Axis").Set(ax, 3);
    if (!ApplyStentRadiusFromCenterlinePointArray(cl, filter, wdg, aid))
    {
        vtkSMPropertyHelper(wdg, "Radius").Set(SHYXGetStentRadiusFromFilter(filter));
    }
    wdg->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::onCylinderEndInteraction()
{
    vtkSMProxy* filter = this->proxy();
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!filter || !wdg)
    {
        return;
    }

    // vtkImplicitCylinderRepresentation::EndWidgetInteraction() resets RepresentationState but
    // leaves InteractionState set to the handle that was active; use that to distinguish outline
    // (bounding box) drags from radius / axis / center manipulations (see vtkImplicitCylinderWidget).
    vtkImplicitCylinderWidget* cylW = vtkImplicitCylinderWidget::SafeDownCast(wdg->GetWidget());
    vtkImplicitCylinderRepresentation* cylRep = cylW ? cylW->GetCylinderRepresentation() : nullptr;
    const int interactionState = cylRep ? cylRep->GetInteractionState() : vtkImplicitCylinderRepresentation::Outside;
    const bool snapToCenterline =
        (interactionState == vtkImplicitCylinderRepresentation::MovingOutline);

    if (!snapToCenterline)
    {
        if (interactionState == vtkImplicitCylinderRepresentation::RotatingAxis)
        {
            if (auto* shyx = vtkSHYXImplicitCylinderRepresentation::SafeDownCast(cylRep))
            {
                vtkSMUncheckedPropertyHelper(filter, "StentLength").Set(shyx->GetFiniteStentLength());
            }
        }
        double c[3], ax[3];
        vtkSMPropertyHelper(wdg, "Center").Get(c, 3);
        vtkSMPropertyHelper(wdg, "Axis").Get(ax, 3);
        if (vtkMath::Normalize(ax) < kEps)
        {
            ax[0] = 0.0;
            ax[1] = 0.0;
            ax[2] = 1.0;
        }
        vtkSMUncheckedPropertyHelper(filter, "StentWidgetCenter").Set(c, 3);
        vtkSMUncheckedPropertyHelper(filter, "StentWidgetAxis").Set(ax, 3);
        const double R = vtkSMPropertyHelper(wdg, "Radius").GetAsDouble();
        vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(R);
        wdg->UpdateVTKObjects();
        this->placeWidget();
        this->render();
        return;
    }

    // Do not write StentLength from WidgetBounds: projecting an axis-aligned box onto the cylinder
    // axis gives |ax|dx+|ay|dy+|az|dz, which is strictly greater than the true finite-cylinder length
    // whenever the axis is oblique to world axes, so every snap would inflate StentLength. Length is
    // driven from the filter property (Properties panel); placeWidget() rebuilds bounds from it.

    double centerRelease[3];
    vtkSMPropertyHelper(wdg, "Center").Get(centerRelease, 3);

    vtkIdType snappedId = -1;
    vtkPolyData* clSnap = nullptr;
    if (vtkPolyData* cl = this->centerlineClientPoly())
    {
        if (vtkPoints* pts = cl->GetPoints())
        {
            snappedId = pqSHYXVascularStentCylinderWidget::nearestPointId(pts, centerRelease);
            vtkSMUncheckedPropertyHelper(filter, "AnchorCenterlinePointId").Set(static_cast<int>(snappedId));
            double snapped[3];
            cl->GetPoint(snappedId, snapped);
            vtkSMUncheckedPropertyHelper(filter, "StentWidgetCenter").Set(snapped, 3);
            vtkSMPropertyHelper(wdg, "Center").Set(snapped, 3);
            clSnap = cl;
        }
    }

    double axis[3];
    if (snappedId >= 0 && clSnap)
    {
        pqSHYXVascularStentCylinderWidget::tangentAtCenterlineVertex(clSnap, snappedId, axis);
        if (vtkMath::Normalize(axis) < kEps)
        {
            axis[0] = 0.0;
            axis[1] = 0.0;
            axis[2] = 1.0;
        }
        vtkSMPropertyHelper(wdg, "Axis").Set(axis, 3);
        vtkSMUncheckedPropertyHelper(filter, "StentWidgetAxis").Set(axis, 3);
    }
    else
    {
        vtkSMPropertyHelper(wdg, "Axis").Get(axis, 3);
        if (vtkMath::Normalize(axis) < kEps)
        {
            axis[0] = 0.0;
            axis[1] = 0.0;
            axis[2] = 1.0;
        }
        vtkSMUncheckedPropertyHelper(filter, "StentWidgetAxis").Set(axis, 3);
    }

    if (snappedId >= 0 && clSnap)
    {
        if (!ApplyStentRadiusFromCenterlinePointArray(clSnap, filter, wdg, snappedId))
        {
            const double R = vtkSMPropertyHelper(wdg, "Radius").GetAsDouble();
            vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(R);
        }
    }
    else
    {
        const double R = vtkSMPropertyHelper(wdg, "Radius").GetAsDouble();
        vtkSMUncheckedPropertyHelper(filter, "StentRadius").Set(R);
    }

    wdg->UpdateVTKObjects();
    this->placeWidget();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::onStentLengthPropertyModified()
{
    if (!this->widgetProxy() || !this->proxy())
    {
        return;
    }
    this->placeWidget();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::syncFiniteLengthHintFromFilter()
{
    vtkSMNewWidgetRepresentationProxy* w = this->widgetProxy();
    vtkSMProxy* filter = this->proxy();
    if (!w || !filter)
    {
        return;
    }
    vtkImplicitCylinderWidget* cylW = vtkImplicitCylinderWidget::SafeDownCast(w->GetWidget());
    if (!cylW)
    {
        return;
    }
    auto* shyx = vtkSHYXImplicitCylinderRepresentation::SafeDownCast(cylW->GetCylinderRepresentation());
    if (!shyx)
    {
        return;
    }
    shyx->SetFiniteStentLengthHint(SHYXGetStentLengthFromFilter(filter));
}

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::updateWidget(bool showing_advanced_properties)
{
    for (int i = 0; i < 2; ++i)
    {
        if (this->AdvancedPropertyWidgets[i])
        {
            this->AdvancedPropertyWidgets[i]->setVisible(showing_advanced_properties);
        }
    }
}
