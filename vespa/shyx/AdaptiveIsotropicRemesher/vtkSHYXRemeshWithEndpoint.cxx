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
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/shape_predicates.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/Kernel/global_functions.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/property_map.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <thread>
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
double* vtkSHYXRemeshWithEndpoint::GetUncappedSizeHistCenters()
{
    return this->UncappedSizeHistCenters;
}

//------------------------------------------------------------------------------
double* vtkSHYXRemeshWithEndpoint::GetUncappedSizeHistCounts()
{
    return this->UncappedSizeHistCounts;
}

//------------------------------------------------------------------------------
double* vtkSHYXRemeshWithEndpoint::GetUncappedSizeHistRange()
{
    return this->UncappedSizeHistRange;
}

//------------------------------------------------------------------------------
int vtkSHYXRemeshWithEndpoint::GetUncappedSizeHistSampleCount()
{
    return this->UncappedSizeHistSampleCount;
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
void vtkSHYXRemeshWithEndpoint::ClearUncappedSizeHistogram()
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
void vtkSHYXRemeshWithEndpoint::FillUncappedSizeHistogram(const std::vector<double>& uncappedSizes)
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
    os << indent << "UncappedSizeHistSampleCount: " << this->UncappedSizeHistSampleCount
       << std::endl;
    os << indent << "UncappedSizeHistRange: [" << this->UncappedSizeHistRange[0] << ", "
       << this->UncappedSizeHistRange[1] << "]" << std::endl;
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

    this->ClearUncappedSizeHistogram();

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

    const auto tReq = std::chrono::steady_clock::now();
    // Histogram panel previews (wall remesh off) only log a one-liner unless VERBOSE=1.
    const bool profilePreviewVerbose =
      this->EnableWallRemesh || sizingProfileVerbose();
    std::optional<SizingProfileRunGuard> profileRun;
    if (profilePreviewVerbose)
    {
        profileRun.emplace(this->EnableWallRemesh != 0);
        char buf[384];
        std::snprintf(buf, sizeof(buf),
            "==== BEGIN pts=%lld cells=%lld wall=%d iters=%d recompute=%d relax=%d "
            "min=%g max=%g tol=%g R=%g scale=%d cap=%d ====",
            static_cast<long long>(patchIn->GetNumberOfPoints()),
            static_cast<long long>(patchIn->GetNumberOfCells()), this->EnableWallRemesh ? 1 : 0,
            this->NumberOfIterations, this->RemeshRecomputeCurvatureEachIteration ? 1 : 0,
            this->NumberOfRelaxationSteps, minLen, maxLen, this->AdaptiveTolerance,
            this->AdaptiveSizingNeighborMaxRatio, this->ScaleToRange ? 1 : 0,
            this->EnableCapRemesh ? 1 : 0);
        sizingProfileLog(buf);
    }
    else
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "PREVIEW (quiet) pts=%lld cells=%lld — set VESPA_SIZING_PROFILE_VERBOSE=1 for detail",
            static_cast<long long>(patchIn->GetNumberOfPoints()),
            static_cast<long long>(patchIn->GetNumberOfCells()));
        // No run guard: stays a single unmarked line.
        sizingProfileLog(buf);
    }

    std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
        std::make_unique<vtkCGALHelper::Vespa_surface>();
    {
        const auto tTo = std::chrono::steady_clock::now();
        if (!vtkCGALHelper::toCGAL(patchIn, cgalMesh.get()))
        {
            vtkErrorMacro(
                "Could not convert extracted patch to CGAL surface (check manifold / triangles).");
            return 0;
        }
        if (profilePreviewVerbose)
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[1] toCGAL: %.1f ms", msSince(tTo));
            sizingProfileLog(buf);
        }
    }

    try
    {
        {
            const auto tN = std::chrono::steady_clock::now();
            PrepareIccVertexNormalsForAdaptiveSizing(cgalMesh->surface, nullptr);
            if (profilePreviewVerbose)
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "[2] PrepareIccVertexNormals (full): %.1f ms",
                    msSince(tN));
                sizingProfileLog(buf);
            }
        }

        std::vector<double> uncappedSizes;
        using SizingTy = FeatureAwareAdaptiveSizingField;
        std::optional<SizingTy> sizingStorage;
        {
            const auto tSz = std::chrono::steady_clock::now();
            if (profilePreviewVerbose)
            {
                sizingProfileLog("[3] sizing field build ...");
            }
            sizingStorage.emplace(this->AdaptiveTolerance, std::make_pair(minLen, maxLen),
                cgalMesh->surface.faces(), cgalMesh->surface,
                static_cast<double>(this->AdaptiveSizingNeighborMaxRatio), this->ScaleToRange,
                &uncappedSizes);
            if (profilePreviewVerbose)
            {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "[3] sizing field build done: %.1f ms",
                    msSince(tSz));
                sizingProfileLog(buf);
            }
        }
        this->FillUncappedSizeHistogram(uncappedSizes);

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

            auto logMeshHealth = [&](const char* tag, const SizingTy* sizingPtr) {
                CGAL_Surface& sm = cgalMesh->surface;
                const auto tH = std::chrono::steady_clock::now();

                const std::size_t nv = static_cast<std::size_t>(sm.number_of_vertices());
                const std::size_t ne = static_cast<std::size_t>(sm.number_of_edges());
                const std::size_t nf = static_cast<std::size_t>(sm.number_of_faces());
                const std::size_t nh = static_cast<std::size_t>(sm.number_of_halfedges());
                const bool tri = CGAL::is_triangle_mesh(sm);
                // verbose=false: avoid dumping to stderr; just bool.
                const bool valid = CGAL::is_valid_polygon_mesh(sm, false);

                std::size_t nBorderE = 0;
                for (CGAL_Surface::Edge_index e : sm.edges())
                {
                    if (sm.is_border(e))
                    {
                        ++nBorderE;
                    }
                }

                std::vector<CGAL_Surface::Face_index> degenFaces;
                pmp::degenerate_faces(sm, std::back_inserter(degenFaces));

                // Non-manifold heuristic: duplicated neighbor in the vertex link.
                std::size_t nNonManifoldV = 0;
                for (CGAL_Surface::Vertex_index v : sm.vertices())
                {
                    if (sm.is_removed(v))
                    {
                        continue;
                    }
                    std::vector<CGAL_Surface::Vertex_index> nbr;
                    nbr.reserve(16);
                    bool bad = false;
                    for (CGAL_Surface::Halfedge_index h : CGAL::halfedges_around_target(v, sm))
                    {
                        const CGAL_Surface::Vertex_index u = sm.source(h);
                        for (CGAL_Surface::Vertex_index x : nbr)
                        {
                            if (x == u)
                            {
                                bad = true;
                                break;
                            }
                        }
                        if (bad)
                        {
                            break;
                        }
                        nbr.push_back(u);
                    }
                    if (bad)
                    {
                        ++nNonManifoldV;
                    }
                }

                double lenMin = std::numeric_limits<double>::infinity();
                double lenMax = 0.0;
                double lenSum = 0.0;
                std::size_t nLen = 0;
                std::size_t nZeroLen = 0;
                for (CGAL_Surface::Edge_index e : sm.edges())
                {
                    const CGAL_Surface::Halfedge_index h = sm.halfedge(e);
                    const auto& pa = sm.point(sm.source(h));
                    const auto& pb = sm.point(sm.target(h));
                    const double sq = CGAL::to_double(CGAL::squared_distance(pa, pb));
                    const double len = std::sqrt((std::max)(0.0, sq));
                    if (!(len > 0.0))
                    {
                        ++nZeroLen;
                    }
                    lenMin = (std::min)(lenMin, len);
                    lenMax = (std::max)(lenMax, len);
                    lenSum += len;
                    ++nLen;
                }

                std::size_t nSizePos = 0;
                std::size_t nSizeZero = 0;
                std::size_t nFeatOn = 0;
                if (sizingPtr)
                {
                    for (CGAL_Surface::Vertex_index v : sm.vertices())
                    {
                        const double s = CGAL::to_double(sizingPtr->at(v, sm));
                        if (s > 0.0)
                        {
                            ++nSizePos;
                        }
                        else
                        {
                            ++nSizeZero;
                        }
                    }
                }
                for (CGAL_Surface::Edge_index e : sm.edges())
                {
                    if (boost::get(featureEdges, e))
                    {
                        ++nFeatOn;
                    }
                }

                {
                    char buf[512];
                    std::snprintf(buf, sizeof(buf),
                        "MESH %s nv=%zu ne=%zu nf=%zu nh=%zu valid=%d tri=%d borderE=%zu "
                        "degenF=%zu nonManifoldV~=%zu zeroLenE=%zu featE=%zu | "
                        "edgeLen[%.6g..%.6g] mean=%.6g | sizePos=%zu sizeZero=%zu | diag=%.1fms",
                        tag, nv, ne, nf, nh, valid ? 1 : 0, tri ? 1 : 0, nBorderE,
                        degenFaces.size(), nNonManifoldV, nZeroLen, nFeatOn,
                        nLen ? lenMin : 0.0, nLen ? lenMax : 0.0,
                        nLen ? (lenSum / static_cast<double>(nLen)) : 0.0, nSizePos, nSizeZero,
                        msSince(tH));
                    sizingProfileLog(buf);
                }
            };

            auto doRemeshSingleIteration = [&](unsigned int iterIndex1Based, unsigned int iterTotal) {
                const std::size_t nv0 =
                    static_cast<std::size_t>(cgalMesh->surface.number_of_vertices());
                const std::size_t nf0 =
                    static_cast<std::size_t>(cgalMesh->surface.number_of_faces());
                {
                    char buf[192];
                    std::snprintf(buf, sizeof(buf),
                        "[4] remesh START %u/%u  nv=%zu nf=%zu", iterIndex1Based, iterTotal, nv0,
                        nf0);
                    sizingProfileLog(buf);
                }
                {
                    char tag[64];
                    std::snprintf(tag, sizeof(tag), "before remesh %u/%u", iterIndex1Based,
                        iterTotal);
                    logMeshHealth(tag, &sizing);
                }

                std::atomic<bool> remeshDone{false};
                std::thread heartbeat([&]() {
                    int sec = 0;
                    while (!remeshDone.load(std::memory_order_relaxed))
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        if (remeshDone.load(std::memory_order_relaxed))
                        {
                            break;
                        }
                        sec += 5;
                        char buf[160];
                        std::snprintf(buf, sizeof(buf),
                            "[4] remesh STILL RUNNING %u/%u  t=%ds (inside CGAL; mesh not sampled)",
                            iterIndex1Based, iterTotal, sec);
                        sizingProfileLog(buf);
                    }
                });

                const auto tRemesh = std::chrono::steady_clock::now();
                pmp::isotropic_remeshing(
                    cgalMesh->surface.faces(), sizing, cgalMesh->surface, remeshNp(1));
                remeshDone.store(true, std::memory_order_relaxed);
                heartbeat.join();

                const std::size_t nv1 =
                    static_cast<std::size_t>(cgalMesh->surface.number_of_vertices());
                const std::size_t nf1 =
                    static_cast<std::size_t>(cgalMesh->surface.number_of_faces());
                {
                    char buf[192];
                    std::snprintf(buf, sizeof(buf),
                        "[4] remesh END   %u/%u  %.1f ms  nv %zu -> %zu (%+lld)  nf %zu -> %zu",
                        iterIndex1Based, iterTotal, msSince(tRemesh), nv0, nv1,
                        static_cast<long long>(nv1) - static_cast<long long>(nv0), nf0, nf1);
                    sizingProfileLog(buf);
                }
                {
                    char tag[64];
                    std::snprintf(tag, sizeof(tag), "after remesh %u/%u", iterIndex1Based,
                        iterTotal);
                    logMeshHealth(tag, &sizing);
                }
            };

            sizingProfileLog("[4] WALL remesh phase begin");
            const auto tWallAll = std::chrono::steady_clock::now();

            if (remeshIterations <= 1u)
            {
                doRemeshSingleIteration(1u, 1u);
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
                    doRemeshSingleIteration(pass + 1u, remeshIterations);
                }
                if (this->RemeshRecomputeCurvatureEachIteration)
                {
                    sizing.recompute_curvature(cgalMesh->surface);
                }
                doRemeshSingleIteration(remeshIterations, remeshIterations);
            }

            {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "[4] WALL remesh phase total: %.1f ms",
                    msSince(tWallAll));
                sizingProfileLog(buf);
            }

            // === Phase 2: Hole fill (FairingContinuity=0) + Cap remesh ===
            if (this->EnableCapRemesh)
            {
                sizingProfileLog("[4b] CAP phase begin");
                const auto tCapAll = std::chrono::steady_clock::now();

                // Reset feature edges after wall remesh (new edges default to false already,
                // but be explicit for safety).
                for (CGAL_Surface::Edge_index e : cgalMesh->surface.edges())
                {
                    boost::put(featureEdges, e, false);
                }

                // Collect one representative border halfedge per open boundary loop.
                std::vector<CGAL_Surface::Halfedge_index> holeStarters;
                {
                    const auto tHoles = std::chrono::steady_clock::now();
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
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "[4b] find holes: %.1f ms n=%zu",
                        msSince(tHoles), holeStarters.size());
                    sizingProfileLog(buf);
                }

                if (!holeStarters.empty())
                {
                    std::vector<CGAL_Surface::Face_index> allCapFaces;
                    std::vector<CGAL_Surface::Vertex_index> allCapVertices;

                    {
                        const auto tFill = std::chrono::steady_clock::now();
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
                        char buf[128];
                        std::snprintf(buf, sizeof(buf),
                            "[4b] hole fill+fair: %.1f ms cap_faces=%zu", msSince(tFill),
                            allCapFaces.size());
                        sizingProfileLog(buf);
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
                        {
                            char buf[128];
                            std::snprintf(buf, sizeof(buf),
                                "[4b] cap remesh START 1/%u faces=%zu", capIters,
                                allCapFaces.size());
                            sizingProfileLog(buf);
                            const auto tCapR = std::chrono::steady_clock::now();
                            pmp::isotropic_remeshing(
                                allCapFaces, capSizing, cgalMesh->surface, capNp());
                            std::snprintf(buf, sizeof(buf), "[4b] cap remesh END   1/%u %.1f ms",
                                capIters, msSince(tCapR));
                            sizingProfileLog(buf);
                        }

                        // Remaining iterations: optionally recompute BFS before each pass.
                        for (unsigned int pass = 1; pass < capIters; ++pass)
                        {
                            if (this->CapRefineSizingField)
                            {
                                capSizing.recompute(cgalMesh->surface);
                            }
                            auto currentCapFaces = capSizing.collectCapFaces(cgalMesh->surface);
                            {
                                char buf[128];
                                std::snprintf(buf, sizeof(buf),
                                    "[4b] cap remesh START %u/%u faces=%zu", pass + 1u, capIters,
                                    currentCapFaces.size());
                                sizingProfileLog(buf);
                                const auto tCapR = std::chrono::steady_clock::now();
                                pmp::isotropic_remeshing(
                                    currentCapFaces, capSizing, cgalMesh->surface, capNp());
                                std::snprintf(buf, sizeof(buf),
                                    "[4b] cap remesh END   %u/%u %.1f ms", pass + 1u, capIters,
                                    msSince(tCapR));
                                sizingProfileLog(buf);
                            }
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
                        {
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "[4b] cap tag components n=%d",
                                componentId);
                            sizingProfileLog(buf);
                        }
                    }
                }
                {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "[4b] CAP phase total: %.1f ms",
                        msSince(tCapAll));
                    sizingProfileLog(buf);
                }
            }
        }
    }
    catch (std::exception& e)
    {
        vtkErrorMacro("CGAL Exception: " << e.what());
        return 0;
    }

    {
        const auto tOut = std::chrono::steady_clock::now();
        vtkCGALHelper::toVTK(cgalMesh.get(), output);
        this->interpolateAttributes(patchIn, output);
        if (profilePreviewVerbose)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[5] toVTK+interpolate: %.1f ms", msSince(tOut));
            sizingProfileLog(buf);
            std::snprintf(buf, sizeof(buf), "==== END total %.1f ms ====", msSince(tReq));
            sizingProfileLog(buf);
        }
    }

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
