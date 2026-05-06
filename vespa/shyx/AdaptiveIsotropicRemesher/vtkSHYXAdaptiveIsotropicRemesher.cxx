#include "vtkSHYXAdaptiveIsotropicRemesher.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
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

#include <CGAL/Polygon_mesh_processing/Adaptive_sizing_field.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/interpolated_corrected_curvatures.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/Kernel_traits.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/selection.h>
#include <CGAL/number_utils.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
}

//------------------------------------------------------------------------------
vtkSHYXAdaptiveIsotropicRemesher::~vtkSHYXAdaptiveIsotropicRemesher()
{
  this->SetFeatureMaskArrayName(nullptr);
  this->SetRemeshRangeArrayName(nullptr);
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
  os << indent << "RemeshRecomputeCurvatureEachIteration: "
     << (this->RemeshRecomputeCurvatureEachIteration ? "on" : "off") << std::endl;
  os << indent << "ProtectAngle: " << this->ProtectAngle << std::endl;
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
  os << indent << "NumberOfRelaxationSteps: " << this->NumberOfRelaxationSteps << std::endl;
  os << indent << "ShapeSmoothingIterations: " << this->ShapeSmoothingIterations << std::endl;
  os << indent << "ShapeSmoothingTimeStep: " << this->ShapeSmoothingTimeStep << std::endl;
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
  os << indent << "RemeshRangeArrayName: "
     << (this->RemeshRangeArrayName ? this->RemeshRangeArrayName : "(null)") << std::endl;
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
  vtkPolyData* outputMaskPatchRemeshed = vtkPolyData::GetData(outputVector, 3);

  if (!input || !output || !outputFeatures || !outputMaskPatch || !outputMaskPatchRemeshed)
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
  if (this->ShapeSmoothingIterations < 0)
  {
    vtkErrorMacro("ShapeSmoothingIterations must be >= 0, got " << this->ShapeSmoothingIterations);
    return 0;
  }
  if (this->ShapeSmoothingIterations > 0 && this->ShapeSmoothingTimeStep <= 0.0)
  {
    vtkErrorMacro("ShapeSmoothingTimeStep must be positive when shape smoothing is enabled.");
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
    if (!CollectCellsByScalarValueRange(input, this->RemeshRangeArrayName, this->RemeshRangeMin,
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
        std::vector<vtkIdType> faceIdxToVtkIn;
        BuildCgalFaceIndexToVtkCell(cgalMesh->surface, faceIdxToVtkIn);
        ApplyFeatureRegionMaskToSharpEdges(
          cgalMesh->surface, featureEdges, inputFeatureMask, faceIdxToVtkIn);
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

    if (this->FeatureSizingStandAlone)
    {
      std::vector<char> iccFaceMask;
      const std::vector<char>* iccMaskPtr = nullptr;
      if (this->FeatureMaskEnabled && inputFeatureMaskOk)
      {
        std::vector<vtkIdType> faceIdxToVtkIn;
        BuildCgalFaceIndexToVtkCell(cgalMesh->surface, faceIdxToVtkIn);
        BuildCgalFaceMaskFromVtkCells(cgalMesh->surface, inputFeatureMask, faceIdxToVtkIn, iccFaceMask);
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
      static_cast<unsigned int>(std::max(0, this->NumberOfIterations));

    // Build sizing and run isotropic_remeshing. With FeatureSizingStandAlone off this is the
    // original single-field path (unchanged). With it on we use FeatureAwareAdaptiveSizingField,
    // which holds two independent per-vertex target maps -- one for the global region, one for
    // feature/constrained edges -- and dispatches per edge via featureEdges (sharp + mask
    // region/boundary + selection boundary -- whatever has been written into
    // edge_is_constrained_map up to this point). It is not safe to instantiate two
    // CGAL::Polygon_mesh_processing::Adaptive_sizing_field on the same mesh because they share
    // a dynamic_vertex_property_t<FT> map and the second one would silently overwrite the
    // first's per-vertex targets. The face_range fed to the field matches the range passed
    // to isotropic_remeshing, as required by Adaptive_sizing_field semantics.
    //
    // Optional RemeshRecomputeCurvatureEachIteration: one CGAL iteration per pass and refresh ICC
    // (dual field: recompute_curvature; single field: reconstruct Adaptive_sizing_field).
    if (this->RemeshRecomputeCurvatureEachIteration)
    {
      if (patchRemesh)
      {
        if (this->FeatureSizingStandAlone)
        {
          FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
            std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
            std::make_pair(featMinLen, featMaxLen), remeshFaces, cgalMesh->surface, featureEdges);
          for (unsigned int pass = 0; pass < remeshIterations; ++pass)
          {
            if (pass > 0)
            {
              sizing.recompute_curvature(cgalMesh->surface);
            }
            pmp::isotropic_remeshing(
              remeshFaces, sizing, cgalMesh->surface, remeshNp(1));
          }
        }
        else
        {
          for (unsigned int pass = 0; pass < remeshIterations; ++pass)
          {
            pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
              std::make_pair(minLen, maxLen), remeshFaces, cgalMesh->surface);
            pmp::isotropic_remeshing(
              remeshFaces, sizing, cgalMesh->surface, remeshNp(1));
          }
        }
      }
      else
      {
        if (this->FeatureSizingStandAlone)
        {
          FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
            std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
            std::make_pair(featMinLen, featMaxLen), cgalMesh->surface.faces(), cgalMesh->surface,
            featureEdges);
          for (unsigned int pass = 0; pass < remeshIterations; ++pass)
          {
            if (pass > 0)
            {
              sizing.recompute_curvature(cgalMesh->surface);
            }
            pmp::isotropic_remeshing(
              cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(1));
          }
        }
        else
        {
          for (unsigned int pass = 0; pass < remeshIterations; ++pass)
          {
            pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
              std::make_pair(minLen, maxLen), cgalMesh->surface.faces(), cgalMesh->surface);
            pmp::isotropic_remeshing(
              cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(1));
          }
        }
      }
    }
    else if (patchRemesh)
    {
      if (this->FeatureSizingStandAlone)
      {
        FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
          std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
          std::make_pair(featMinLen, featMaxLen), remeshFaces, cgalMesh->surface, featureEdges);
        pmp::isotropic_remeshing(remeshFaces, sizing, cgalMesh->surface, remeshNp(remeshIterations));
      }
      else
      {
        pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
          std::make_pair(minLen, maxLen), remeshFaces, cgalMesh->surface);
        pmp::isotropic_remeshing(remeshFaces, sizing, cgalMesh->surface, remeshNp(remeshIterations));
      }
    }
    else
    {
      if (this->FeatureSizingStandAlone)
      {
        FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
          std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
          std::make_pair(featMinLen, featMaxLen), cgalMesh->surface.faces(), cgalMesh->surface,
          featureEdges);
        pmp::isotropic_remeshing(
          cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(remeshIterations));
      }
      else
      {
        pmp::Adaptive_sizing_field sizing(this->AdaptiveTolerance,
          std::make_pair(minLen, maxLen), cgalMesh->surface.faces(), cgalMesh->surface);
        pmp::isotropic_remeshing(
          cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(remeshIterations));
      }
    }

    if (this->ShapeSmoothingIterations > 0)
    {
      std::vector<std::array<double, 3>> smoothAnchorPoints;
      CollectSelectionBoundaryWorldPoints(input, selected, smoothAnchorPoints);
      if (this->DetectFeatureEdges && this->FeatureMaskEnabled)
      {
        vtkNew<vtkPolyData> remeshedSurfForSmoothAnchors;
        vtkCGALHelper::toVTK(cgalMesh.get(), remeshedSurfForSmoothAnchors);
        this->interpolateAttributes(input, remeshedSurfForSmoothAnchors);
        std::vector<char> remeshMaskCells;
        if (ComputeFeatureFaceMask(remeshedSurfForSmoothAnchors, this->FeatureMaskArrayName,
              this->FeatureMaskThreshold, this->FeatureMaskAllScalars, remeshMaskCells))
        {
          vtkNew<vtkPolyData> remeshedThresholdPatch;
          ExtractMaskedFacesPolyData(remeshedSurfForSmoothAnchors, remeshMaskCells, remeshedThresholdPatch);
          if (remeshedThresholdPatch->GetNumberOfCells() > 0)
          {
            CollectPatchSharpFeatureWorldPoints(remeshedThresholdPatch, this->ProtectAngle,
              this->SharpFeatureSideFilter, smoothAnchorPoints);
          }
        }
      }
      const double smoothAnchorTolSq = std::max(1e-36, (1e-4 * L) * (1e-4 * L));

      if (this->DetectFeatureEdges)
      {
        DetectSharpEdgesWithFilter(
          cgalMesh->surface, this->ProtectAngle, this->SharpFeatureSideFilter, featureEdges);

        vtkNew<vtkPolyData> tmpSurf;
        vtkCGALHelper::toVTK(cgalMesh.get(), tmpSurf);
        this->interpolateAttributes(input, tmpSurf);

        if (this->FeatureMaskEnabled)
        {
          std::vector<char> smoothMask;
          if (ComputeFeatureFaceMask(tmpSurf, this->FeatureMaskArrayName,
                this->FeatureMaskThreshold, this->FeatureMaskAllScalars, smoothMask))
          {
            std::vector<vtkIdType> faceIdxToSm;
            BuildCgalFaceIndexToVtkCell(cgalMesh->surface, faceIdxToSm);
            ApplyFeatureRegionMaskToSharpEdges(
              cgalMesh->surface, featureEdges, smoothMask, faceIdxToSm);

            std::vector<char> faceInMaskRegion(
              static_cast<size_t>(cgalMesh->surface.number_of_faces()), 0);
            for (CGAL_Surface::Face_index f : cgalMesh->surface.faces())
            {
              const std::size_t fi = static_cast<std::size_t>(f.idx());
              if (fi >= faceIdxToSm.size())
              {
                continue;
              }
              const vtkIdType vtkC = faceIdxToSm[fi];
              if (vtkC >= 0 && static_cast<size_t>(vtkC) < smoothMask.size() &&
                  smoothMask[static_cast<size_t>(vtkC)])
              {
                faceInMaskRegion[fi] = 1;
              }
            }
            AddPatchBoundaryFeatureEdges(cgalMesh->surface, featureEdges, faceInMaskRegion);
          }
          else
          {
            vtkWarningMacro("Feature mask: could not evaluate threshold on the remeshed surface "
                            "before smooth_shape (same array/threshold as remesh); mask clipping "
                            "and mask-boundary feature constraints are skipped for smoothing.");
          }
        }
      }
      else
      {
        for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
        {
          boost::put(featureEdges, e, false);
        }
      }

      CGAL_Surface& sm = cgalMesh->surface;
      auto vertexConstrained =
        sm.template add_property_map<Graph_Verts, bool>("v:vespa_shape_smooth_c", false).first;

      for (CGAL_Surface::Edge_index e : sm.edges())
      {
        if (boost::get(featureEdges, e))
        {
          const CGAL_Surface::Halfedge_index h = sm.halfedge(e);
          boost::put(vertexConstrained, sm.source(h), true);
          boost::put(vertexConstrained, sm.target(h), true);
        }
      }

      MarkVerticesNearAnchorPoints(sm, vertexConstrained, smoothAnchorPoints, smoothAnchorTolSq);

      pmp::smooth_shape(sm, this->ShapeSmoothingTimeStep,
        pmp::parameters::number_of_iterations(static_cast<unsigned int>(this->ShapeSmoothingIterations))
          .vertex_is_constrained_map(vertexConstrained));

      sm.remove_property_map(vertexConstrained);
    }
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  vtkCGALHelper::toVTK(cgalMesh.get(), output);
  this->interpolateAttributes(input, output);

  if (this->FeatureMaskEnabled)
  {
    std::vector<char> remeshedOutputMask;
    if (ComputeFeatureFaceMask(output, this->FeatureMaskArrayName, this->FeatureMaskThreshold,
          this->FeatureMaskAllScalars, remeshedOutputMask))
    {
      ExtractMaskedFacesPolyData(output, remeshedOutputMask, outputMaskPatchRemeshed);
    }
    else
    {
      vtkWarningMacro("Feature mask: could not evaluate threshold on port 0 output; "
                      "mask patch (remeshed) output port is empty.");
      outputMaskPatchRemeshed->Initialize();
    }
  }
  else
  {
    outputMaskPatchRemeshed->Initialize();
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

    if (this->ShapeSmoothingIterations > 0)
    {
      if (this->FeatureMaskEnabled && outputMaskPatchRemeshed->GetNumberOfCells() > 0)
      {
        try
        {
          vtkCGALHelper::Vespa_surface cgalPort3;
          if (vtkCGALHelper::toCGAL(outputMaskPatchRemeshed, &cgalPort3, nullptr))
          {
            auto featPort3 = get(CGAL::edge_is_feature, cgalPort3.surface);
            DetectSharpEdgesWithFilter(
              cgalPort3.surface, this->ProtectAngle, this->SharpFeatureSideFilter, featPort3);
            AppendSharpFeatureVertsToPolyData(outputFeatures, cgalPort3.surface, featPort3);
          }
        }
        catch (const std::exception& e)
        {
          vtkWarningMacro(
            "Sharp feature points on remeshed mask patch (port 3) failed: " << e.what());
        }
      }
      else if (!this->FeatureMaskEnabled)
      {
        CGAL_Surface& sm = cgalMesh->surface;
        auto featFull = get(CGAL::edge_is_feature, sm);
        DetectSharpEdgesWithFilter(sm, this->ProtectAngle, this->SharpFeatureSideFilter, featFull);
        AppendSharpFeatureVertsToPolyData(outputFeatures, sm, featFull);
      }
    }
    else
    {
      vtkNew<vtkCellArray> emptyVerts;
      outputFeatures->SetVerts(emptyVerts);
    }
  }
  else
  {
    outputFeatures->Initialize();
  }

  return 1;
}
