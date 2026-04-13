#include "vtkSHYXDisconnectedRegionFuse.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkStaticPointLocator.h>  // faster than vtkPointLocator
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkSmartPointer.h>
#include <vtkDataArray.h>

#ifdef VESPA_USE_SMP
#include <vtkSMPThreadLocal.h>
#include <vtkSMPTools.h>
#endif

#include <algorithm>
#include <array>
#include <unordered_map>

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
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXDisconnectedRegionFuse::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

    if (!input || input->GetNumberOfPoints() == 0) return 1;

    // 1. Connectivity filter for region labeling
    vtkSmartPointer<vtkPolyDataConnectivityFilter> conn = vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    conn->SetInputData(input);
    conn->SetExtractionModeToAllRegions();
    conn->ColorRegionsOn();  // produces "RegionId" array
    conn->Update();

    vtkPolyData* connOutput = conn->GetOutput();
    vtkDataArray* regionIds = connOutput->GetPointData()->GetArray("RegionId");
    int nRegions = conn->GetNumberOfExtractedRegions();

    // Single region, nothing to fuse
    if (nRegions <= 1)
    {
        output->ShallowCopy(input);
        return 1;
    }

    vtkIdType nPoints = connOutput->GetNumberOfPoints();
    
    // 2. Group points by RegionId (std::array copy avoids pointer invalidation)
    std::vector<std::vector<PointInfo>> regions(nRegions);
    for (vtkIdType i = 0; i < nPoints; ++i)
    {
        int r = static_cast<int>(regionIds->GetTuple1(i));
        double p[3];
        connOutput->GetPoint(i, p);
        regions[r].push_back({i, {p[0], p[1], p[2]}});
    }

    // 3. Find cross-region merge pairs
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
            double p[3];
            connOutput->GetPoint(oldId, p);
            center[0] += p[0]; center[1] += p[1]; center[2] += p[2];
        }
        double size = static_cast<double>(clusters[i].size());
        center[0] /= size; center[1] /= size; center[2] /= size;
        newPoints->SetPoint(i, center);
    }

    // 5. Update cell topology, drop degenerate cells
    vtkSmartPointer<vtkCellArray> newPolys = vtkSmartPointer<vtkCellArray>::New();
    vtkCellArray* inPolys = connOutput->GetPolys();
    
    vtkSmartPointer<vtkIdList> cellPts = vtkSmartPointer<vtkIdList>::New();
    std::vector<vtkIdType> keptCellIds;
    vtkIdType cellId = 0;

    for (inPolys->InitTraversal(); inPolys->GetNextCell(cellPts); ++cellId)
    {
        std::vector<vtkIdType> newIds;
        for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
        {
            vtkIdType oldId = cellPts->GetId(k);
            vtkIdType root = uf.find(oldId);
            vtkIdType nid = rootToNewId[root];
            if (newIds.empty() || newIds.back() != nid)
            {
                newIds.push_back(nid);
            }
        }
        // closed loop check
        if (newIds.size() > 1 && newIds.front() == newIds.back()) newIds.pop_back();

        if (newIds.size() >= 3)
        {
            newPolys->InsertNextCell(static_cast<vtkIdType>(newIds.size()), newIds.data());
            keptCellIds.push_back(cellId);
        }
    }

    output->SetPoints(newPoints);
    output->SetPolys(newPolys);

    // 6. Map attribute data (PointData & CellData)
    vtkPointData* inPD = connOutput->GetPointData();
    vtkPointData* outPD = output->GetPointData();
    outPD->CopyAllocate(inPD, newPointCount);
    for (vtkIdType i = 0; i < newPointCount; ++i)
    {
        // attribute from first point in equivalence class
        outPD->CopyData(inPD, clusters[i][0], i);
    }

    vtkCellData* inCD = connOutput->GetCellData();
    vtkCellData* outCD = output->GetCellData();
    outCD->CopyAllocate(inCD, static_cast<vtkIdType>(keptCellIds.size()));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(keptCellIds.size()); ++i)
    {
        outCD->CopyData(inCD, keptCellIds[i], i);
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