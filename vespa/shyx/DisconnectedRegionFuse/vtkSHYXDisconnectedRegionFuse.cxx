#include "vtkSHYXDisconnectedRegionFuse.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkStaticPointLocator.h>  // faster than vtkPointLocator
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#ifdef VESPA_USE_SMP
#include <vtkSMPThreadLocal.h>
#include <vtkSMPTools.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDisconnectedRegionFuse);

namespace
{
constexpr const char* kFuseMarkArrayName = "SHYXFuseMark";
enum FuseMarkValue
{
    FUSE_MARK_UNCHANGED = 0,
    FUSE_MARK_REMAPPED = 1,
    FUSE_MARK_NEW = 2
};

// Union-Find for vertex equivalence class merging
class UnionFind
{
public:
    explicit UnionFind(vtkIdType n) : parent(n), rank(n, 0)
    {
        for (vtkIdType i = 0; i < n; ++i) parent[i] = i;
    }
    vtkIdType find(vtkIdType x)
    {
        if (parent[x] != x)
            parent[x] = find(parent[x]);
        return parent[x];
    }
    void unite(vtkIdType x, vtkIdType y)
    {
        vtkIdType px = find(x), py = find(y);
        if (px == py) return;
        if (rank[px] < rank[py]) std::swap(px, py);
        parent[py] = px;
        if (rank[px] == rank[py]) ++rank[px];
    }
private:
    std::vector<vtkIdType> parent;
    std::vector<int> rank;
};

void UniteAlongCells(
    vtkCellArray* cells, vtkIdType ptOffset, UnionFind& conn, bool closeLoop, vtkIdList* cellPts)
{
    if (!cells)
    {
        return;
    }
    for (cells->InitTraversal(); cells->GetNextCell(cellPts);)
    {
        const vtkIdType n = cellPts->GetNumberOfIds();
        if (n < 2)
        {
            continue;
        }
        for (vtkIdType k = 1; k < n; ++k)
        {
            conn.unite(ptOffset + cellPts->GetId(k - 1), ptOffset + cellPts->GetId(k));
        }
        if (closeLoop && n >= 3)
        {
            conn.unite(ptOffset + cellPts->GetId(n - 1), ptOffset + cellPts->GetId(0));
        }
    }
}

void UniteVertCells(vtkCellArray* cells, vtkIdType ptOffset, UnionFind& conn, vtkIdList* cellPts)
{
    if (!cells)
    {
        return;
    }
    for (cells->InitTraversal(); cells->GetNextCell(cellPts);)
    {
        const vtkIdType n = cellPts->GetNumberOfIds();
        for (vtkIdType k = 1; k < n; ++k)
        {
            conn.unite(ptOffset + cellPts->GetId(0), ptOffset + cellPts->GetId(k));
        }
    }
}

void RemapCellArray(
    vtkCellArray* inCells,
    vtkCellArray* outCells,
    vtkIdList* cellPts,
    vtkIdType ptOffset,
    UnionFind& uf,
    const std::unordered_map<vtkIdType, vtkIdType>& rootToNewId,
    vtkIdType minKeep,
    int inp,
    vtkIdType cellIdOffset,
    const std::vector<char>& pointFused,
    std::vector<std::pair<int, vtkIdType>>& keptCellSource,
    std::vector<int>& cellMarks)
{
    if (!inCells)
    {
        return;
    }
    vtkIdType localCellId = 0;
    for (inCells->InitTraversal(); inCells->GetNextCell(cellPts); ++localCellId)
    {
        std::vector<vtkIdType> newIds;
        newIds.reserve(static_cast<size_t>(cellPts->GetNumberOfIds()));
        int mark = FUSE_MARK_UNCHANGED;
        for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
        {
            const vtkIdType oldGlobal = ptOffset + cellPts->GetId(k);
            if (oldGlobal >= 0 && static_cast<size_t>(oldGlobal) < pointFused.size() &&
                pointFused[static_cast<size_t>(oldGlobal)])
            {
                mark = FUSE_MARK_REMAPPED;
            }
            const vtkIdType root = uf.find(oldGlobal);
            const auto found = rootToNewId.find(root);
            if (found == rootToNewId.end())
            {
                continue;
            }
            const vtkIdType nid = found->second;
            if (newIds.empty() || newIds.back() != nid)
            {
                newIds.push_back(nid);
            }
        }
        if (newIds.size() > 1 && newIds.front() == newIds.back())
        {
            newIds.pop_back();
        }
        if (static_cast<vtkIdType>(newIds.size()) >= minKeep)
        {
            outCells->InsertNextCell(static_cast<vtkIdType>(newIds.size()), newIds.data());
            keptCellSource.emplace_back(inp, cellIdOffset + localCellId);
            cellMarks.push_back(mark);
        }
    }
}

using EdgeKey = std::pair<vtkIdType, vtkIdType>;

EdgeKey MakeEdge(vtkIdType a, vtkIdType b)
{
    return a < b ? EdgeKey{a, b} : EdgeKey{b, a};
}

double Dist2(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

double Clamp01(double x)
{
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

double Dot3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool Normalize3(std::array<double, 3>& v)
{
    const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 1e-18)
    {
        return false;
    }
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
    return true;
}

struct PointHint
{
    int lineDegree = 0;
    std::array<double, 3> outward = {0, 0, 0};
    bool hasOutward = false;
    std::array<double, 3> normal = {0, 0, 0};
    bool hasNormal = false;
};

void BuildPointHints(
    const std::vector<vtkPolyData*>& pdIn,
    const std::vector<vtkIdType>& ptOffset,
    int nInputs,
    vtkIdType nPoints,
    const std::vector<std::array<double, 3>>& globalPos,
    std::vector<PointHint>& hints)
{
    hints.assign(static_cast<size_t>(nPoints), PointHint{});
    std::vector<std::vector<vtkIdType>> lineNbr(static_cast<size_t>(nPoints));
    vtkSmartPointer<vtkIdList> walk = vtkSmartPointer<vtkIdList>::New();

    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd)
        {
            continue;
        }
        const vtkIdType off = ptOffset[static_cast<size_t>(inp)];
        if (pd->GetLines())
        {
            for (pd->GetLines()->InitTraversal(); pd->GetLines()->GetNextCell(walk);)
            {
                const vtkIdType n = walk->GetNumberOfIds();
                for (vtkIdType k = 1; k < n; ++k)
                {
                    const vtkIdType a = off + walk->GetId(k - 1);
                    const vtkIdType b = off + walk->GetId(k);
                    if (a == b)
                    {
                        continue;
                    }
                    lineNbr[static_cast<size_t>(a)].push_back(b);
                    lineNbr[static_cast<size_t>(b)].push_back(a);
                }
            }
        }
        if (pd->GetPolys())
        {
            for (pd->GetPolys()->InitTraversal(); pd->GetPolys()->GetNextCell(walk);)
            {
                const vtkIdType n = walk->GetNumberOfIds();
                if (n < 3)
                {
                    continue;
                }
                std::array<double, 3> nrm = {0, 0, 0};
                for (vtkIdType k = 0; k < n; ++k)
                {
                    const auto& cur = globalPos[static_cast<size_t>(off + walk->GetId(k))];
                    const auto& nxt = globalPos[static_cast<size_t>(off + walk->GetId((k + 1) % n))];
                    nrm[0] += (cur[1] - nxt[1]) * (cur[2] + nxt[2]);
                    nrm[1] += (cur[2] - nxt[2]) * (cur[0] + nxt[0]);
                    nrm[2] += (cur[0] - nxt[0]) * (cur[1] + nxt[1]);
                }
                const double area = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
                if (area < 1e-18 || !Normalize3(nrm))
                {
                    continue;
                }
                for (vtkIdType k = 0; k < n; ++k)
                {
                    PointHint& h = hints[static_cast<size_t>(off + walk->GetId(k))];
                    h.normal[0] += nrm[0] * area;
                    h.normal[1] += nrm[1] * area;
                    h.normal[2] += nrm[2] * area;
                    h.hasNormal = true;
                }
            }
        }
    }

    for (vtkIdType i = 0; i < nPoints; ++i)
    {
        PointHint& h = hints[static_cast<size_t>(i)];
        auto& nbrs = lineNbr[static_cast<size_t>(i)];
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
        h.lineDegree = static_cast<int>(nbrs.size());
        if (h.lineDegree == 1)
        {
            const auto& p = globalPos[static_cast<size_t>(i)];
            const auto& q = globalPos[static_cast<size_t>(nbrs[0])];
            h.outward = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
            h.hasOutward = Normalize3(h.outward);
        }
        if (h.hasNormal && !Normalize3(h.normal))
        {
            h.hasNormal = false;
        }
    }
}

double PointComplianceToward(const PointHint& h, const std::array<double, 3>& dirFromThis)
{
    double c = 0.0;
    int n = 0;
    if (h.lineDegree > 0)
    {
        double lineC = 0.0;
        if (h.hasOutward)
        {
            lineC = Clamp01(Dot3(dirFromThis, h.outward));
        }
        const double endness = (h.lineDegree == 1) ? 1.0 : 0.15;
        c += lineC * endness;
        ++n;
    }
    if (h.hasNormal)
    {
        c += 1.0 - std::abs(Dot3(dirFromThis, h.normal));
        ++n;
    }
    if (n == 0)
    {
        return 1.0;
    }
    return c / static_cast<double>(n);
}

double PairCompliance(vtkIdType i, vtkIdType j, const std::vector<PointHint>& hints,
    const std::vector<std::array<double, 3>>& globalPos)
{
    std::array<double, 3> dir = {globalPos[static_cast<size_t>(j)][0] - globalPos[static_cast<size_t>(i)][0],
        globalPos[static_cast<size_t>(j)][1] - globalPos[static_cast<size_t>(i)][1],
        globalPos[static_cast<size_t>(j)][2] - globalPos[static_cast<size_t>(i)][2]};
    if (!Normalize3(dir))
    {
        return 0.0;
    }
    const std::array<double, 3> back = {-dir[0], -dir[1], -dir[2]};
    return 0.5 * (PointComplianceToward(hints[static_cast<size_t>(i)], dir) +
        PointComplianceToward(hints[static_cast<size_t>(j)], back));
}

double PairCost(vtkIdType i, vtkIdType j, double dist, double weight, double threshold,
    const std::vector<PointHint>& hints, const std::vector<std::array<double, 3>>& globalPos)
{
    const double w = Clamp01(weight);
    if (w <= 0.0 || hints.empty())
    {
        return dist;
    }
    const double compliance = PairCompliance(i, j, hints, globalPos);
    return (1.0 - w) * dist + w * threshold * (1.0 - compliance);
}

double NeighborCompliance(vtkIdType from, vtkIdType other, vtkIdType toward,
    const std::vector<PointHint>& hints, const std::vector<std::array<double, 3>>& globalPos)
{
    std::array<double, 3> toTarget = {
        globalPos[static_cast<size_t>(toward)][0] - globalPos[static_cast<size_t>(from)][0],
        globalPos[static_cast<size_t>(toward)][1] - globalPos[static_cast<size_t>(from)][1],
        globalPos[static_cast<size_t>(toward)][2] - globalPos[static_cast<size_t>(from)][2]};
    std::array<double, 3> alongEdge = {
        globalPos[static_cast<size_t>(other)][0] - globalPos[static_cast<size_t>(from)][0],
        globalPos[static_cast<size_t>(other)][1] - globalPos[static_cast<size_t>(from)][1],
        globalPos[static_cast<size_t>(other)][2] - globalPos[static_cast<size_t>(from)][2]};
    if (!Normalize3(alongEdge))
    {
        return 0.0;
    }
    const PointHint& h = hints[static_cast<size_t>(from)];
    const PointHint& ho = hints[static_cast<size_t>(other)];
    if (h.hasNormal)
    {
        const double dn = Dot3(toTarget, h.normal);
        std::array<double, 3> tang = {toTarget[0] - h.normal[0] * dn, toTarget[1] - h.normal[1] * dn,
            toTarget[2] - h.normal[2] * dn};
        if (Normalize3(tang))
        {
            return Clamp01(Dot3(alongEdge, tang));
        }
    }
    if (h.lineDegree > 0)
    {
        const double endness = (ho.lineDegree == 1) ? 1.0 : 0.15;
        if (Normalize3(toTarget))
        {
            return Clamp01(0.5 * endness + 0.5 * Clamp01(Dot3(alongEdge, toTarget)));
        }
        return endness;
    }
    if (Normalize3(toTarget))
    {
        return Clamp01(Dot3(alongEdge, toTarget));
    }
    return 0.0;
}

void CountPolyEdgeUses(vtkCellArray* polys, vtkIdList* cellPts, std::map<EdgeKey, int>& uses)
{
    if (!polys)
    {
        return;
    }
    for (polys->InitTraversal(); polys->GetNextCell(cellPts);)
    {
        const vtkIdType n = cellPts->GetNumberOfIds();
        if (n < 3)
        {
            continue;
        }
        for (vtkIdType k = 0; k < n; ++k)
        {
            ++uses[MakeEdge(cellPts->GetId(k), cellPts->GetId((k + 1) % n))];
        }
    }
}

void AddAdj(std::vector<std::vector<vtkIdType>>& adj, vtkIdType a, vtkIdType b)
{
    if (a == b)
    {
        return;
    }
    adj[static_cast<size_t>(a)].push_back(b);
    adj[static_cast<size_t>(b)].push_back(a);
}

vtkIdType BestNeighborToward(
    const std::vector<vtkIdType>& nbrs,
    vtkIdType from,
    vtkIdType toward,
    const std::vector<std::array<double, 3>>& globalPos,
    const std::vector<PointHint>& hints,
    double weight,
    double threshold)
{
    vtkIdType best = -1;
    double bestCost = std::numeric_limits<double>::infinity();
    const auto& t = globalPos[static_cast<size_t>(toward)];
    const double w = Clamp01(weight);
    for (vtkIdType other : nbrs)
    {
        if (other == from || other == toward)
        {
            continue;
        }
        const double d = std::sqrt(Dist2(globalPos[static_cast<size_t>(other)], t));
        double cost = d;
        if (w > 0.0 && !hints.empty())
        {
            const double c = NeighborCompliance(from, other, toward, hints, globalPos);
            cost = (1.0 - w) * d + w * threshold * (1.0 - c);
        }
        if (cost < bestCost)
        {
            bestCost = cost;
            best = other;
        }
    }
    return best;
}

void InsertLineCell(vtkCellArray* lines, vtkIdType a, vtkIdType b,
    std::vector<std::pair<int, vtkIdType>>& keptCellSource, std::vector<int>& cellMarks)
{
    if (a == b)
    {
        return;
    }
    const vtkIdType ids[2] = {a, b};
    lines->InsertNextCell(2, ids);
    keptCellSource.emplace_back(-1, 0);
    cellMarks.push_back(FUSE_MARK_NEW);
}

void InsertTriangleCell(vtkCellArray* polys, vtkIdType a, vtkIdType b, vtkIdType c,
    std::vector<std::pair<int, vtkIdType>>& keptCellSource, std::vector<int>& cellMarks)
{
    if (a == b || b == c || a == c)
    {
        return;
    }
    const vtkIdType ids[3] = {a, b, c};
    polys->InsertNextCell(3, ids);
    keptCellSource.emplace_back(-1, 0);
    cellMarks.push_back(FUSE_MARK_NEW);
}
} 

//------------------------------------------------------------------------------
vtkSHYXDisconnectedRegionFuse::vtkSHYXDisconnectedRegionFuse()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkSHYXDisconnectedRegionFuse::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXDisconnectedRegionFuse::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkInformationVector* inVec = inputVector[0];
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

    const int nInputs = inVec->GetNumberOfInformationObjects();
    if (nInputs < 1)
    {
        vtkErrorMacro(<< "At least one input connection is required.");
        return 0;
    }

    std::vector<vtkPolyData*> pdIn(static_cast<size_t>(nInputs), nullptr);
    std::vector<vtkIdType> ptOffset(static_cast<size_t>(nInputs) + 1, 0);
    for (int i = 0; i < nInputs; ++i)
    {
        pdIn[static_cast<size_t>(i)] = vtkPolyData::GetData(inVec, i);
        const vtkIdType np =
            pdIn[static_cast<size_t>(i)] ? pdIn[static_cast<size_t>(i)]->GetNumberOfPoints() : 0;
        ptOffset[static_cast<size_t>(i) + 1] = ptOffset[static_cast<size_t>(i)] + np;
    }
    const vtkIdType nPoints = ptOffset[static_cast<size_t>(nInputs)];
    if (nPoints == 0)
    {
        return 1;
    }

    std::vector<std::array<double, 3>> globalPos(static_cast<size_t>(nPoints));
    std::vector<int> globalInputOfPoint(static_cast<size_t>(nPoints));
    std::vector<vtkIdType> globalLocalId(static_cast<size_t>(nPoints));

    for (int i = 0; i < nInputs; ++i)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(i)];
        if (!pd)
        {
            continue;
        }
        const vtkIdType np = pd->GetNumberOfPoints();
        for (vtkIdType j = 0; j < np; ++j)
        {
            const vtkIdType gid = ptOffset[static_cast<size_t>(i)] + j;
            double p[3];
            pd->GetPoint(j, p);
            globalPos[static_cast<size_t>(gid)] = {p[0], p[1], p[2]};
            globalInputOfPoint[static_cast<size_t>(gid)] = i;
            globalLocalId[static_cast<size_t>(gid)] = j;
        }
    }

    std::vector<int> componentOfPoint(static_cast<size_t>(nPoints), 0);
    int nComponents = 0;
    if (this->FuseWithinInput)
    {
        UnionFind connectivity(nPoints);
        vtkSmartPointer<vtkIdList> walkPts = vtkSmartPointer<vtkIdList>::New();
        for (int i = 0; i < nInputs; ++i)
        {
            vtkPolyData* pd = pdIn[static_cast<size_t>(i)];
            if (!pd)
            {
                continue;
            }
            const vtkIdType off = ptOffset[static_cast<size_t>(i)];
            UniteVertCells(pd->GetVerts(), off, connectivity, walkPts);
            UniteAlongCells(pd->GetLines(), off, connectivity, false, walkPts);
            UniteAlongCells(pd->GetPolys(), off, connectivity, true, walkPts);
            UniteAlongCells(pd->GetStrips(), off, connectivity, false, walkPts);
        }

        std::unordered_map<vtkIdType, int> rootToComponent;
        for (vtkIdType i = 0; i < nPoints; ++i)
        {
            const vtkIdType root = connectivity.find(i);
            auto inserted = rootToComponent.emplace(root, nComponents);
            if (inserted.second)
            {
                ++nComponents;
            }
            componentOfPoint[static_cast<size_t>(i)] = inserted.first->second;
        }
    }
    else
    {
        nComponents = nInputs;
        for (vtkIdType i = 0; i < nPoints; ++i)
        {
            componentOfPoint[static_cast<size_t>(i)] = globalInputOfPoint[static_cast<size_t>(i)];
        }
    }

    std::vector<vtkIdType> componentPointCount(static_cast<size_t>(nComponents), 0);
    for (vtkIdType i = 0; i < nPoints; ++i)
    {
        ++componentPointCount[static_cast<size_t>(componentOfPoint[static_cast<size_t>(i)])];
    }

    const bool passthrough = (nInputs == 1 && nComponents <= 1 && this->FuseVerts &&
        this->FuseLines && this->FusePolys);
    if (passthrough)
    {
        vtkPolyData* input = pdIn[0];
        if (!input)
        {
            return 1;
        }
        output->ShallowCopy(input);
        return 1;
    }

    std::vector<PointHint> pointHints;
    const double complianceWeight = this->ComplianceWeight;
    if (complianceWeight > 0.0)
    {
        BuildPointHints(pdIn, ptOffset, nInputs, nPoints, globalPos, pointHints);
    }

    // Closest (or compliance-blended) gap per region pair, then Kruskal.
    // Raising T keeps already-accepted welds; it only adds farther region-region joins.
    std::vector<std::pair<vtkIdType, vtkIdType>> mergePairs;
    if (nComponents > 1 && this->FuseThreshold > 0.0)
    {
        vtkSmartPointer<vtkPoints> allPts = vtkSmartPointer<vtkPoints>::New();
        allPts->SetDataTypeToDouble();
        allPts->SetNumberOfPoints(nPoints);
        for (vtkIdType i = 0; i < nPoints; ++i)
        {
            allPts->SetPoint(i, globalPos[static_cast<size_t>(i)].data());
        }
        vtkSmartPointer<vtkPolyData> locPd = vtkSmartPointer<vtkPolyData>::New();
        locPd->SetPoints(allPts);

        vtkSmartPointer<vtkStaticPointLocator> locator = vtkSmartPointer<vtkStaticPointLocator>::New();
        locator->SetDataSet(locPd);
        locator->BuildLocator();

        struct NearHit
        {
            vtkIdType i = -1;
            vtkIdType j = -1;
            double dist = 0.0;
            double cost = 0.0;
            int ci = 0;
            int cj = 0;
        };
        std::vector<NearHit> nearestForeign;

        auto considerHits = [&](vtkIdType i, vtkIdList* ids, std::vector<NearHit>& out)
        {
            const int ci = componentOfPoint[static_cast<size_t>(i)];
            const auto& pi = globalPos[static_cast<size_t>(i)];
            vtkIdType bestJ = -1;
            double bestCost = std::numeric_limits<double>::infinity();
            double bestDist = 0.0;
            int bestCj = ci;
            for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k)
            {
                const vtkIdType j = ids->GetId(k);
                if (j == i)
                {
                    continue;
                }
                const int cj = componentOfPoint[static_cast<size_t>(j)];
                if (cj == ci)
                {
                    continue;
                }
                const auto& pj = globalPos[static_cast<size_t>(j)];
                const double dx = pi[0] - pj[0];
                const double dy = pi[1] - pj[1];
                const double dz = pi[2] - pj[2];
                const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double cost =
                    PairCost(i, j, d, complianceWeight, this->FuseThreshold, pointHints, globalPos);
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestDist = d;
                    bestJ = j;
                    bestCj = cj;
                }
            }
            if (bestJ >= 0)
            {
                out.push_back({i, bestJ, bestDist, bestCost, ci, bestCj});
            }
        };

#ifdef VESPA_USE_SMP
        vtkSMPThreadLocal<std::vector<NearHit>> threadHits;
        vtkSMPTools::For(0, nPoints, [&](vtkIdType begin, vtkIdType end) {
            auto& local = threadHits.Local();
            vtkSmartPointer<vtkIdList> ids = vtkSmartPointer<vtkIdList>::New();
            for (vtkIdType i = begin; i < end; ++i)
            {
                ids->Reset();
                locator->FindPointsWithinRadius(
                    this->FuseThreshold, globalPos[static_cast<size_t>(i)].data(), ids);
                considerHits(i, ids, local);
            }
        });
        for (auto it = threadHits.begin(); it != threadHits.end(); ++it)
        {
            nearestForeign.insert(nearestForeign.end(), it->begin(), it->end());
        }
#else
        vtkSmartPointer<vtkIdList> ids = vtkSmartPointer<vtkIdList>::New();
        for (vtkIdType i = 0; i < nPoints; ++i)
        {
            ids->Reset();
            locator->FindPointsWithinRadius(
                this->FuseThreshold, globalPos[static_cast<size_t>(i)].data(), ids);
            considerHits(i, ids, nearestForeign);
        }
#endif

        struct BestEdge
        {
            vtkIdType a = -1;
            vtkIdType b = -1;
            double dist = std::numeric_limits<double>::infinity();
            double cost = std::numeric_limits<double>::infinity();
        };
        std::map<std::pair<int, int>, BestEdge> bestBetween;
        for (const NearHit& hit : nearestForeign)
        {
            const int lo = std::min(hit.ci, hit.cj);
            const int hi = std::max(hit.ci, hit.cj);
            BestEdge& slot = bestBetween[{lo, hi}];
            if (hit.cost < slot.cost)
            {
                slot.a = hit.i;
                slot.b = hit.j;
                slot.dist = hit.dist;
                slot.cost = hit.cost;
            }
        }

        std::vector<BestEdge> edges;
        edges.reserve(bestBetween.size());
        for (const auto& kv : bestBetween)
        {
            if (kv.second.a >= 0)
            {
                edges.push_back(kv.second);
            }
        }
        std::sort(edges.begin(), edges.end(),
            [](const BestEdge& x, const BestEdge& y) { return x.cost < y.cost; });

        UnionFind regionUF(nComponents);
        for (const BestEdge& e : edges)
        {
            if (e.dist > this->FuseThreshold)
            {
                continue;
            }
            const int ca = componentOfPoint[static_cast<size_t>(e.a)];
            const int cb = componentOfPoint[static_cast<size_t>(e.b)];
            if (regionUF.find(ca) == regionUF.find(cb))
            {
                continue;
            }
            regionUF.unite(ca, cb);
            mergePairs.emplace_back(e.a, e.b);
        }
    }

    UnionFind uf(nPoints);
    const bool constructPrimitives =
        (this->FusePositionMode == FUSE_POSITION_CONSTRUCT_PRIMITIVES);
    if (!constructPrimitives)
    {
        for (const auto& pair : mergePairs)
        {
            uf.unite(pair.first, pair.second);
        }
    }

    // Compute new point coords (centroid of each equivalence class)
    std::unordered_map<vtkIdType, vtkIdType> rootToNewId;
    std::vector<std::vector<vtkIdType>> clusters;
    vtkIdType newPointCount = 0;

    for (vtkIdType i = 0; i < nPoints; ++i)
    {
        vtkIdType root = uf.find(i);
        if (rootToNewId.find(root) == rootToNewId.end())
        {
            rootToNewId[root] = newPointCount++;
            clusters.emplace_back();
        }
        clusters[rootToNewId[root]].push_back(i);
    }

    std::vector<char> pointFused(static_cast<size_t>(nPoints), 0);
    for (const auto& members : clusters)
    {
        if (members.size() > 1)
        {
            for (vtkIdType oldId : members)
            {
                pointFused[static_cast<size_t>(oldId)] = 1;
            }
        }
    }

    vtkSmartPointer<vtkPoints> newPoints = vtkSmartPointer<vtkPoints>::New();
    newPoints->SetDataTypeToDouble();
    newPoints->SetNumberOfPoints(newPointCount);
    std::vector<vtkIdType> clusterRep(static_cast<size_t>(newPointCount), 0);

    const bool snapSmallToLarge =
        (this->FusePositionMode == FUSE_POSITION_SNAP_SMALL_TO_LARGE);
    for (vtkIdType i = 0; i < newPointCount; ++i)
    {
        const auto& members = clusters[static_cast<size_t>(i)];
        vtkIdType rep = members[0];
        if (snapSmallToLarge && members.size() > 1)
        {
            vtkIdType bestSize = componentPointCount[static_cast<size_t>(
                componentOfPoint[static_cast<size_t>(rep)])];
            for (vtkIdType oldId : members)
            {
                const vtkIdType sz = componentPointCount[static_cast<size_t>(
                    componentOfPoint[static_cast<size_t>(oldId)])];
                if (sz > bestSize)
                {
                    bestSize = sz;
                    rep = oldId;
                }
            }
            newPoints->SetPoint(i, globalPos[static_cast<size_t>(rep)].data());
        }
        else
        {
            double center[3] = {0, 0, 0};
            for (vtkIdType oldId : members)
            {
                const auto& p = globalPos[static_cast<size_t>(oldId)];
                center[0] += p[0];
                center[1] += p[1];
                center[2] += p[2];
            }
            const double size = static_cast<double>(members.size());
            center[0] /= size;
            center[1] /= size;
            center[2] /= size;
            newPoints->SetPoint(i, center);
        }
        clusterRep[static_cast<size_t>(i)] = rep;
    }

    // 5. Remap enabled cell arrays (VTK order: verts, lines, polys). Drop degenerates.
    vtkSmartPointer<vtkCellArray> newVerts = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkCellArray> newLines = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkCellArray> newPolys = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkIdList> cellPts = vtkSmartPointer<vtkIdList>::New();
    std::vector<std::pair<int, vtkIdType>> keptCellSource;
    std::vector<int> cellMarks;

    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FuseVerts)
        {
            continue;
        }
        RemapCellArray(pd->GetVerts(), newVerts, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 1, inp, 0, pointFused, keptCellSource, cellMarks);
    }
    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FuseLines)
        {
            continue;
        }
        RemapCellArray(pd->GetLines(), newLines, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 2, inp, pd->GetNumberOfVerts(), pointFused, keptCellSource, cellMarks);
    }

    std::vector<std::pair<vtkIdType, vtkIdType>> lineBridges;
    std::vector<std::array<vtkIdType, 3>> triBridges;
    if (constructPrimitives && !mergePairs.empty())
    {
        auto toNewId = [&](vtkIdType gid) -> vtkIdType {
            const auto found = rootToNewId.find(uf.find(gid));
            return found == rootToNewId.end() ? vtkIdType{-1} : found->second;
        };

        std::vector<std::vector<vtkIdType>> polyBdryAdj(static_cast<size_t>(nPoints));
        std::vector<std::vector<vtkIdType>> polyAdj(static_cast<size_t>(nPoints));
        std::vector<std::vector<vtkIdType>> lineAdj(static_cast<size_t>(nPoints));
        vtkSmartPointer<vtkIdList> walk = vtkSmartPointer<vtkIdList>::New();
        for (int inp = 0; inp < nInputs; ++inp)
        {
            vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
            if (!pd)
            {
                continue;
            }
            const vtkIdType off = ptOffset[static_cast<size_t>(inp)];
            std::map<EdgeKey, int> uses;
            CountPolyEdgeUses(pd->GetPolys(), walk, uses);
            if (pd->GetPolys())
            {
                for (pd->GetPolys()->InitTraversal(); pd->GetPolys()->GetNextCell(walk);)
                {
                    const vtkIdType n = walk->GetNumberOfIds();
                    if (n < 3)
                    {
                        continue;
                    }
                    for (vtkIdType k = 0; k < n; ++k)
                    {
                        const vtkIdType la = walk->GetId(k);
                        const vtkIdType lb = walk->GetId((k + 1) % n);
                        const vtkIdType ga = off + la;
                        const vtkIdType gb = off + lb;
                        AddAdj(polyAdj, ga, gb);
                        if (uses[MakeEdge(la, lb)] == 1)
                        {
                            AddAdj(polyBdryAdj, ga, gb);
                        }
                    }
                }
            }
            if (pd->GetLines())
            {
                for (pd->GetLines()->InitTraversal(); pd->GetLines()->GetNextCell(walk);)
                {
                    const vtkIdType n = walk->GetNumberOfIds();
                    for (vtkIdType k = 1; k < n; ++k)
                    {
                        AddAdj(lineAdj, off + walk->GetId(k - 1), off + walk->GetId(k));
                    }
                }
            }
        }

        auto pickToward = [&](vtkIdType from, vtkIdType toward) -> vtkIdType {
            vtkIdType n = BestNeighborToward(polyBdryAdj[static_cast<size_t>(from)], from, toward,
                globalPos, pointHints, complianceWeight, this->FuseThreshold);
            if (n < 0)
            {
                n = BestNeighborToward(polyAdj[static_cast<size_t>(from)], from, toward, globalPos,
                    pointHints, complianceWeight, this->FuseThreshold);
            }
            if (n < 0)
            {
                n = BestNeighborToward(lineAdj[static_cast<size_t>(from)], from, toward, globalPos,
                    pointHints, complianceWeight, this->FuseThreshold);
            }
            return n;
        };

        for (const auto& pair : mergePairs)
        {
            const vtkIdType a = toNewId(pair.first);
            const vtkIdType b = toNewId(pair.second);
            if (a < 0 || b < 0 || a == b)
            {
                continue;
            }

            bool addedFace = false;
            if (this->FusePolys)
            {
                const vtkIdType a2 = pickToward(pair.first, pair.second);
                const vtkIdType b2 = pickToward(pair.second, pair.first);
                const vtkIdType na2 = a2 >= 0 ? toNewId(a2) : vtkIdType{-1};
                const vtkIdType nb2 = b2 >= 0 ? toNewId(b2) : vtkIdType{-1};
                if (na2 >= 0 && nb2 >= 0 && na2 != nb2 && na2 != b && nb2 != a)
                {
                    const auto& pa = globalPos[static_cast<size_t>(pair.first)];
                    const auto& pb = globalPos[static_cast<size_t>(pair.second)];
                    const auto& pa2 = globalPos[static_cast<size_t>(a2)];
                    const auto& pb2 = globalPos[static_cast<size_t>(b2)];
                    if (Dist2(pa, pb2) <= Dist2(pa2, pb))
                    {
                        triBridges.push_back({a, na2, nb2});
                        triBridges.push_back({a, nb2, b});
                    }
                    else
                    {
                        triBridges.push_back({a, na2, b});
                        triBridges.push_back({na2, nb2, b});
                    }
                    addedFace = true;
                }
                else if (na2 >= 0 && na2 != b)
                {
                    triBridges.push_back({a, na2, b});
                    addedFace = true;
                }
                else if (nb2 >= 0 && nb2 != a)
                {
                    triBridges.push_back({a, b, nb2});
                    addedFace = true;
                }
            }
            if (!addedFace && (this->FuseLines || this->FusePolys))
            {
                lineBridges.emplace_back(a, b);
            }
        }
        for (const auto& lb : lineBridges)
        {
            InsertLineCell(newLines, lb.first, lb.second, keptCellSource, cellMarks);
        }
    }
    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FusePolys)
        {
            continue;
        }
        RemapCellArray(pd->GetPolys(), newPolys, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 3, inp, pd->GetNumberOfVerts() + pd->GetNumberOfLines(), pointFused,
            keptCellSource, cellMarks);
    }
    for (const auto& tri : triBridges)
    {
        InsertTriangleCell(newPolys, tri[0], tri[1], tri[2], keptCellSource, cellMarks);
    }

    output->SetPoints(newPoints);
    output->SetVerts(newVerts);
    output->SetLines(newLines);
    output->SetPolys(newPolys);

    // 6. Map attribute data (PointData & CellData); schema from first non-empty input
    vtkPolyData* templatePd = nullptr;
    for (int i = 0; i < nInputs; ++i)
    {
        if (pdIn[static_cast<size_t>(i)] && pdIn[static_cast<size_t>(i)]->GetNumberOfPoints() > 0)
        {
            templatePd = pdIn[static_cast<size_t>(i)];
            break;
        }
    }

    vtkPointData* outPD = output->GetPointData();
    if (templatePd)
    {
        outPD->CopyAllocate(templatePd->GetPointData(), newPointCount);
    }
    for (vtkIdType i = 0; i < newPointCount; ++i)
    {
        const vtkIdType gid = clusterRep[static_cast<size_t>(i)];
        const int srcInp = globalInputOfPoint[static_cast<size_t>(gid)];
        const vtkIdType srcPt = globalLocalId[static_cast<size_t>(gid)];
        vtkPolyData* srcPd = pdIn[static_cast<size_t>(srcInp)];
        if (srcPd)
        {
            outPD->CopyData(srcPd->GetPointData(), srcPt, i);
        }
    }

    vtkCellData* outCD = output->GetCellData();
    vtkPolyData* templateCdPd = nullptr;
    for (int i = 0; i < nInputs; ++i)
    {
        if (pdIn[static_cast<size_t>(i)] && pdIn[static_cast<size_t>(i)]->GetNumberOfCells() > 0)
        {
            templateCdPd = pdIn[static_cast<size_t>(i)];
            break;
        }
    }
    if (templateCdPd)
    {
        outCD->CopyAllocate(templateCdPd->GetCellData(), static_cast<vtkIdType>(keptCellSource.size()));
    }
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(keptCellSource.size()); ++i)
    {
        const int srcInp = keptCellSource[static_cast<size_t>(i)].first;
        const vtkIdType srcCell = keptCellSource[static_cast<size_t>(i)].second;
        if (srcInp < 0)
        {
            continue;
        }
        vtkPolyData* srcPd = pdIn[static_cast<size_t>(srcInp)];
        if (srcPd)
        {
            outCD->CopyData(srcPd->GetCellData(), srcCell, i);
        }
    }

    vtkSmartPointer<vtkIntArray> fuseMark = vtkSmartPointer<vtkIntArray>::New();
    fuseMark->SetName(kFuseMarkArrayName);
    fuseMark->SetNumberOfComponents(1);
    const vtkIdType nOutCells = static_cast<vtkIdType>(keptCellSource.size());
    fuseMark->SetNumberOfTuples(nOutCells);
    for (vtkIdType i = 0; i < nOutCells; ++i)
    {
        const int mark =
            (static_cast<size_t>(i) < cellMarks.size()) ? cellMarks[static_cast<size_t>(i)]
                                                        : FUSE_MARK_UNCHANGED;
        fuseMark->SetValue(i, mark);
    }
    outCD->AddArray(fuseMark);
    outCD->SetScalars(fuseMark);

    return 1;
}

//------------------------------------------------------------------------------
void vtkSHYXDisconnectedRegionFuse::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "FuseThreshold: " << this->FuseThreshold << "\n";
    os << indent << "FuseWithinInput: " << (this->FuseWithinInput ? "On\n" : "Off\n");
    os << indent << "FusePositionMode: " << this->FusePositionMode;
    if (this->FusePositionMode == FUSE_POSITION_SNAP_SMALL_TO_LARGE)
    {
        os << " (Snap small to large)\n";
    }
    else if (this->FusePositionMode == FUSE_POSITION_CONSTRUCT_PRIMITIVES)
    {
        os << " (Construct primitives)\n";
    }
    else
    {
        os << " (Average)\n";
    }
    os << indent << "ComplianceWeight: " << this->ComplianceWeight << "\n";
    os << indent << "FuseVerts: " << (this->FuseVerts ? "On\n" : "Off\n");
    os << indent << "FuseLines: " << (this->FuseLines ? "On\n" : "Off\n");
    os << indent << "FusePolys: " << (this->FusePolys ? "On\n" : "Off\n");
}

VTK_ABI_NAMESPACE_END