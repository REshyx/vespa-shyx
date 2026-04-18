#include "vtkSHYXMinimumOBBFilter.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkOBBTree.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkTriangle.h"

#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXMinimumOBBFilter);

namespace
{
const double DegenerateEps = 1e-12;

void AppendTuple3(vtkFieldData* fd, const char* name, const double v[3])
{
    vtkNew<vtkDoubleArray> arr;
    arr->SetName(name);
    arr->SetNumberOfComponents(3);
    arr->SetNumberOfTuples(1);
    arr->SetTuple(0, v);
    fd->AddArray(arr);
}

void AppendTuple1(vtkFieldData* fd, const char* name, double v)
{
    vtkNew<vtkDoubleArray> arr;
    arr->SetName(name);
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(1);
    arr->SetValue(0, v);
    fd->AddArray(arr);
}

/** Axis-aligned box from point bounds; pads zero-thickness dimensions slightly. */
void BuildWorldAabbBox(vtkPolyData* output, vtkPoints* pts)
{
    double b[6];
    pts->GetBounds(b);
    double dx = b[1] - b[0];
    double dy = b[3] - b[2];
    double dz = b[5] - b[4];
    const double pad = DegenerateEps;
    if (dx < pad)
    {
        b[0] -= pad;
        b[1] += pad;
    }
    if (dy < pad)
    {
        b[2] -= pad;
        b[3] += pad;
    }
    if (dz < pad)
    {
        b[4] -= pad;
        b[5] += pad;
    }

    vtkNew<vtkPoints> corners;
    corners->SetNumberOfPoints(8);
    corners->SetPoint(0, b[0], b[2], b[4]);
    corners->SetPoint(1, b[1], b[2], b[4]);
    corners->SetPoint(2, b[0], b[3], b[4]);
    corners->SetPoint(3, b[1], b[3], b[4]);
    corners->SetPoint(4, b[0], b[2], b[5]);
    corners->SetPoint(5, b[1], b[2], b[5]);
    corners->SetPoint(6, b[0], b[3], b[5]);
    corners->SetPoint(7, b[1], b[3], b[5]);

    vtkNew<vtkCellArray> polys;
    const int faces[6][4] = {
        { 0, 1, 3, 2 },
        { 4, 6, 7, 5 },
        { 0, 4, 5, 1 },
        { 2, 3, 7, 6 },
        { 0, 2, 6, 4 },
        { 1, 5, 7, 3 },
    };
    for (int f = 0; f < 6; ++f)
    {
        vtkIdType a = faces[f][0];
        vtkIdType b0 = faces[f][1];
        vtkIdType c = faces[f][2];
        vtkIdType d0 = faces[f][3];
        vtkIdType tri0[3] = { a, b0, c };
        vtkIdType tri1[3] = { a, c, d0 };
        polys->InsertNextCell(3, tri0);
        polys->InsertNextCell(3, tri1);
    }

    output->SetPoints(corners);
    output->SetPolys(polys);
    output->GetPointData()->CopyAllOff();
    output->GetCellData()->CopyAllOff();

    double center[3] = { 0.5 * (b[0] + b[1]), 0.5 * (b[2] + b[3]), 0.5 * (b[4] + b[5]) };
    double half[3] = { 0.5 * (b[1] - b[0]), 0.5 * (b[3] - b[2]), 0.5 * (b[5] - b[4]) };
    double ax0[3] = { 1, 0, 0 };
    double ax1[3] = { 0, 1, 0 };
    double ax2[3] = { 0, 0, 1 };
    vtkFieldData* fd = output->GetFieldData();
    fd->Initialize();
    AppendTuple3(fd, "OBB.Center", center);
    AppendTuple3(fd, "OBB.HalfLengths", half);
    AppendTuple3(fd, "OBB.Axis0", ax0);
    AppendTuple3(fd, "OBB.Axis1", ax1);
    AppendTuple3(fd, "OBB.Axis2", ax2);
    AppendTuple1(fd, "OBB.Volume", 8.0 * half[0] * half[1] * half[2]);
    AppendTuple1(fd, "OBB.IsAxisAlignedFallback", 1.0);
}

/**
 * corner + t0*e0 + t1*e1 + t2*e2, ti in {0,1}. Index i = t0 + 2*t1 + 4*t2.
 * e0,e1,e2 are full edge vectors (not half).
 */
void BuildObbTriangleMesh(vtkPolyData* output, const double corner[3], const double e0[3], const double e1[3],
    const double e2[3], const double sizeEigen[3])
{
    double ctr[3];
    for (int d = 0; d < 3; ++d)
    {
        ctr[d] = corner[d] + 0.5 * (e0[d] + e1[d] + e2[d]);
    }

    vtkNew<vtkPoints> corners;
    corners->SetNumberOfPoints(8);
    for (int i = 0; i < 8; ++i)
    {
        const double t0 = (i & 1) ? 1.0 : 0.0;
        const double t1 = (i & 2) ? 1.0 : 0.0;
        const double t2 = (i & 4) ? 1.0 : 0.0;
        double p[3];
        for (int d = 0; d < 3; ++d)
        {
            p[d] = corner[d] + t0 * e0[d] + t1 * e1[d] + t2 * e2[d];
        }
        corners->SetPoint(i, p);
    }

    const int faces[6][4] = {
        { 0, 1, 3, 2 },
        { 4, 6, 7, 5 },
        { 0, 4, 5, 1 },
        { 2, 3, 7, 6 },
        { 0, 2, 6, 4 },
        { 1, 5, 7, 3 },
    };

    vtkNew<vtkCellArray> polys;
    for (int f = 0; f < 6; ++f)
    {
        vtkIdType id0 = faces[f][0];
        vtkIdType id1 = faces[f][1];
        vtkIdType id2 = faces[f][2];
        vtkIdType id3 = faces[f][3];
        double p0[3], p1[3], p2[3], p3[3];
        corners->GetPoint(id0, p0);
        corners->GetPoint(id1, p1);
        corners->GetPoint(id2, p2);
        corners->GetPoint(id3, p3);

        double n[3];
        vtkTriangle::ComputeNormal(p0, p1, p2, n);
        const double qc[3] = { 0.25 * (p0[0] + p1[0] + p2[0] + p3[0]), 0.25 * (p0[1] + p1[1] + p2[1] + p3[1]),
            0.25 * (p0[2] + p1[2] + p2[2] + p3[2]) };
        const double toC[3] = { qc[0] - ctr[0], qc[1] - ctr[1], qc[2] - ctr[2] };
        vtkIdType a = id0;
        vtkIdType bq = id1;
        vtkIdType c = id2;
        vtkIdType dq = id3;
        if (vtkMath::Dot(n, toC) < 0.0)
        {
            bq = id3;
            dq = id1;
        }
        vtkIdType tri0[3] = { a, bq, c };
        vtkIdType tri1[3] = { a, c, dq };
        polys->InsertNextCell(3, tri0);
        polys->InsertNextCell(3, tri1);
    }

    output->SetPoints(corners);
    output->SetPolys(polys);
    output->GetPointData()->CopyAllOff();
    output->GetCellData()->CopyAllOff();

    const double l0 = vtkMath::Norm(e0);
    const double l1 = vtkMath::Norm(e1);
    const double l2 = vtkMath::Norm(e2);
    double u0[3] = { e0[0], e0[1], e0[2] };
    double u1[3] = { e1[0], e1[1], e1[2] };
    double u2[3] = { e2[0], e2[1], e2[2] };
    if (l0 > DegenerateEps)
    {
        vtkMath::Normalize(u0);
    }
    if (l1 > DegenerateEps)
    {
        vtkMath::Normalize(u1);
    }
    if (l2 > DegenerateEps)
    {
        vtkMath::Normalize(u2);
    }
    double half[3] = { 0.5 * l0, 0.5 * l1, 0.5 * l2 };
    vtkFieldData* fd = output->GetFieldData();
    fd->Initialize();
    AppendTuple3(fd, "OBB.Center", ctr);
    AppendTuple3(fd, "OBB.HalfLengths", half);
    AppendTuple3(fd, "OBB.Axis0", u0);
    AppendTuple3(fd, "OBB.Axis1", u1);
    AppendTuple3(fd, "OBB.Axis2", u2);
    AppendTuple3(fd, "OBB.EigenvalueSizes", sizeEigen);
    AppendTuple1(fd, "OBB.Volume", l0 * l1 * l2);
    AppendTuple1(fd, "OBB.IsAxisAlignedFallback", 0.0);
}
} // namespace

//------------------------------------------------------------------------------

vtkSHYXMinimumOBBFilter::vtkSHYXMinimumOBBFilter()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

void vtkSHYXMinimumOBBFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "CopyInputPoints: " << this->CopyInputPoints << "\n";
}

int vtkSHYXMinimumOBBFilter::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

int vtkSHYXMinimumOBBFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

int vtkSHYXMinimumOBBFilter::RequestData(
    vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
    if (!input || !output)
    {
        vtkErrorMacro(<< "Null input or output.");
        return 0;
    }

    const vtkIdType nPts = input->GetNumberOfPoints();
    if (nPts < 1)
    {
        vtkWarningMacro(<< "Input has no points; output cleared.");
        output->Initialize();
        return 1;
    }

    vtkPoints* ptsForObb = nullptr;
    vtkNew<vtkPoints> ownedPts;
    vtkPointSet* ps = vtkPointSet::SafeDownCast(input);
    if (ps && ps->GetPoints() && !this->CopyInputPoints)
    {
        ptsForObb = ps->GetPoints();
    }
    else
    {
        ownedPts->SetDataType(VTK_DOUBLE);
        ownedPts->Allocate(nPts);
        double x[3];
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            input->GetPoint(i, x);
            ownedPts->InsertNextPoint(x);
        }
        ptsForObb = ownedPts;
    }

    if (ptsForObb->GetNumberOfPoints() < 1)
    {
        vtkWarningMacro(<< "No points collected; output cleared.");
        output->Initialize();
        return 1;
    }

    double b[6];
    ptsForObb->GetBounds(b);
    const double span =
        std::fabs(b[1] - b[0]) + std::fabs(b[3] - b[2]) + std::fabs(b[5] - b[4]);
    if (span < DegenerateEps)
    {
        BuildWorldAabbBox(output, ptsForObb);
        return 1;
    }

    double corner[3];
    double emax[3];
    double emid[3];
    double emin[3];
    double sizeEigen[3];
    vtkOBBTree::ComputeOBB(ptsForObb, corner, emax, emid, emin, sizeEigen);

    const double nmax = vtkMath::Norm(emax);
    const double nmid = vtkMath::Norm(emid);
    const double nmin = vtkMath::Norm(emin);
    if (nmax < DegenerateEps || nmid < DegenerateEps || nmin < DegenerateEps)
    {
        BuildWorldAabbBox(output, ptsForObb);
        return 1;
    }

    BuildObbTriangleMesh(output, corner, emax, emid, emin, sizeEigen);
    return 1;
}

VTK_ABI_NAMESPACE_END
