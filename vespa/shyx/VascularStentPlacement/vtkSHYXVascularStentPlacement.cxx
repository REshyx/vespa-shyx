#include "vtkSHYXVascularStentPlacement.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellLocator.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkGenericCell.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSmartPointer.h>

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
constexpr double kTolDist = 1e-7;

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

void ScaleAdd(double y[3], const double x[3], double s, const double d[3])
{
    y[0] = x[0] + s * d[0];
    y[1] = x[1] + s * d[1];
    y[2] = x[2] + s * d[2];
}

double Norm(const double v[3])
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double Dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double SmoothStep01(double t)
{
    if (t <= 0.0)
    {
        return 0.0;
    }
    if (t >= 1.0)
    {
        return 1.0;
    }
    return t * t * (3.0 - 2.0 * t);
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

void WalkGeodesic(vtkPoints* centerPts, const AdjMap& adj, vtkIdType prev, vtkIdType cur, double maxDist,
    std::vector<std::array<double, 3>>& outPts)
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

void DensifyAxisPolyline(const std::vector<std::array<double, 3>>& in, double maxSpacing,
    std::vector<std::array<double, 3>>& out)
{
    out.clear();
    if (in.size() < 2)
    {
        out = in;
        return;
    }
    if (maxSpacing <= kEps)
    {
        out = in;
        return;
    }

    auto pushIfDistinct = [&out](const std::array<double, 3>& q) {
        if (out.empty())
        {
            out.push_back(q);
            return;
        }
        double d[3];
        Sub(d, q.data(), out.back().data());
        if (Norm(d) > kEps)
        {
            out.push_back(q);
        }
    };

    for (size_t i = 0; i + 1 < in.size(); ++i)
    {
        const double* p0 = in[i].data();
        const double* p1 = in[i + 1].data();
        double e[3];
        Sub(e, p1, p0);
        const double L = Norm(e);
        if (L < kEps)
        {
            continue;
        }
        int nIntervals = static_cast<int>(std::ceil(L / maxSpacing));
        nIntervals = std::max(1, nIntervals);
        for (int k = 0; k <= nIntervals; ++k)
        {
            const double t = static_cast<double>(k) / static_cast<double>(nIntervals);
            std::array<double, 3> q{};
            LinInterp3(q.data(), p0, p1, t);
            pushIfDistinct(q);
        }
    }

    if (out.size() < 2)
    {
        out = in;
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

double DistanceToAxis(vtkCellLocator* locator, vtkGenericCell* cell, const double p[3], double closestOut[3],
    vtkIdType& cellIdOut)
{
    double dist2 = 0.0;
    int subId = -1;
    cellIdOut = -1;
    locator->FindClosestPoint(p, closestOut, cell, cellIdOut, subId, dist2);
    if (cellIdOut < 0)
    {
        return VTK_DOUBLE_MAX;
    }
    return std::sqrt(dist2);
}

void SegmentTangentFromCell(vtkPolyData* axisPd, vtkIdType cellId, double tdir[3])
{
    vtkIdType nLinePts = 0;
    const vtkIdType* lineIds = nullptr;
    axisPd->GetCellPoints(cellId, nLinePts, lineIds);
    tdir[0] = 1.0;
    tdir[1] = 0.0;
    tdir[2] = 0.0;
    if (nLinePts != 2 || !lineIds)
    {
        return;
    }
    double p0[3], p1[3];
    axisPd->GetPoint(lineIds[0], p0);
    axisPd->GetPoint(lineIds[1], p1);
    Sub(tdir, p1, p0);
    const double tlen = Norm(tdir);
    if (tlen < kEps)
    {
        return;
    }
    tdir[0] /= tlen;
    tdir[1] /= tlen;
    tdir[2] /= tlen;
}

void RadialPerpendicular(const double x[3], const double closest[3], const double tdir[3], double radial[3],
    double& rlenOut)
{
    double xc[3];
    Sub(xc, x, closest);
    const double axial = Dot(xc, tdir);
    radial[0] = xc[0] - axial * tdir[0];
    radial[1] = xc[1] - axial * tdir[1];
    radial[2] = xc[2] - axial * tdir[2];
    rlenOut = Norm(radial);
}

/** Move along ±n until distance to axis is R (bisection). n must be unit. */
bool SolveAlongNormalForRadius(vtkCellLocator* locator, vtkGenericCell* cell, vtkPolyData* axisPd,
    const double x[3], const double n[3], double R, const double closest0[3], vtkIdType cellId0, double yOut[3])
{
    double tdir0[3];
    SegmentTangentFromCell(axisPd, cellId0, tdir0);
    double rh[3];
    double rlen = 0.0;
    RadialPerpendicular(x, closest0, tdir0, rh, rlen);
    if (rlen < kEps)
    {
        return false;
    }
    double ndir[3];
    Copy3(ndir, n);
    if (Dot(ndir, rh) < 0.0)
    {
        ndir[0] = -n[0];
        ndir[1] = -n[1];
        ndir[2] = -n[2];
    }

    auto distMinusR = [&](double t) -> double {
        double p[3];
        ScaleAdd(p, x, t, ndir);
        double ctmp[3];
        vtkIdType cid = -1;
        return DistanceToAxis(locator, cell, p, ctmp, cid) - R;
    };

    const double f0 = distMinusR(0.0);
    if (std::fabs(f0) < kTolDist * std::max(1.0, R))
    {
        Copy3(yOut, x);
        return true;
    }

    double tHi = std::max(8.0 * R, 1e-6);
    double fHi = distMinusR(tHi);
    int guard = 0;
    while (f0 * fHi > 0.0 && guard < 28)
    {
        tHi *= 1.6;
        fHi = distMinusR(tHi);
        ++guard;
    }
    if (f0 * fHi > 0.0)
    {
        return false;
    }

    double lo = 0.0;
    double hi = tHi;
    double flo = f0;
    double fhi = fHi;
    for (int it = 0; it < 48; ++it)
    {
        const double mid = 0.5 * (lo + hi);
        const double fm = distMinusR(mid);
        if (std::fabs(fm) < kTolDist * std::max(1.0, R))
        {
            ScaleAdd(yOut, x, mid, ndir);
            return true;
        }
        if (flo * fm <= 0.0)
        {
            hi = mid;
            fhi = fm;
        }
        else
        {
            lo = mid;
            flo = fm;
        }
    }
    const double tSol = 0.5 * (lo + hi);
    ScaleAdd(yOut, x, tSol, ndir);
    return true;
}

void FallbackCylinderPoint(vtkPolyData* axisPd, vtkCellLocator* locator, vtkGenericCell* cell, const double x[3],
    vtkIdType cellId, double R, double yOut[3])
{
    vtkIdType nLinePts = 0;
    const vtkIdType* lineIds = nullptr;
    axisPd->GetCellPoints(cellId, nLinePts, lineIds);
    if (nLinePts != 2 || !lineIds)
    {
        Copy3(yOut, x);
        return;
    }
    double p0[3], p1[3];
    axisPd->GetPoint(lineIds[0], p0);
    axisPd->GetPoint(lineIds[1], p1);
    double tdir[3];
    Sub(tdir, p1, p0);
    const double tlen = Norm(tdir);
    if (tlen < kEps)
    {
        Copy3(yOut, x);
        return;
    }
    tdir[0] /= tlen;
    tdir[1] /= tlen;
    tdir[2] /= tlen;
    double closest[3];
    double dist2 = 0.0;
    int subId = -1;
    locator->FindClosestPoint(x, closest, cell, cellId, subId, dist2);
    double rlen = 0.0;
    double radial[3];
    RadialPerpendicular(x, closest, tdir, radial, rlen);
    if (rlen < kEps)
    {
        Copy3(yOut, x);
        return;
    }
    radial[0] /= rlen;
    radial[1] /= rlen;
    radial[2] /= rlen;
    yOut[0] = closest[0] + R * radial[0];
    yOut[1] = closest[1] + R * radial[1];
    yOut[2] = closest[2] + R * radial[2];
}

} // namespace

vtkSHYXVascularStentPlacement::vtkSHYXVascularStentPlacement()
{
    this->SetNumberOfInputPorts(2);
    this->SetNumberOfOutputPorts(1);
    this->CenterlineRadiusArrayName = nullptr;
    this->SetCenterlineRadiusArrayName("MaximumInscribedSphereRadius");
}

vtkSHYXVascularStentPlacement::~vtkSHYXVascularStentPlacement()
{
    this->SetCenterlineRadiusArrayName(nullptr);
}

void vtkSHYXVascularStentPlacement::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "AnchorCenterlinePointId: " << this->AnchorCenterlinePointId << "\n";
    os << indent << "StentLength: " << this->StentLength << "\n";
    os << indent << "StentRadius: " << this->StentRadius << "\n";
    os << indent << "StentAxisSampleSpacing: " << this->StentAxisSampleSpacing << "\n";
    os << indent << "CapSideSmoothInfluenceRange: " << this->CapSideSmoothInfluenceRange << "\n";
    os << indent << "PreferInputPointNormals: " << (this->PreferInputPointNormals ? "on" : "off") << "\n";
    os << indent << "StentWidgetCenter: (" << this->StentWidgetCenter[0] << ", " << this->StentWidgetCenter[1] << ", "
       << this->StentWidgetCenter[2] << ")\n";
    os << indent << "StentWidgetAxis: (" << this->StentWidgetAxis[0] << ", " << this->StentWidgetAxis[1] << ", "
       << this->StentWidgetAxis[2] << ")\n";
    os << indent << "CenterlineRadiusArrayName: "
       << (this->CenterlineRadiusArrayName ? this->CenterlineRadiusArrayName : "") << "\n";
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

    if (this->StentAxisSampleSpacing > kEps)
    {
        std::vector<std::array<double, 3>> densePoly;
        DensifyAxisPolyline(poly, this->StentAxisSampleSpacing, densePoly);
        if (densePoly.size() >= 2)
        {
            poly.swap(densePoly);
        }
    }

    vtkSmartPointer<vtkPolyData> axisPd = BuildAxisPolyline(poly);
    if (!axisPd || axisPd->GetNumberOfPoints() < 2)
    {
        vtkErrorMacro(<< "Stent axis polyline is degenerate (insufficient centerline length or graph walk).");
        return 0;
    }

    double capP0[3] = { 0.0, 0.0, 0.0 };
    double capPlast[3] = { 0.0, 0.0, 0.0 };
    double capTStart[3] = { 1.0, 0.0, 0.0 };
    double capTEnd[3] = { 1.0, 0.0, 0.0 };
    bool axisEndsValid = false;
    {
        vtkPoints* axPts = axisPd->GetPoints();
        const vtkIdType nAx = axisPd->GetNumberOfPoints();
        if (axPts && nAx >= 2)
        {
            double pSecond[3], pPrev[3];
            axPts->GetPoint(0, capP0);
            axPts->GetPoint(nAx - 1, capPlast);
            axPts->GetPoint(1, pSecond);
            axPts->GetPoint(nAx - 2, pPrev);
            Sub(capTStart, pSecond, capP0);
            Sub(capTEnd, capPlast, pPrev);
            const double lenS = Norm(capTStart);
            const double lenE = Norm(capTEnd);
            if (lenS >= kEps && lenE >= kEps)
            {
                capTStart[0] /= lenS;
                capTStart[1] /= lenS;
                capTStart[2] /= lenS;
                capTEnd[0] /= lenE;
                capTEnd[1] /= lenE;
                capTEnd[2] /= lenE;
                axisEndsValid = true;
            }
        }
    }
    if (!axisEndsValid)
    {
        vtkErrorMacro(<< "Could not build axis end tangents for flat stent slab.");
        return 0;
    }

    vtkNew<vtkPolyDataNormals> normalsFilter;
    vtkSmartPointer<vtkDataArray> pointNormals;
    if (this->PreferInputPointNormals && surface->GetPointData()->GetNormals())
    {
        vtkDataArray* src = surface->GetPointData()->GetNormals();
        pointNormals.TakeReference(src->NewInstance());
        pointNormals->DeepCopy(src);
    }
    else
    {
        normalsFilter->SetInputData(surface);
        normalsFilter->SplittingOff();
        normalsFilter->ConsistencyOn();
        normalsFilter->AutoOrientNormalsOn();
        normalsFilter->ComputePointNormalsOn();
        normalsFilter->ComputeCellNormalsOff();
        normalsFilter->Update();
        vtkPolyData* nout = normalsFilter->GetOutput();
        pointNormals = nout->GetPointData()->GetNormals();
        if (!pointNormals)
        {
            vtkErrorMacro(<< "vtkPolyDataNormals failed to produce point normals.");
            return 0;
        }
    }

    vtkNew<vtkCellLocator> locator;
    locator->SetDataSet(axisPd);
    locator->CacheCellBoundsOn();
    locator->AutomaticOn();
    locator->BuildLocator();

    vtkNew<vtkGenericCell> cell;
    const double R = this->StentRadius;
    const double R2 = R * R;
    const double W = this->CapSideSmoothInfluenceRange;

    output->CopyStructure(surface);
    output->GetPointData()->PassData(surface->GetPointData());
    output->GetCellData()->PassData(surface->GetCellData());

    vtkNew<vtkPoints> outPts;
    outPts->DeepCopy(surface->GetPoints());

    const vtkIdType nSurfPts = surface->GetNumberOfPoints();
    if (pointNormals->GetNumberOfTuples() != nSurfPts)
    {
        vtkErrorMacro(<< "Point normal count does not match surface points.");
        return 0;
    }

    for (vtkIdType pid = 0; pid < nSurfPts; ++pid)
    {
        double x[3];
        surface->GetPoint(pid, x);
        double n[3];
        pointNormals->GetTuple(pid, n);
        const double nlen = Norm(n);
        if (nlen < kEps)
        {
            continue;
        }
        n[0] /= nlen;
        n[1] /= nlen;
        n[2] /= nlen;

        double closest[3];
        double dist2 = 0.0;
        vtkIdType cellId = -1;
        int subId = -1;
        locator->FindClosestPoint(x, closest, cell, cellId, subId, dist2);
        if (cellId < 0 || dist2 > R2)
        {
            continue;
        }

        double v0[3];
        Sub(v0, x, capP0);
        const double s0 = Dot(v0, capTStart);
        double vL[3];
        Sub(vL, x, capPlast);
        const double sE = Dot(vL, capTEnd);

        const bool inStrict = (dist2 <= R2) && (s0 >= 0.0) && (sE <= 0.0);
        const bool inStartBand =
            (dist2 <= R2) && (s0 < 0.0) && (W > kEps) && (s0 > -W) && (sE <= 0.0);
        const bool inEndBand = (dist2 <= R2) && (sE > 0.0) && (W > kEps) && (sE < W) && (s0 >= 0.0);

        if (!inStrict && !inStartBand && !inEndBand)
        {
            continue;
        }

        double yTarget[3];
        if (!SolveAlongNormalForRadius(locator, cell, axisPd, x, n, R, closest, cellId, yTarget))
        {
            FallbackCylinderPoint(axisPd, locator, cell, x, cellId, R, yTarget);
        }

        double alpha = 1.0;
        if (!inStrict)
        {
            alpha = 1.0;
            if (inStartBand)
            {
                alpha *= SmoothStep01((s0 + W) / W);
            }
            if (inEndBand)
            {
                alpha *= SmoothStep01((W - sE) / W);
            }
        }

        double newx[3];
        newx[0] = x[0] + alpha * (yTarget[0] - x[0]);
        newx[1] = x[1] + alpha * (yTarget[1] - x[1]);
        newx[2] = x[2] + alpha * (yTarget[2] - x[2]);
        outPts->SetPoint(pid, newx);
    }

    output->SetPoints(outPts);
    return 1;
}

VTK_ABI_NAMESPACE_END
