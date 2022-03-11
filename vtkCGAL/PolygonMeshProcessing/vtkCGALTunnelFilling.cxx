#include "vtkCGALTunnelFilling.h"

// VTK related includes
#include "vtkDataSetSurfaceFilter.h"
#include "vtkExtractSelection.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"
#include "vtkTriangleFilter.h"
#include "vtkThreshold.h"

// CGAL related includes
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>

vtkStandardNewMacro(vtkCGALTunnelFilling);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
vtkCGALTunnelFilling::vtkCGALTunnelFilling()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkCGALTunnelFilling::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkCGALTunnelFilling::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
  }
  else
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkCGALTunnelFilling::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Get the input and output data objects.
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  // Selection
  vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
  if (!selInfo)
  {
    // When not given a selection, nothing to do.
    vtkWarningMacro("No selection made, nothing to do");
    output->ShallowCopy(input);
    return 1;
  }
  vtkSelection* inputSel   = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
  const auto    selNbNodes = inputSel->GetNumberOfNodes();
  if (selNbNodes > 0)
  {
    // pipeline to remove selection
    vtkNew<vtkExtractSelection> extractSelection;
    extractSelection->SetInputData(0, input);
    extractSelection->SetInputData(1, inputSel);
    extractSelection->PreserveTopologyOn();
    vtkNew<vtkThreshold> threshold;
    threshold->SetInputConnection(extractSelection->GetOutputPort());
    threshold->SetInputArrayToProcess(
      0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "vtkInsidedness");
    threshold->SetUpperThreshold(0.5);
    vtkNew<vtkDataSetSurfaceFilter> surface;
    surface->SetInputConnection(threshold->GetOutputPort());
    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(surface->GetOutputPort());
    tri->Update();
    input->ShallowCopy(vtkPolyData::SafeDownCast(tri->GetOutput(0)));
  }

  // Create the triangle mesh for CGAL
  // --------------------------------

  std::unique_ptr<CGAL_Mesh> cgalMesh = this->toCGAL(input);

  // CGAL Processing
  // ---------------

  try
  {
    // TODO
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  // VTK Output
  // ----------

  output->ShallowCopy(this->toVTK(cgalMesh.get()));

  this->interpolateAttributes(input, output);

  return 1;
}
