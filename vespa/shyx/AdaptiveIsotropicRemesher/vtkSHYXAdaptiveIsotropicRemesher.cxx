#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkDataObject.h>
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
void vtkSHYXAdaptiveIsotropicRemesher::RefreshSuggestedEdgeLengthsFromBounds()
{
  if (this->GetNumberOfInputConnections(0) < 1)
  {
    vtkWarningMacro("RefreshSuggestedEdgeLengthsFromBounds: no input on port 0.");
    return;
  }

  vtkPolyData* input = nullptr;

  // Prefer the producer's output port: when this method runs from ParaView InvokeCommand,
  // GetPolyDataInput()/GetInputDataObject() are often still empty until we read from upstream.
  vtkAlgorithmOutput* inConn = this->GetInputConnection(0, 0);
  if (inConn)
  {
    vtkAlgorithm* producer = inConn->GetProducer();
    const int outIndex     = inConn->GetIndex();
    if (producer)
    {
      producer->Update(outIndex);
      input = vtkPolyData::SafeDownCast(producer->GetOutputDataObject(outIndex));
    }
  }

  if (!input || input->GetNumberOfPoints() == 0)
  {
    input = vtkPolyData::SafeDownCast(this->GetInputDataObject(0, 0));
  }
  if (!input || input->GetNumberOfPoints() == 0)
  {
    input = this->GetPolyDataInput(0);
  }

  if (!input || input->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro(
      "RefreshSuggestedEdgeLengthsFromBounds: could not obtain a non-empty vtkPolyData input.");
    return;
  }

  const double diag = input->GetLength();
  if (diag <= 0.0)
  {
    vtkWarningMacro("RefreshSuggestedEdgeLengthsFromBounds: input diagonal length is zero.");
    return;
  }

  this->SetMinEdgeLength(0.005 * diag);
  this->SetMaxEdgeLength(0.05 * diag);
  this->Modified();
}

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

  const double diag = input->GetLength();
  double minLen     = this->MinEdgeLength;
  double maxLen     = this->MaxEdgeLength;
  if (minLen <= 0.0)
  {
    minLen = 0.005 * diag;
  }
  if (maxLen <= 0.0)
  {
    maxLen = 0.05 * diag;
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
