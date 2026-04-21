#include "vtkSHYXSelectionOBBSubtractFilter.h"

#include "vtkCGALBooleanOperation.h"
#include "vtkSHYXMinimumOBBFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
#include <vtkFieldData.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkSmartPointer.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXSelectionOBBSubtractFilter);

namespace
{
void AppendAllPointsFromDataset(vtkDataSet* ds, vtkPoints* outPts)
{
    if (!ds || !outPts)
    {
        return;
    }
    double x[3];
    const vtkIdType n = ds->GetNumberOfPoints();
    for (vtkIdType i = 0; i < n; ++i)
    {
        ds->GetPoint(i, x);
        outPts->InsertNextPoint(x);
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
        if (!mesh->GetCell(cid))
        {
            continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        for (vtkIdType k = 0; k < npts; ++k)
        {
            if (selPt.count(pids[k]) != 0u)
            {
                selected.insert(cid);
                break;
            }
        }
    }
}

void AddVerticesOfCells(vtkPolyData* mesh, const std::set<vtkIdType>& cells, vtkPoints* outPts)
{
    if (!mesh || !outPts)
    {
        return;
    }
    std::set<vtkIdType> seen;
    double x[3];
    vtkIdType npts;
    const vtkIdType* pids;
    for (vtkIdType cid : cells)
    {
        mesh->GetCellPoints(cid, npts, pids);
        for (vtkIdType k = 0; k < npts; ++k)
        {
            const vtkIdType pid = pids[k];
            if (seen.insert(pid).second)
            {
                mesh->GetPoint(pid, x);
                outPts->InsertNextPoint(x);
            }
        }
    }
}

vtkSmartPointer<vtkPolyData> PointsToVertexPolyData(vtkPoints* pts)
{
    auto* pd = vtkPolyData::New();
    vtkSmartPointer<vtkPolyData> out;
    out.TakeReference(pd);
    if (!pts || pts->GetNumberOfPoints() == 0)
    {
        return out;
    }
    out->SetPoints(pts);
    vtkNew<vtkCellArray> verts;
    const vtkIdType n = pts->GetNumberOfPoints();
    for (vtkIdType i = 0; i < n; ++i)
    {
        verts->InsertNextCell(1, &i);
    }
    out->SetVerts(verts);
    out->GetPointData()->CopyAllOff();
    out->GetCellData()->CopyAllOff();
    return out;
}

bool ReadTuple3(vtkFieldData* fd, const char* name, double out[3])
{
    if (!fd || !name)
    {
        return false;
    }
    auto* arr = vtkDataArray::SafeDownCast(fd->GetAbstractArray(name));
    if (!arr || arr->GetNumberOfTuples() < 1 || arr->GetNumberOfComponents() != 3)
    {
        return false;
    }
    arr->GetTuple(0, out);
    return true;
}

bool ReadObbHalfLengths(vtkPolyData* obbMesh, double half[3])
{
    if (!obbMesh)
    {
        return false;
    }
    vtkFieldData* fd = obbMesh->GetFieldData();
    return fd && ReadTuple3(fd, "OBB.HalfLengths", half);
}

/** True when vtkSHYXMinimumOBBFilter field arrays are present on the raw OBB polydata. */
bool ReadObbField(vtkPolyData* obbMesh, double center[3], double half[3], double u0[3], double u1[3], double u2[3])
{
    if (!obbMesh)
    {
        return false;
    }
    vtkFieldData* fd = obbMesh->GetFieldData();
    if (!fd)
    {
        return false;
    }
    if (!ReadTuple3(fd, "OBB.Center", center) || !ReadTuple3(fd, "OBB.HalfLengths", half) ||
        !ReadTuple3(fd, "OBB.Axis0", u0) || !ReadTuple3(fd, "OBB.Axis1", u1) || !ReadTuple3(fd, "OBB.Axis2", u2))
    {
        return false;
    }
    vtkMath::Normalize(u0);
    vtkMath::Normalize(u1);
    vtkMath::Normalize(u2);
    return true;
}

std::uint64_t HashSelectionPoints(vtkPoints* pts)
{
    if (!pts || pts->GetNumberOfPoints() < 1)
    {
        return 0;
    }
    std::uint64_t h = static_cast<std::uint64_t>(pts->GetNumberOfPoints());
    const vtkIdType n = pts->GetNumberOfPoints();
    const vtkIdType step = std::max(vtkIdType(1), n / 64);
    double x[3];
    for (vtkIdType i = 0; i < n; i += step)
    {
        pts->GetPoint(i, x);
        for (int j = 0; j < 3; ++j)
        {
            const auto bits = static_cast<std::uint64_t>(std::llround(x[j] * 1e6));
            h = h * 1315423911ULL + bits;
        }
    }
    return h;
}

bool ComputeBaselinePRSFromObbMesh(vtkPolyData* obbMesh, double pos[3], double rot[3], double scale[3])
{
    double C[3], h[3], u0[3], u1[3], u2[3];
    if (!ReadObbField(obbMesh, C, h, u0, u1, u2))
    {
        return false;
    }
    vtkNew<vtkMatrix4x4> rm;
    rm->Identity();
    for (int col = 0; col < 3; ++col)
    {
        const double* u = (col == 0) ? u0 : (col == 1) ? u1 : u2;
        for (int row = 0; row < 3; ++row)
        {
            rm->SetElement(row, col, u[row]);
        }
    }
    vtkTransform::GetOrientation(rot, rm);
    scale[0] = 2.0 * h[0];
    scale[1] = 2.0 * h[1];
    scale[2] = 2.0 * h[2];

    // vtkPVTransform / vtkTransform list order yields M = T * Rz * Rx * Ry * S on column vectors, so
    // ref corner (0,0,0) maps to Translation only: Position = world min corner of the OBB.
    pos[0] = C[0] - h[0] * u0[0] - h[1] * u1[0] - h[2] * u2[0];
    pos[1] = C[1] - h[0] * u0[1] - h[1] * u1[1] - h[2] * u2[1];
    pos[2] = C[2] - h[0] * u0[2] - h[1] * u1[2] - h[2] * u2[2];
    return true;
}

/** Full M from Interactive-Box PRS on raw OBB mesh (always apply /(2*h) when OBB field exists). */
void BuildFullPRSMatrix(vtkPolyData* obbMesh, const double position[3], const double rotationDeg[3],
    const double scaleIn[3], vtkMatrix4x4* outM)
{
    constexpr double eps = 1e-30;
    double sx = std::max(scaleIn[0], eps);
    double sy = std::max(scaleIn[1], eps);
    double sz = std::max(scaleIn[2], eps);

    double h[3];
    if (ReadObbHalfLengths(obbMesh, h))
    {
        sx /= std::max(2.0 * h[0], eps);
        sy /= std::max(2.0 * h[1], eps);
        sz /= std::max(2.0 * h[2], eps);
    }

    // Match ParaView vtkPVTransform::UpdateMatrix (misc_utilities.xml Transform2 / BoxRepresentation).
    vtkNew<vtkTransform> tr;
    tr->Identity();
    tr->Translate(position[0], position[1], position[2]);
    tr->RotateZ(rotationDeg[2]);
    tr->RotateX(rotationDeg[0]);
    tr->RotateY(rotationDeg[1]);
    tr->Scale(sx, sy, sz);
    tr->GetMatrix(outM);
}

void ApplyObbTransformRelativeToBaseline(vtkPolyData* obbMesh, const double curPos[3], const double curRot[3],
    const double curScale[3], const double basePos[3], const double baseRot[3], const double baseScale[3],
    vtkPolyData* outPd)
{
    if (!obbMesh || !outPd || obbMesh->GetNumberOfPoints() == 0)
    {
        if (outPd)
        {
            outPd->Initialize();
        }
        return;
    }

    constexpr double ueps = 1e-5;
    const bool curLooksLikeXmlDefaults = std::fabs(curScale[0] - 1.0) < ueps && std::fabs(curScale[1] - 1.0) < ueps &&
        std::fabs(curScale[2] - 1.0) < ueps && std::fabs(curRot[0]) < ueps && std::fabs(curRot[1]) < ueps &&
        std::fabs(curRot[2]) < ueps && std::fabs(curPos[0]) < ueps && std::fabs(curPos[1]) < ueps &&
        std::fabs(curPos[2]) < ueps;

    vtkNew<vtkMatrix4x4> Minit, Mcur, Minv, Mout;
    BuildFullPRSMatrix(obbMesh, basePos, baseRot, baseScale, Minit);

    if (curLooksLikeXmlDefaults)
    {
        Mcur->DeepCopy(Minit);
    }
    else
    {
        BuildFullPRSMatrix(obbMesh, curPos, curRot, curScale, Mcur);
    }

    vtkMatrix4x4::Invert(Minit, Minv);
    vtkMatrix4x4::Multiply4x4(Mcur, Minv, Mout);

    vtkNew<vtkTransform> tf;
    tf->SetMatrix(Mout);

    vtkNew<vtkTransformPolyDataFilter> tpf;
    tpf->SetInputData(obbMesh);
    tpf->SetTransform(tf);
    tpf->Update();
    outPd->DeepCopy(tpf->GetOutput());
}

/**
 * Absolute transform (no baseline): used when OBB field is missing.
 * Same operator order as ParaView vtkPVTransform::UpdateMatrix (Translate, RotateZ, RotateX, RotateY, Scale).
 * Scale[] is the Interactive Box edge length for a 0..1 reference box; divide by (2*h) before vtk Scale.
 * Until the GUI has pushed real box parameters (placeWidget), XML defaults are Scale=(1,1,1) and zero
 * PRS — those are *not* world edge lengths; treat as identity so the first Apply matches the raw OBB mesh.
 */
void TransformObbWithPRS(vtkPolyData* obbMesh, const double position[3], const double rotationDeg[3],
    const double scaleIn[3], vtkPolyData* outPd)
{
    if (!obbMesh || !outPd || obbMesh->GetNumberOfPoints() == 0)
    {
        if (outPd)
        {
            outPd->Initialize();
        }
        return;
    }
    constexpr double eps = 1e-30;
    constexpr double ueps = 1e-5;
    double sx = std::max(scaleIn[0], eps);
    double sy = std::max(scaleIn[1], eps);
    double sz = std::max(scaleIn[2], eps);

    const bool prsStillXmlDefaults = std::fabs(scaleIn[0] - 1.0) < ueps && std::fabs(scaleIn[1] - 1.0) < ueps &&
        std::fabs(scaleIn[2] - 1.0) < ueps && std::fabs(rotationDeg[0]) < ueps && std::fabs(rotationDeg[1]) < ueps &&
        std::fabs(rotationDeg[2]) < ueps && std::fabs(position[0]) < ueps && std::fabs(position[1]) < ueps &&
        std::fabs(position[2]) < ueps;

    double h[3];
    if (ReadObbHalfLengths(obbMesh, h) && !prsStillXmlDefaults)
    {
        sx /= std::max(2.0 * h[0], eps);
        sy /= std::max(2.0 * h[1], eps);
        sz /= std::max(2.0 * h[2], eps);
    }

    vtkNew<vtkTransform> tr;
    tr->Identity();
    tr->Translate(position[0], position[1], position[2]);
    tr->RotateZ(rotationDeg[2]);
    tr->RotateX(rotationDeg[0]);
    tr->RotateY(rotationDeg[1]);
    tr->Scale(sx, sy, sz);

    vtkNew<vtkTransformPolyDataFilter> tpf;
    tpf->SetInputData(obbMesh);
    tpf->SetTransform(tr);
    tpf->Update();
    outPd->DeepCopy(tpf->GetOutput());
}

void UpdateSelectionOBBReferenceBoundsFromAabb(vtkSHYXSelectionOBBSubtractFilter* self, vtkPolyData* pd)
{
    if (!self || !pd || pd->GetNumberOfPoints() < 1)
    {
        return;
    }
    double b[6];
    pd->GetBounds(b);
    constexpr double pad = 1e-4;
    for (int i = 0; i < 3; ++i)
    {
        if (b[2 * i + 1] - b[2 * i] < 1e-9)
        {
            b[2 * i] -= pad;
            b[2 * i + 1] += pad;
        }
    }
    self->SetReferenceBounds(b[0], b[1], b[2], b[3], b[4], b[5]);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXSelectionOBBSubtractFilter::vtkSHYXSelectionOBBSubtractFilter()
{
    this->SetNumberOfInputPorts(2);
    this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
vtkSHYXSelectionOBBSubtractFilter::~vtkSHYXSelectionOBBSubtractFilter()
{
    this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionOBBSubtractFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
    this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionOBBSubtractFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "UseSelectionInput: " << this->UseSelectionInput << "\n";
    os << indent << "CopyInputPointsForOBB: " << this->CopyInputPointsForOBB << "\n";
    os << indent << "UpdateAttributes: " << this->UpdateAttributes << "\n";
    os << indent << "SelectionCellArrayName: "
       << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
    os << indent << "Position: (" << this->Position[0] << ", " << this->Position[1] << ", " << this->Position[2]
       << ")\n";
    os << indent << "Rotation (deg): (" << this->Rotation[0] << ", " << this->Rotation[1] << ", "
       << this->Rotation[2] << ")\n";
    os << indent << "Scale: (" << this->Scale[0] << ", " << this->Scale[1] << ", " << this->Scale[2] << ")\n";
    os << indent << "ReferenceBounds: (" << this->ReferenceBounds[0] << ", " << this->ReferenceBounds[1] << ", "
       << this->ReferenceBounds[2] << ", " << this->ReferenceBounds[3] << ", " << this->ReferenceBounds[4] << ", "
       << this->ReferenceBounds[5] << ")\n";
    os << indent << "UseReferenceBounds: " << this->UseReferenceBounds << "\n";
    os << indent << "ObbBaselineValid: " << this->ObbBaselineValid << "\n";
    os << indent << "ObbSelectionFingerprint: " << this->ObbSelectionFingerprint << "\n";
    os << indent << "BaselinePosition: (" << this->BaselinePosition[0] << ", " << this->BaselinePosition[1] << ", "
       << this->BaselinePosition[2] << ")\n";
    os << indent << "BaselineRotation (deg): (" << this->BaselineRotation[0] << ", " << this->BaselineRotation[1]
       << ", " << this->BaselineRotation[2] << ")\n";
    os << indent << "BaselineScale: (" << this->BaselineScale[0] << ", " << this->BaselineScale[1] << ", "
       << this->BaselineScale[2] << ")\n";
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionOBBSubtractFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXSelectionOBBSubtractFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0 || port == 1)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionOBBSubtractFilter::RequestData(
    vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
    vtkPolyData* outDiff = vtkPolyData::GetData(outputVector, 0);
    vtkPolyData* outObb = vtkPolyData::GetData(outputVector, 1);
    if (!mesh || !outDiff || !outObb)
    {
        vtkErrorMacro(<< "Missing mesh or output.");
        return 0;
    }

    vtkNew<vtkPoints> selPts;
    selPts->SetDataType(VTK_DOUBLE);

    const bool useSelPort = this->UseSelectionInput != 0 && this->GetNumberOfInputConnections(1) > 0;
    if (useSelPort)
    {
        vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
        if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
        {
            vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
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
                    AppendAllPointsFromDataset(extracted, selPts);
                    if (selPts->GetNumberOfPoints() == 0)
                    {
                        std::set<vtkIdType> cells;
                        CollectCellsFromExtracted(mesh, extracted, cells);
                        AddVerticesOfCells(mesh, cells, selPts);
                    }
                }
            }
        }
    }

    if (selPts->GetNumberOfPoints() == 0 && this->SelectionCellArrayName &&
        this->SelectionCellArrayName[0] != '\0')
    {
        vtkDataArray* arr = mesh->GetCellData()->GetArray(this->SelectionCellArrayName);
        if (!arr)
        {
            vtkWarningMacro(<< "SelectionCellArrayName \"" << this->SelectionCellArrayName
                              << "\" not found on input cell data.");
        }
        else
        {
            std::set<vtkIdType> selectedCells;
            const vtkIdType nc = mesh->GetNumberOfCells();
            for (vtkIdType cid = 0; cid < nc; ++cid)
            {
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
                    selectedCells.insert(cid);
                }
            }
            AddVerticesOfCells(mesh, selectedCells, selPts);
        }
    }

    if (selPts->GetNumberOfPoints() < 1)
    {
        outDiff->ShallowCopy(mesh);
        outObb->Initialize();
        this->ObbBaselineValid = false;
        this->ObbSelectionFingerprint = 0ULL;
        return 1;
    }

    vtkSmartPointer<vtkPolyData> cloudPd = PointsToVertexPolyData(selPts);
    vtkNew<vtkSHYXMinimumOBBFilter> obbFilter;
    obbFilter->SetInputData(cloudPd);
    obbFilter->SetCopyInputPoints(this->CopyInputPointsForOBB);
    obbFilter->Update();
    vtkPolyData* obbMesh = vtkPolyData::SafeDownCast(obbFilter->GetOutput());
    if (!obbMesh || obbMesh->GetNumberOfPoints() == 0)
    {
        vtkWarningMacro(<< "OBB computation produced empty geometry.");
        outDiff->ShallowCopy(mesh);
        outObb->Initialize();
        this->ObbBaselineValid = false;
        this->ObbSelectionFingerprint = 0ULL;
        return 1;
    }

    const std::uint64_t newFp = HashSelectionPoints(selPts);
    if (newFp != this->ObbSelectionFingerprint)
    {
        this->ObbSelectionFingerprint = newFp;
        if (ComputeBaselinePRSFromObbMesh(
                obbMesh, this->BaselinePosition, this->BaselineRotation, this->BaselineScale))
        {
            this->ObbBaselineValid = true;
        }
        else
        {
            this->ObbBaselineValid = false;
        }
    }

    vtkNew<vtkPolyData> obbTransformed;
    if (this->ObbBaselineValid)
    {
        ApplyObbTransformRelativeToBaseline(obbMesh, this->Position, this->Rotation, this->Scale,
            this->BaselinePosition, this->BaselineRotation, this->BaselineScale, obbTransformed);
    }
    else
    {
        TransformObbWithPRS(obbMesh, this->Position, this->Rotation, this->Scale, obbTransformed);
    }
    outObb->DeepCopy(obbTransformed);

    vtkNew<vtkCGALBooleanOperation> boolOp;
    boolOp->SetInputData(0, mesh);
    boolOp->SetInputData(1, obbTransformed);
    boolOp->SetOperationType(vtkCGALBooleanOperation::DIFFERENCE);
    boolOp->SetUpdateAttributes(this->UpdateAttributes);
    boolOp->Update();
    outDiff->ShallowCopy(boolOp->GetOutput());
    return 1;
}

VTK_ABI_NAMESPACE_END
