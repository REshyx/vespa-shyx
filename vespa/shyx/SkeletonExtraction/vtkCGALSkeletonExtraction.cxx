#include "vtkCGALSkeletonExtraction.h"

// VESPA related includes
#include "vtkCGALHelper.h"

// VTK related includes
#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkLine.h>
#include <vtkMath.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkTriangle.h>

// CGAL related includes
#include <CGAL/Mean_curvature_flow_skeletonization.h>

#include <CGAL/number_utils.h>

#include <array>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

vtkStandardNewMacro(vtkCGALSkeletonExtraction);

using Skeletonization = CGAL::Mean_curvature_flow_skeletonization<CGAL_Surface>;
using Skeleton        = Skeletonization::Skeleton;
using Skeleton_vertex = Skeleton::vertex_descriptor;
using Skeleton_edge   = Skeleton::edge_descriptor;

namespace
{
constexpr char kNewCapBranchArrayName[] = "NewCapBranch";

struct ClosestHit
{
    vtkIdType cellId        = -1;
    vtkIdType pt0           = -1;
    vtkIdType pt1           = -1;
    vtkIdType segmentIndex  = 0;
    double    closest[3]    = { 0.0, 0.0, 0.0 };
    double    dist2         = std::numeric_limits<double>::infinity();
};

bool CellCentroidAndArea(vtkPolyData* pd, vtkIdType cid, double c[3], double& area)
{
    vtkIdType        npts = 0;
    const vtkIdType* pts  = nullptr;
    pd->GetCellPoints(cid, npts, pts);
    if (npts < 3 || !pts)
    {
        return false;
    }

    double       acc[3]  = { 0.0, 0.0, 0.0 };
    double       areaSum = 0.0;
    double       p0[3];
    pd->GetPoint(pts[0], p0);
    for (vtkIdType i = 1; i + 1 < npts; ++i)
    {
        double p1[3];
        double p2[3];
        pd->GetPoint(pts[i], p1);
        pd->GetPoint(pts[i + 1], p2);
        const double a = vtkTriangle::TriangleArea(p0, p1, p2);
        if (a <= 0.0)
        {
            continue;
        }
        acc[0] += a * (p0[0] + p1[0] + p2[0]) / 3.0;
        acc[1] += a * (p0[1] + p1[1] + p2[1]) / 3.0;
        acc[2] += a * (p0[2] + p1[2] + p2[2]) / 3.0;
        areaSum += a;
    }
    if (areaSum <= 0.0)
    {
        return false;
    }
    c[0] = acc[0] / areaSum;
    c[1] = acc[1] / areaSum;
    c[2] = acc[2] / areaSum;
    area = areaSum;
    return true;
}

void CollectCapPatchCentroids(
    vtkPolyData* surface, vtkDataArray* ep, std::vector<std::array<double, 3>>& centroids)
{
    const vtkIdType nCells = surface->GetNumberOfCells();
    const vtkIdType nPts   = surface->GetNumberOfPoints();
    surface->BuildCells();

    std::vector<char>                  isCap(static_cast<size_t>(nCells), 0);
    std::vector<std::vector<vtkIdType>> adj(static_cast<size_t>(nPts));
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        if (ep->GetComponent(cid, 0) <= 0.0)
        {
            continue;
        }
        isCap[static_cast<size_t>(cid)] = 1;
        vtkIdType        npts           = 0;
        const vtkIdType* pts            = nullptr;
        surface->GetCellPoints(cid, npts, pts);
        for (vtkIdType i = 0; i < npts; ++i)
        {
            adj[static_cast<size_t>(pts[i])].push_back(cid);
        }
    }

    std::vector<char>     visited(static_cast<size_t>(nCells), 0);
    std::vector<vtkIdType> stack;
    stack.reserve(64);

    for (vtkIdType seed = 0; seed < nCells; ++seed)
    {
        if (!isCap[static_cast<size_t>(seed)] || visited[static_cast<size_t>(seed)])
        {
            continue;
        }
        stack.clear();
        stack.push_back(seed);
        visited[static_cast<size_t>(seed)] = 1;
        std::vector<vtkIdType> patch;
        while (!stack.empty())
        {
            const vtkIdType cid = stack.back();
            stack.pop_back();
            patch.push_back(cid);
            vtkIdType        npts = 0;
            const vtkIdType* pts  = nullptr;
            surface->GetCellPoints(cid, npts, pts);
            for (vtkIdType i = 0; i < npts; ++i)
            {
                const auto& nbrs = adj[static_cast<size_t>(pts[i])];
                for (vtkIdType nb : nbrs)
                {
                    if (!visited[static_cast<size_t>(nb)])
                    {
                        visited[static_cast<size_t>(nb)] = 1;
                        stack.push_back(nb);
                    }
                }
            }
        }

        double acc[3]  = { 0.0, 0.0, 0.0 };
        double areaSum = 0.0;
        for (vtkIdType cid : patch)
        {
            double c[3]  = { 0.0, 0.0, 0.0 };
            double area  = 0.0;
            if (!CellCentroidAndArea(surface, cid, c, area))
            {
                continue;
            }
            acc[0] += area * c[0];
            acc[1] += area * c[1];
            acc[2] += area * c[2];
            areaSum += area;
        }
        if (areaSum <= 0.0)
        {
            continue;
        }
        centroids.push_back({ acc[0] / areaSum, acc[1] / areaSum, acc[2] / areaSum });
    }
}

bool FindClosestOnLines(vtkPolyData* pd, const double x[3], ClosestHit& hit)
{
    pd->BuildCells();
    const vtkIdType nCells = pd->GetNumberOfCells();
    hit                    = ClosestHit{};
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const int type = pd->GetCellType(cid);
        if (type != VTK_LINE && type != VTK_POLY_LINE)
        {
            continue;
        }
        vtkIdType        npts = 0;
        const vtkIdType* pts  = nullptr;
        pd->GetCellPoints(cid, npts, pts);
        if (npts < 2 || !pts)
        {
            continue;
        }
        for (vtkIdType i = 0; i + 1 < npts; ++i)
        {
            double p1[3];
            double p2[3];
            pd->GetPoint(pts[i], p1);
            pd->GetPoint(pts[i + 1], p2);
            if (vtkMath::Distance2BetweenPoints(p1, p2) <= 0.0)
            {
                continue;
            }
            double t       = 0.0;
            double closest[3];
            vtkLine::DistanceToLine(x, p1, p2, t, closest);
            if (t < 0.0)
            {
                t = 0.0;
            }
            else if (t > 1.0)
            {
                t = 1.0;
            }
            closest[0] = p1[0] + t * (p2[0] - p1[0]);
            closest[1] = p1[1] + t * (p2[1] - p1[1]);
            closest[2] = p1[2] + t * (p2[2] - p1[2]);
            const double dist2 = vtkMath::Distance2BetweenPoints(x, closest);
            if (dist2 < hit.dist2)
            {
                hit.cellId       = cid;
                hit.pt0          = pts[i];
                hit.pt1          = pts[i + 1];
                hit.segmentIndex = i;
                hit.closest[0]   = closest[0];
                hit.closest[1]   = closest[1];
                hit.closest[2]   = closest[2];
                hit.dist2        = dist2;
            }
        }
    }
    return hit.cellId >= 0;
}

int LineVertexDegree(vtkPolyData* pd, vtkIdType vid)
{
    pd->BuildCells();
    int             deg    = 0;
    const vtkIdType nCells = pd->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const int type = pd->GetCellType(cid);
        if (type != VTK_LINE && type != VTK_POLY_LINE)
        {
            continue;
        }
        vtkIdType        npts = 0;
        const vtkIdType* pts  = nullptr;
        pd->GetCellPoints(cid, npts, pts);
        if (npts < 2 || !pts)
        {
            continue;
        }
        for (vtkIdType i = 0; i + 1 < npts; ++i)
        {
            if (pts[i] == vid)
            {
                ++deg;
            }
            if (pts[i + 1] == vid)
            {
                ++deg;
            }
        }
    }
    return deg;
}

vtkIntArray* GetNewCapBranchArray(vtkPolyData* pd)
{
    if (!pd)
    {
        return nullptr;
    }
    return vtkIntArray::SafeDownCast(pd->GetCellData()->GetArray(kNewCapBranchArrayName));
}

vtkIdType EnsureAttachVertex(vtkPolyData* pd, const ClosestHit& hit, double tol2)
{
    double p0[3];
    double p1[3];
    pd->GetPoint(hit.pt0, p0);
    pd->GetPoint(hit.pt1, p1);
    if (vtkMath::Distance2BetweenPoints(hit.closest, p0) <= tol2)
    {
        return hit.pt0;
    }
    if (vtkMath::Distance2BetweenPoints(hit.closest, p1) <= tol2)
    {
        return hit.pt1;
    }

    const vtkIdType newId = pd->GetPoints()->InsertNextPoint(hit.closest);

    vtkIntArray* flags = GetNewCapBranchArray(pd);

    vtkSmartPointer<vtkCellArray> newLines = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkIntArray>  remapped = vtkSmartPointer<vtkIntArray>::New();
    remapped->SetName(kNewCapBranchArrayName);
    remapped->SetNumberOfComponents(1);

    const vtkIdType nCells = pd->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const int type = pd->GetCellType(cid);
        if (type != VTK_LINE && type != VTK_POLY_LINE)
        {
            continue;
        }
        vtkIdType        npts = 0;
        const vtkIdType* pts  = nullptr;
        pd->GetCellPoints(cid, npts, pts);
        const int flag =
            (flags && cid < flags->GetNumberOfTuples()) ? flags->GetValue(cid) : 0;
        if (cid != hit.cellId)
        {
            newLines->InsertNextCell(npts, pts);
            remapped->InsertNextValue(flag);
            continue;
        }
        for (vtkIdType i = 0; i + 1 < npts; ++i)
        {
            if (i == hit.segmentIndex)
            {
                vtkIdType a[2] = { pts[i], newId };
                vtkIdType b[2] = { newId, pts[i + 1] };
                newLines->InsertNextCell(2, a);
                remapped->InsertNextValue(flag);
                newLines->InsertNextCell(2, b);
                remapped->InsertNextValue(flag);
            }
            else
            {
                vtkIdType s[2] = { pts[i], pts[i + 1] };
                newLines->InsertNextCell(2, s);
                remapped->InsertNextValue(flag);
            }
        }
    }
    pd->SetLines(newLines);
    pd->GetCellData()->RemoveArray(kNewCapBranchArrayName);
    pd->GetCellData()->AddArray(remapped);
    pd->BuildCells();
    return newId;
}
} // namespace

//------------------------------------------------------------------------------
vtkCGALSkeletonExtraction::vtkCGALSkeletonExtraction()
{
    this->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_CELLS, "EndpointIndex");
}

//------------------------------------------------------------------------------
void vtkCGALSkeletonExtraction::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "MaxTriangleAngle: " << this->MaxTriangleAngle << std::endl;
    os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
    os << indent << "MaxIterations: " << this->MaxIterations << std::endl;
    os << indent << "AreaThreshold: " << this->AreaThreshold << std::endl;
    os << indent << "QualitySpeedTradeoff: " << this->QualitySpeedTradeoff << std::endl;
    os << indent << "MediallyCentered: " << this->MediallyCentered << std::endl;
    os << indent << "MediallyCenteredSpeedTradeoff: " << this->MediallyCenteredSpeedTradeoff
       << std::endl;
    os << indent << "AppendCapEndpoints: " << (this->AppendCapEndpoints ? 1 : 0) << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkCGALSkeletonExtraction::AppendCapEndpointsToOutput(
    vtkPolyData* surface, vtkDataArray* capArray, vtkPolyData* skeleton)
{
    if (!surface || !capArray || !skeleton)
    {
        return;
    }
    if (capArray->GetNumberOfTuples() != surface->GetNumberOfCells())
    {
        vtkWarningMacro("Cap array tuple count does not match number of cells; skip append.");
        return;
    }
    if (skeleton->GetNumberOfLines() < 1)
    {
        vtkWarningMacro("Skeleton has no lines; skip append cap endpoints.");
        return;
    }

    std::vector<std::array<double, 3>> centroids;
    CollectCapPatchCentroids(surface, capArray, centroids);
    if (centroids.empty())
    {
        vtkWarningMacro("Append Cap Endpoints is ON but no cells have cap array > 0; skip.");
        return;
    }

    double bounds[6];
    skeleton->GetBounds(bounds);
    vtkBoundingBox box;
    box.SetBounds(bounds);
    const double L    = box.GetMaxLength();
    const double tol  = 1e-8 * (L > 0.0 ? L : 1.0);
    const double tol2 = tol * tol;

    skeleton->BuildCells();
    vtkSmartPointer<vtkIntArray> branchFlags = vtkSmartPointer<vtkIntArray>::New();
    branchFlags->SetName(kNewCapBranchArrayName);
    branchFlags->SetNumberOfComponents(1);
    branchFlags->SetNumberOfTuples(skeleton->GetNumberOfCells());
    branchFlags->Fill(0);
    skeleton->GetCellData()->AddArray(branchFlags);

    int              appended = 0;
    std::vector<int> newBranchDegrees;
    for (const auto& c : centroids)
    {
        const double x[3] = { c[0], c[1], c[2] };
        ClosestHit   hit;
        if (!FindClosestOnLines(skeleton, x, hit))
        {
            vtkWarningMacro("Failed to locate a skeleton line for a cap centroid; skip that patch.");
            continue;
        }
        const vtkIdType attach = EnsureAttachVertex(skeleton, hit, tol2);
        const int       attachDeg = LineVertexDegree(skeleton, attach);
        const int       isNewBranch = (attachDeg > 1) ? 1 : 0;
        if (isNewBranch)
        {
            newBranchDegrees.push_back(attachDeg);
        }

        const vtkIdType capPt = skeleton->GetPoints()->InsertNextPoint(x);
        vtkSmartPointer<vtkLine> stub = vtkSmartPointer<vtkLine>::New();
        stub->GetPointIds()->SetId(0, capPt);
        stub->GetPointIds()->SetId(1, attach);
        skeleton->GetLines()->InsertNextCell(stub);
        skeleton->BuildCells();

        vtkIntArray* flags = GetNewCapBranchArray(skeleton);
        if (flags)
        {
            flags->InsertNextValue(isNewBranch);
        }
        ++appended;
    }

    vtkDebugMacro(<< "Appended " << appended << " cap endpoint(s) to the skeleton.");
    if (!newBranchDegrees.empty())
    {
        std::ostringstream oss;
        oss << "Append Cap Endpoints: " << newBranchDegrees.size()
            << " cap stub(s) attached at a skeleton vertex with degree > 1 "
               "(new branch rather than extending a leaf). "
               "Those lines are marked 1 on cell array '"
            << kNewCapBranchArrayName << "'. Attach degree(s): ";
        for (size_t i = 0; i < newBranchDegrees.size(); ++i)
        {
            if (i)
            {
                oss << ", ";
            }
            oss << newBranchDegrees[i];
        }
        oss << ".";
        vtkWarningMacro(<< oss.str());
    }
}

//------------------------------------------------------------------------------
int vtkCGALSkeletonExtraction::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
    vtkPolyData* output = vtkPolyData::GetData(outputVector);

    if (!input || !output)
    {
        vtkErrorMacro("Missing input or output.");
        return 0;
    }

    if (input->GetNumberOfCells() == 0)
    {
        vtkErrorMacro("Input mesh is empty.");
        return 0;
    }

    // VTK -> CGAL
    std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
        std::make_unique<vtkCGALHelper::Vespa_surface>();
    if (!vtkCGALHelper::toCGAL(input, cgalMesh.get()))
    {
        vtkErrorMacro("Failed to convert input to CGAL surface mesh.");
        return 0;
    }

    if (!CGAL::is_closed(cgalMesh->surface))
    {
        vtkErrorMacro("Input mesh must be closed (watertight) for skeleton extraction.");
        return 0;
    }

    // <= 0: same scale as ParaView vtkSMBoundsDomain scaled_extent (0.001 * AABB longest side).
    double minEdgeLength = this->MinEdgeLength;
    if (minEdgeLength <= 0.0)
    {
        double b[6];
        input->GetBounds(b);
        vtkBoundingBox box;
        box.SetBounds(b);
        const double L = box.GetMaxLength();
        if (L <= 0.0)
        {
            vtkErrorMacro("Input mesh has zero bounding-box extent.");
            return 0;
        }
        minEdgeLength = 0.001 * L;
    }

    // Skeleton extraction via Mean Curvature Flow
    Skeleton skeleton;
    try
    {
        Skeletonization mcs(cgalMesh->surface);
        mcs.set_max_triangle_angle(this->MaxTriangleAngle * (CGAL_PI / 180.0));
        mcs.set_min_edge_length(minEdgeLength);
        mcs.set_max_iterations(this->MaxIterations);
        mcs.set_area_variation_factor(this->AreaThreshold);
        mcs.set_quality_speed_tradeoff(this->QualitySpeedTradeoff);
        mcs.set_is_medially_centered(this->MediallyCentered);
        mcs.set_medially_centered_speed_tradeoff(this->MediallyCenteredSpeedTradeoff);
        mcs.contract_until_convergence();
        mcs.convert_to_skeleton(skeleton);
    }
    catch (std::exception& e)
    {
        vtkErrorMacro("CGAL Exception: " << e.what());
        return 0;
    }

    // Skeleton graph -> VTK polylines
    vtkSmartPointer<vtkPoints>    points = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> lines  = vtkSmartPointer<vtkCellArray>::New();

    std::map<Skeleton_vertex, vtkIdType> vertexMap;

    for (Skeleton_vertex v : CGAL::make_range(vertices(skeleton)))
    {
        const auto& pt = skeleton[v].point;
        vtkIdType id   = points->InsertNextPoint(pt.x(), pt.y(), pt.z());
        vertexMap[v]   = id;
    }

    for (Skeleton_edge e : CGAL::make_range(edges(skeleton)))
    {
        vtkSmartPointer<vtkLine> line = vtkSmartPointer<vtkLine>::New();
        line->GetPointIds()->SetId(0, vertexMap[source(e, skeleton)]);
        line->GetPointIds()->SetId(1, vertexMap[target(e, skeleton)]);
        lines->InsertNextCell(line);
    }

    output->SetPoints(points);
    output->SetLines(lines);

    if (this->AppendCapEndpoints)
    {
        vtkDataArray* capArray = this->GetInputArrayToProcess(0, inputVector);
        if (!capArray)
        {
            vtkWarningMacro("Append Cap Endpoints is ON but no cap array was found "
                            "(default cell EndpointIndex); skip.");
        }
        else
        {
            this->AppendCapEndpointsToOutput(input, capArray, output);
        }
    }

    return 1;
}
