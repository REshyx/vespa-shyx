#include "pqSHYXVascularStentCylinderWidget.h"

#include "pqActiveObjects.h"
#include "pqInteractivePropertyWidgetAbstract.h"

#include "vtkAlgorithm.h"
#include "vtkCellArray.h"
#include "vtkMath.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSourceProxy.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace
{
constexpr double kEps = 1e-12;

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
    : pqCylinderPropertyWidget(smproxy, smgroup, parent)
{
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
}

//-----------------------------------------------------------------------------
pqSHYXVascularStentCylinderWidget::~pqSHYXVascularStentCylinderWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXVascularStentCylinderWidget::select()
{
    this->syncStentWidgetFromAnchorOnSelect();
    this->pqCylinderPropertyWidget::select();
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
    const double R = vtkSMPropertyHelper(filter, "StentRadius").GetAsDouble();
    const double L = vtkSMPropertyHelper(filter, "StentLength").GetAsDouble();
    double bds[6];
    pqSHYXVascularStentCylinderWidget::finiteCylinderWorldAABB(center, axis, R, L, bds);
    vtkSMPropertyHelper(w, "WidgetBounds").Set(bds, 6);
    w->UpdateVTKObjects();
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
    double ach[3];
    cl->GetPoint(anchor, ach);
    double tsum[3] = { 0.0, 0.0, 0.0 };
    for (vtkIdType nx : it->second)
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
    if (vtkMath::Normalize(tsum) < kEps)
    {
        return;
    }
    axisOut[0] = tsum[0];
    axisOut[1] = tsum[1];
    axisOut[2] = tsum[2];
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

    const double halfL = 0.5 * std::max(length, 0.0);
    const double R = std::max(radius, 0.0);
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
    const double pad = std::max(1e-3, 0.02 * std::max(R, halfL));
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

    vtkSMPropertyHelper(filter, "StentWidgetCenter").Set(c, 3);
    vtkSMPropertyHelper(filter, "StentWidgetAxis").Set(ax, 3);
    filter->UpdateVTKObjects();

    vtkSMPropertyHelper(wdg, "Center").Set(c, 3);
    vtkSMPropertyHelper(wdg, "Axis").Set(ax, 3);
    vtkSMPropertyHelper(wdg, "Radius").Set(vtkSMPropertyHelper(filter, "StentRadius").GetAsDouble());
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

    double center[3];
    vtkSMPropertyHelper(wdg, "Center").Get(center, 3);

    if (vtkPolyData* cl = this->centerlineClientPoly())
    {
        if (vtkPoints* pts = cl->GetPoints())
        {
            const vtkIdType nid = pqSHYXVascularStentCylinderWidget::nearestPointId(pts, center);
            vtkSMPropertyHelper(filter, "AnchorCenterlinePointId").Set(static_cast<int>(nid));
            double snapped[3];
            cl->GetPoint(nid, snapped);
            vtkSMPropertyHelper(filter, "StentWidgetCenter").Set(snapped, 3);
            vtkSMPropertyHelper(wdg, "Center").Set(snapped, 3);
        }
    }

    double axis[3];
    vtkSMPropertyHelper(wdg, "Axis").Get(axis, 3);
    if (vtkMath::Normalize(axis) < kEps)
    {
        axis[0] = 0.0;
        axis[1] = 0.0;
        axis[2] = 1.0;
    }
    vtkSMPropertyHelper(filter, "StentWidgetAxis").Set(axis, 3);

    const double R = vtkSMPropertyHelper(wdg, "Radius").GetAsDouble();
    vtkSMPropertyHelper(filter, "StentRadius").Set(R);

    filter->UpdateVTKObjects();
    wdg->UpdateVTKObjects();
    this->placeWidget();
    this->render();
}
