#include "vtkCGALMeshDeformation.h"

// VTK related includes
#include "vtkExtractSelection.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkSelection.h"

// CGAL related includes
#include <CGAL/Surface_mesh_deformation.h>

vtkStandardNewMacro(vtkCGALMeshDeformation);

using Mesh_Deformation = CGAL::Surface_mesh_deformation<CGAL_Surface>;

//------------------------------------------------------------------------------
vtkCGALMeshDeformation::vtkCGALMeshDeformation()
{
  this->SetNumberOfInputPorts(3);
}

//------------------------------------------------------------------------------
void vtkCGALMeshDeformation::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "Iterations :" << this->Iterations << std::endl;
  os << indent << "Tolerance :" << this->Tolerance << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALMeshDeformation::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
  }
  else if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPointSet");
  }
  else
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkCGALMeshDeformation::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkPolyData* input   = vtkPolyData::GetData(inputVector[0]);
  vtkPointSet* targets = vtkPointSet::GetData(inputVector[1]);
  vtkPolyData* output  = vtkPolyData::GetData(outputVector);

  if (!input || !targets || !output)
  {
    vtkErrorMacro(<< "Missing input or output!");
    return 0;
  }

  // Get the optional selection input
  vtkInformation*     selInfo = inputVector[2]->GetInformationObject(0);
  vtkNew<vtkPointSet> roi;

  // If no selection is given, use control points as ROI
  // A better approach could use all points within a radius
  // of the control points to avoid self intersections
  if (selInfo)
  {
    vtkSelection* roiSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
    vtkNew<vtkExtractSelection> extractSelection;
    extractSelection->SetInputData(0, input);
    extractSelection->SetInputData(1, roiSel);
    extractSelection->Update();
    roi->ShallowCopy(extractSelection->GetOutputDataObject(0));
  }
  else
  {
    roi->ShallowCopy(targets);
  }

  // Create the triangle mesh for CGAL
  // --------------------------------

  std::unique_ptr<CGAL_Mesh> cgalMesh = this->toCGAL(input);

  // Create the deformation object
  // ---------------------------------

  Mesh_Deformation deformer(cgalMesh->surface);

  // Find global ID array name
  // ---------------------------------

  if (this->GlobalIdArray.empty())
  {
    if (!input->GetPointData()->GetGlobalIds())
    {
      vtkErrorMacro(
        << "Could not find a default array for global IDs. Please specify an array name "
           "using SetGlobalIdArray().");
      return 0;
    }
    else
    {
      this->GlobalIdArray = input->GetPointData()->GetGlobalIds()->GetName();
    }
  }

  // Define the ROI
  // ---------------------------------

  if (!roi->GetPointData()->GetArray(this->GlobalIdArray.c_str()))
  {
    vtkErrorMacro(<< "No array named " << this->GlobalIdArray << " for the ROI!");
    return 0;
  }

  auto gids =
    vtk::DataArrayValueRange<1>(roi->GetPointData()->GetArray(this->GlobalIdArray.c_str()));
  std::vector<Graph_Verts> roiVerts(gids.cbegin(), gids.cend());
  try
  {
    deformer.insert_roi_vertices(roiVerts.begin(), roiVerts.end());
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // Define the control points
  // ---------------------------------

  if (!targets->GetPointData()->GetArray(this->GlobalIdArray.c_str()))
  {
    vtkErrorMacro(<< "No array named " << this->GlobalIdArray << " for the control points!");
    return 0;
  }

  gids =
    vtk::DataArrayValueRange<1>(targets->GetPointData()->GetArray(this->GlobalIdArray.c_str()));
  std::vector<Graph_Verts> ctrlPoints(gids.cbegin(), gids.cend());
  try
  {
    deformer.insert_control_vertices(ctrlPoints.begin(), ctrlPoints.end());
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // Define the target positions for the control points
  // ---------------------------------

  for (vtkIdType ptIdx = 0; ptIdx < targets->GetNumberOfPoints(); ++ptIdx)
  {
    double coords[3] = { 0.0, 0.0, 0.0 };
    targets->GetPoint(ptIdx, coords);
    Mesh_Deformation::Point targetPos(coords[0], coords[1], coords[2]);
    try
    {
      deformer.set_target_position(ctrlPoints[ptIdx], targetPos);
    }
    catch (std::exception& e)
    {
      vtkErrorMacro("CGAL Exception: " << e.what());
      return 0;
    }
  }

  // CGAL Processing
  // ---------------

  try
  {
    // Deform mesh given ROI and control point targets
    deformer.deform(this->Iterations, this->Tolerance);
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalMesh.get()));
  this->copyAttributes(input, output);

  return 1;
}
