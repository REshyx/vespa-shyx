#include "vtkSHYXEndpointStentRepresentation.h"

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkWindow.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXEndpointStentRepresentation);

namespace
{
constexpr double kEps = 1e-12;

void Normalize3(double v[3])
{
    if (vtkMath::Normalize(v) < kEps)
    {
        v[0] = 0.0;
        v[1] = 0.0;
        v[2] = 1.0;
    }
}

void CrossUnit(const double a[3], const double b[3], double out[3])
{
    vtkMath::Cross(a, b, out);
    Normalize3(out);
}

void BuildPerpFrame(const double tangent[3], double n1[3], double n2[3])
{
    double t[3] = { tangent[0], tangent[1], tangent[2] };
    Normalize3(t);

    double ref[3] = { 1.0, 0.0, 0.0 };
    if (std::fabs(vtkMath::Dot(t, ref)) > 0.9)
    {
        ref[0] = 0.0;
        ref[1] = 1.0;
        ref[2] = 0.0;
    }
    CrossUnit(t, ref, n1);
    CrossUnit(t, n1, n2);
}

/** Keep circumferential parameterization aligned along a curved path (avoid funnel twists). */
bool ParallelTransportFrame(const double prevTangent[3], const double prevN1[3], const double newTangent[3],
    double newN1[3], double newN2[3])
{
    double t[3] = { newTangent[0], newTangent[1], newTangent[2] };
    Normalize3(t);

    double n1[3];
    const double axial = vtkMath::Dot(prevN1, t);
    for (int i = 0; i < 3; ++i)
    {
        n1[i] = prevN1[i] - axial * t[i];
    }
    if (vtkMath::Norm(n1) < kEps)
    {
        double fallback[3];
        const double axial2 = vtkMath::Dot(prevTangent, t);
        for (int i = 0; i < 3; ++i)
        {
            fallback[i] = prevTangent[i] - axial2 * t[i];
        }
        if (vtkMath::Norm(fallback) < kEps)
        {
            BuildPerpFrame(t, newN1, newN2);
            return true;
        }
        for (int i = 0; i < 3; ++i)
        {
            n1[i] = fallback[i];
        }
    }
    Normalize3(n1);
    vtkMath::Cross(t, n1, newN2);
    Normalize3(newN2);
    for (int i = 0; i < 3; ++i)
    {
        newN1[i] = n1[i];
    }
    return true;
}

bool InterpolatePathAtArcLength(vtkPoints* pts, const std::vector<double>& cumLen, double s,
    double pos[3], double tangent[3])
{
    if (!pts || cumLen.size() < 2 || pts->GetNumberOfPoints() < 2)
    {
        return false;
    }
    const double total = cumLen.back();
    if (total <= kEps)
    {
        return false;
    }
    s = std::clamp(s, 0.0, total);

    size_t seg = 0;
    while (seg + 1 < cumLen.size() && cumLen[seg + 1] < s - kEps)
    {
        ++seg;
    }
    const double s0 = cumLen[seg];
    const double s1 = cumLen[seg + 1];
    const double t = (s1 > s0 + kEps) ? (s - s0) / (s1 - s0) : 0.0;

    double p0[3], p1[3];
    pts->GetPoint(static_cast<vtkIdType>(seg), p0);
    pts->GetPoint(static_cast<vtkIdType>(seg + 1), p1);
    for (int i = 0; i < 3; ++i)
    {
        pos[i] = p0[i] + t * (p1[i] - p0[i]);
        tangent[i] = p1[i] - p0[i];
    }
    Normalize3(tangent);
    return true;
}

bool BuildStentRings(vtkPoints* pathPts, double radius, int circRes, int numRings,
    std::vector<std::vector<std::array<double, 3>>>& rings)
{
    rings.clear();
    if (!pathPts || pathPts->GetNumberOfPoints() < 2 || radius <= kEps || circRes < 4 || numRings < 2)
    {
        return false;
    }

    const vtkIdType n = pathPts->GetNumberOfPoints();
    std::vector<double> cumLen(static_cast<size_t>(n), 0.0);
    for (vtkIdType i = 1; i < n; ++i)
    {
        double a[3], b[3];
        pathPts->GetPoint(i - 1, a);
        pathPts->GetPoint(i, b);
        cumLen[static_cast<size_t>(i)] = cumLen[static_cast<size_t>(i - 1)] + std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
    }
    const double totalLen = cumLen.back();
    if (totalLen <= kEps)
    {
        return false;
    }

    rings.resize(static_cast<size_t>(numRings));
    double prevTangent[3] = { 0.0, 0.0, 1.0 };
    double prevN1[3] = { 1.0, 0.0, 0.0 };
    bool haveFrame = false;

    for (int ring = 0; ring < numRings; ++ring)
    {
        const double s = (numRings == 1) ? 0.0 : (static_cast<double>(ring) / static_cast<double>(numRings - 1)) * totalLen;
        double center[3], tangent[3];
        if (!InterpolatePathAtArcLength(pathPts, cumLen, s, center, tangent))
        {
            return false;
        }

        double n1[3], n2[3];
        if (!haveFrame)
        {
            BuildPerpFrame(tangent, n1, n2);
            haveFrame = true;
        }
        else
        {
            ParallelTransportFrame(prevTangent, prevN1, tangent, n1, n2);
        }
        for (int i = 0; i < 3; ++i)
        {
            prevTangent[i] = tangent[i];
            prevN1[i] = n1[i];
        }

        // Odd rings are rotated by half a circumferential cell so longitudinal links form diamonds.
        const double phase = (ring % 2 == 1) ? vtkMath::Pi() / static_cast<double>(circRes) : 0.0;
        rings[static_cast<size_t>(ring)].resize(static_cast<size_t>(circRes));
        for (int j = 0; j < circRes; ++j)
        {
            const double theta =
                phase + (static_cast<double>(j) / static_cast<double>(circRes)) * 2.0 * vtkMath::Pi();
            const double c = std::cos(theta);
            const double sn = std::sin(theta);
            rings[static_cast<size_t>(ring)][static_cast<size_t>(j)] = {
                center[0] + radius * (n1[0] * c + n2[0] * sn),
                center[1] + radius * (n1[1] * c + n2[1] * sn),
                center[2] + radius * (n1[2] * c + n2[2] * sn),
            };
        }
    }
    return true;
}

void InsertLine(vtkCellArray* lines, vtkIdType a, vtkIdType b)
{
    vtkIdType ids[2] = { a, b };
    lines->InsertNextCell(2, ids);
}

void InsertQuad(vtkCellArray* polys, vtkIdType a, vtkIdType b, vtkIdType c, vtkIdType d)
{
    vtkIdType ids[4] = { a, b, c, d };
    polys->InsertNextCell(4, ids);
}

} // namespace

//-----------------------------------------------------------------------------
vtkSHYXEndpointStentRepresentation::vtkSHYXEndpointStentRepresentation()
{
    this->PathPoly = vtkPolyData::New();
    this->WirePoly = vtkPolyData::New();
    this->ShellPoly = vtkPolyData::New();

    this->WireMapper = vtkPolyDataMapper::New();
    this->WireMapper->SetInputData(this->WirePoly);

    this->ShellMapper = vtkPolyDataMapper::New();
    this->ShellMapper->SetInputData(this->ShellPoly);

    this->WireActor = vtkActor::New();
    this->WireActor->SetMapper(this->WireMapper);
    vtkProperty* wireProp = this->WireActor->GetProperty();
    wireProp->SetColor(0.88, 0.72, 0.18);
    wireProp->SetOpacity(1.0);
    wireProp->SetLineWidth(2.5);
    wireProp->SetAmbient(0.35);
    wireProp->SetDiffuse(0.65);
    wireProp->SetSpecular(0.4);
    wireProp->SetSpecularPower(20.0);

    this->ShellActor = vtkActor::New();
    this->ShellActor->SetMapper(this->ShellMapper);
    vtkProperty* shellProp = this->ShellActor->GetProperty();
    shellProp->SetColor(0.92, 0.94, 0.98);
    shellProp->SetOpacity(0.20);
    shellProp->SetAmbient(0.45);
    shellProp->SetDiffuse(0.55);
    shellProp->SetSpecular(0.25);
}

//-----------------------------------------------------------------------------
vtkSHYXEndpointStentRepresentation::~vtkSHYXEndpointStentRepresentation()
{
    this->WireActor->Delete();
    this->ShellActor->Delete();
    this->WireMapper->Delete();
    this->ShellMapper->Delete();
    this->WirePoly->Delete();
    this->ShellPoly->Delete();
    this->PathPoly->Delete();
}

//-----------------------------------------------------------------------------
void vtkSHYXEndpointStentRepresentation::SetStentPathPolyData(vtkPolyData* path)
{
    this->PathPoly->Reset();
    if (path && path->GetPoints() && path->GetPoints()->GetNumberOfPoints() >= 2)
    {
        this->PathPoly->DeepCopy(path);
    }
    this->Modified();
}

//-----------------------------------------------------------------------------
void vtkSHYXEndpointStentRepresentation::BuildStentPreviewMesh()
{
    this->WirePoly->Reset();
    this->ShellPoly->Reset();

    if (!this->ShowStentPreview || this->StentRadius <= kEps)
    {
        return;
    }

    vtkPoints* pathPts = this->PathPoly->GetPoints();
    if (!pathPts || pathPts->GetNumberOfPoints() < 2)
    {
        return;
    }

    double totalLen = 0.0;
    for (vtkIdType i = 1; i < pathPts->GetNumberOfPoints(); ++i)
    {
        double a[3], b[3];
        pathPts->GetPoint(i - 1, a);
        pathPts->GetPoint(i, b);
        totalLen += std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
    }
    if (totalLen <= kEps)
    {
        return;
    }

    const int circRes = 20;
    const double ringSpacing = std::clamp(
        std::max(this->StentRadius * 1.25, totalLen / 24.0), this->StentRadius * 0.75, totalLen * 0.35);
    const int numRings = std::clamp(static_cast<int>(std::lround(totalLen / ringSpacing)) + 1, 4, 48);

    std::vector<std::vector<std::array<double, 3>>> rings;
    if (!BuildStentRings(pathPts, this->StentRadius, circRes, numRings, rings))
    {
        return;
    }

    vtkNew<vtkPoints> wirePts;
    vtkNew<vtkCellArray> wireLines;
    vtkNew<vtkPoints> shellPts;
    vtkNew<vtkCellArray> shellPolys;

    const vtkIdType ringStride = circRes;
    for (int ring = 0; ring < numRings; ++ring)
    {
        vtkIdType base = wirePts->GetNumberOfPoints();
        for (int j = 0; j < circRes; ++j)
        {
            const auto& p = rings[static_cast<size_t>(ring)][static_cast<size_t>(j)];
            wirePts->InsertNextPoint(p[0], p[1], p[2]);
            shellPts->InsertNextPoint(p[0], p[1], p[2]);
            const int jn = (j + 1) % circRes;
            InsertLine(wireLines, base + j, base + jn);
        }
    }

    for (int ring = 0; ring < numRings - 1; ++ring)
    {
        const vtkIdType base0 = static_cast<vtkIdType>(ring) * ringStride;
        const vtkIdType base1 = static_cast<vtkIdType>(ring + 1) * ringStride;
        for (int j = 0; j < circRes; ++j)
        {
            const int jn = (j + 1) % circRes;
            InsertLine(wireLines, base0 + j, base1 + j);
            InsertQuad(shellPolys, base0 + j, base0 + jn, base1 + jn, base1 + j);
        }
    }

    this->WirePoly->SetPoints(wirePts);
    this->WirePoly->SetLines(wireLines);
    this->ShellPoly->SetPoints(shellPts);
    this->ShellPoly->SetPolys(shellPolys);
}

//-----------------------------------------------------------------------------
void vtkSHYXEndpointStentRepresentation::BuildRepresentation()
{
    this->Superclass::BuildRepresentation();
    this->BuildStentPreviewMesh();
    this->WirePoly->Modified();
    this->ShellPoly->Modified();
}

//-----------------------------------------------------------------------------
void vtkSHYXEndpointStentRepresentation::ReleaseGraphicsResources(vtkWindow* w)
{
    this->Superclass::ReleaseGraphicsResources(w);
    this->WireActor->ReleaseGraphicsResources(w);
    this->ShellActor->ReleaseGraphicsResources(w);
}

//-----------------------------------------------------------------------------
int vtkSHYXEndpointStentRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
    int count = this->Superclass::RenderOpaqueGeometry(viewport);
    if (!this->ShowStentPreview || !this->GetVisibility())
    {
        return count;
    }

    this->BuildRepresentation();
    vtkInformation* info = this->GetPropertyKeys();
    this->WireActor->SetPropertyKeys(info);
    this->ShellActor->SetPropertyKeys(info);
    count += this->WireActor->RenderOpaqueGeometry(viewport);
    return count;
}

//-----------------------------------------------------------------------------
int vtkSHYXEndpointStentRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
    int count = this->Superclass::RenderTranslucentPolygonalGeometry(viewport);
    if (!this->ShowStentPreview || !this->GetVisibility())
    {
        return count;
    }

    this->BuildRepresentation();
    vtkInformation* info = this->GetPropertyKeys();
    this->ShellActor->SetPropertyKeys(info);
    count += this->ShellActor->RenderTranslucentPolygonalGeometry(viewport);
    return count;
}

//-----------------------------------------------------------------------------
double* vtkSHYXEndpointStentRepresentation::GetBounds()
{
    this->BuildRepresentation();
    if (this->WirePoly->GetNumberOfPoints() > 0)
    {
        return this->WirePoly->GetBounds();
    }
    return this->Superclass::GetBounds();
}

//-----------------------------------------------------------------------------
void vtkSHYXEndpointStentRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "StentRadius: " << this->StentRadius << "\n";
    os << indent << "ShowStentPreview: " << this->ShowStentPreview << "\n";
}

VTK_ABI_NAMESPACE_END
