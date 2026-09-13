#include "vtkSHYXDisconnectedRegionFuse.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkIdList.h>
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
    std::vector<std::pair<int, vtkIdType>>& keptCellSource)
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
        for (vtkIdType k = 0; k < cellPts->GetNumberOfIds(); ++k)
        {
            const vtkIdType oldGlobal = ptOffset + cellPts->GetId(k);
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
        }
    }
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

    // Cross-component pairs within FuseThreshold (one locator; skip same component).
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

#ifdef VESPA_USE_SMP
        vtkSMPThreadLocal<std::vector<std::pair<vtkIdType, vtkIdType>>> threadPairs;
        vtkSMPTools::For(0, nPoints, [&](vtkIdType begin, vtkIdType end) {
            auto& local = threadPairs.Local();
            vtkSmartPointer<vtkIdList> ids = vtkSmartPointer<vtkIdList>::New();
            for (vtkIdType i = begin; i < end; ++i)
            {
                ids->Reset();
                locator->FindPointsWithinRadius(
                    this->FuseThreshold, globalPos[static_cast<size_t>(i)].data(), ids);
                const int ci = componentOfPoint[static_cast<size_t>(i)];
                for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k)
                {
                    const vtkIdType j = ids->GetId(k);
                    if (j <= i)
                    {
                        continue;
                    }
                    if (componentOfPoint[static_cast<size_t>(j)] != ci)
                    {
                        local.emplace_back(i, j);
                    }
                }
            }
        });
        for (auto it = threadPairs.begin(); it != threadPairs.end(); ++it)
        {
            mergePairs.insert(mergePairs.end(), it->begin(), it->end());
        }
#else
        vtkSmartPointer<vtkIdList> ids = vtkSmartPointer<vtkIdList>::New();
        for (vtkIdType i = 0; i < nPoints; ++i)
        {
            ids->Reset();
            locator->FindPointsWithinRadius(
                this->FuseThreshold, globalPos[static_cast<size_t>(i)].data(), ids);
            const int ci = componentOfPoint[static_cast<size_t>(i)];
            for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k)
            {
                const vtkIdType j = ids->GetId(k);
                if (j <= i)
                {
                    continue;
                }
                if (componentOfPoint[static_cast<size_t>(j)] != ci)
                {
                    mergePairs.emplace_back(i, j);
                }
            }
        }
#endif
    }

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

    // 5. Remap enabled cell arrays (VTK order: verts, lines, polys). Drop degenerates.
    vtkSmartPointer<vtkCellArray> newVerts = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkCellArray> newLines = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkCellArray> newPolys = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkIdList> cellPts = vtkSmartPointer<vtkIdList>::New();
    std::vector<std::pair<int, vtkIdType>> keptCellSource;

    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FuseVerts)
        {
            continue;
        }
        RemapCellArray(pd->GetVerts(), newVerts, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 1, inp, 0, keptCellSource);
    }
    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FuseLines)
        {
            continue;
        }
        RemapCellArray(pd->GetLines(), newLines, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 2, inp, pd->GetNumberOfVerts(), keptCellSource);
    }
    for (int inp = 0; inp < nInputs; ++inp)
    {
        vtkPolyData* pd = pdIn[static_cast<size_t>(inp)];
        if (!pd || !this->FusePolys)
        {
            continue;
        }
        RemapCellArray(pd->GetPolys(), newPolys, cellPts, ptOffset[static_cast<size_t>(inp)], uf,
            rootToNewId, 3, inp, pd->GetNumberOfVerts() + pd->GetNumberOfLines(), keptCellSource);
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
    os << indent << "FuseWithinInput: " << (this->FuseWithinInput ? "On\n" : "Off\n");
    os << indent << "FuseVerts: " << (this->FuseVerts ? "On\n" : "Off\n");
    os << indent << "FuseLines: " << (this->FuseLines ? "On\n" : "Off\n");
    os << indent << "FusePolys: " << (this->FusePolys ? "On\n" : "Off\n");
}

VTK_ABI_NAMESPACE_END