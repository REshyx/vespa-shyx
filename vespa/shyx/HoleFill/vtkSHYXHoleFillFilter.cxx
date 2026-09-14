#include "vtkSHYXHoleFillFilter.h"

#include "vtkCGALHelper.h"
#include "vtkCGALPatchFilling.h"

#include <vtkAlgorithmOutput.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractSelection.h>
#include <vtkGeometryFilter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkSmartPointer.h>
#include <vtkThreshold.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>

#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

vtkStandardNewMacro(vtkSHYXHoleFillFilter);

namespace pmp = CGAL::Polygon_mesh_processing;

namespace
{

using Graph_halfedge = boost::graph_traits<CGAL_Surface>::halfedge_descriptor;

vtkSmartPointer<vtkPolyData> ForceDataSetToPolyData(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    return pd;
  }

  vtkNew<vtkGeometryFilter> geometry;
  geometry->SetInputData(ds);
  geometry->Update();
  vtkPolyData* out = geometry->GetOutput();
  if (!out)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
  copy->ShallowCopy(out);
  return copy;
}

/** Same extract-then-invert-insidedness pipeline as vtkCGALPatchFilling (original cell/point ids). */
vtkSmartPointer<vtkPolyData> RemoveSelection(vtkPolyData* mesh, vtkSelection* sel)
{
  if (!mesh)
  {
    return nullptr;
  }
  if (!sel || sel->GetNumberOfNodes() == 0)
  {
    return mesh;
  }

  vtkNew<vtkExtractSelection> extractSelection;
  extractSelection->SetInputData(0, mesh);
  extractSelection->SetInputData(1, sel);
  extractSelection->PreserveTopologyOn();
  vtkNew<vtkThreshold> threshold;
  threshold->SetInputConnection(extractSelection->GetOutputPort());
  threshold->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS_THEN_CELLS, "vtkInsidedness");
  threshold->SetUpperThreshold(0.5);
  vtkNew<vtkDataSetSurfaceFilter> surface;
  surface->SetInputConnection(threshold->GetOutputPort());
  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputConnection(surface->GetOutputPort());
  tri->Update();
  vtkPolyData* out = vtkPolyData::SafeDownCast(tri->GetOutput(0));
  if (!out)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
  copy->ShallowCopy(out);
  return copy;
}

bool SoupOrientRepairToSurface(vtkPolyData* vtkMesh, vtkCGALHelper::Vespa_surface* cgalMesh,
  std::size_t* soupPtsAfter, std::size_t* soupFacesAfter, std::string* err)
{
  vtkCGALHelper::Vespa_soup soup;
  vtkCGALHelper::toCGAL(vtkMesh, &soup);
  try
  {
    (void)pmp::orient_polygon_soup(soup.points, soup.faces);
    pmp::repair_polygon_soup(soup.points, soup.faces);
    if (soupPtsAfter)
    {
      *soupPtsAfter = soup.points.size();
    }
    if (soupFacesAfter)
    {
      *soupFacesAfter = soup.faces.size();
    }
    if (!pmp::is_polygon_soup_a_polygon_mesh(soup.faces))
    {
      if (err)
      {
        *err = "polygon soup is not a polygon mesh after orient/repair_polygon_soup "
               "(non-manifold or inconsistent faces). Use SHYX Mesh Checker.";
      }
      return false;
    }
    pmp::polygon_soup_to_polygon_mesh(soup.points, soup.faces, cgalMesh->surface);
    cgalMesh->coords = get(CGAL::vertex_point, cgalMesh->surface);
  }
  catch (const std::exception& e)
  {
    if (err)
    {
      *err = e.what();
    }
    return false;
  }
  return true;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXHoleFillFilter::vtkSHYXHoleFillFilter()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FairingContinuity: " << this->FairingContinuity << std::endl;
  os << indent << "RepairPolygonSoup: " << (this->RepairPolygonSoup ? "on" : "off") << std::endl;
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::SetUpdateAttributes(bool update)
{
  if (update)
  {
    vtkWarningMacro("Unsupported: vtkSHYXHoleFillFilter does not interpolate attributes onto new "
                    "patch geometry (same behavior as vtkCGALPatchFilling).");
  }
  this->Superclass::SetUpdateAttributes(false);
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkSHYXHoleFillFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
  }
  else
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkSHYXHoleFillFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!output)
  {
    vtkErrorMacro(<< "Null output.");
    return 0;
  }

  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  if (!input)
  {
    vtkErrorMacro(<< "Missing mesh input on port 0.");
    return 0;
  }

  vtkSmartPointer<vtkPolyData> mesh = ForceDataSetToPolyData(input);
  if (!mesh || mesh->GetNumberOfCells() == 0)
  {
    vtkErrorMacro(<< "Failed to convert input to a non-empty vtkPolyData surface.");
    return 0;
  }

  vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
  if (selInfo)
  {
    vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
    mesh = RemoveSelection(mesh, inputSel);
    if (!mesh || mesh->GetNumberOfCells() == 0)
    {
      vtkErrorMacro(<< "Selection removal produced an empty surface.");
      return 0;
    }
  }

  if (!this->RepairPolygonSoup)
  {
    vtkNew<vtkCGALPatchFilling> fill;
    fill->SetFairingContinuity(this->FairingContinuity);
    fill->SetInputData(0, mesh);
    fill->Update();
    vtkPolyData* filled = fill->GetOutput();
    if (!filled)
    {
      vtkErrorMacro(<< "vtkCGALPatchFilling produced no output.");
      return 0;
    }
    output->ShallowCopy(filled);
    return 1;
  }

  const vtkIdType nPtsIn = mesh->GetNumberOfPoints();
  const vtkIdType nCellsIn = mesh->GetNumberOfCells();
  auto cgalMesh = std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::size_t soupPts = 0;
  std::size_t soupFaces = 0;
  std::string err;
  if (!SoupOrientRepairToSurface(mesh, cgalMesh.get(), &soupPts, &soupFaces, &err))
  {
    vtkErrorMacro(<< "Soup repair / polygon_soup_to_polygon_mesh failed: " << err);
    return 0;
  }
  if (static_cast<vtkIdType>(soupPts) != nPtsIn || static_cast<vtkIdType>(soupFaces) != nCellsIn)
  {
    vtkWarningMacro(<< "repair_polygon_soup changed the mesh (points " << nPtsIn << " -> " << soupPts
                     << ", faces " << nCellsIn << " -> " << soupFaces
                     << "); filling holes on the repaired Surface_mesh (same as Mesh Checker port 0).");
  }

  const vtkIdType nFacesBeforeFill = static_cast<vtkIdType>(cgalMesh->surface.number_of_faces());
  bool fillOk = true;
  std::size_t nCycles = 0;
  try
  {
    std::vector<Graph_halfedge> borderCycles;
    pmp::extract_boundary_cycles(cgalMesh->surface, std::back_inserter(borderCycles));
    nCycles = borderCycles.size();
    std::vector<Graph_Verts> patch_vertices;
    std::vector<Graph_Faces> patch_facets;
    for (Graph_halfedge h : borderCycles)
    {
      fillOk &= std::get<0>(pmp::triangulate_refine_and_fair_hole(cgalMesh->surface, h,
        pmp::parameters::fairing_continuity(this->FairingContinuity)
          .face_output_iterator(std::back_inserter(patch_facets))
          .vertex_output_iterator(std::back_inserter(patch_vertices))));
    }
  }
  catch (const std::exception& e)
  {
    vtkErrorMacro(<< "CGAL hole fill exception: " << e.what());
    return 0;
  }

  if (nCycles == 0)
  {
    vtkWarningMacro(<< "No boundary cycles after soup repair; output is the repaired mesh with no "
                       "new fill faces.");
  }
  else if (!fillOk)
  {
    vtkWarningMacro(<< "triangulate_refine_and_fair_hole failed on at least one of " << nCycles
                     << " boundary cycle(s). Try Fairing Continuity = 0. Output may be unchanged or "
                        "only partially filled.");
  }

  if (!vtkCGALHelper::toVTK(cgalMesh.get(), output))
  {
    vtkErrorMacro(<< "Failed to convert filled CGAL surface back to vtkPolyData.");
    return 0;
  }

  if (nCycles > 0 && fillOk && output->GetNumberOfCells() == nFacesBeforeFill)
  {
    vtkWarningMacro(<< "Hole fill reported success on " << nCycles
                     << " cycle(s) but face count did not change (" << nFacesBeforeFill << ").");
  }
  return 1;
}
