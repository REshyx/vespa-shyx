#include "vtkSHYXMinimumOBBFilter.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkOBBTree.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkTriangle.h"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/optimal_bounding_box.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

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

/**
 * CGAL Optimal_bounding_box corner order (same as make_hexahedron):
 *   0:(xmin,ymin,zmin) 1:(xmax,ymin,zmin) 2:(xmax,ymax,zmin) 3:(xmin,ymax,zmin)
 *   4:(xmin,ymax,zmax) 5:(xmin,ymin,zmax) 6:(xmax,ymin,zmax) 7:(xmax,ymax,zmax)
 * Edge vectors from corner 0: e0=p1-p0, e1=p3-p0, e2=p5-p0.
 */
bool BuildMinVolumeObbFromPoints(vtkPolyData* output, vtkPoints* pts)
{
    if (!output || !pts)
    {
        return false;
    }
    const vtkIdType n = pts->GetNumberOfPoints();
    // CGAL::oriented_bounding_box requires at least 4 points.
    if (n < 4)
    {
        return false;
    }

    using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Point = Kernel::Point_3;

    std::vector<Point> cgalPts;
    cgalPts.reserve(static_cast<std::size_t>(n));
    double x[3];
    for (vtkIdType i = 0; i < n; ++i)
    {
        pts->GetPoint(i, x);
        cgalPts.emplace_back(x[0], x[1], x[2]);
    }

    std::array<Point, 8> obbCorners;
    try
    {
        CGAL::oriented_bounding_box(cgalPts, obbCorners, CGAL::parameters::use_convex_hull(true));
    }
    catch (...)
    {
        return false;
    }

    double corner[3] = { CGAL::to_double(obbCorners[0].x()), CGAL::to_double(obbCorners[0].y()),
        CGAL::to_double(obbCorners[0].z()) };
    double e0[3] = { CGAL::to_double(obbCorners[1].x()) - corner[0],
        CGAL::to_double(obbCorners[1].y()) - corner[1], CGAL::to_double(obbCorners[1].z()) - corner[2] };
    double e1[3] = { CGAL::to_double(obbCorners[3].x()) - corner[0],
        CGAL::to_double(obbCorners[3].y()) - corner[1], CGAL::to_double(obbCorners[3].z()) - corner[2] };
    double e2[3] = { CGAL::to_double(obbCorners[5].x()) - corner[0],
        CGAL::to_double(obbCorners[5].y()) - corner[1], CGAL::to_double(obbCorners[5].z()) - corner[2] };

    const double n0 = vtkMath::Norm(e0);
    const double n1 = vtkMath::Norm(e1);
    const double n2 = vtkMath::Norm(e2);
    if (n0 < DegenerateEps || n1 < DegenerateEps || n2 < DegenerateEps)
    {
        return false;
    }

    // EigenvalueSizes unused for CGAL path; store edge lengths for reference.
    const double sizeEigen[3] = { n0, n1, n2 };
    BuildObbTriangleMesh(output, corner, e0, e1, e2, sizeEigen);
    return true;
}

double ReadVolumeField(vtkPolyData* pd)
{
    if (!pd)
    {
        return -1.0;
    }
    vtkFieldData* fd = pd->GetFieldData();
    if (!fd)
    {
        return -1.0;
    }
    auto* arr = vtkDataArray::SafeDownCast(fd->GetAbstractArray("OBB.Volume"));
    if (!arr || arr->GetNumberOfTuples() < 1)
    {
        return -1.0;
    }
    return arr->GetTuple1(0);
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

std::uint64_t HashObbField(const double C[3], const double h[3], const double u0[3], const double u1[3],
    const double u2[3])
{
    std::uint64_t x = 14695981039346656037ULL;
    auto mix = [&](double d) {
        x ^= static_cast<std::uint64_t>(std::llround(d * 1e6));
        x *= 1099511628211ULL;
    };
    for (int i = 0; i < 3; ++i)
    {
        mix(C[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        mix(h[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        mix(u0[i]);
        mix(u1[i]);
        mix(u2[i]);
    }
    return x == 0 ? 1ULL : x;
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
    pos[0] = C[0] - h[0] * u0[0] - h[1] * u1[0] - h[2] * u2[0];
    pos[1] = C[1] - h[0] * u0[1] - h[1] * u1[1] - h[2] * u2[1];
    pos[2] = C[2] - h[0] * u0[2] - h[1] * u1[2] - h[2] * u2[2];
    return true;
}

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
    os << indent << "BoxType: " << this->BoxType << "\n";
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
    os << indent << "ObbFieldFingerprint: " << this->ObbFieldFingerprint << "\n";
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
        this->ObbBaselineValid = false;
        this->ObbFieldFingerprint = 0ULL;
        this->ObbFitJustChanged = false;
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
        this->ObbBaselineValid = false;
        this->ObbFieldFingerprint = 0ULL;
        this->ObbFitJustChanged = false;
        return 1;
    }

    vtkNew<vtkPolyData> rawObb;
    double b[6];
    ptsForObb->GetBounds(b);
    const double span =
        std::fabs(b[1] - b[0]) + std::fabs(b[3] - b[2]) + std::fabs(b[5] - b[4]);
    const double aabbVolume = std::max(0.0, (b[1] - b[0]) * (b[3] - b[2]) * (b[5] - b[4]));
    const bool forceAabb = (this->BoxType == BOX_TYPE_AABB) || (span < DegenerateEps);

    if (forceAabb)
    {
        BuildWorldAabbBox(rawObb, ptsForObb);
    }
    else if (this->BoxType == BOX_TYPE_OBB_MIN_VOLUME)
    {
        const bool ok = BuildMinVolumeObbFromPoints(rawObb, ptsForObb);
        const double vol = ok ? ReadVolumeField(rawObb) : -1.0;
        // Evolutionary min-volume is approximate; never accept a box larger than the world AABB.
        if (!ok || vol < 0.0 || (aabbVolume > DegenerateEps && vol > aabbVolume * (1.0 + 1e-6)))
        {
            BuildWorldAabbBox(rawObb, ptsForObb);
        }
    }
    else // BOX_TYPE_OBB_PCA
    {
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
            BuildWorldAabbBox(rawObb, ptsForObb);
        }
        else
        {
            BuildObbTriangleMesh(rawObb, corner, emax, emid, emin, sizeEigen);
        }
    }

    double C[3], h[3], u0[3], u1[3], u2[3];
    if (ReadObbField(rawObb, C, h, u0, u1, u2))
    {
        // Mix BoxType so OBB <-> AABB switches always refresh the Interactive-Box baseline.
        std::uint64_t fp = HashObbField(C, h, u0, u1, u2);
        fp ^= (static_cast<std::uint64_t>(this->BoxType) + 1ULL) * 0x9e3779b97f4a7c15ULL;
        if (fp != this->ObbFieldFingerprint)
        {
            this->ObbFieldFingerprint = fp;
            this->ObbFitJustChanged = true;
            if (ComputeBaselinePRSFromObbMesh(
                    rawObb, this->BaselinePosition, this->BaselineRotation, this->BaselineScale))
            {
                this->ObbBaselineValid = true;
            }
            else
            {
                this->ObbBaselineValid = false;
            }
        }
    }
    else
    {
        this->ObbBaselineValid = false;
        this->ObbFieldFingerprint = 0ULL;
        this->ObbFitJustChanged = false;
    }

    auto prsNearlyEqual = [](const double a[3], const double b[3], double tol) {
        return std::fabs(a[0] - b[0]) < tol && std::fabs(a[1] - b[1]) < tol && std::fabs(a[2] - b[2]) < tol;
    };

    // After BoxType/input fit changes, SM may still hold the previous box's PRS for one Apply.
    // Using relative transform then warps the new mesh into the old pose (one-step lag). Output the
    // raw fitted box until the interactive PRS has been re-placed to the new baseline.
    if (this->ObbFitJustChanged)
    {
        constexpr double tol = 1e-4;
        const bool prsMatchesBaseline = this->ObbBaselineValid &&
            prsNearlyEqual(this->Position, this->BaselinePosition, tol) &&
            prsNearlyEqual(this->Rotation, this->BaselineRotation, tol) &&
            prsNearlyEqual(this->Scale, this->BaselineScale, tol);
        constexpr double ueps = 1e-5;
        const bool prsLooksLikeXmlDefaults = std::fabs(this->Scale[0] - 1.0) < ueps &&
            std::fabs(this->Scale[1] - 1.0) < ueps && std::fabs(this->Scale[2] - 1.0) < ueps &&
            std::fabs(this->Rotation[0]) < ueps && std::fabs(this->Rotation[1]) < ueps &&
            std::fabs(this->Rotation[2]) < ueps && std::fabs(this->Position[0]) < ueps &&
            std::fabs(this->Position[1]) < ueps && std::fabs(this->Position[2]) < ueps;

        if (prsMatchesBaseline || prsLooksLikeXmlDefaults)
        {
            this->ObbFitJustChanged = false;
        }
        else
        {
            output->DeepCopy(rawObb);
            return 1;
        }
    }

    if (this->ObbBaselineValid)
    {
        ApplyObbTransformRelativeToBaseline(rawObb, this->Position, this->Rotation, this->Scale,
            this->BaselinePosition, this->BaselineRotation, this->BaselineScale, output);
    }
    else
    {
        TransformObbWithPRS(rawObb, this->Position, this->Rotation, this->Scale, output);
    }
    return 1;
}

VTK_ABI_NAMESPACE_END
