#include "vtkSHYXRemeshWithEndpoint.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkGeometryFilter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkThreshold.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

vtkStandardNewMacro(vtkSHYXRemeshWithEndpoint);

namespace pmp = CGAL::Polygon_mesh_processing;

#include "vtkSHYXAdaptiveIsotropicRemesherInternals.h"
#include "vtkSHYXFeatureAwareAdaptiveSizingField.h"

using namespace vespa_shyx_air_remesh_internals;

namespace
{
/** Prefer cell data when both point and cell arrays match (same as feature-mask resolution). */
bool ResolveThresholdArray(vtkPolyData* pd, const char* arrayName, int& associationOut)
{
    associationOut = vtkDataObject::FIELD_ASSOCIATION_NONE;
    if (!pd || !arrayName || arrayName[0] == '\0')
    {
        return false;
    }
    vtkDataArray* const cellArr = pd->GetCellData()->GetArray(arrayName);
    vtkDataArray* const ptArr = pd->GetPointData()->GetArray(arrayName);
    const vtkIdType nCells = pd->GetNumberOfCells();
    const vtkIdType nPts = pd->GetNumberOfPoints();
    const bool cellOk = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
    const bool pointOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);
    if (cellOk && pointOk)
    {
        associationOut = vtkDataObject::FIELD_ASSOCIATION_CELLS;
        return true;
    }
    if (cellOk)
    {
        associationOut = vtkDataObject::FIELD_ASSOCIATION_CELLS;
        return true;
    }
    if (pointOk)
    {
        associationOut = vtkDataObject::FIELD_ASSOCIATION_POINTS;
        return true;
    }
    return false;
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXRemeshWithEndpoint::vtkSHYXRemeshWithEndpoint()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
    this->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, "EndpointIndex");
}

//------------------------------------------------------------------------------
void vtkSHYXRemeshWithEndpoint::SetEndpointIndexArrayName(const char* name)
{
    const bool hasName = (name != nullptr && name[0] != '\0');
    this->SetInputArrayToProcess(0, 0, 0,
        hasName ? vtkDataObject::FIELD_ASSOCIATION_POINTS : vtkDataObject::FIELD_ASSOCIATION_NONE,
        hasName ? name : nullptr);
}

//------------------------------------------------------------------------------
const char* vtkSHYXRemeshWithEndpoint::GetEndpointIndexArrayName()
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
void vtkSHYXRemeshWithEndpoint::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "EndpointIndexAllScalars: " << (this->EndpointIndexAllScalars ? "on" : "off")
       << std::endl;
    if (const char* const na = this->GetEndpointIndexArrayName())
    {
        os << indent << "EndpointIndexArrayName: " << na << std::endl;
    }
    else
    {
        os << indent << "EndpointIndexArrayName: (null)" << std::endl;
    }
    os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
    os << indent << "MaxEdgeLength: " << this->MaxEdgeLength << std::endl;
    os << indent << "AdaptiveTolerance: " << this->AdaptiveTolerance << std::endl;
    os << indent << "AdaptiveSizingNeighborMaxRatio: " << this->AdaptiveSizingNeighborMaxRatio
       << std::endl;
    os << indent << "ScaleToRange: " << (this->ScaleToRange ? "on" : "off") << std::endl;
    os << indent << "RemeshRecomputeCurvatureEachIteration: "
       << (this->RemeshRecomputeCurvatureEachIteration ? "on" : "off") << std::endl;
    os << indent << "NumberOfIterations: " << this->NumberOfIterations << std::endl;
    os << indent << "NumberOfRelaxationSteps: " << this->NumberOfRelaxationSteps << std::endl;
    os << indent << "RemeshProtectConstraints: " << (this->RemeshProtectConstraints ? "on" : "off")
       << std::endl;
    os << indent << "RemeshCollapseConstraints: " << (this->RemeshCollapseConstraints ? "on" : "off")
       << std::endl;
    os << indent << "RemeshRelaxConstraints: " << (this->RemeshRelaxConstraints ? "on" : "off")
       << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXRemeshWithEndpoint::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXRemeshWithEndpoint::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
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
    if (this->NumberOfRelaxationSteps < 0)
    {
        vtkErrorMacro("NumberOfRelaxationSteps must be >= 0, got " << this->NumberOfRelaxationSteps);
        return 0;
    }

    const char* epName = this->GetEndpointIndexArrayName();
    int scalarAssoc = vtkDataObject::FIELD_ASSOCIATION_NONE;
    if (!ResolveThresholdArray(input, epName, scalarAssoc))
    {
        vtkErrorMacro("Could not resolve endpoint/marker array (need a non-empty name and a "
                      "matching point- or cell-centered array on the input).");
        return 0;
    }

    vtkNew<vtkThreshold> threshold;
    threshold->SetInputData(input);
    threshold->SetInputArrayToProcess(0, 0, 0, scalarAssoc, epName);
    threshold->SetThresholdFunction(vtkThreshold::THRESHOLD_BETWEEN);
    threshold->SetLowerThreshold(-1.0e200);
    threshold->SetUpperThreshold(-1.0e-9);
    threshold->SetSelectedComponent(0);
    threshold->SetComponentModeToUseSelected();
    threshold->SetAllScalars(this->EndpointIndexAllScalars ? 1 : 0);
    threshold->Update();
    vtkDataSet* const thOut = vtkDataSet::SafeDownCast(threshold->GetOutputDataObject(0));
    if (!thOut || thOut->GetNumberOfCells() == 0)
    {
        vtkWarningMacro("vtkThreshold produced no cells for first component in ("
            << (-1.0e200) << ", " << (-1.0e-9) << ") on \"" << (epName ? epName : "")
            << "\"; passing input through.");
        output->ShallowCopy(input);
        return 1;
    }

    vtkNew<vtkGeometryFilter> geometry;
    geometry->SetInputConnection(threshold->GetOutputPort());
    vtkNew<vtkTriangleFilter> triangle;
    triangle->SetInputConnection(geometry->GetOutputPort());
    triangle->Update();
    vtkPolyData* patchIn = vtkPolyData::SafeDownCast(triangle->GetOutputDataObject(0));
    if (!patchIn || patchIn->GetNumberOfCells() == 0)
    {
        vtkWarningMacro("Geometry/triangle extraction yielded no surface; passing input through.");
        output->ShallowCopy(input);
        return 1;
    }

    double b[6];
    patchIn->GetBounds(b);
    vtkBoundingBox box;
    box.SetBounds(b);
    const double L = box.GetMaxLength();
    if (L <= 0.0)
    {
        vtkErrorMacro("Extracted patch has zero bounding-box extent.");
        return 0;
    }

    const double minLen = this->MinEdgeLength;
    const double maxLen = this->MaxEdgeLength;
    if (!(minLen > 0.0 && maxLen > minLen))
    {
        vtkErrorMacro("Need 0 < MinEdgeLength < MaxEdgeLength (got " << minLen << " / " << maxLen
                                                                      << ").");
        return 0;
    }

    std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
        std::make_unique<vtkCGALHelper::Vespa_surface>();
    if (!vtkCGALHelper::toCGAL(patchIn, cgalMesh.get()))
    {
        vtkErrorMacro("Could not convert extracted patch to CGAL surface (check manifold / triangles).");
        return 0;
    }

    try
    {
        auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
        for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
        {
            boost::put(featureEdges, e, false);
        }

        PrepareIccVertexNormalsForAdaptiveSizing(cgalMesh->surface, nullptr);

        const auto remeshNp = [&](unsigned int iteration_count) {
            return pmp::parameters::number_of_iterations(iteration_count)
                .number_of_relaxation_steps(static_cast<unsigned int>(this->NumberOfRelaxationSteps))
                .protect_constraints(this->RemeshProtectConstraints)
                .collapse_constraints(this->RemeshCollapseConstraints)
                .relax_constraints(this->RemeshRelaxConstraints)
                .do_split(true)
                .do_collapse(true)
                .do_flip(true)
                .edge_is_constrained_map(featureEdges);
        };

        const unsigned int remeshIterations = static_cast<unsigned int>(this->NumberOfIterations);

        using SizingTy = FeatureAwareAdaptiveSizingField;
        std::optional<SizingTy> sizingStorage;
        sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen),
            cgalMesh->surface.faces(), cgalMesh->surface,
            static_cast<double>(this->AdaptiveSizingNeighborMaxRatio),
            this->ScaleToRange);
        SizingTy& sizing = *sizingStorage;

        auto doRemeshSingleIteration = [&]() {
            pmp::isotropic_remeshing(
                cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(1));
        };

        if (remeshIterations <= 1u)
        {
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
            doRemeshSingleIteration();
        }
    }
    catch (std::exception& e)
    {
        vtkErrorMacro("CGAL Exception: " << e.what());
        return 0;
    }

    vtkCGALHelper::toVTK(cgalMesh.get(), output);
    this->interpolateAttributes(patchIn, output);
    return 1;
}
