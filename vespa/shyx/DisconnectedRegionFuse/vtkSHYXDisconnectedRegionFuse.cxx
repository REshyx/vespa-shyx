#include "vtkSHYXDisconnectedRegionFuse.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
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
#include <unordered_map>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDisconnectedRegionFuse);

// Union-Find for vertex equivalence class merging
namespace
{
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

// Point info store (copy coords, avoid VTK internal pointer invalidation)
struct PointInfo {
    vtkIdType id;
    std::array<double, 3> pos;
};
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

    if (nInputs == 1)
    {
        vtkPolyData* input = vtkPolyData::GetData(inVec, 0);
        if (!input || input->GetNumberOfPoints() == 0)
        {
            return 1;
        }
        output->ShallowCopy(input);
        return 1;
    }

    // 1. Each input connection is one fuse domain; global point id = prefix offsets over inputs.
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

    const int nRegions = nInputs;
    std::vector<std::vector<PointInfo>> regions(static_cast<size_t>(nRegions));
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
            regions[static_cast<size_t>(i)].push_back({gid, {p[0], p[1], p[2]}});
        }
    }

    // 2. Find cross-region merge pairs
    std::vector<std::pair<vtkIdType, vtkIdType>> mergePairs;
#ifdef VESPA_USE_SMP
    vtkSMPThreadLocal<std::vector<std::pair<vtkIdType, vtkIdType>>> threadPairs;
#endif

    for (int i = 0; i < nRegions; ++i)
    {
        const auto& ptsI = regions[i];
        if (ptsI.empty()) continue;

        // Build Locator for region I
        vtkSmartPointer<vtkPoints> vtkPtsI = vtkSmartPointer<vtkPoints>::New();
        vtkPtsI->SetDataTypeToDouble();
        for (const auto& pi : ptsI) vtkPtsI->InsertNextPoint(pi.pos.data());

        vtkSmartPointer<vtkPolyData> pdI = vtkSmartPointer<vtkPolyData>::New();
        pdI->SetPoints(vtkPtsI);

        vtkSmartPointer<vtkStaticPointLocator> locator = vtkSmartPointer<vtkStaticPointLocator>::New();
        locator->SetDataSet(pdI);
        locator->BuildLocator();

        // Iterate all other regions J
        for (int j = 0; j < nRegions; ++j)
        {
            if (i == j) continue;  // do not merge within same region

            const auto& ptsJ = regions[j];
            vtkIdType nj = static_cast<vtkIdType>(ptsJ.size());

#ifdef VESPA_USE_SMP
            vtkSMPTools::For(0, nj, [&](vtkIdType begin, vtkIdType end) {
                auto& local = threadPairs.Local();
                for (vtkIdType k = begin; k < end; ++k)
                {
                    double distSq = 0;
                    // search within radius only
                    vtkIdType closestInI = locator->FindClosestPointWithinRadius(this->FuseThreshold, ptsJ[k].pos.data(), distSq);
                    if (closestInI >= 0)
                    {
                        local.emplace_back(ptsI[closestInI].id, ptsJ[k].id);
                    }
                }
            });
#else
            for (const auto& pj : ptsJ)
            {
                double distSq = 0;
                vtkIdType closestInI = locator->FindClosestPointWithinRadius(this->FuseThreshold, pj.pos.data(), distSq);
                if (closestInI >= 0)
                {
                    mergePairs.emplace_back(ptsI[closestInI].id, pj.id);
                }
            }
#endif
        }
    }

#ifdef VESPA_USE_SMP
    for (auto it = threadPairs.begin(); it != threadPairs.end(); ++it)
    {
        mergePairs.insert(mergePairs.end(), it->begin(), it->end());
    }
#endif

    // 4. UnionFind for equivalence classes
    UnionFind uf(nPoints);
    for (const auto& pair : mergePairs) uf.unite(pair.first, pair.second);

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

    vtkSmartPointer<vtkPoints> newPoints = vtkSmartPointer<vtkPoints>::New();
    newPoints->SetDataTypeToDouble();
    newPoints->SetNumberOfPoints(newPointCount);

    for (vtkIdType i = 0; i < newPointCount; ++i)
    {
        double center[3] = {0, 0, 0};
        for (vtkIdType oldId : clusters[i])
        {
            const auto& p = globalPos[static_cast<size_t>(oldId)];
            center[0] += p[0]; center[1] += p[1]; center[2] += p[2];
        }
        double size = static_cast<double>(clusters[i].size());
        center[0] /= size; center[1] /= size; center[2] /= size;
        newPoints->SetPoint(i, center);
    }

    // 5. Update cell topology from each input, drop degenerate cells
    vtkSmartPointer<vtkCellArray> newPolys = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkIdList> cellPts = vtkSmartPointer<vtkIdList>::New();
    std::vector<std::pair<int, vtkIdType>> keptCellSource;

    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd)
        {
            continue;
        }
        vtkCellArray* inPolys = pd->GetPolys();
        vtkIdType localCellId = 0;
        for (inPolys->InitTraversal(); inPolys->GetNextCell(cellPts); ++localCellId)
        {
            std::vector<vtkIdType> newIds;
            for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
            {
                const vtkIdType oldGlobal = ptOffset[static_cast<size_t>(inp)] + cellPts->GetId(k);
                const vtkIdType root = uf.find(oldGlobal);
                const vtkIdType nid = rootToNewId[root];
                if (newIds.empty() || newIds.back() != nid)
                {
                    newIds.push_back(nid);
                }
            }
            if (newIds.size() > 1 && newIds.front() == newIds.back())
            {
                newIds.pop_back();
            }

            if (newIds.size() >= 3)
            {
                newPolys->InsertNextCell(static_cast<vtkIdType>(newIds.size()), newIds.data());
                keptCellSource.emplace_back(inp, localCellId);
            }
        }
    }

    output->SetPoints(newPoints);
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
        const vtkIdType gid = clusters[static_cast<size_t>(i)][0];
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
        vtkPolyData* srcPd = pdIn[static_cast<size_t>(srcInp)];
        if (srcPd)
        {
            outCD->CopyData(srcPd->GetCellData(), srcCell, i);
        }
    }

    return 1;
}

//------------------------------------------------------------------------------
void vtkSHYXDisconnectedRegionFuse::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "FuseThreshold: " << this->FuseThreshold << "\n";
}

VTK_ABI_NAMESPACE_END