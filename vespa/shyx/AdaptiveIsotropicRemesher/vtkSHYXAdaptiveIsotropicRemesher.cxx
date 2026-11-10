#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkCellType.h>
#include <vtkIdList.h>

#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/Kernel_traits.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/selection.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <optional>

namespace
{
double VespaUncappedAdaptiveEdgeLengthFromTol(double tol, double kmin, double kmax)
{
  if (std::isnan(kmin) || std::isnan(kmax))
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double maxAbs = (std::max)(std::abs(kmin), std::abs(kmax));
  if (maxAbs <= 0.0)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double vsq = 6.0 * tol / maxAbs - 3.0 * tol * tol;
  if (vsq <= 0.0)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return std::sqrt(vsq);
}

void SetDiagnosticsBlock(
  vtkMultiBlockDataSet* mb, unsigned int index, vtkDataObject* block, const char* name)
{
  mb->SetBlock(index, block);
  if (vtkInformation* meta = mb->GetMetaData(index))
  {
    meta->Set(vtkCompositeDataSet::NAME(), name);
  }
}
} // namespace

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

vtkStandardNewMacro(vtkSHYXAdaptiveIsotropicRemesher);

namespace pmp = CGAL::Polygon_mesh_processing;

#include "vtkSHYXAdaptiveIsotropicRemesherInternals.h"
#include "vtkSHYXFeatureAwareAdaptiveSizingField.h"

using namespace vespa_shyx_air_remesh_internals;

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(2);
  this->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_NONE, nullptr);
}

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::~vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetFeatureMaskArrayName(nullptr);
}

//------------------------------------------------------------------------------
double* vtkSHYXAdaptiveIsotropicRemesher::GetUncappedSizeHistCenters()
{
  return this->UncappedSizeHistCenters;
}

//------------------------------------------------------------------------------
double* vtkSHYXAdaptiveIsotropicRemesher::GetUncappedSizeHistCounts()
{
  return this->UncappedSizeHistCounts;
}

//------------------------------------------------------------------------------
double* vtkSHYXAdaptiveIsotropicRemesher::GetUncappedSizeHistRange()
{
  return this->UncappedSizeHistRange;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::GetUncappedSizeHistSampleCount()
{
  return this->UncappedSizeHistSampleCount;
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::ClearUncappedSizeHistogram()
{
  for (int i = 0; i < UncappedSizeHistBinCount; ++i)
  {
    this->UncappedSizeHistCenters[i] = 0.0;
    this->UncappedSizeHistCounts[i] = 0.0;
  }
  this->UncappedSizeHistRange[0] = 0.0;
  this->UncappedSizeHistRange[1] = 0.0;
  this->UncappedSizeHistSampleCount = 0;
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::FillUncappedSizeHistogram(
  const std::vector<double>& uncappedSizes)
{
  this->ClearUncappedSizeHistogram();
  double vmin = std::numeric_limits<double>::infinity();
  double vmax = -std::numeric_limits<double>::infinity();
  int nValid = 0;
  for (double v : uncappedSizes)
  {
    if (!std::isfinite(v) || !(v > 0.0))
    {
      continue;
    }
    vmin = (std::min)(vmin, v);
    vmax = (std::max)(vmax, v);
    ++nValid;
  }
  if (nValid < 1 || !std::isfinite(vmin) || !std::isfinite(vmax))
  {
    return;
  }
  if (!(vmax > vmin))
  {
    vmax = vmin + (std::max)(1.0e-12, std::abs(vmin) * 1.0e-9);
  }

  const double span = vmax - vmin;
  for (int i = 0; i < UncappedSizeHistBinCount; ++i)
  {
    this->UncappedSizeHistCenters[i] =
      vmin + (static_cast<double>(i) + 0.5) * span / UncappedSizeHistBinCount;
    this->UncappedSizeHistCounts[i] = 0.0;
  }
  for (double v : uncappedSizes)
  {
    if (!std::isfinite(v) || !(v > 0.0))
    {
      continue;
    }
    double t = (v - vmin) / span;
    t = (std::min)(1.0, (std::max)(0.0, t));
    int bin = static_cast<int>(t * (UncappedSizeHistBinCount - 1));
    if (bin < 0)
    {
      bin = 0;
    }
    else if (bin >= UncappedSizeHistBinCount)
    {
      bin = UncappedSizeHistBinCount - 1;
    }
    this->UncappedSizeHistCounts[bin] += 1.0;
  }
  this->UncappedSizeHistRange[0] = vmin;
  this->UncappedSizeHistRange[1] = vmax;
  this->UncappedSizeHistSampleCount = nValid;
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::SetRemeshRangeArrayName(const char* name)
{
  const bool hasName = (name != nullptr && name[0] != '\0');
  this->SetInputArrayToProcess(0, 0, 0,
    hasName ? vtkDataObject::FIELD_ASSOCIATION_POINTS : vtkDataObject::FIELD_ASSOCIATION_NONE,
    hasName ? name : nullptr);
}

//------------------------------------------------------------------------------
const char* vtkSHYXAdaptiveIsotropicRemesher::GetRemeshRangeArrayName()
{
  vtkInformation* const ai = this->GetInputArrayInformation(0);
  if (ai && ai->Has(vtkDataObject::FIELD_NAME()))
  {
    const char* const n = ai->Get(vtkDataObject::FIELD_NAME());
    if (n && n[0] != '\0')
    {
      return n;
    }
  }
  return nullptr;
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXAdaptiveIsotropicRemesher::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
  os << indent << "MaxEdgeLength: " << this->MaxEdgeLength << std::endl;
  os << indent << "AdaptiveTolerance: " << this->AdaptiveTolerance << std::endl;
  os << indent << "IccNeighborhoodMode: " << this->IccNeighborhoodMode << std::endl;
  os << indent << "IccNeighborhoodRings: " << this->IccNeighborhoodRings << std::endl;
  os << indent << "IccBallRadius: " << this->IccBallRadius << std::endl;
  os << indent << "AdaptiveSizingNeighborMaxRatio: " << this->AdaptiveSizingNeighborMaxRatio << std::endl;
  os << indent << "ScaleToRange: " << (this->ScaleToRange ? "on" : "off") << std::endl;
  os << indent << "SizingFieldScale: " << this->SizingFieldScale << std::endl;
  os << indent << "RemeshRecomputeCurvatureEachIteration: "
     << (this->RemeshRecomputeCurvatureEachIteration ? "on" : "off") << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
  os << indent << "NumberOfRelaxationSteps: " << this->NumberOfRelaxationSteps << std::endl;
  os << indent << "SharpFeatureSideFilter: " << this->SharpFeatureSideFilter << std::endl;
  os << indent << "RemeshProtectConstraints: " << (this->RemeshProtectConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshCollapseConstraints: " << (this->RemeshCollapseConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshRelaxConstraints: " << (this->RemeshRelaxConstraints ? "on" : "off") << std::endl;
  os << indent << "RemeshDoSplit: " << (this->RemeshDoSplit ? "on" : "off") << std::endl;
  os << indent << "RemeshDoCollapse: " << (this->RemeshDoCollapse ? "on" : "off") << std::endl;
  os << indent << "RemeshDoFlip: " << (this->RemeshDoFlip ? "on" : "off") << std::endl;
  os << indent << "RemeshDoProject: " << (this->RemeshDoProject ? "on" : "off") << std::endl;
  os << indent << "DetectFeatureEdges: " << (this->DetectFeatureEdges ? "on" : "off") << std::endl;
  os << indent << "FeatureMaskEnabled: " << (this->FeatureMaskEnabled ? "on" : "off") << std::endl;
  os << indent << "FeatureMaskArrayName: "
     << (this->FeatureMaskArrayName ? this->FeatureMaskArrayName : "(null)") << std::endl;
  os << indent << "FeatureMaskThreshold: " << this->FeatureMaskThreshold << std::endl;
  os << indent << "FeatureMaskAllScalars: " << (this->FeatureMaskAllScalars ? "on" : "off") << std::endl;
  os << indent << "RemeshRegionMode: " << this->RemeshRegionMode << std::endl;
  if (const char* const rra = this->GetRemeshRangeArrayName())
  {
    os << indent << "RemeshRangeArrayName: " << rra << std::endl;
  }
  else
  {
    os << indent << "RemeshRangeArrayName: (null)" << std::endl;
  }
  os << indent << "RemeshRangeMin: " << this->RemeshRangeMin << std::endl;
  os << indent << "RemeshRangeMax: " << this->RemeshRangeMax << std::endl;
  os << indent << "RemeshRangeAllScalars: " << (this->RemeshRangeAllScalars ? "on" : "off") << std::endl;
  os << indent << "EnableRemesh: " << (this->EnableRemesh ? "on" : "off") << std::endl;
  os << indent << "UncappedSizeHistSampleCount: " << this->UncappedSizeHistSampleCount << std::endl;
  os << indent << "UncappedSizeHistRange: [" << this->UncappedSizeHistRange[0] << ", "
     << this->UncappedSizeHistRange[1] << "]" << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkMultiBlockDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  vtkMultiBlockDataSet* diagnostics = vtkMultiBlockDataSet::GetData(outputVector, 1);
  // Mask faces also feed CGAL constraints. Sizing preview is filled before the last remesh pass.
  // Both are published with the sharp-feature lines as named blocks on port 1.
  vtkNew<vtkPolyData> maskPatch;
  vtkNew<vtkPolyData> sizingPreview;
  vtkNew<vtkPolyData> featureLines;

  if (!input || !output || !diagnostics)
  {
    vtkErrorMacro("Missing input or output.");
    return 0;
  }
  diagnostics->Initialize();

  this->ClearUncappedSizeHistogram();

  if (this->AdaptiveTolerance <= 0.0)
  {
    vtkErrorMacro("AdaptiveTolerance must be positive, got " << this->AdaptiveTolerance);
    return 0;
  }
  if (this->EnableRemesh)
  {
    if (this->NumberOfIterations < 1)
    {
      vtkErrorMacro("NumberOfIterations must be >= 1.");
      return 0;
    }
    if (this->NumberOfRelaxationSteps < 0)
    {
      vtkErrorMacro("NumberOfRelaxationSteps must be >= 0, got " << this->NumberOfRelaxationSteps);
      return 0;
    }
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

  const double minLen = this->MinEdgeLength;
  const double maxLen = this->MaxEdgeLength;
  if (!(minLen > 0.0 && maxLen > minLen))
  {
    vtkErrorMacro("Need 0 < MinEdgeLength < MaxEdgeLength (got "
      << minLen << " / " << maxLen
      << "). In ParaView use scale/Reset on Min and Max Edge Length "
         "(BoundsDomain); non-ParaView callers must set positive lengths explicitly.");
    return 0;
  }


  std::vector<char> inputFeatureMask;
  bool inputFeatureMaskOk = false;
  if (this->FeatureMaskEnabled)
  {
    inputFeatureMaskOk = ComputeFeatureFaceMask(input, this->FeatureMaskArrayName,
      this->FeatureMaskThreshold, this->FeatureMaskAllScalars, inputFeatureMask);
    if (!inputFeatureMaskOk)
    {
      vtkWarningMacro("Feature mask is enabled but the chosen array could not be evaluated on the "
                      "input. Sharp-edge constraints use unmasked detection.");
    }
    else
    {
      ExtractMaskedFacesPolyData(input, inputFeatureMask, maskPatch);
    }
  }

  std::set<vtkIdType> selected;
  if (this->RemeshRegionMode == 1)
  {
    if (!CollectCellsByScalarValueRange(input, this->GetRemeshRangeArrayName(), this->RemeshRangeMin,
          this->RemeshRangeMax, this->RemeshRangeAllScalars, selected))
    {
      vtkWarningMacro("Remesh region mode is scalar range but the range could not be applied "
                      "(need a non-empty array name, RemeshRangeMin <= RemeshRangeMax, and a "
                      "matching point- or cell-centered array on the input). Remeshing globally.");
    }
  }
  else if (this->GetNumberOfInputConnections(1) > 0)
  {
    vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
    if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
    {
      vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
      if (inputSel && inputSel->GetNumberOfNodes() > 0)
      {
        vtkNew<vtkExtractSelection> extractSelection;
        extractSelection->SetInputData(0, input);
        extractSelection->SetInputData(1, inputSel);
        extractSelection->Update();
        vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
        if (extracted &&
          (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
        {
          CollectCellsFromExtracted(input, extracted, selected);
        }
      }
    }
  }

  // One Apply: toCGAL through toVTK. `step` names the phase that threw.
  std::string step = "Convert input to CGAL";
  try
  {
    const CgalThrowOnError cgalErrors;

    std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
      std::make_unique<vtkCGALHelper::Vespa_surface>();
    std::vector<Graph_Faces> vtkCellToCgalFace;
    if (!vtkCGALHelper::toCGAL(input, cgalMesh.get(), &vtkCellToCgalFace))
    {
      throw std::runtime_error("conversion returned false (check triangles and manifold)");
    }

    step = "Map remesh region";
    std::vector<vtkIdType> inputFaceSlotToVtkCell;
    if (this->FeatureMaskEnabled && inputFeatureMaskOk)
    {
      BuildCgalFaceSlotToInputVtkCellId(cgalMesh->surface, vtkCellToCgalFace, inputFaceSlotToVtkCell);
    }

    std::vector<Graph_Faces> remeshFaces;
    remeshFaces.reserve(selected.size());
    if (!selected.empty())
    {
      for (const vtkIdType cid : selected)
      {
        if (cid < 0 || cid >= static_cast<vtkIdType>(vtkCellToCgalFace.size()))
        {
          continue;
        }
        const auto fd = vtkCellToCgalFace[static_cast<size_t>(cid)];
        if (cgalMesh->surface.is_valid(fd))
        {
          remeshFaces.push_back(fd);
        }
      }
    }

    const bool patchRemesh = !remeshFaces.empty();
    if (!selected.empty() && !patchRemesh)
    {
      vtkWarningMacro(
        "Remesh region did not resolve to any remeshable faces on the input; remeshing globally.");
    }

    step = "Detect sharp features";
    auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
    if (this->EnableRemesh && this->DetectFeatureEdges)
    {
      DetectSharpEdgesWithFilter(
        cgalMesh->surface, this->ProtectAngle, this->SharpFeatureSideFilter, featureEdges);
      if (this->FeatureMaskEnabled && inputFeatureMaskOk)
      {
        ApplyFeatureRegionMaskToSharpEdges(
          cgalMesh->surface, featureEdges, inputFeatureMask, inputFaceSlotToVtkCell);
      }

      if (this->FeatureMaskEnabled && inputFeatureMaskOk &&
        maskPatch->GetNumberOfCells() > 0)
      {
        vtkCGALHelper::Vespa_surface cgalForPort1Patch;
        if (vtkCGALHelper::toCGAL(maskPatch, &cgalForPort1Patch, nullptr))
        {
          auto patchFeat = get(CGAL::edge_is_feature, cgalForPort1Patch.surface);
          DetectSharpEdgesWithFilter(cgalForPort1Patch.surface, this->ProtectAngle,
            this->SharpFeatureSideFilter, patchFeat);
          LiftPatchSharpFeaturesToFullMesh(this, cgalMesh->surface, featureEdges,
            cgalForPort1Patch.surface, patchFeat, L);
        }
      }
    }

    if (this->EnableRemesh && !selected.empty())
    {
      AddSelectionBoundaryUsingVtkTopology(this, input, cgalMesh->surface, featureEdges, selected);
    }

    step = "Build ICC sizing field";
    {
      std::vector<char> iccFaceMask;
      const std::vector<char>* iccMaskPtr = nullptr;
      if (this->FeatureMaskEnabled && inputFeatureMaskOk)
      {
        BuildCgalFaceMaskFromVtkCells(cgalMesh->surface, inputFeatureMask, inputFaceSlotToVtkCell,
          iccFaceMask);
        iccMaskPtr = &iccFaceMask;
      }
      PrepareIccVertexNormalsForAdaptiveSizing(cgalMesh->surface, iccMaskPtr);
    }

    const auto remeshNp = [&](unsigned int iteration_count) {
      return pmp::parameters::number_of_iterations(iteration_count)
        .number_of_relaxation_steps(static_cast<unsigned int>(this->NumberOfRelaxationSteps))
        .protect_constraints(this->RemeshProtectConstraints)
        .collapse_constraints(this->RemeshCollapseConstraints)
        .relax_constraints(this->RemeshRelaxConstraints)
        .do_split(this->RemeshDoSplit)
        .do_collapse(this->RemeshDoCollapse)
        .do_flip(this->RemeshDoFlip)
        .do_project(this->RemeshDoProject)
        .edge_is_constrained_map(featureEdges);
    };

    const unsigned int remeshIterations =
      static_cast<unsigned int>(this->NumberOfIterations);

    using SizingTy = FeatureAwareAdaptiveSizingField;
    std::vector<double> uncappedSizes;
    const int iccRings = (this->IccNeighborhoodMode == ICC_NEIGHBORHOOD_RINGS)
      ? this->IccNeighborhoodRings
      : 0;
    const double iccRadius = (this->IccNeighborhoodMode == ICC_NEIGHBORHOOD_BALL)
      ? this->IccBallRadius
      : -1.0;
    std::optional<SizingTy> sizingStorage;
    if (patchRemesh)
    {
      sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen), remeshFaces,
        cgalMesh->surface, static_cast<double>(this->AdaptiveSizingNeighborMaxRatio),
        this->ScaleToRange, &uncappedSizes, iccRadius, iccRings, this->SizingFieldScale);
    }
    else
    {
      sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen),
        cgalMesh->surface.faces(), cgalMesh->surface,
        static_cast<double>(this->AdaptiveSizingNeighborMaxRatio),
        this->ScaleToRange, &uncappedSizes, iccRadius, iccRings, this->SizingFieldScale);
    }
    this->FillUncappedSizeHistogram(uncappedSizes);
    SizingTy& sizing = *sizingStorage;

    // Sizing block: CGAL mesh immediately before the last remesh sub-step —
    // the input converted to VTK when NumberOfIterations==1; after NumberOfIterations-1
    // single-iteration CGAL passes otherwise. ICC preview mask: iterations==1 uses the input VTK
    // cell mask; iterations>1 probes input attributes onto the preview then evaluates the same
    // Feature mask rule (requires UpdateAttributes).
    const auto fillSizingIccPreviewPort = [&]() {
      CGAL_Surface& smDiag = cgalMesh->surface;
      vtkNew<vtkPolyData> snap;
      if (!vtkCGALHelper::toVTK(cgalMesh.get(), snap))
      {
        throw std::runtime_error("sizing preview toVTK returned false");
      }
      sizingPreview->DeepCopy(snap);
      const vtkIdType nPtsDiag = sizingPreview->GetNumberOfPoints();

      {
        std::vector<char> iccFaceMaskPv;
        const std::vector<char>* iccMaskPtrPv = nullptr;
        if (this->FeatureMaskEnabled)
        {
          if (remeshIterations <= 1u || !this->EnableRemesh)
          {
            if (inputFeatureMaskOk)
            {
              BuildCgalFaceMaskFromVtkCells(
                smDiag, inputFeatureMask, inputFaceSlotToVtkCell, iccFaceMaskPv);
              iccMaskPtrPv = &iccFaceMaskPv;
            }
          }
          else if (this->FeatureMaskArrayName != nullptr &&
            this->FeatureMaskArrayName[0] != '\0')
          {
            if (!this->UpdateAttributes)
            {
              vtkWarningMacro(
                "Feature mask ICC preview with Remesh iterations > 1 requires UpdateAttributes ON "
                "(ParaView: transfer input arrays to output) so mask fields are probed onto the sizing preview.");
            }
            else
            {
              this->interpolateAttributes(input, sizingPreview);
              std::vector<char> evaluatedPreviewMask;
              if (ComputeFeatureFaceMask(sizingPreview, this->FeatureMaskArrayName,
                    this->FeatureMaskThreshold, this->FeatureMaskAllScalars, evaluatedPreviewMask))
              {
                std::vector<vtkIdType> faceIdxPv;
                BuildCgalFaceIndexToVtkCell(smDiag, faceIdxPv);
                BuildCgalFaceMaskFromVtkCells(
                  smDiag, evaluatedPreviewMask, faceIdxPv, iccFaceMaskPv);
                iccMaskPtrPv = &iccFaceMaskPv;
              }
              else
              {
                vtkWarningMacro("ICC sizing preview: could not evaluate Feature mask on the probed "
                                "preview mesh (check array name and topology).");
              }
            }
          }
        }
        PrepareIccVertexNormalsForAdaptiveSizing(smDiag, iccMaskPtrPv);
      }

      sizing.recompute_sizes_from_current_icc_normals(smDiag);

      const auto vnMapOpt =
        smDiag.property_map<CGAL_Surface::Vertex_index, CGAL_Kernel::Vector_3>("v:vespa_icc_normal");

      vtkNew<vtkDoubleArray> szGlobalDiag;
      szGlobalDiag->SetName("VespaAdaptiveSizeGlobal");
      szGlobalDiag->SetNumberOfComponents(1);
      szGlobalDiag->SetNumberOfTuples(nPtsDiag);

      const double nanDiag = std::numeric_limits<double>::quiet_NaN();
      const auto optPg =
        smDiag.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_global");
      if (optPg.has_value())
      {
        vtkIdType pid = 0;
        for (CGAL_Surface::Vertex_index vx : CGAL::vertices(smDiag))
        {
          szGlobalDiag->SetValue(pid, boost::get(*optPg, vx));
          ++pid;
        }
      }
      else
      {
        for (vtkIdType pid = 0; pid < nPtsDiag; ++pid)
        {
          szGlobalDiag->SetValue(pid, nanDiag);
        }
      }

      std::vector<double> iccKminDiag;
      std::vector<double> iccKmaxDiag;
      std::vector<double> iccKmeanDiscard;
      std::vector<double> iccKgaussDiscard;
      const CGAL_Surface::Property_map<CGAL_Surface::Vertex_index, CGAL_Kernel::Vector_3>* vnPtrDiag =
        vnMapOpt.has_value() ? &(*vnMapOpt) : nullptr;
      // Principal + uncapped arrays are preview-only: use the **full** mesh as the ICC domain.
      // Patch-local Face_filtered_graph (patchRemesh + remeshFaces) only covers the remesh selection
      // plus a 1-ring halo; all other vertices keep initial NaN — often most of the surface (e.g. ~80%
      // when a small region is remeshed). Actual remesh sizing still uses the patch-expanded graph in
      // FeatureAwareAdaptiveSizingField's constructor; this path is intentionally global for VTK.
      ComputeIccVertexCurvatureScalars(smDiag, false, std::vector<CGAL_Surface::Face_index>{},
        vnPtrDiag, iccKminDiag, iccKmaxDiag, iccKmeanDiscard, iccKgaussDiscard,
        iccRadius, iccRings);
      (void)iccKmeanDiscard;
      (void)iccKgaussDiscard;

      vtkNew<vtkDoubleArray> iccPMinDiag;
      vtkNew<vtkDoubleArray> iccPMaxDiag;
      vtkNew<vtkDoubleArray> szGlobUncDiag;
      iccPMinDiag->SetName("VespaIccPrincipalCurvatureMin");
      iccPMaxDiag->SetName("VespaIccPrincipalCurvatureMax");
      szGlobUncDiag->SetName("VespaAdaptiveSizeGlobalUncapped");
      iccPMinDiag->SetNumberOfComponents(1);
      iccPMaxDiag->SetNumberOfComponents(1);
      szGlobUncDiag->SetNumberOfComponents(1);
      iccPMinDiag->SetNumberOfTuples(nPtsDiag);
      iccPMaxDiag->SetNumberOfTuples(nPtsDiag);
      szGlobUncDiag->SetNumberOfTuples(nPtsDiag);

      {
        vtkIdType pid = 0;
        for (CGAL_Surface::Vertex_index vx : CGAL::vertices(smDiag))
        {
          const std::size_t idi = static_cast<std::size_t>(vx.idx());
          const double km = (idi < iccKminDiag.size()) ? iccKminDiag[idi] : nanDiag;
          const double kM = (idi < iccKmaxDiag.size()) ? iccKmaxDiag[idi] : nanDiag;
          iccPMinDiag->SetValue(pid, km);
          iccPMaxDiag->SetValue(pid, kM);
          szGlobUncDiag->SetValue(
            pid, VespaUncappedAdaptiveEdgeLengthFromTol(this->AdaptiveTolerance, km, kM));
          ++pid;
        }
      }

      vtkPointData* pddiag = sizingPreview->GetPointData();
      pddiag->AddArray(szGlobalDiag);
      pddiag->AddArray(szGlobUncDiag);
      pddiag->AddArray(iccPMinDiag);
      pddiag->AddArray(iccPMaxDiag);
      AddVespaIccNonMaskNormalsAsPointArray(smDiag, sizingPreview, this);
      pddiag->SetActiveScalars("VespaAdaptiveSizeGlobal");
    };

    auto doRemeshSingleIteration = [&]() {
      // Compact Surface_mesh garbage left by prior remesh; without this, a subsequent
      // isotropic_remeshing can hang (see AdaptiveIsotropicRemesher/README.md §3).
      if (cgalMesh->surface.has_garbage())
      {
        cgalMesh->surface.collect_garbage();
      }
      if (patchRemesh)
      {
        pmp::isotropic_remeshing(
          remeshFaces, sizing, cgalMesh->surface, remeshNp(1));
      }
      else
      {
        pmp::isotropic_remeshing(
          cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(1));
      }
      if (cgalMesh->surface.has_garbage())
      {
        cgalMesh->surface.collect_garbage();
      }
    };

    if (!this->EnableRemesh)
    {
      step = "ICC sizing preview";
      fillSizingIccPreviewPort();
    }
    else if (remeshIterations <= 1u)
    {
      step = "ICC sizing preview";
      fillSizingIccPreviewPort();
      step = "Isotropic remesh 1/1";
      doRemeshSingleIteration();
    }
    else
    {
      const unsigned int preliminaryPasses = remeshIterations - 1u;
      for (unsigned int pass = 0; pass < preliminaryPasses; ++pass)
      {
        if (this->RemeshRecomputeCurvatureEachIteration && pass > 0)
        {
          step = "Recompute curvature " + std::to_string(pass + 1) + "/" +
            std::to_string(remeshIterations);
          if (cgalMesh->surface.has_garbage())
          {
            cgalMesh->surface.collect_garbage();
          }
          sizing.recompute_curvature(cgalMesh->surface);
        }
        step = "Isotropic remesh " + std::to_string(pass + 1) + "/" +
          std::to_string(remeshIterations);
        doRemeshSingleIteration();
      }
      if (this->RemeshRecomputeCurvatureEachIteration)
      {
        step = "Recompute curvature " + std::to_string(remeshIterations) + "/" +
          std::to_string(remeshIterations);
        if (cgalMesh->surface.has_garbage())
        {
          cgalMesh->surface.collect_garbage();
        }
        sizing.recompute_curvature(cgalMesh->surface);
      }
      step = "ICC sizing preview";
      fillSizingIccPreviewPort();
      step = "Isotropic remesh " + std::to_string(remeshIterations) + "/" +
        std::to_string(remeshIterations);
      doRemeshSingleIteration();
    }

    step = "Write remeshed surface";
    if (!vtkCGALHelper::toVTK(cgalMesh.get(), output))
    {
      throw std::runtime_error("toVTK returned false");
    }
    step = "Transfer input attributes";
    this->interpolateAttributes(input, output);

    step = "Extract sharp feature lines";
    if (this->EnableRemesh && this->DetectFeatureEdges)
    {
      try
      {
        bool linesFromInputMaskPatch = false;
        if (this->FeatureMaskEnabled && inputFeatureMaskOk && maskPatch->GetNumberOfCells() > 0)
        {
          try
          {
            vtkCGALHelper::Vespa_surface cgalMaskPatch;
            if (vtkCGALHelper::toCGAL(maskPatch, &cgalMaskPatch, nullptr))
            {
              auto featMask = get(CGAL::edge_is_feature, cgalMaskPatch.surface);
              DetectSharpEdgesWithFilter(cgalMaskPatch.surface, this->ProtectAngle,
                this->SharpFeatureSideFilter, featMask);
              FillFeaturePolyDataSharpLinesOnly(cgalMaskPatch.surface, featMask, featureLines);
              linesFromInputMaskPatch = true;
            }
            else
            {
              throw std::runtime_error("could not build a CGAL surface from the input mask patch");
            }
          }
          catch (const std::exception& e)
          {
            vtkWarningMacro("Extract sharp feature lines (input mask): " << e.what()
              << " Falling back to the full remeshed surface.");
          }
        }

        if (!linesFromInputMaskPatch)
        {
          CGAL_Surface& sm = cgalMesh->surface;
          auto featOutMap = get(CGAL::edge_is_feature, sm);
          DetectSharpEdgesWithFilter(
            sm, this->ProtectAngle, this->SharpFeatureSideFilter, featOutMap);
          if (this->FeatureMaskEnabled)
          {
            std::vector<char> outMask;
            const bool outOk = ComputeFeatureFaceMask(output, this->FeatureMaskArrayName,
              this->FeatureMaskThreshold, this->FeatureMaskAllScalars, outMask);
            if (!outOk)
            {
              vtkWarningMacro("Feature mask is enabled but the array could not be evaluated on the "
                              "remeshed surface (e.g. missing data after interpolation); sharp-feature "
                              "lines use unmasked edges on the full output.");
            }
            else
            {
              std::vector<vtkIdType> faceIdxToVtk;
              BuildCgalFaceIndexToVtkCell(sm, faceIdxToVtk);
              ApplyFeatureRegionMaskToSharpEdges(sm, featOutMap, outMask, faceIdxToVtk);
            }
          }
          FillFeaturePolyDataSharpLinesOnly(sm, featOutMap, featureLines);
        }
      }
      catch (const std::exception& e)
      {
        vtkWarningMacro("Extract sharp feature lines: " << e.what()
          << " The Sharp features block is empty; the remeshed surface is unchanged.");
        featureLines->Initialize();
      }
    }

    step = "Write diagnostics";
    diagnostics->SetNumberOfBlocks(3);
    SetDiagnosticsBlock(diagnostics, vtkSHYXAdaptiveIsotropicRemesher::SharpFeaturesBlock,
      featureLines, "Sharp features");
    SetDiagnosticsBlock(diagnostics, vtkSHYXAdaptiveIsotropicRemesher::MaskPatchBlock, maskPatch,
      "Mask patch");
    SetDiagnosticsBlock(diagnostics, vtkSHYXAdaptiveIsotropicRemesher::SizingPreviewBlock,
      sizingPreview, "Sizing ICC preview");
  }
  catch (const std::exception& e)
  {
    vtkErrorMacro("" << step.c_str() << ": " << e.what());
    output->Initialize();
    diagnostics->Initialize();
    return 0;
  }
  catch (...)
  {
    vtkErrorMacro("" << step.c_str() << ": unknown C++ exception");
    output->Initialize();
    diagnostics->Initialize();
    return 0;
  }

  return 1;
}
