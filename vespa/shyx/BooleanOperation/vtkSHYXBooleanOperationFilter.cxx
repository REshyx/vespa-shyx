#include "vtkSHYXBooleanOperationFilter.h"

#include "vtkCGALHelper.h"

#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/Polygon_mesh_processing/corefinement.h>

#include <exception>
#include <memory>

vtkStandardNewMacro(vtkSHYXBooleanOperationFilter);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
vtkSHYXBooleanOperationFilter::vtkSHYXBooleanOperationFilter()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkSHYXBooleanOperationFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  switch (this->OperationType)
  {
    case vtkSHYXBooleanOperationFilter::DIFFERENCE:
      os << indent << "OperationType: Difference" << std::endl;
      break;
    case vtkSHYXBooleanOperationFilter::INTERSECTION:
      os << indent << "OperationType: Intersection" << std::endl;
      break;
    case vtkSHYXBooleanOperationFilter::UNION:
      os << indent << "OperationType: Union" << std::endl;
      break;
    default:
      os << indent << "OperationType: Unknown" << std::endl;
      break;
  }
  os << indent << "ThrowOnSelfIntersection: " << (this->ThrowOnSelfIntersection ? "on" : "off")
     << std::endl;
  os << indent << "OrientToBoundVolumeWhenNeeded: "
     << (this->OrientToBoundVolumeWhenNeeded ? "on" : "off") << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkSHYXBooleanOperationFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkSHYXBooleanOperationFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXBooleanOperationFilter::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* inputData = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* sourceData = vtkPolyData::GetData(inputVector[1]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  if (!inputData || !sourceData)
  {
    vtkErrorMacro("Missing input or source.");
    return 0;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalInputMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  vtkCGALHelper::toCGAL(inputData, cgalInputMesh.get());
  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalSourceMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  vtkCGALHelper::toCGAL(sourceData, cgalSourceMesh.get());

  if (!CGAL::is_closed(cgalInputMesh->surface))
  {
    vtkWarningMacro(
      << "Input mesh is not closed; CGAL boolean may fail or be meaningless for volume ops.");
  }
  if (!CGAL::is_closed(cgalSourceMesh->surface))
  {
    vtkWarningMacro(
      << "Source mesh is not closed; CGAL boolean may fail or be meaningless for volume ops.");
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalOutMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();

  bool res = true;
  try
  {
    if (this->OrientToBoundVolumeWhenNeeded)
    {
      if (!pmp::does_bound_a_volume(cgalInputMesh->surface))
      {
        pmp::orient_to_bound_a_volume(cgalInputMesh->surface);
      }
      if (!pmp::does_bound_a_volume(cgalSourceMesh->surface))
      {
        pmp::orient_to_bound_a_volume(cgalSourceMesh->surface);
      }
    }

    try
    {
      if (this->ThrowOnSelfIntersection)
      {
        switch (this->OperationType)
        {
          case vtkSHYXBooleanOperationFilter::DIFFERENCE:
            res = pmp::corefine_and_compute_difference(cgalInputMesh->surface,
              cgalSourceMesh->surface, cgalOutMesh->surface,
              pmp::parameters::throw_on_self_intersection(true), pmp::parameters::all_default());
            break;
          case vtkSHYXBooleanOperationFilter::INTERSECTION:
            res = pmp::corefine_and_compute_intersection(cgalInputMesh->surface,
              cgalSourceMesh->surface, cgalOutMesh->surface,
              pmp::parameters::throw_on_self_intersection(true), pmp::parameters::all_default());
            break;
          case vtkSHYXBooleanOperationFilter::UNION:
            res = pmp::corefine_and_compute_union(cgalInputMesh->surface, cgalSourceMesh->surface,
              cgalOutMesh->surface, pmp::parameters::throw_on_self_intersection(true),
              pmp::parameters::all_default());
            break;
          default:
            vtkErrorMacro("Unknown boolean operation.");
            res = false;
            break;
        }
      }
      else
      {
        switch (this->OperationType)
        {
          case vtkSHYXBooleanOperationFilter::DIFFERENCE:
            res = pmp::corefine_and_compute_difference(cgalInputMesh->surface,
              cgalSourceMesh->surface, cgalOutMesh->surface,
              pmp::parameters::throw_on_self_intersection(false), pmp::parameters::all_default());
            break;
          case vtkSHYXBooleanOperationFilter::INTERSECTION:
            res = pmp::corefine_and_compute_intersection(cgalInputMesh->surface,
              cgalSourceMesh->surface, cgalOutMesh->surface,
              pmp::parameters::throw_on_self_intersection(false), pmp::parameters::all_default());
            break;
          case vtkSHYXBooleanOperationFilter::UNION:
            res = pmp::corefine_and_compute_union(cgalInputMesh->surface, cgalSourceMesh->surface,
              cgalOutMesh->surface, pmp::parameters::throw_on_self_intersection(false),
              pmp::parameters::all_default());
            break;
          default:
            vtkErrorMacro("Unknown boolean operation.");
            res = false;
            break;
        }
      }
    }
    catch (const CGAL::Polygon_mesh_processing::Corefinement::Self_intersection_exception&)
    {
      vtkErrorMacro(
        << "Self-intersection encountered during boolean (ThrowOnSelfIntersection is on).");
      return 0;
    }
  }
  catch (const std::exception& e)
  {
    vtkErrorMacro("CGAL exception: " << e.what());
    return 0;
  }

  if (!res)
  {
    vtkWarningMacro(<< "Boolean operation returned false. Diagnostics: input self-intersect="
                    << pmp::does_self_intersect(cgalInputMesh->surface)
                    << " source self-intersect=" << pmp::does_self_intersect(cgalSourceMesh->surface)
                    << " input bounds volume=" << pmp::does_bound_a_volume(cgalInputMesh->surface)
                    << " source bounds volume="
                    << pmp::does_bound_a_volume(cgalSourceMesh->surface));
    return 0;
  }

  vtkCGALHelper::toVTK(cgalOutMesh.get(), output);
  this->interpolateAttributes(inputData, output);
  return 1;
}
