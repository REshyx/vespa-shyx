#include "vtkCGALSurfaceToVolumeMesh.h"

#include "vtkCGALHelper.h"

#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkUnstructuredGrid.h>

#include <CGAL/Mesh_complex_3_in_triangulation_3.h>
#include <CGAL/Mesh_criteria_3.h>
#include <CGAL/Mesh_triangulation_3.h>
#include <CGAL/Polyhedral_mesh_domain_with_features_3.h>
#include <CGAL/make_mesh_3.h>
#include <CGAL/number_utils.h>

#include <exception>
#include <map>
#include <memory>

vtkStandardNewMacro(vtkCGALSurfaceToVolumeMesh);

// CGAL Mesh_3 types (matching vtkCGALHelper kernel and surface)
// Polyhedral_mesh_domain_with_features_3 supports detect_features() for sharp edges/corners
using CGAL_Kernel   = CGAL::Exact_predicates_inexact_constructions_kernel;
using Polyhedron    = CGAL::Surface_mesh<CGAL_Kernel::Point_3>;
using Mesh_domain   = CGAL::Polyhedral_mesh_domain_with_features_3<CGAL_Kernel, Polyhedron>;
#ifdef CGAL_CONCURRENT_MESH_3
using Concurrency_tag = CGAL::Parallel_tag;
#else
using Concurrency_tag = CGAL::Sequential_tag;
#endif
using Tr            = CGAL::Mesh_triangulation_3<Mesh_domain, CGAL::Default, Concurrency_tag>::type;
using C3t3          = CGAL::Mesh_complex_3_in_triangulation_3<Tr, typename Mesh_domain::Corner_index, typename Mesh_domain::Curve_index>;
using Mesh_criteria = CGAL::Mesh_criteria_3<Tr>;

namespace params = CGAL::parameters;

//------------------------------------------------------------------------------
vtkCGALSurfaceToVolumeMesh::vtkCGALSurfaceToVolumeMesh()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkCGALSurfaceToVolumeMesh::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkCGALSurfaceToVolumeMesh::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkUnstructuredGrid");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkCGALSurfaceToVolumeMesh::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "FacetAngle: " << this->FacetAngle << std::endl;
    os << indent << "FacetSize: " << this->FacetSize << std::endl;
    os << indent << "FacetDistance: " << this->FacetDistance << std::endl;
    os << indent << "CellRadiusEdgeRatio: " << this->CellRadiusEdgeRatio << std::endl;
    os << indent << "CellSize: " << this->CellSize << std::endl;
    os << indent << "DetectFeatures: " << (this->DetectFeatures ? "ON" : "OFF") << std::endl;
    os << indent << "FeatureAngle: " << this->FeatureAngle << std::endl;
    os << indent << "EdgeSize: " << this->EdgeSize << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALSurfaceToVolumeMesh::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
    vtkUnstructuredGrid* output = vtkUnstructuredGrid::GetData(outputVector);

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

    // Ensure consistent orientation for CGAL
    vtkNew<vtkPolyDataNormals> normalizer;
    normalizer->SetInputData(input);
    normalizer->SplittingOff();
    normalizer->NonManifoldTraversalOff();
    normalizer->Update();
    vtkPolyData* normalizedInput = normalizer->GetOutput();

    // VTK -> CGAL Polyhedron (same as Vespa_surface but we need Polyhedron directly)
    std::unique_ptr<vtkCGALHelper::Vespa_surface> vespaSurface =
        std::make_unique<vtkCGALHelper::Vespa_surface>();
    if (!vtkCGALHelper::toCGAL(normalizedInput, vespaSurface.get()))
    {
        vtkErrorMacro("Failed to convert input to CGAL surface mesh.");
        return 0;
    }

    Polyhedron polyhedron = vespaSurface->surface;

    if (!CGAL::is_triangle_mesh(polyhedron))
    {
        vtkErrorMacro("Input geometry must be triangulated.");
        return 0;
    }

    if (!CGAL::is_closed(polyhedron))
    {
        vtkErrorMacro("Input mesh must be closed (watertight) for volume meshing.");
        return 0;
    }

    // Create domain
    Mesh_domain domain(polyhedron);

    // Detect sharp feature edges and corners (optional)
    // Feature edges: dihedral angle >= FeatureAngle (degrees). Corners: auto at edge junctions.
    if (this->DetectFeatures)
    {
        domain.detect_features(static_cast<CGAL_Kernel::FT>(this->FeatureAngle));
    }

    // Mesh criteria (Mesh_criteria_3 has deleted assignment, must construct in one go)
    const bool useCellSize = (this->CellSize > 0.0);
    const bool useEdgeSize = (this->DetectFeatures && this->EdgeSize > 0.0);

    Mesh_criteria criteria =
        (useEdgeSize && useCellSize)
            ? Mesh_criteria(
                  params::edge_size(this->EdgeSize)
                      .facet_angle(this->FacetAngle)
                      .facet_size(this->FacetSize)
                      .facet_distance(this->FacetDistance)
                      .cell_radius_edge_ratio(this->CellRadiusEdgeRatio)
                      .cell_size(this->CellSize))
            : (useEdgeSize)
                  ? Mesh_criteria(
                        params::edge_size(this->EdgeSize)
                            .facet_angle(this->FacetAngle)
                            .facet_size(this->FacetSize)
                            .facet_distance(this->FacetDistance)
                            .cell_radius_edge_ratio(this->CellRadiusEdgeRatio))
                  : (useCellSize)
                        ? Mesh_criteria(
                              params::facet_angle(this->FacetAngle)
                                  .facet_size(this->FacetSize)
                                  .facet_distance(this->FacetDistance)
                                  .cell_radius_edge_ratio(this->CellRadiusEdgeRatio)
                                  .cell_size(this->CellSize))
                        : Mesh_criteria(
                              params::facet_angle(this->FacetAngle)
                                  .facet_size(this->FacetSize)
                                  .facet_distance(this->FacetDistance)
                                  .cell_radius_edge_ratio(this->CellRadiusEdgeRatio));

    // Generate mesh
    C3t3 c3t3;
    try
    {
        c3t3 = CGAL::make_mesh_3<C3t3>(
            domain, criteria,
            params::no_perturb().no_exude());
    }
    catch (std::exception& e)
    {
        vtkErrorMacro("CGAL Mesh_3 Exception: " << e.what());
        return 0;
    }

    if (c3t3.number_of_cells_in_complex() == 0)
    {
        vtkErrorMacro("CGAL produced no tetrahedral cells. Check input mesh validity.");
        return 0;
    }

    // Build vertex index map (C3t3 vertex handle -> vtkIdType)
    std::map<C3t3::Vertex_handle, vtkIdType> vertexMap;
    vtkIdType nextId = 0;
    for (C3t3::Cells_in_complex_iterator cit = c3t3.cells_in_complex_begin();
         cit != c3t3.cells_in_complex_end(); ++cit)
    {
        for (int i = 0; i < 4; ++i)
        {
            C3t3::Vertex_handle vh = cit->vertex(i);
            if (vertexMap.find(vh) == vertexMap.end())
            {
                vertexMap[vh] = nextId++;
            }
        }
    }

    // Output points
    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(static_cast<vtkIdType>(vertexMap.size()));

    const auto& tr = c3t3.triangulation();
    for (const auto& pair : vertexMap)
    {
        const auto& p = tr.point(pair.first);
        points->SetPoint(pair.second,
            CGAL::to_double(p.x()),
            CGAL::to_double(p.y()),
            CGAL::to_double(p.z()));
    }

    // Output tetrahedra
    output->SetPoints(points);
    output->Allocate(static_cast<vtkIdType>(c3t3.number_of_cells_in_complex()));

    vtkIdType tetIds[4];
    for (C3t3::Cells_in_complex_iterator cit = c3t3.cells_in_complex_begin();
         cit != c3t3.cells_in_complex_end(); ++cit)
    {
        tetIds[0] = vertexMap[cit->vertex(0)];
        tetIds[1] = vertexMap[cit->vertex(1)];
        tetIds[2] = vertexMap[cit->vertex(2)];
        tetIds[3] = vertexMap[cit->vertex(3)];
        output->InsertNextCell(VTK_TETRA, 4, tetIds);
    }
    output->Squeeze();

    return 1;
}
