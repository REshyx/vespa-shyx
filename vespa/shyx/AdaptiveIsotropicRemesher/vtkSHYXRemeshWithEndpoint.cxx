#include "vtkSHYXRemeshWithEndpoint.h"

#include "vtkCGALHelper.h"

#include <vtkAlgorithm.h>
#include <vtkBoundingBox.h>
#include <vtkCleanPolyData.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkGeometryFilter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkThreshold.h>
#include <vtkTriangleFilter.h>

#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <algorithm>
#include <unordered_set>
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
/**
 * Expansion-ratio sizing field for cap remesh.
 *
 * Seam vertices (cap vertices adjacent to at least one wall vertex) are seeded with the
 * average length of their adjacent wall edges.  A BFS then propagates into the cap
 * interior, multiplying the size by \c expansionRatio at each hop.
 *
 * Two vertex membership maps are maintained throughout remeshing:
 *   - isCapVertMap_   : true for all cap vertices (seam + interior)
 *   - isInteriorCapMap_ : true for interior-only cap vertices (not seam)
 * Both are updated in register_split_vertex so that recompute() and collectCapFaces()
 * remain accurate after any number of split/collapse operations.
 */
class CapExpansionSizingField
{
public:
    using FT                  = double;
    using Point_3             = CGAL_Surface::Point;
    using vertex_descriptor   = CGAL_Surface::Vertex_index;
    using halfedge_descriptor = CGAL_Surface::Halfedge_index;
    using face_descriptor     = CGAL_Surface::Face_index;

    CapExpansionSizingField(CGAL_Surface& mesh,
        const std::unordered_set<std::size_t>& capFaceSet, double expansionRatio)
        : expansionRatio_(expansionRatio > 1.0 ? expansionRatio : 1.0)
    {
        sizeMap_ =
            mesh.add_property_map<vertex_descriptor, FT>("v:vespa_cap_size", FT(0)).first;
        isCapVertMap_ =
            mesh.add_property_map<vertex_descriptor, bool>("v:vespa_cap_is_vert", false).first;
        isInteriorCapMap_ =
            mesh.add_property_map<vertex_descriptor, bool>("v:vespa_cap_is_interior", false).first;
        buildSizes_(mesh, capFaceSet);
    }

    FT at(vertex_descriptor v, const CGAL_Surface& /*sm*/) const { return get(sizeMap_, v); }

    std::optional<FT> is_too_long(
        vertex_descriptor va, vertex_descriptor vb, const CGAL_Surface& sm) const
    {
        const FT s = (CGAL::min)(get(sizeMap_, va), get(sizeMap_, vb));
        const FT sqlen = CGAL::squared_distance(sm.point(va), sm.point(vb));
        const FT sqt = CGAL::square((FT(4) / FT(3)) * s);
        if (sqt > FT(0) && sqlen > sqt)
        {
            return sqlen / sqt;
        }
        return std::nullopt;
    }

    std::optional<FT> is_too_short(halfedge_descriptor h, const CGAL_Surface& sm) const
    {
        const auto va = sm.source(h);
        const auto vb = sm.target(h);
        const FT s = (CGAL::min)(get(sizeMap_, va), get(sizeMap_, vb));
        const FT sqlen = CGAL::squared_distance(sm.point(va), sm.point(vb));
        const FT sqt = CGAL::square((FT(4) / FT(5)) * s);
        if (sqt > FT(0) && sqlen < sqt)
        {
            return sqlen / sqt;
        }
        return std::nullopt;
    }

    Point_3 split_placement(halfedge_descriptor h, const CGAL_Surface& sm) const
    {
        return CGAL::midpoint(sm.point(sm.source(h)), sm.point(sm.target(h)));
    }

    /** Called by CGAL for every vertex inserted via edge split. */
    void register_split_vertex(vertex_descriptor v, const CGAL_Surface& sm)
    {
        // New vertex is always interior cap (seam edges are protected from splitting).
        put(isCapVertMap_,     v, true);
        put(isInteriorCapMap_, v, true);

        FT sg = FT(0);
        std::size_t n = 0;
        for (halfedge_descriptor ha : CGAL::halfedges_around_target(v, sm))
        {
            sg += get(sizeMap_, sm.source(ha));
            ++n;
        }
        if (n > 0)
        {
            put(sizeMap_, v, sg / FT(n));
        }
    }

    /**
     * Re-run BFS from seam vertices using the current mesh topology.
     * Seam sizes are re-derived from adjacent wall edge lengths (unchanged because seam
     * edges are constrained).  Interior cap vertices (added by previous splits) receive
     * updated sizes anchored to the actual BFS distance rather than interpolation drift.
     */
    void recompute(CGAL_Surface& mesh)
    {
        // Recompute seam sizes from current mesh geometry.
        std::unordered_map<std::size_t, FT> seamSizes;
        for (vertex_descriptor v : mesh.vertices())
        {
            if (!get(isCapVertMap_, v) || get(isInteriorCapMap_, v))
            {
                continue; // skip wall vertices and interior cap vertices
            }
            // v is a seam vertex: average of adjacent wall edge lengths.
            FT sumLen = FT(0);
            int wallCount = 0;
            for (halfedge_descriptor h : CGAL::halfedges_around_target(v, mesh))
            {
                if (!get(isCapVertMap_, mesh.source(h)))
                {
                    sumLen += CGAL::sqrt(
                        CGAL::squared_distance(mesh.point(v), mesh.point(mesh.source(h))));
                    ++wallCount;
                }
            }
            if (wallCount > 0)
            {
                seamSizes[static_cast<std::size_t>(v)] = sumLen / FT(wallCount);
            }
        }
        runBfs_(mesh, seamSizes);
    }

    /**
     * Return all current cap faces: faces where at least one vertex is an interior cap
     * vertex (guaranteed to exclude pure-wall faces since seam edges are constrained).
     */
    std::vector<face_descriptor> collectCapFaces(const CGAL_Surface& mesh) const
    {
        std::vector<face_descriptor> result;
        for (face_descriptor f : mesh.faces())
        {
            halfedge_descriptor h = mesh.halfedge(f);
            for (int i = 0; i < 3; ++i)
            {
                if (get(isInteriorCapMap_, mesh.target(h)))
                {
                    result.push_back(f);
                    break;
                }
                h = mesh.next(h);
            }
        }
        return result;
    }

private:
    void buildSizes_(CGAL_Surface& mesh, const std::unordered_set<std::size_t>& capFaceSet)
    {
        // --- Collect cap vertices and classify seam vs. interior ----------------
        std::unordered_set<std::size_t> capVertIdx;
        for (CGAL_Surface::Face_index f : mesh.faces())
        {
            if (capFaceSet.count(static_cast<std::size_t>(f)) == 0)
            {
                continue;
            }
            CGAL_Surface::Halfedge_index h = mesh.halfedge(f);
            for (int i = 0; i < 3; ++i)
            {
                capVertIdx.insert(static_cast<std::size_t>(mesh.target(h)));
                h = mesh.next(h);
            }
        }

        // Seed the membership maps.
        for (std::size_t vi : capVertIdx)
        {
            put(isCapVertMap_, vertex_descriptor(vi), true);
        }

        // --- Seed seam vertices with the average adjacent wall edge length ------
        std::unordered_map<std::size_t, FT> seamSizes;
        for (std::size_t vi : capVertIdx)
        {
            vertex_descriptor v(vi);
            FT sumLen = FT(0);
            int wallEdgeCount = 0;
            for (halfedge_descriptor h : CGAL::halfedges_around_target(v, mesh))
            {
                const std::size_t si = static_cast<std::size_t>(mesh.source(h));
                if (capVertIdx.count(si) == 0)
                {
                    sumLen += CGAL::sqrt(
                        CGAL::squared_distance(mesh.point(v), mesh.point(mesh.source(h))));
                    ++wallEdgeCount;
                }
            }
            if (wallEdgeCount > 0)
            {
                seamSizes[vi] = sumLen / FT(wallEdgeCount);
            }
        }

        // All cap vertices NOT on the seam are interior cap vertices.
        for (std::size_t vi : capVertIdx)
        {
            if (seamSizes.count(vi) == 0)
            {
                put(isInteriorCapMap_, vertex_descriptor(vi), true);
            }
        }

        runBfs_(mesh, seamSizes);
    }

    /** BFS from seam vertices, propagating size × expansionRatio_ per hop. */
    void runBfs_(CGAL_Surface& mesh, const std::unordered_map<std::size_t, FT>& seamSizes)
    {
        for (auto& [vi, sz] : seamSizes)
        {
            put(sizeMap_, vertex_descriptor(vi), sz);
        }

        std::queue<std::size_t> bfsQueue;
        std::unordered_set<std::size_t> visited;
        for (auto& [vi, sz] : seamSizes)
        {
            visited.insert(vi);
            bfsQueue.push(vi);
        }
        while (!bfsQueue.empty())
        {
            const std::size_t vi = bfsQueue.front();
            bfsQueue.pop();
            const FT parentSize = get(sizeMap_, vertex_descriptor(vi));
            for (halfedge_descriptor h :
                CGAL::halfedges_around_target(vertex_descriptor(vi), mesh))
            {
                const vertex_descriptor nbr = mesh.source(h);
                const std::size_t ni = static_cast<std::size_t>(nbr);
                if (!get(isInteriorCapMap_, nbr) || visited.count(ni) > 0)
                {
                    continue;
                }
                put(sizeMap_, nbr, parentSize * expansionRatio_);
                visited.insert(ni);
                bfsQueue.push(ni);
            }
        }

        // Safety fallback for any unreached interior cap vertex.
        FT fallback = FT(0);
        if (!seamSizes.empty())
        {
            for (auto& [vi, sz] : seamSizes)
            {
                fallback += sz;
            }
            fallback /= FT(seamSizes.size());
        }
        const FT safeDefault = fallback > FT(0) ? fallback : FT(1);
        for (vertex_descriptor v : mesh.vertices())
        {
            if (get(isInteriorCapMap_, v) && get(sizeMap_, v) == FT(0))
            {
                put(sizeMap_, v, safeDefault);
            }
        }
    }

    CGAL_Surface::Property_map<vertex_descriptor, FT>   sizeMap_;
    CGAL_Surface::Property_map<vertex_descriptor, bool> isCapVertMap_;
    CGAL_Surface::Property_map<vertex_descriptor, bool> isInteriorCapMap_;
    double expansionRatio_;
};

} // namespace

//------------------------------------------------------------------------------
vtkSHYXRemeshWithEndpoint::vtkSHYXRemeshWithEndpoint()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
    this->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_CELLS, "EndpointIndex");
}

//------------------------------------------------------------------------------
void vtkSHYXRemeshWithEndpoint::SetEndpointIndexArrayName(const char* name)
{
    const bool hasName = (name != nullptr && name[0] != '\0');
    this->SetInputArrayToProcess(0, 0, 0,
        hasName ? vtkDataObject::FIELD_ASSOCIATION_CELLS : vtkDataObject::FIELD_ASSOCIATION_NONE,
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
    os << indent << "LargestConnectedRegionOnly: "
       << (this->LargestConnectedRegionOnly ? "on" : "off") << std::endl;
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
    os << indent << "EnableWallRemesh: " << (this->EnableWallRemesh ? "on" : "off") << std::endl;
    os << indent << "RemeshProtectConstraints: " << (this->RemeshProtectConstraints ? "on" : "off")
       << std::endl;
    os << indent << "RemeshCollapseConstraints: " << (this->RemeshCollapseConstraints ? "on" : "off")
       << std::endl;
    os << indent << "RemeshRelaxConstraints: " << (this->RemeshRelaxConstraints ? "on" : "off")
       << std::endl;
    os << indent << "EnableCapRemesh: " << (this->EnableCapRemesh ? "on" : "off") << std::endl;
    os << indent << "CapExpansionRatio: " << this->CapExpansionRatio << std::endl;
    os << indent << "CapNumberOfIterations: " << this->CapNumberOfIterations << std::endl;
    os << indent << "CapNumberOfRelaxationSteps: " << this->CapNumberOfRelaxationSteps << std::endl;
    os << indent << "CapRemeshProtectConstraints: "
       << (this->CapRemeshProtectConstraints ? "on" : "off") << std::endl;
    os << indent << "CapRefineSizingField: " << (this->CapRefineSizingField ? "on" : "off")
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
    if (this->EnableWallRemesh)
    {
        if (this->NumberOfIterations < 1)
        {
            vtkErrorMacro("NumberOfIterations must be >= 1.");
            return 0;
        }
        if (this->NumberOfRelaxationSteps < 0)
        {
            vtkErrorMacro(
                "NumberOfRelaxationSteps must be >= 0, got " << this->NumberOfRelaxationSteps);
            return 0;
        }
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
    vtkNew<vtkPolyDataConnectivityFilter> largestRegionFilter;
    vtkNew<vtkCleanPolyData> largestRegionCleanUnused;
    if (this->LargestConnectedRegionOnly && patchIn && patchIn->GetNumberOfCells() > 0)
    {
        largestRegionFilter->SetInputData(patchIn);
        largestRegionFilter->SetExtractionModeToLargestRegion();
        largestRegionFilter->Update();
        // vtkPolyDataConnectivityFilter keeps the full input point list; drop vertices not
        // referenced by any output cell (vtkCleanPolyData). Point merging is OFF so no coincident
        // points are collapsed—only unused points are removed.
        largestRegionCleanUnused->SetInputConnection(largestRegionFilter->GetOutputPort());
        largestRegionCleanUnused->PointMergingOff();
        largestRegionCleanUnused->SetTolerance(0.0);
        largestRegionCleanUnused->SetToleranceIsAbsolute(true);
        largestRegionCleanUnused->ConvertPolysToLinesOff();
        largestRegionCleanUnused->ConvertLinesToPointsOff();
        largestRegionCleanUnused->ConvertStripsToPolysOff();
        largestRegionCleanUnused->Update();
        patchIn = vtkPolyData::SafeDownCast(largestRegionCleanUnused->GetOutput());
    }
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
        PrepareIccVertexNormalsForAdaptiveSizing(cgalMesh->surface, nullptr);

        using SizingTy = FeatureAwareAdaptiveSizingField;
        std::optional<SizingTy> sizingStorage;
        sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen),
            cgalMesh->surface.faces(), cgalMesh->surface,
            static_cast<double>(this->AdaptiveSizingNeighborMaxRatio),
            this->ScaleToRange);

        if (this->EnableWallRemesh)
        {
            auto featureEdges = get(CGAL::edge_is_feature, cgalMesh->surface);
            for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
            {
                boost::put(featureEdges, e, false);
            }

            SizingTy& sizing = *sizingStorage;
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

            // === Phase 2: Hole fill (FairingContinuity=0) + Cap remesh ===
            if (this->EnableCapRemesh)
            {
                // Reset feature edges after wall remesh (new edges default to false already,
                // but be explicit for safety).
                for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
                {
                    boost::put(featureEdges, e, false);
                }

                // Collect one representative border halfedge per open boundary loop.
                std::vector<CGAL_Surface::Halfedge_index> holeStarters;
                {
                    std::set<CGAL_Surface::Halfedge_index> visited;
                    for (CGAL_Surface::Halfedge_index h : cgalMesh->surface.halfedges())
                    {
                        if (cgalMesh->surface.is_border(h) && visited.find(h) == visited.end())
                        {
                            holeStarters.push_back(h);
                            CGAL_Surface::Halfedge_index cur = h;
                            do
                            {
                                visited.insert(cur);
                                cur = cgalMesh->surface.next(cur);
                            } while (cur != h);
                        }
                    }
                }

                if (!holeStarters.empty())
                {
                    std::vector<CGAL_Surface::Face_index> allCapFaces;
                    std::vector<CGAL_Surface::Vertex_index> allCapVertices;

                    for (CGAL_Surface::Halfedge_index bh : holeStarters)
                    {
                        std::vector<CGAL_Surface::Face_index> hf;
                        std::vector<CGAL_Surface::Vertex_index> hv;
                        pmp::triangulate_refine_and_fair_hole(
                            cgalMesh->surface, bh,
                            std::back_inserter(hf),
                            std::back_inserter(hv),
                            pmp::parameters::fairing_continuity(0u));
                        allCapFaces.insert(allCapFaces.end(), hf.begin(), hf.end());
                        allCapVertices.insert(allCapVertices.end(), hv.begin(), hv.end());
                    }

                    if (!allCapFaces.empty())
                    {
                        // Build cap face index set (used for seam marking and sizing field).
                        std::unordered_set<std::size_t> capFaceIdx;
                        capFaceIdx.reserve(allCapFaces.size());
                        for (CGAL_Surface::Face_index f : allCapFaces)
                        {
                            capFaceIdx.insert(static_cast<std::size_t>(f));
                        }

                        // Mark seam edges (wall ↔ cap boundary) as constrained so that
                        // protect_constraints keeps the cap stitched to the wall mesh.
                        for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
                        {
                            const CGAL_Surface::Halfedge_index h0 = cgalMesh->surface.halfedge(e);
                            const CGAL_Surface::Halfedge_index h1 = cgalMesh->surface.opposite(h0);
                            const bool f0Cap = capFaceIdx.count(
                                static_cast<std::size_t>(cgalMesh->surface.face(h0))) > 0;
                            const bool f1Cap = capFaceIdx.count(
                                static_cast<std::size_t>(cgalMesh->surface.face(h1))) > 0;
                            boost::put(featureEdges, e, f0Cap != f1Cap);
                        }

                        // Expansion sizing field: seam size = adjacent wall edge length,
                        // grows by CapExpansionRatio per BFS hop into the cap interior.
                        CapExpansionSizingField capSizing(
                            cgalMesh->surface, capFaceIdx, this->CapExpansionRatio);

                        const unsigned int capIters =
                            static_cast<unsigned int>(this->CapNumberOfIterations);

                        // Named-parameter builder (always single iteration, loops managed below).
                        const auto capNp = [&]() {
                            return pmp::parameters::number_of_iterations(1u)
                                .number_of_relaxation_steps(
                                    static_cast<unsigned int>(this->CapNumberOfRelaxationSteps))
                                .protect_constraints(this->CapRemeshProtectConstraints)
                                .do_split(true)
                                .do_collapse(true)
                                .do_flip(true)
                                .edge_is_constrained_map(featureEdges);
                        };

                        // First iteration always uses the original allCapFaces range.
                        pmp::isotropic_remeshing(allCapFaces, capSizing, cgalMesh->surface, capNp());

                        // Remaining iterations: optionally recompute BFS before each pass.
                        for (unsigned int pass = 1; pass < capIters; ++pass)
                        {
                            if (this->CapRefineSizingField)
                            {
                                capSizing.recompute(cgalMesh->surface);
                            }
                            auto currentCapFaces = capSizing.collectCapFaces(cgalMesh->surface);
                            pmp::isotropic_remeshing(
                                currentCapFaces, capSizing, cgalMesh->surface, capNp());
                        }

                        // Tag each disconnected cap patch with ids 1..n (multiple holes),
                        // ordered by total cap area descending (largest patch → 1).
                        const std::vector<CGAL_Surface::Face_index> capFaceList =
                            capSizing.collectCapFaces(cgalMesh->surface);
                        std::unordered_set<std::size_t> capFaceSetForCC;
                        capFaceSetForCC.reserve(capFaceList.size());
                        for (CGAL_Surface::Face_index f : capFaceList)
                        {
                            capFaceSetForCC.insert(static_cast<std::size_t>(f));
                        }
                        auto capFaceTagMap = cgalMesh->surface
                            .add_property_map<CGAL_Surface::Face_index, int>(
                                "f:vespa_cap_idx", 0)
                            .first;
                        std::unordered_set<std::size_t> visitedCapFace;
                        visitedCapFace.reserve(capFaceList.size());
                        int componentId = 0;
                        for (CGAL_Surface::Face_index f : capFaceList)
                        {
                            const std::size_t fi = static_cast<std::size_t>(f);
                            if (visitedCapFace.count(fi) > 0)
                            {
                                continue;
                            }
                            ++componentId;
                            std::queue<CGAL_Surface::Face_index> fq;
                            visitedCapFace.insert(fi);
                            fq.push(f);
                            while (!fq.empty())
                            {
                                const CGAL_Surface::Face_index cf = fq.front();
                                fq.pop();
                                put(capFaceTagMap, cf, componentId);
                                for (CGAL_Surface::Halfedge_index h :
                                    CGAL::halfedges_around_face(
                                        cgalMesh->surface.halfedge(cf), cgalMesh->surface))
                                {
                                    const CGAL_Surface::Halfedge_index hop =
                                        cgalMesh->surface.opposite(h);
                                    const CGAL_Surface::Face_index adj =
                                        cgalMesh->surface.face(hop);
                                    if (adj == CGAL_Surface::null_face())
                                    {
                                        continue;
                                    }
                                    const std::size_t ai = static_cast<std::size_t>(adj);
                                    if (capFaceSetForCC.count(ai) == 0 ||
                                        visitedCapFace.count(ai) > 0)
                                    {
                                        continue;
                                    }
                                    visitedCapFace.insert(ai);
                                    fq.push(adj);
                                }
                            }
                        }

                        // Remap component ids 1..n by total cap area (largest → 1).
                        if (componentId > 0)
                        {
                            const auto triangleArea = [](const CGAL_Surface& sm,
                                                          CGAL_Surface::Face_index f) -> double {
                                CGAL_Surface::Halfedge_index h = sm.halfedge(f);
                                const CGAL_Surface::Point& pa = sm.point(sm.source(h));
                                h = sm.next(h);
                                const CGAL_Surface::Point& pb = sm.point(sm.source(h));
                                h = sm.next(h);
                                const CGAL_Surface::Point& pc = sm.point(sm.source(h));
                                const double sq =
                                    CGAL::to_double(CGAL::squared_area(pa, pb, pc));
                                return std::sqrt((std::max)(0.0, sq));
                            };

                            std::vector<double> areaByComp(
                                static_cast<std::size_t>(componentId + 1), 0.0);
                            for (CGAL_Surface::Face_index f : capFaceList)
                            {
                                const int tid = get(capFaceTagMap, f);
                                if (tid > 0 && tid <= componentId)
                                {
                                    areaByComp[static_cast<std::size_t>(tid)] +=
                                        triangleArea(cgalMesh->surface, f);
                                }
                            }

                            std::vector<int> order(static_cast<std::size_t>(componentId));
                            for (int i = 0; i < componentId; ++i)
                            {
                                order[static_cast<std::size_t>(i)] = i + 1;
                            }
                            std::sort(order.begin(), order.end(),
                                [&](int a, int b) {
                                    return areaByComp[static_cast<std::size_t>(a)] >
                                        areaByComp[static_cast<std::size_t>(b)];
                                });

                            std::vector<int> remap(
                                static_cast<std::size_t>(componentId + 1), 0);
                            for (int newId = 0; newId < componentId; ++newId)
                            {
                                remap[static_cast<std::size_t>(
                                    order[static_cast<std::size_t>(newId)])] = newId + 1;
                            }

                            for (CGAL_Surface::Face_index f : capFaceList)
                            {
                                const int tid = get(capFaceTagMap, f);
                                if (tid > 0 && tid <= componentId)
                                {
                                    put(capFaceTagMap, f, remap[static_cast<std::size_t>(tid)]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    catch (std::exception& e)
    {
        vtkErrorMacro("CGAL Exception: " << e.what());
        return 0;
    }

    vtkCGALHelper::toVTK(cgalMesh.get(), output);
    this->interpolateAttributes(patchIn, output);

    if (!this->EnableWallRemesh)
    {
        const auto szMapOpt =
            cgalMesh->surface.property_map<CGAL_Surface::Vertex_index, double>("v:vespa_size_global");
        if (szMapOpt.has_value())
        {
            vtkNew<vtkDoubleArray> vespaSize;
            vespaSize->SetName("VespaSizeGlobal");
            vespaSize->SetNumberOfComponents(1);
            const vtkIdType nPts = output->GetNumberOfPoints();
            vespaSize->SetNumberOfTuples(nPts);
            vtkIdType pid = 0;
            for (CGAL_Surface::Vertex_index v : cgalMesh->surface.vertices())
            {
                vespaSize->SetValue(pid++, CGAL::to_double(get(*szMapOpt, v)));
            }
            output->GetPointData()->AddArray(vespaSize);
        }
    }

    // --- Fix up EndpointIndex for cap cells/vertices ----------------------------
    // interpolateAttributes probes from patchIn (wall-only, all EndpointIndex < 0),
    // so cap cells receive the wrong negative value.  Correct to positive ids 1..n
    // (one id per disconnected cap patch; ids ordered by cap area largest→smallest)
    // using f:vespa_cap_idx before toVTK.
    // toVTK iterates faces() / vertices() in the same order as here.
    if (const char* const epName = this->GetEndpointIndexArrayName())
    {
        const auto capFaceTagOpt =
            cgalMesh->surface.property_map<CGAL_Surface::Face_index, int>("f:vespa_cap_idx");
        const auto capIntPtOpt = cgalMesh->surface.property_map<CGAL_Surface::Vertex_index, bool>(
            "v:vespa_cap_is_interior");

        // --- Cell data ---
        if (capFaceTagOpt.has_value())
        {
            vtkDataArray* const cellArr = output->GetCellData()->GetArray(epName);
            if (cellArr)
            {
                vtkIdType cid = 0;
                for (CGAL_Surface::Face_index f : cgalMesh->surface.faces())
                {
                    const int capTag = get(*capFaceTagOpt, f);
                    if (capTag > 0)
                    {
                        const double tagVal = static_cast<double>(capTag);
                        for (int c = 0; c < cellArr->GetNumberOfComponents(); ++c)
                        {
                            cellArr->SetComponent(cid, c, tagVal);
                        }
                    }
                    ++cid;
                }
            }
        }

        // --- Point data (interior cap only; seam stays probed wall value) ---
        if (capFaceTagOpt.has_value() && capIntPtOpt.has_value())
        {
            vtkDataArray* const ptArr = output->GetPointData()->GetArray(epName);
            if (ptArr)
            {
                vtkIdType pid = 0;
                for (CGAL_Surface::Vertex_index v : cgalMesh->surface.vertices())
                {
                    if (get(*capIntPtOpt, v))
                    {
                        int capTag = 0;
                        for (CGAL_Surface::Halfedge_index h :
                            CGAL::halfedges_around_target(v, cgalMesh->surface))
                        {
                            const CGAL_Surface::Face_index f = cgalMesh->surface.face(h);
                            if (f == CGAL_Surface::null_face())
                            {
                                continue;
                            }
                            const int tid = get(*capFaceTagOpt, f);
                            if (tid > 0)
                            {
                                capTag = tid;
                                break;
                            }
                        }
                        if (capTag > 0)
                        {
                            const double tagVal = static_cast<double>(capTag);
                            for (int c = 0; c < ptArr->GetNumberOfComponents(); ++c)
                            {
                                ptArr->SetComponent(pid, c, tagVal);
                            }
                        }
                    }
                    ++pid;
                }
            }
        }
    }

    return 1;
}
