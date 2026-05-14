#include "vtkSHYXVascularStentPlacement.h"

#include <vtkCellArray.h>
#include <vtkCellLocator.h>
#include <vtkDataObject.h>
#include <vtkGenericCell.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkSmartPointer.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXVascularStentPlacement);

namespace
{
constexpr double kEps = 1e-12;

void Sub(double a[3], const double b[3], const double c[3])
{
    a[0] = b[0] - c[0];
    a[1] = b[1] - c[1];
    a[2] = b[2] - c[2];
}

void Copy3(double d[3], const double s[3])
{
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

double Norm(const double v[3])
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double Dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void LinInterp3(double o[3], const double p0[3], const double p1[3], double t)
{
    o[0] = p0[0] + t * (p1[0] - p0[0]);
    o[1] = p0[1] + t * (p1[1] - p0[1]);
    o[2] = p0[2] + t * (p1[2] - p0[2]);
}

using AdjMap = std::unordered_map<vtkIdType, std::vector<vtkIdType>>;

void AddUndirectedEdge(AdjMap& adj, vtkIdType a, vtkIdType b)
{
    if (a == b)
    {
        return;
    }
    adj[a].push_back(b);
    adj[b].push_back(a);
}

void BuildAdjacency(vtkPolyData* cl, AdjMap& adj)
{
    adj.clear();
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
            AddUndirectedEdge(adj, pts[i], pts[i + 1]);
        }
    }
}

void DedupeNeighbors(AdjMap& adj)
{
    for (auto& kv : adj)
    {
        auto& v = kv.second;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
}

vtkIdType PickNextNeighbor(const AdjMap& adj, vtkIdType cur, vtkIdType prev)
{
    auto it = adj.find(cur);
    if (it == adj.end())
    {
        return -1;
    }
    std::vector<vtkIdType> cand = it->second;
    std::sort(cand.begin(), cand.end());
    for (vtkIdType nx : cand)
    {
        if (nx != prev)
        {
            return nx;
        }
    }
    return -1;
}

/**
 * Walk polyline graph from edge (prev -> cur) accumulating at most maxDist geometric length.
 * Appends sampled 3D points along the path starting with the first point reached past |prev|
 * (may be interior to the first edge). Does not include |prev|'s coordinates as first output
 * unless the walk stops inside the first edge (then one interior point).
 */
void WalkGeodesic(vtkPoints* centerPts, const AdjMap& adj, vtkIdType prev, vtkIdType cur,
    double maxDist, std::vector<std::array<double, 3>>& outPts)
{
    outPts.clear();
    if (maxDist <= kEps)
    {
        return;
    }

    double remaining = maxDist;
    double pPrev[3], pCur[3];
    centerPts->GetPoint(prev, pPrev);
    centerPts->GetPoint(cur, pCur);

    while (remaining > kEps)
    {
        double e[3];
        Sub(e, pCur, pPrev);
        const double elen = Norm(e);
        if (elen < kEps)
        {
            vtkIdType nx = PickNextNeighbor(adj, cur, prev);
            if (nx < 0)
            {
                break;
            }
            prev = cur;
            cur = nx;
            centerPts->GetPoint(prev, pPrev);
            centerPts->GetPoint(cur, pCur);
            continue;
        }

        if (remaining + kEps >= elen)
        {
            outPts.push_back({ pCur[0], pCur[1], pCur[2] });
            remaining -= elen;
            vtkIdType nx = PickNextNeighbor(adj, cur, prev);
            if (nx < 0)
            {
                break;
            }
            prev = cur;
            cur = nx;
            centerPts->GetPoint(prev, pPrev);
            centerPts->GetPoint(cur, pCur);
        }
        else
        {
            const double t = remaining / elen;
            std::array<double, 3> q{};
            LinInterp3(q.data(), pPrev, pCur, t);
            outPts.push_back(q);
            break;
        }
    }
}

void PickTwoSideNeighbors(vtkIdType anchor, const AdjMap& adj, vtkIdType& nA, vtkIdType& nB)
{
    nA = -1;
    nB = -1;
    auto it = adj.find(anchor);
    if (it == adj.end())
    {
        return;
    }
    std::vector<vtkIdType> cand = it->second;
    std::sort(cand.begin(), cand.end());
    if (!cand.empty())
    {
        nA = cand[0];
    }
    if (cand.size() >= 2)
    {
        nB = cand[1];
    }
    else if (cand.size() == 1)
    {
        nB = -1;
    }
}

vtkSmartPointer<vtkPolyData> BuildAxisPolyline(const std::vector<std::array<double, 3>>& poly)
{
    auto out = vtkSmartPointer<vtkPolyData>::New();
    if (poly.size() < 2)
    {
        return out;
    }
    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(static_cast<vtkIdType>(poly.size()));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(poly.size()); ++i)
    {
        pts->SetPoint(i, poly[static_cast<size_t>(i)].data());
    }
    vtkNew<vtkCellArray> lines;
    for (vtkIdType i = 0; i + 1 < static_cast<vtkIdType>(poly.size()); ++i)
    {
        vtkIdType c[2] = { i, i + 1 };
        lines->InsertNextCell(2, c);
    }
    out->SetPoints(pts);
    out->SetLines(lines);
    return out;
}

} // namespace

vtkSHYXVascularStentPlacement::vtkSHYXVascularStentPlacement()
{
    this->SetNumberOfInputPorts(2);
    this->SetNumberOfOutputPorts(1);
}

void vtkSHYXVascularStentPlacement::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "AnchorCenterlinePointId: " << this->AnchorCenterlinePointId << "\n";
    os << indent << "StentLength: " << this->StentLength << "\n";
    os << indent << "StentRadius: " << this->StentRadius << "\n";
    os << indent << "StentWidgetCenter: (" << this->StentWidgetCenter[0] << ", " << this->StentWidgetCenter[1] << ", "
       << this->StentWidgetCenter[2] << ")\n";
    os << indent << "StentWidgetAxis: (" << this->StentWidgetAxis[0] << ", " << this->StentWidgetAxis[1] << ", "
       << this->StentWidgetAxis[2] << ")\n";
}

void vtkSHYXVascularStentPlacement::SetCenterlineConnection(vtkAlgorithmOutput* algOutput)
{
    this->SetInputConnection(1, algOutput);
}

int vtkSHYXVascularStentPlacement::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0 || port == 1)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    return 0;
}

int vtkSHYXVascularStentPlacement::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

int vtkSHYXVascularStentPlacement::RequestData(vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* surface = vtkPolyData::GetData(inputVector[0], 0);
    vtkPolyData* centerline = vtkPolyData::GetData(inputVector[1], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
    if (!surface || !output)
    {
        vtkErrorMacro(<< "Invalid surface or output.");
        return 0;
    }
    if (!centerline || centerline->GetNumberOfPoints() == 0 || centerline->GetNumberOfLines() == 0)
    {
        vtkErrorMacro(<< "Centerline (port 1) must be non-empty vtkPolyData with line cells.");
        return 0;
    }

    vtkPoints* cpts = centerline->GetPoints();
    if (!cpts)
    {
        vtkErrorMacro(<< "Centerline has no points.");
        return 0;
    }

    const vtkIdType nCenterPts = centerline->GetNumberOfPoints();
    vtkIdType anchor = this->AnchorCenterlinePointId;
    if (anchor < 0 || anchor >= nCenterPts)
    {
        vtkErrorMacro(<< "AnchorCenterlinePointId " << anchor << " out of range [0, " << (nCenterPts - 1) << "].");
        return 0;
    }

    if (this->StentLength <= kEps || this->StentRadius <= kEps)
    {
        vtkWarningMacro(<< "StentLength or StentRadius too small; passing geometry through unchanged.");
        output->ShallowCopy(surface);
        return 1;
    }

    AdjMap adj;
    BuildAdjacency(centerline, adj);
    DedupeNeighbors(adj);

    if (adj.find(anchor) == adj.end() || adj[anchor].empty())
    {
        vtkErrorMacro(<< "Anchor point " << anchor << " has no incident centerline edges.");
        return 0;
    }

    const double half = 0.5 * this->StentLength;
    double anchorPos[3];
    cpts->GetPoint(anchor, anchorPos);

    vtkIdType nA = -1;
    vtkIdType nB = -1;
    PickTwoSideNeighbors(anchor, adj, nA, nB);

    std::vector<std::array<double, 3>> negBranch;
    std::vector<std::array<double, 3>> posBranch;

    if (nA >= 0)
    {
        WalkGeodesic(cpts, adj, anchor, nA, half, negBranch);
    }
    if (nB >= 0)
    {
        WalkGeodesic(cpts, adj, anchor, nB, half, posBranch);
    }
    else if (nA >= 0)
    {
        // Degree-1 at anchor: put full stent length into the single available direction.
        WalkGeodesic(cpts, adj, anchor, nA, this->StentLength, negBranch);
    }

    std::vector<std::array<double, 3>> poly;
    poly.reserve(negBranch.size() + posBranch.size() + 4);
    for (auto it = negBranch.rbegin(); it != negBranch.rend(); ++it)
    {
        poly.push_back(*it);
    }
    poly.push_back({ anchorPos[0], anchorPos[1], anchorPos[2] });
    for (const auto& p : posBranch)
    {
        poly.push_back(p);
    }

    vtkSmartPointer<vtkPolyData> axisPd = BuildAxisPolyline(poly);
    if (!axisPd || axisPd->GetNumberOfPoints() < 2)
    {
        vtkErrorMacro(<< "Stent axis polyline is degenerate (insufficient centerline length or graph walk).");
        return 0;
    }

    vtkNew<vtkCellLocator> locator;
    locator->SetDataSet(axisPd);
    locator->CacheCellBoundsOn();
    locator->AutomaticOn();
    locator->BuildLocator();

    vtkNew<vtkGenericCell> cell;
    const double influence = std::max(this->StentRadius * 5.0, this->StentLength * 0.25);

    output->CopyStructure(surface);
    output->GetPointData()->PassData(surface->GetPointData());

    vtkNew<vtkPoints> outPts;
    outPts->DeepCopy(surface->GetPoints());

    const vtkIdType nSurfPts = surface->GetNumberOfPoints();
    for (vtkIdType pid = 0; pid < nSurfPts; ++pid)
    {
        double x[3];
        surface->GetPoint(pid, x);

        double closest[3];
        double dist2 = 0.0;
        vtkIdType cellId = -1;
        int subId = -1;
        locator->FindClosestPoint(x, closest, cell, cellId, subId, dist2);

        if (cellId < 0 || dist2 > influence * influence)
        {
            continue;
        }

        vtkIdType nLinePts = 0;
        const vtkIdType* lineIds = nullptr;
        axisPd->GetCellPoints(cellId, nLinePts, lineIds);
        if (nLinePts != 2 || !lineIds)
        {
            continue;
        }
        const vtkIdType i0 = lineIds[0];
        const vtkIdType i1 = lineIds[1];
        double p0[3], p1[3];
        axisPd->GetPoint(i0, p0);
        axisPd->GetPoint(i1, p1);

        double tdir[3];
        Sub(tdir, p1, p0);
        const double tlen = Norm(tdir);
        if (tlen < kEps)
        {
            continue;
        }
        tdir[0] /= tlen;
        tdir[1] /= tlen;
        tdir[2] /= tlen;

        double xc[3];
        Sub(xc, x, closest);
        const double axial = Dot(xc, tdir);
        double radial[3] = { xc[0] - axial * tdir[0], xc[1] - axial * tdir[1], xc[2] - axial * tdir[2] };
        const double rlen = Norm(radial);
        if (rlen < kEps)
        {
            continue;
        }
        radial[0] /= rlen;
        radial[1] /= rlen;
        radial[2] /= rlen;

        double newx[3];
        newx[0] = closest[0] + this->StentRadius * radial[0];
        newx[1] = closest[1] + this->StentRadius * radial[1];
        newx[2] = closest[2] + this->StentRadius * radial[2];
        outPts->SetPoint(pid, newx);
    }

    output->SetPoints(outPts);
    return 1;
}

VTK_ABI_NAMESPACE_END
