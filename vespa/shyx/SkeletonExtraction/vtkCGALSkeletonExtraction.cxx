#include "vtkCGALSkeletonExtraction.h"

// VESPA related includes
#include "vtkCGALHelper.h"

// VTK related includes
#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkLine.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

// CGAL related includes
#include <CGAL/Mean_curvature_flow_skeletonization.h>

#include <CGAL/number_utils.h>

#include <exception>
#include <memory>

vtkStandardNewMacro(vtkCGALSkeletonExtraction);

using Skeletonization = CGAL::Mean_curvature_flow_skeletonization<CGAL_Surface>;
using Skeleton        = Skeletonization::Skeleton;
using Skeleton_vertex = Skeleton::vertex_descriptor;
using Skeleton_edge   = Skeleton::edge_descriptor;

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
    this->Superclass::PrintSelf(os, indent);
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

    // <= 0: same scale as ParaView vtkSMBoundsDomain scaled_extent (0.002 * AABB longest side).
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
        minEdgeLength = 0.002 * L;
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

    return 1;
}
