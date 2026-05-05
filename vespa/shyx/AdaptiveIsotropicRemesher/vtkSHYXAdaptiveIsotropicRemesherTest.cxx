#include "vtkSHYXAdaptiveIsotropicRemesherTest.h"

#include "vtkSHYXAdaptiveIsotropicRemesherInternals.h"
#include "vtkSHYXFeatureAwareAdaptiveSizingField.h"

#include "vtkCGALHelper.h"

#include <vtkBoundingBox.h>
#include <vtkCellArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>

#include <boost/property_map/property_map.hpp>

#include <exception>
#include <memory>
#include <set>
#include <vector>

vtkStandardNewMacro(vtkSHYXAdaptiveIsotropicRemesherTest);

using namespace vespa_shyx_air_remesh_internals;

//------------------------------------------------------------------------------
int vtkSHYXAdaptiveIsotropicRemesherTest::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* outputFeatures = vtkPolyData::GetData(outputVector, 1);
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
  if (this->FeatureAdaptiveTolerance <= 0.0)
  {
    vtkErrorMacro("FeatureAdaptiveTolerance must be positive for sizing preview (got "
      << this->FeatureAdaptiveTolerance << ").");
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
  if (!(featMinLen > 0.0 && featMaxLen > featMinLen))
  {
    vtkErrorMacro("Need 0 < FeatureMinEdgeLength < FeatureMaxEdgeLength for sizing preview (got "
      << featMinLen << " / " << featMaxLen << ").");
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
                      "input. Output port 2 is empty; CGAL constraints for sizing preview use "
                      "unmasked sharp-edge detection.");
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
                      "matching point- or cell-centered array on the input). Sizing uses the "
                      "whole surface.");
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
      "Remesh region did not resolve to any remeshable faces on the input; sizing uses the "
      "whole surface.");
  }

  vtkNew<vtkDoubleArray> szGlobal;
  vtkNew<vtkDoubleArray> szFeature;
  szGlobal->SetName("VespaAdaptiveSizeGlobal");
  szFeature->SetName("VespaAdaptiveSizeFeature");
  szGlobal->SetNumberOfComponents(1);
  szFeature->SetNumberOfComponents(1);

  const vtkIdType nPts = input->GetNumberOfPoints();
  szGlobal->SetNumberOfTuples(nPts);
  szFeature->SetNumberOfTuples(nPts);

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

    if (patchRemesh)
    {
      FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
        std::make_pair(featMinLen, featMaxLen), remeshFaces, cgalMesh->surface, featureEdges,
        this->FeatureSizingUsePolylineCurvature);
      (void)sizing;
    }
    else
    {
      FeatureAwareAdaptiveSizingField<decltype(featureEdges)> sizing(this->AdaptiveTolerance,
        std::make_pair(minLen, maxLen), this->FeatureAdaptiveTolerance,
        std::make_pair(featMinLen, featMaxLen), cgalMesh->surface.faces(), cgalMesh->surface,
        featureEdges, this->FeatureSizingUsePolylineCurvature);
      (void)sizing;
    }

    // CGAL 6.x Surface_mesh::property_map returns std::optional<Property_map>.
    const auto optPg =
      cgalMesh->surface.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_global");
    const auto optPf =
      cgalMesh->surface.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_feature");
    if (!optPg.has_value() || !optPf.has_value())
    {
      vtkErrorMacro("Could not read Vespa sizing vertex maps from CGAL surface.");
      return 0;
    }

    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
      const CGAL_Surface::Vertex_index vx(static_cast<std::size_t>(pid));
      szGlobal->SetValue(pid, boost::get(*optPg, vx));
      szFeature->SetValue(pid, boost::get(*optPf, vx));
    }

    output->DeepCopy(input);
    output->GetPointData()->AddArray(szGlobal);
    output->GetPointData()->AddArray(szFeature);

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
        }
        catch (const std::exception& e)
        {
          vtkWarningMacro("Sharp feature lines on input mask patch failed: " << e.what());
        }
      }

      if (!linesFromInputMaskPatch)
      {
        FillFeaturePolyDataSharpLinesOnly(cgalMesh->surface, featureEdges, outputFeatures);
      }
      vtkNew<vtkCellArray> emptyVerts;
      outputFeatures->SetVerts(emptyVerts);
    }
    else
    {
      outputFeatures->Initialize();
    }

    outputMaskPatchRemeshed->Initialize();
  }
  catch (std::exception& e)
  {
    vtkErrorMacro("CGAL Exception: " << e.what());
    return 0;
  }

  return 1;
}
