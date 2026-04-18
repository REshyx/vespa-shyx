#include "vtkSHYXSelectionExtrudeFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkFieldData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXSelectionExtrudeFilter);

namespace
{
using EdgeKey = std::pair<vtkIdType, vtkIdType>;

EdgeKey MakeEdge(vtkIdType a, vtkIdType b)
{
    return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
}

void TriangleNormalArea(
    vtkPoints* pts, vtkIdType i0, vtkIdType i1, vtkIdType i2, double normal[3], double& area)
{
    double p0[3], p1[3], p2[3];
    pts->GetPoint(i0, p0);
    pts->GetPoint(i1, p1);
    pts->GetPoint(i2, p2);
    double v0[3], v1[3];
    v0[0] = p1[0] - p0[0];
    v0[1] = p1[1] - p0[1];
    v0[2] = p1[2] - p0[2];
    v1[0] = p2[0] - p0[0];
    v1[1] = p2[1] - p0[1];
    v1[2] = p2[2] - p0[2];
    vtkMath::Cross(v0, v1, normal);
    area = vtkMath::Norm(normal) * 0.5;
    if (area > 1e-30)
    {
        normal[0] /= (2.0 * area);
        normal[1] /= (2.0 * area);
        normal[2] /= (2.0 * area);
    }
    else
    {
        normal[0] = normal[1] = 0.0;
        normal[2] = 1.0;
        area = 0.0;
    }
}

void CollectCellsFromExtracted(vtkPolyData* mesh, vtkDataSet* extracted, std::set<vtkIdType>& selected)
{
    if (!mesh || !extracted)
    {
        return;
    }
    const vtkIdType nMeshCells = mesh->GetNumberOfCells();

    vtkDataArray* ocid = extracted->GetCellData()->GetArray("vtkOriginalCellIds");
    if (auto* cellIds = vtkIdTypeArray::SafeDownCast(ocid))
    {
        if (cellIds->GetNumberOfTuples() == extracted->GetNumberOfCells())
        {
            for (vtkIdType i = 0; i < cellIds->GetNumberOfTuples(); ++i)
            {
                const vtkIdType cid = cellIds->GetValue(i);
                if (cid >= 0 && cid < nMeshCells)
                {
                    selected.insert(cid);
                }
            }
        }
    }
    if (!selected.empty())
    {
        return;
    }

    vtkDataArray* opid = extracted->GetPointData()->GetArray("vtkOriginalPointIds");
    auto* ptIds = vtkIdTypeArray::SafeDownCast(opid);
    if (!ptIds || ptIds->GetNumberOfTuples() == 0)
    {
        return;
    }
    std::set<vtkIdType> selPt;
    for (vtkIdType i = 0; i < ptIds->GetNumberOfTuples(); ++i)
    {
        const vtkIdType pid = ptIds->GetValue(i);
        if (pid >= 0 && pid < mesh->GetNumberOfPoints())
        {
            selPt.insert(pid);
        }
    }
    if (selPt.empty())
    {
        return;
    }
    vtkIdType npts;
    const vtkIdType* pids;
    for (vtkIdType cid = 0; cid < nMeshCells; ++cid)
    {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        for (int k = 0; k < 3; ++k)
        {
            if (selPt.count(pids[k]) != 0u)
            {
                selected.insert(cid);
                break;
            }
        }
    }
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXSelectionExtrudeFilter::vtkSHYXSelectionExtrudeFilter()
{
    this->SetNumberOfInputPorts(2);
    this->LastAverageNormal[0] = 0.0;
    this->LastAverageNormal[1] = 0.0;
    this->LastAverageNormal[2] = 1.0;
}

//------------------------------------------------------------------------------
vtkSHYXSelectionExtrudeFilter::~vtkSHYXSelectionExtrudeFilter()
{
    this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionExtrudeFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
    this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionExtrudeFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "ExtrusionDistance: " << this->ExtrusionDistance << "\n";
    os << indent << "FlipExtrusionDirection: " << this->FlipExtrusionDirection << "\n";
    os << indent << "SelectionCellArrayName: "
       << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
    os << indent << "LastAverageNormal: (" << this->LastAverageNormal[0] << ", "
       << this->LastAverageNormal[1] << ", " << this->LastAverageNormal[2] << ")\n";
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionExtrudeFilter::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    if (port == 1)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
        info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionExtrudeFilter::RequestData(
    vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

    if (!mesh)
    {
        vtkErrorMacro("Input port 0 (surface) is required.");
        return 0;
    }

    this->LastAverageNormal[0] = 0.0;
    this->LastAverageNormal[1] = 0.0;
    this->LastAverageNormal[2] = 1.0;

    vtkPoints* inPts = mesh->GetPoints();
    if (!inPts || mesh->GetNumberOfCells() == 0)
    {
        output->ShallowCopy(mesh);
        return 1;
    }

    std::set<vtkIdType> selected;

    if (this->GetNumberOfInputConnections(1) > 0)
    {
        vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
        if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
        {
            vtkSelection* inputSel =
                vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
            if (inputSel && inputSel->GetNumberOfNodes() > 0)
            {
                vtkNew<vtkExtractSelection> extractSelection;
                extractSelection->SetInputData(0, mesh);
                extractSelection->SetInputData(1, inputSel);
                extractSelection->Update();
                vtkDataSet* extracted =
                    vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
                if (extracted &&
                    (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
                {
                    CollectCellsFromExtracted(mesh, extracted, selected);
                }
            }
        }
    }

    // Fallback: cell array on port 0
    if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
    {
        vtkDataArray* arr = mesh->GetCellData()->GetArray(this->SelectionCellArrayName);
        if (!arr)
        {
            vtkWarningMacro("SelectionCellArrayName \"" << this->SelectionCellArrayName
                                                         << "\" not found on input cell data.");
        }
        else
        {
            const vtkIdType nc = mesh->GetNumberOfCells();
            for (vtkIdType cid = 0; cid < nc; ++cid)
            {
                if (mesh->GetCellType(cid) != VTK_TRIANGLE)
                {
                    continue;
                }
                bool take = false;
                if (arr->IsIntegral())
                {
                    take = (arr->GetTuple1(cid) != 0.0);
                }
                else
                {
                    take = (arr->GetTuple1(cid) > 0.5);
                }
                if (take)
                {
                    selected.insert(cid);
                }
            }
        }
    }

    {
        std::set<vtkIdType> triOnly;
        vtkIdType skipped = 0;
        for (vtkIdType cid : selected)
        {
            if (mesh->GetCellType(cid) == VTK_TRIANGLE)
            {
                triOnly.insert(cid);
            }
            else
            {
                ++skipped;
            }
        }
        if (skipped > 0)
        {
            vtkWarningMacro(<< skipped << " selected non-triangle cells were ignored.");
        }
        selected.swap(triOnly);
    }

    if (selected.empty())
    {
        vtkWarningMacro("No selected triangles; passing input through unchanged.");
        output->ShallowCopy(mesh);
        vtkNew<vtkDoubleArray> fn;
        fn->SetName("SHYX_SelectionExtrude_AvgNormal");
        fn->SetNumberOfComponents(3);
        fn->SetNumberOfTuples(1);
        fn->SetTuple3(0, this->LastAverageNormal[0], this->LastAverageNormal[1], this->LastAverageNormal[2]);
        output->GetFieldData()->AddArray(fn);
        return 1;
    }

    // Area-weighted average normal (geometric normals on mesh)
    double sumN[3] = { 0.0, 0.0, 0.0 };
    for (vtkIdType cid : selected)
    {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            continue;
        }
        vtkIdType npts;
        const vtkIdType* pids;
        mesh->GetCellPoints(cid, npts, pids);
        if (npts != 3)
        {
            continue;
        }
        double n[3], area;
        TriangleNormalArea(inPts, pids[0], pids[1], pids[2], n, area);
        sumN[0] += n[0] * area;
        sumN[1] += n[1] * area;
        sumN[2] += n[2] * area;
    }
    const double len = vtkMath::Norm(sumN);
    if (len < 1e-30)
    {
        vtkWarningMacro("Average normal degenerate; using (0,0,1).");
        this->LastAverageNormal[0] = 0.0;
        this->LastAverageNormal[1] = 0.0;
        this->LastAverageNormal[2] = 1.0;
    }
    else
    {
        this->LastAverageNormal[0] = sumN[0] / len;
        this->LastAverageNormal[1] = sumN[1] / len;
        this->LastAverageNormal[2] = sumN[2] / len;
    }

    if (std::abs(this->ExtrusionDistance) < 1e-30)
    {
        output->ShallowCopy(mesh);
        vtkNew<vtkDoubleArray> fn;
        fn->SetName("SHYX_SelectionExtrude_AvgNormal");
        fn->SetNumberOfComponents(3);
        fn->SetNumberOfTuples(1);
        fn->SetTuple3(0, this->LastAverageNormal[0], this->LastAverageNormal[1], this->LastAverageNormal[2]);
        output->GetFieldData()->AddArray(fn);
        return 1;
    }

    double dir[3] = { this->LastAverageNormal[0], this->LastAverageNormal[1], this->LastAverageNormal[2] };
    if (this->FlipExtrusionDirection)
    {
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
    }
    const double d = this->ExtrusionDistance;
    double offset[3] = { d * dir[0], d * dir[1], d * dir[2] };

    // Boundary edges of selected patch
    std::map<EdgeKey, int> edgeCount;
    for (vtkIdType cid : selected)
    {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            continue;
        }
        vtkIdType npts;
        const vtkIdType* pids;
        mesh->GetCellPoints(cid, npts, pids);
        if (npts != 3)
        {
            continue;
        }
        ++edgeCount[MakeEdge(pids[0], pids[1])];
        ++edgeCount[MakeEdge(pids[1], pids[2])];
        ++edgeCount[MakeEdge(pids[2], pids[0])];
    }

    std::vector<EdgeKey> boundaryEdges;
    boundaryEdges.reserve(edgeCount.size());
    for (const auto& kv : edgeCount)
    {
        if (kv.second == 1)
        {
            boundaryEdges.push_back(kv.first);
        }
    }

    // Top vertex map: any point incident to a selected triangle gets an offset copy
    std::map<vtkIdType, vtkIdType> topMap;
    const vtkIdType nOldPts = inPts->GetNumberOfPoints();
    vtkNew<vtkPoints> outPts;
    outPts->SetNumberOfPoints(nOldPts);
    for (vtkIdType i = 0; i < nOldPts; ++i)
    {
        double p[3];
        inPts->GetPoint(i, p);
        outPts->SetPoint(i, p);
    }

    for (vtkIdType cid : selected)
    {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            continue;
        }
        vtkIdType npts;
        const vtkIdType* pids;
        mesh->GetCellPoints(cid, npts, pids);
        for (int k = 0; k < 3; ++k)
        {
            const vtkIdType vid = pids[k];
            if (topMap.count(vid) != 0)
            {
                continue;
            }
            double p[3];
            inPts->GetPoint(vid, p);
            const vtkIdType nid = outPts->InsertNextPoint(p[0] + offset[0], p[1] + offset[1], p[2] + offset[2]);
            topMap[vid] = nid;
        }
    }

    vtkNew<vtkCellArray> polys;
    vtkIdType npts;
    const vtkIdType* pids;
    vtkNew<vtkIdList> idl;

    vtkIdType skippedNonTri = 0;
    const vtkIdType nCells = mesh->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (selected.count(cid) != 0u)
        {
            continue;
        }
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            ++skippedNonTri;
            continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        idl->Reset();
        idl->InsertNextId(pids[0]);
        idl->InsertNextId(pids[1]);
        idl->InsertNextId(pids[2]);
        polys->InsertNextCell(idl);
    }
    if (skippedNonTri > 0)
    {
        vtkWarningMacro(<< skippedNonTri << " unselected non-triangle cells were omitted (filter expects a triangle surface).");
    }

    // Selected triangles at their original positions are omitted (open hole); only extruded top cap + side walls.

    // Top selected (same winding as input selection)
    for (vtkIdType cid : selected)
    {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
            continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        idl->Reset();
        idl->InsertNextId(topMap[pids[0]]);
        idl->InsertNextId(topMap[pids[1]]);
        idl->InsertNextId(topMap[pids[2]]);
        polys->InsertNextCell(idl);
    }

    // Side quads
    for (const EdgeKey& ek : boundaryEdges)
    {
        const vtkIdType v0 = ek.first;
        const vtkIdType v1 = ek.second;
        const auto it0 = topMap.find(v0);
        const auto it1 = topMap.find(v1);
        if (it0 == topMap.end() || it1 == topMap.end())
        {
            continue;
        }
        idl->Reset();
        idl->InsertNextId(v0);
        idl->InsertNextId(v1);
        idl->InsertNextId(it1->second);
        idl->InsertNextId(it0->second);
        polys->InsertNextCell(idl);
    }

    output->SetPoints(outPts);
    output->SetPolys(polys);
    output->GetPointData()->CopyAllOn();
    output->GetCellData()->CopyAllOn();
    // Cell/point data from original mesh is not trivially preserved on new cells; clear to avoid wrong mapping
    output->GetPointData()->Initialize();
    output->GetCellData()->Initialize();

    vtkNew<vtkDoubleArray> fn;
    fn->SetName("SHYX_SelectionExtrude_AvgNormal");
    fn->SetNumberOfComponents(3);
    fn->SetNumberOfTuples(1);
    fn->SetTuple3(0, this->LastAverageNormal[0], this->LastAverageNormal[1], this->LastAverageNormal[2]);
    output->GetFieldData()->AddArray(fn);

    return 1;
}

VTK_ABI_NAMESPACE_END
