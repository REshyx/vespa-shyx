#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
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
} // namespace

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdint>
#include <exception>
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
  this->SetNumberOfOutputPorts(4);
  this->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_NONE, nullptr);
}

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::~vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetFeatureMaskArrayName(nullptr);
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
  os << indent << "FeatureSizingStandAlone: " << (this->FeatureSizingStandAlone ? "on" : "off")
     << std::endl;
  os << indent << "FeatureMinEdgeLength: " << this->FeatureMinEdgeLength << std::endl;
  os << indent << "FeatureMaxEdgeLength: " << this->FeatureMaxEdgeLength << std::endl;
  os << indent << "FeatureAdaptiveTolerance: " << this->FeatureAdaptiveTolerance << std::endl;
  os << indent << "AdaptiveSizingNeighborMaxRatio: " << this->AdaptiveSizingNeighborMaxRatio << std::endl;
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
  if (port == 0 || port == 1 || port == 2 || port == 3)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesher::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input          = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output          = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outputFeatures  = vtkPolyData::GetData(outputVector, 1);
  vtkPolyData* outputMaskPatch = vtkPolyData::GetData(outputVector, 2);
  vtkPolyData* outputSizingDiag = vtkPolyData::GetData(outputVector, 3);

  if (!input || !output || !outputFeatures || !outputMaskPatch || !outputSizingDiag)
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
  if (this->NumberOfRelaxationSteps < 0)
  {
    vtkErrorMacro("NumberOfRelaxationSteps must be >= 0, got " << this->NumberOfRelaxationSteps);
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

  const double featMinLen = this->FeatureMinEdgeLength;
  const double featMaxLen = this->FeatureMaxEdgeLength;
  if (this->FeatureSizingStandAlone)
  {
    if (this->FeatureAdaptiveTolerance <= 0.0)
    {
      vtkErrorMacro("FeatureAdaptiveTolerance must be positive when FeatureSizingStandAlone is on, "
                    "got " << this->FeatureAdaptiveTolerance);
      return 0;
    }
    if (!(featMinLen > 0.0 && featMaxLen > featMinLen))
    {
      vtkErrorMacro("Need 0 < FeatureMinEdgeLength < FeatureMaxEdgeLength when "
                    "FeatureSizingStandAlone is on (got "
        << featMinLen << " / " << featMaxLen
        << "). In ParaView use scale/Reset on the Feature Min/Max Edge Length "
           "(BoundsDomain); non-ParaView callers must set positive lengths explicitly.");
      return 0;
    }
  }

  const double sizingFeatTol =
    this->FeatureSizingStandAlone ? this->FeatureAdaptiveTolerance : this->AdaptiveTolerance;
  const double sizingFeatMin = this->FeatureSizingStandAlone ? featMinLen : minLen;
  const double sizingFeatMax = this->FeatureSizingStandAlone ? featMaxLen : maxLen;

  std::vector<char> inputFeatureMask;
  bool inputFeatureMaskOk = false;
  if (this->FeatureMaskEnabled)
  {
    inputFeatureMaskOk = ComputeFeatureFaceMask(input, this->FeatureMaskArrayName,
      this->FeatureMaskThreshold, this->FeatureMaskAllScalars, inputFeatureMask);
    if (!inputFeatureMaskOk)
    {
      vtkWarningMacro("Feature mask is enabled but the chosen array could not be evaluated on the "
                      "input. Output port 2 is empty; CGAL constraints and port 1 use unmasked "
                      "sharp-edge detection.");
      outputMaskPatch->Initialize();
    }
    else
    {
      ExtractMaskedFacesPolyData(input, inputFeatureMask, outputMaskPatch);
    }
  }
  else
  {
    outputMaskPatch->Initialize();
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

  std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
    std::make_unique<vtkCGALHelper::Vespa_surface>();
  std::vector<Graph_Faces> vtkCellToCgalFace;
  vtkCGALHelper::toCGAL(input, cgalMesh.get(), &vtkCellToCgalFace);

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

  try
  {
    auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
    if (this->DetectFeatureEdges)
    {
      DetectSharpEdgesWithFilter(
        cgalMesh->surface, this->ProtectAngle, this->SharpFeatureSideFilter, featureEdges);
      if (this->FeatureMaskEnabled && inputFeatureMaskOk)
      {
        ApplyFeatureRegionMaskToSharpEdges(
          cgalMesh->surface, featureEdges, inputFeatureMask, inputFaceSlotToVtkCell);
      }

      if (this->FeatureMaskEnabled && inputFeatureMaskOk &&
        outputMaskPatch->GetNumberOfCells() > 0)
      {
        vtkCGALHelper::Vespa_surface cgalForPort1Patch;
        if (vtkCGALHelper::toCGAL(outputMaskPatch, &cgalForPort1Patch, nullptr))
        {
          auto patchFeat = get(CGAL::edge_is_feature, cgalForPort1Patch.surface);
          DetectSharpEdgesWithFilter(cgalForPort1Patch.surface, this->ProtectAngle,
            this->SharpFeatureSideFilter, patchFeat);
          LiftPatchSharpFeaturesToFullMesh(this, cgalMesh->surface, featureEdges,
            cgalForPort1Patch.surface, patchFeat, L);
        }
      }
    }

    if (!selected.empty())
    {
      AddSelectionBoundaryUsingVtkTopology(this, input, cgalMesh->surface, featureEdges, selected);
    }

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
        .edge_is_constrained_map(featureEdges);
    };

    const unsigned int remeshIterations =
      static_cast<unsigned int>(this->NumberOfIterations);

    using SizingTy = FeatureAwareAdaptiveSizingField<decltype(featureEdges)>;
    std::optional<SizingTy> sizingStorage;
    if (patchRemesh)
    {
      sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen), sizingFeatTol,
        std::make_pair(sizingFeatMin, sizingFeatMax), remeshFaces, cgalMesh->surface, featureEdges,
        static_cast<double>(this->AdaptiveSizingNeighborMaxRatio));
    }
    else
    {
      sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen), sizingFeatTol,
        std::make_pair(sizingFeatMin, sizingFeatMax), cgalMesh->surface.faces(), cgalMesh->surface,
        featureEdges, static_cast<double>(this->AdaptiveSizingNeighborMaxRatio));
    }
    SizingTy& sizing = *sizingStorage;

    // Port 3: geometry is the CGAL mesh immediately before the last remesh sub-step —
    // the input converted to VTK when NumberOfIterations==1; after NumberOfIterations-1
    // single-iteration CGAL passes otherwise. ICC preview mask: iterations==1 uses the input VTK
    // cell mask; iterations>1 probes input attributes onto port 3 then evaluates the same Feature
    // mask rule on the preview (requires UpdateAttributes).
    const auto fillSizingIccPreviewPort = [&]() {
      CGAL_Surface& smDiag = cgalMesh->surface;
      vtkNew<vtkPolyData> snap;
      vtkCGALHelper::toVTK(cgalMesh.get(), snap);
      outputSizingDiag->DeepCopy(snap);
      const vtkIdType nPtsDiag = outputSizingDiag->GetNumberOfPoints();

      {
        std::vector<char> iccFaceMaskPv;
        const std::vector<char>* iccMaskPtrPv = nullptr;
        if (this->FeatureMaskEnabled)
        {
          if (remeshIterations <= 1u)
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
                "(ParaView: transfer input arrays to output) so mask fields are probed onto port 3.");
            }
            else
            {
              this->interpolateAttributes(input, outputSizingDiag);
              std::vector<char> evaluatedPreviewMask;
              if (ComputeFeatureFaceMask(outputSizingDiag, this->FeatureMaskArrayName,
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
      vtkNew<vtkDoubleArray> szFeatureDiag;
      szGlobalDiag->SetName("VespaAdaptiveSizeGlobal");
      szFeatureDiag->SetName("VespaAdaptiveSizeFeature");
      szGlobalDiag->SetNumberOfComponents(1);
      szFeatureDiag->SetNumberOfComponents(1);
      szGlobalDiag->SetNumberOfTuples(nPtsDiag);
      szFeatureDiag->SetNumberOfTuples(nPtsDiag);

      const double nanDiag = std::numeric_limits<double>::quiet_NaN();
      const auto optPg =
        smDiag.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_global");
      const auto optPf =
        smDiag.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_feature");
      if (optPg.has_value() && optPf.has_value())
      {
        vtkIdType pid = 0;
        for (CGAL_Surface::Vertex_index vx : CGAL::vertices(smDiag))
        {
          szGlobalDiag->SetValue(pid, boost::get(*optPg, vx));
          szFeatureDiag->SetValue(pid, boost::get(*optPf, vx));
          ++pid;
        }
      }
      else
      {
        for (vtkIdType pid = 0; pid < nPtsDiag; ++pid)
        {
          szGlobalDiag->SetValue(pid, nanDiag);
          szFeatureDiag->SetValue(pid, nanDiag);
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
        vnPtrDiag, iccKminDiag, iccKmaxDiag, iccKmeanDiscard, iccKgaussDiscard);
      (void)iccKmeanDiscard;
      (void)iccKgaussDiscard;

      vtkNew<vtkDoubleArray> iccPMinDiag;
      vtkNew<vtkDoubleArray> iccPMaxDiag;
      vtkNew<vtkDoubleArray> szGlobUncDiag;
      vtkNew<vtkDoubleArray> szFeatUncDiag;
      iccPMinDiag->SetName("VespaIccPrincipalCurvatureMin");
      iccPMaxDiag->SetName("VespaIccPrincipalCurvatureMax");
      szGlobUncDiag->SetName("VespaAdaptiveSizeGlobalUncapped");
      szFeatUncDiag->SetName("VespaAdaptiveSizeFeatureUncapped");
      iccPMinDiag->SetNumberOfComponents(1);
      iccPMaxDiag->SetNumberOfComponents(1);
      szGlobUncDiag->SetNumberOfComponents(1);
      szFeatUncDiag->SetNumberOfComponents(1);
      iccPMinDiag->SetNumberOfTuples(nPtsDiag);
      iccPMaxDiag->SetNumberOfTuples(nPtsDiag);
      szGlobUncDiag->SetNumberOfTuples(nPtsDiag);
      szFeatUncDiag->SetNumberOfTuples(nPtsDiag);

      {
        vtkIdType pid = 0;
        for (CGAL_Surface::Vertex_index vx : CGAL::vertices(smDiag))
        {
          const std::size_t idi = static_cast<std::size_t>(vx.idx());
          const double km =
            (idi < iccKminDiag.size()) ? iccKminDiag[idi] : nanDiag;
          const double kM =
            (idi < iccKmaxDiag.size()) ? iccKmaxDiag[idi] : nanDiag;
          iccPMinDiag->SetValue(pid, km);
          iccPMaxDiag->SetValue(pid, kM);
          szGlobUncDiag->SetValue(
            pid, VespaUncappedAdaptiveEdgeLengthFromTol(this->AdaptiveTolerance, km, kM));
          szFeatUncDiag->SetValue(
            pid, VespaUncappedAdaptiveEdgeLengthFromTol(sizingFeatTol, km, kM));
          ++pid;
        }
      }

      vtkPointData* pddiag = outputSizingDiag->GetPointData();
      pddiag->AddArray(szGlobalDiag);
      pddiag->AddArray(szFeatureDiag);
      pddiag->AddArray(szGlobUncDiag);
      pddiag->AddArray(szFeatUncDiag);
      pddiag->AddArray(iccPMinDiag);
      pddiag->AddArray(iccPMaxDiag);
      AddVespaIccNonMaskNormalsAsPointArray(smDiag, outputSizingDiag, this);
    };

    auto doRemeshSingleIteration = [&]() {
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
    };

    if (remeshIterations <= 1u)
    {
      fillSizingIccPreviewPort();
      doRemeshSingleIteration();
    }
    else
    {
      const unsigned int preliminaryPasses = remeshIterations - 1u;
      for (unsigned int pass = 0; pass < preliminaryPasses; ++pass)
      {
        if (this->RemeshRecomputeCurvatureEachIteration && pass > 0)
        {
          sizing.recompute_curvature(cgalMesh->surface);
        }
        doRemeshSingleIteration();
      }
      if (this->RemeshRecomputeCurvatureEachIteration)
      {
        sizing.recompute_curvature(cgalMesh->surface);
      }
      fillSizingIccPreviewPort();
      doRemeshSingleIteration();
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->interpolateAttributes(input, output);

  {
    CGAL_Surface& smFinal = cgalMesh->surface;
    std::vector<char> outIccFaceMask;
    const std::vector<char>* iccMaskPtrFinal = nullptr;
    if (this->FeatureMaskEnabled)
    {
      std::vector<char> evaluatedFeatureMaskOut;
      if (ComputeFeatureFaceMask(output, this->FeatureMaskArrayName,
            this->FeatureMaskThreshold, this->FeatureMaskAllScalars, evaluatedFeatureMaskOut))
      {
        std::vector<vtkIdType> faceIdxToVtkOut;
        BuildCgalFaceIndexToVtkCell(smFinal, faceIdxToVtkOut);
        BuildCgalFaceMaskFromVtkCells(
          smFinal, evaluatedFeatureMaskOut, faceIdxToVtkOut, outIccFaceMask);
        iccMaskPtrFinal = &outIccFaceMask;
      }
    }
    PrepareIccVertexNormalsForAdaptiveSizing(smFinal, iccMaskPtrFinal);
    AddVespaIccNonMaskNormalsAsPointArray(smFinal, output, this);
  }

  if (this->DetectFeatureEdges)
  {
    bool linesFromInputMaskPatch = false;
    if (this->FeatureMaskEnabled && inputFeatureMaskOk && outputMaskPatch->GetNumberOfCells() > 0)
    {
      try
      {
        vtkCGALHelper::Vespa_surface cgalPort2;
        if (vtkCGALHelper::toCGAL(outputMaskPatch, &cgalPort2, nullptr))
        {
          auto featPort2 = get(CGAL::edge_is_feature, cgalPort2.surface);
          DetectSharpEdgesWithFilter(
            cgalPort2.surface, this->ProtectAngle, this->SharpFeatureSideFilter, featPort2);
          FillFeaturePolyDataSharpLinesOnly(cgalPort2.surface, featPort2, outputFeatures);
          linesFromInputMaskPatch = true;
        }
        else
        {
          vtkWarningMacro("Could not build a CGAL surface from the input mask patch (port 2); "
                          "port 1 lines will use the full remeshed surface.");
        }
      }
      catch (const std::exception& e)
      {
        vtkWarningMacro("Sharp feature lines on input mask patch failed: "
          << e.what() << " Falling back to full remeshed surface for lines.");
      }
    }

    if (!linesFromInputMaskPatch)
    {
      CGAL_Surface& sm = cgalMesh->surface;
      auto featOutMap = get(CGAL::edge_is_feature, sm);
      DetectSharpEdgesWithFilter(sm, this->ProtectAngle, this->SharpFeatureSideFilter, featOutMap);
      if (this->FeatureMaskEnabled)
      {
        std::vector<char> outMask;
        const bool outOk = ComputeFeatureFaceMask(output, this->FeatureMaskArrayName,
          this->FeatureMaskThreshold, this->FeatureMaskAllScalars, outMask);
        if (!outOk)
        {
          vtkWarningMacro("Feature mask is enabled but the array could not be evaluated on the "
                          "remeshed surface (e.g. missing data after interpolation); port 1 lines "
                          "use unmasked sharp features on the full output.");
        }
        else
        {
          std::vector<vtkIdType> faceIdxToVtk;
          BuildCgalFaceIndexToVtkCell(sm, faceIdxToVtk);
          ApplyFeatureRegionMaskToSharpEdges(sm, featOutMap, outMask, faceIdxToVtk);
        }
      }
      FillFeaturePolyDataSharpLinesOnly(sm, featOutMap, outputFeatures);
    }
  }
  else
  {
    outputFeatures->Initialize();
  }

  return 1;
}
