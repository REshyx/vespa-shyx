#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkBoundingBox.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <CGAL/Polygon_mesh_processing/Adaptive_sizing_field.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>

#include <exception>
#include <memory>
#include <utility>

vtkStandardNewMacro(vtkSHYXAdaptiveIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
  os << indent << "MaxEdgeLength: " << this->MaxEdgeLength << std::endl;
  os << indent << "AdaptiveTolerance: " << this->AdaptiveTolerance << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input  = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);

  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }

  if (this->AdaptiveTolerance <= 0.0)
  {
    vtkErrorMacro("AdaptiveTolerance must be positive, got " << this->AdaptiveTolerance);
    return 0;
  }
  if (this->NumberOfIterations < 1)
  {
    vtkErrorMacro("NumberOfIterations must be >= 1.");
    return 0;
  }

  double b[6];
  input->GetBounds(b);
  vtkBoundingBox box;
  box.SetBounds(b);
  const double L = box.GetMaxLength();
  if (L <= 0.0)
  {
    vtkErrorMacro("Input has zero bounding-box extent.");
    return 0;
  }

  double minLen = this->MinEdgeLength;
  double maxLen = this->MaxEdgeLength;
  if (minLen <= 0.0)
  {
    minLen = 0.005 * L;
  }
  if (maxLen <= 0.0)
  {
    maxLen = 0.05 * L;
  }
  if (!(minLen > 0.0 && maxLen > minLen))
  {
    vtkErrorMacro(
      "Need 0 < MinEdgeLength < MaxEdgeLength (or leave min/max <= 0 for automatic bounds). "
      "Current effective min/max: "
      << minLen << " / " << maxLen);
    return 0;
  }

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  vtkCGALHelper::toCGAL(input, cgalMesh.get());

  try
  {
    auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
    pmp::detect_sharp_edges(cgalMesh->surface, this->ProtectAngle, featureEdges);

    pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
      std::make_pair(minLen, maxLen), cgalMesh->surface.faces(), cgalMesh->surface);

    pmp::isotropic_remeshing(cgalMesh->surface.faces(), sizing, cgalMesh->surface,
      pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->NumberOfIterations))
        .number_of_relaxation_steps(3)
        .protect_constraints(true)
        .edge_is_constrained_map(featureEdges));
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->interpolateAttributes(input, output);

  return 1;
}
