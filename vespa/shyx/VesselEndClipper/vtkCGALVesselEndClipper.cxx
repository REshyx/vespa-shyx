#include "vtkCGALVesselEndClipper.h"

#include "vtkCGALHelper.h"

#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>

#include <boost/graph/graph_traits.hpp>
#include <boost/property_map/property_map.hpp>

#include <vtkAppendPolyData.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCleanPolyData.h>
#include <vtkClipPolyData.h>
#include <vtkDoubleArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPoints.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkSmartPointer.h>
#include <vtkTriangleFilter.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

vtkStandardNewMacro(vtkCGALVesselEndClipper);

//------------------------------------------------------------------------------
vtkCGALVesselEndClipper::vtkCGALVesselEndClipper()
{
    this->SetNumberOfInputPorts(2);
    this->SetNumberOfOutputPorts(2);
    this->EndpointSelection = vtkSmartPointer<vtkDataArraySelection>::New();
}

//------------------------------------------------------------------------------
vtkCGALVesselEndClipper::~vtkCGALVesselEndClipper()
{
    this->SetInteractiveCutPackedString(nullptr);
}

//------------------------------------------------------------------------------
int vtkCGALVesselEndClipper::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0 || port == 1)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkCGALVesselEndClipper::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "ClipOffset: " << this->ClipOffset << std::endl;
    os << indent << "TangentDepth: " << this->TangentDepth << std::endl;
    os << indent << "CapEndpoints: " << this->CapEndpoints << std::endl;
    os << indent << "FairingContinuity: " << this->FairingContinuity << std::endl;
    os << indent << "UseInteractiveCutPlanes: " << (this->UseInteractiveCutPlanes ? 1 : 0) << std::endl;
    os << indent << "InteractiveCutPackedString: "
       << (this->InteractiveCutPackedString ? this->InteractiveCutPackedString : "") << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkCGALVesselEndClipper::SetCenterlineConnection(vtkAlgorithmOutput* algOutput)
{
    this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
vtkDataArraySelection* vtkCGALVesselEndClipper::GetEndpointSelection()
{
    return this->EndpointSelection;
}

//------------------------------------------------------------------------------
vtkMTimeType vtkCGALVesselEndClipper::GetMTime()
{
    vtkMTimeType mTime = this->Superclass::GetMTime();
    if (this->EndpointSelection)
    {
        vtkMTimeType selTime = this->EndpointSelection->GetMTime();
        mTime = std::max(mTime, selTime);
    }
    return mTime;
}

//------------------------------------------------------------------------------
static bool ParseInteractivePacked(const char* s, std::vector<double>& out)
{
    out.clear();
    if (!s || !*s)
    {
        return false;
    }
    std::istringstream iss(s);
    double             v = 0.0;
    while (iss >> v)
    {
        out.push_back(v);
    }
    return !out.empty() && (out.size() % 6u) == 0u;
}

//------------------------------------------------------------------------------
static vtkSmartPointer<vtkPolyData> clipOneSide(
    vtkPolyData* input, vtkPlane* plane, bool keepPositive)
{
    vtkSmartPointer<vtkClipPolyData> clipper = vtkSmartPointer<vtkClipPolyData>::New();
    clipper->SetInputData(input);
    clipper->SetClipFunction(plane);
    clipper->SetValue(0.0);
    if (!keepPositive)
        clipper->InsideOutOn();
    clipper->Update();
    return clipper->GetOutput();
}

//------------------------------------------------------------------------------
static vtkSmartPointer<vtkPolyData> extractClosestComponent(
    vtkPolyData* input, const double pt[3])
{
    if (!input || input->GetNumberOfPoints() == 0)
        return nullptr;

    vtkSmartPointer<vtkPolyDataConnectivityFilter> conn =
        vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    conn->SetInputData(input);
    conn->SetExtractionModeToClosestPointRegion();
    conn->SetClosestPoint(pt[0], pt[1], pt[2]);
    conn->Update();
    return conn->GetOutput();
}

//------------------------------------------------------------------------------
// Remove the connected component closest to |endPt| from |side|.
// Returns everything else (may be empty if there was only one component).
static vtkSmartPointer<vtkPolyData> removeClosestComponent(
    vtkPolyData* side, const double endPt[3])
{
    if (!side || side->GetNumberOfPoints() == 0)
        return vtkSmartPointer<vtkPolyData>::New();

    vtkSmartPointer<vtkPolyDataConnectivityFilter> allRegions =
        vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    allRegions->SetInputData(side);
    allRegions->SetExtractionModeToAllRegions();
    allRegions->ColorRegionsOn();
    allRegions->Update();

    int nRegions = allRegions->GetNumberOfExtractedRegions();
    if (nRegions <= 1)
        return vtkSmartPointer<vtkPolyData>::New();

    // Identify which region is closest to the endpoint
    vtkSmartPointer<vtkPolyData> closestComp = extractClosestComponent(side, endPt);
    if (!closestComp || closestComp->GetNumberOfPoints() == 0)
        return vtkSmartPointer<vtkPolyData>::New();

    double probePt[3];
    closestComp->GetPoint(0, probePt);

    vtkSmartPointer<vtkPointLocator> locator = vtkSmartPointer<vtkPointLocator>::New();
    locator->SetDataSet(allRegions->GetOutput());
    locator->BuildLocator();
    vtkIdType ptId = locator->FindClosestPoint(probePt);

    vtkDataArray* regionIds =
        allRegions->GetOutput()->GetPointData()->GetArray("RegionId");
    if (!regionIds)
        return vtkSmartPointer<vtkPolyData>::New();

    int tipRegionId = static_cast<int>(regionIds->GetTuple1(ptId));

    vtkSmartPointer<vtkPolyDataConnectivityFilter> keep =
        vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    keep->SetInputData(side);
    keep->SetExtractionModeToSpecifiedRegions();
    for (int r = 0; r < nRegions; ++r)
    {
        if (r != tipRegionId)
            keep->AddSpecifiedRegion(r);
    }
    keep->Update();
    return keep->GetOutput();
}

//------------------------------------------------------------------------------
static void EnsureCellEndpointIndexArray(vtkPolyData* pd, int fillValue = -1)
{
    if (!pd)
        return;
    const vtkIdType nCells = pd->GetNumberOfCells();
    vtkIntArray* existing = vtkIntArray::SafeDownCast(pd->GetCellData()->GetArray("EndpointIndex"));
    if (existing && existing->GetNumberOfTuples() == nCells)
    {
        pd->GetCellData()->SetActiveScalars("EndpointIndex");
        return;
    }
    vtkNew<vtkIntArray> arr;
    arr->SetName("EndpointIndex");
    arr->SetNumberOfTuples(nCells);
    arr->Fill(fillValue);
    pd->GetCellData()->RemoveArray("EndpointIndex");
    pd->GetCellData()->AddArray(arr);
    pd->GetCellData()->SetActiveScalars("EndpointIndex");
}

//------------------------------------------------------------------------------
template <typename FaceEndpointMap>
static void ExportVespaSurfaceToVTK(
    vtkCGALHelper::Vespa_surface const* cgalMesh, FaceEndpointMap const& endpointIndexByFace, vtkPolyData* vtkMesh)
{
    vtkNew<vtkPoints> pts;
    const vtkIdType outNPts = num_vertices(cgalMesh->surface);
    pts->Allocate(outNPts);
    std::vector<vtkIdType> vmap(outNPts);

    for (auto vertex : vertices(cgalMesh->surface))
    {
        const auto& p = get(cgalMesh->coords, vertex);
        vtkIdType   id =
            pts->InsertNextPoint(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
        vmap[vertex] = id;
    }
    pts->Squeeze();

    vtkNew<vtkCellArray> cells;
    cells->AllocateEstimate(num_faces(cgalMesh->surface), 3);

    vtkNew<vtkIntArray> epCell;
    epCell->SetName("EndpointIndex");
    epCell->SetNumberOfComponents(1);

    for (auto face : faces(cgalMesh->surface))
    {
        vtkNew<vtkIdList> ids;
        ids->Allocate(3);
        for (auto edge : halfedges_around_face(halfedge(face, cgalMesh->surface), cgalMesh->surface))
        {
            ids->InsertNextId(vmap[source(edge, cgalMesh->surface)]);
        }
        cells->InsertNextCell(ids);
        epCell->InsertNextValue(get(endpointIndexByFace, face));
    }
    cells->Squeeze();

    vtkMesh->Reset();
    vtkMesh->SetPoints(pts);
    vtkMesh->SetPolys(cells);
    vtkMesh->GetCellData()->AddArray(epCell);
    vtkMesh->GetCellData()->SetActiveScalars("EndpointIndex");
}

//------------------------------------------------------------------------------
int vtkCGALVesselEndClipper::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* vesselMesh = vtkPolyData::GetData(inputVector[0]);
    vtkPolyData* centerline = vtkPolyData::GetData(inputVector[1]);
    vtkPolyData* output     = vtkPolyData::GetData(outputVector, 0);
    vtkPolyData* vizOutput  = vtkPolyData::GetData(outputVector, 1);

    if (!vesselMesh || !centerline)
    {
        vtkErrorMacro("Missing vessel mesh or centerline input.");
        return 0;
    }

    if (vesselMesh->GetNumberOfCells() == 0)
    {
        vtkErrorMacro("Vessel mesh is empty.");
        return 0;
    }

    if (centerline->GetNumberOfLines() == 0)
    {
        vtkErrorMacro("Centerline contains no line cells.");
        return 0;
    }

    // ---------------------------------------------------------------
    // 1. Build adjacency graph from centerline line cells
    // ---------------------------------------------------------------
    vtkIdType nPts = centerline->GetNumberOfPoints();
    std::vector<std::vector<vtkIdType>> adjacency(nPts);

    vtkCellArray* lines = centerline->GetLines();
    if (!lines)
    {
        vtkErrorMacro("Centerline contains no line cells.");
        return 0;
    }

    lines->InitTraversal();
    vtkIdType        nCellPts;
    const vtkIdType* cellPts;
    while (lines->GetNextCell(nCellPts, cellPts))
    {
        if (nCellPts == 2)
        {
            adjacency[cellPts[0]].push_back(cellPts[1]);
            adjacency[cellPts[1]].push_back(cellPts[0]);
        }
    }

    // ---------------------------------------------------------------
    // 2. Identify degree-1 vertices and compute clipping planes.
    // ---------------------------------------------------------------
    struct ClipInfo
    {
        double      origin[3];
        double      normal[3];
        double      endPt[3];
        std::string name;
    };
    std::vector<ClipInfo> clips;

    const int tangentDepth = this->TangentDepth;

    for (vtkIdType i = 0; i < nPts; ++i)
    {
        if (adjacency[i].size() != 1)
            continue;

        double leafPt[3];
        centerline->GetPoint(i, leafPt);

        vtkIdType current = i;
        vtkIdType prev    = -1;
        for (int step = 0; step < tangentDepth; ++step)
        {
            vtkIdType next = -1;
            for (vtkIdType neighbor : adjacency[current])
            {
                if (neighbor != prev)
                {
                    next = neighbor;
                    break;
                }
            }
            if (next < 0)
                break;
            prev    = current;
            current = next;
        }

        double deepPt[3];
        centerline->GetPoint(current, deepPt);

        double tx  = leafPt[0] - deepPt[0];
        double ty  = leafPt[1] - deepPt[1];
        double tz  = leafPt[2] - deepPt[2];
        double len = std::sqrt(tx * tx + ty * ty + tz * tz);

        if (len < 1e-15)
            continue;

        tx /= len;
        ty /= len;
        tz /= len;

        ClipInfo ci;
        ci.origin[0] = leafPt[0] + this->ClipOffset * tx;
        ci.origin[1] = leafPt[1] + this->ClipOffset * ty;
        ci.origin[2] = leafPt[2] + this->ClipOffset * tz;
        ci.normal[0] = tx;
        ci.normal[1] = ty;
        ci.normal[2] = tz;
        ci.endPt[0]  = leafPt[0];
        ci.endPt[1]  = leafPt[1];
        ci.endPt[2]  = leafPt[2];

        clips.push_back(ci);
    }

    // Fixed 5-digit zero padding so UI string sort matches numeric order.
    {
        const int nClips = static_cast<int>(clips.size());
        for (int e = 0; e < nClips; ++e)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Endpoint %05d (%.2f, %.2f, %.2f)", e,
                clips[e].endPt[0], clips[e].endPt[1], clips[e].endPt[2]);
            clips[e].name = buf;
        }
    }

    // ---------------------------------------------------------------
    // 2a. Optional interactive plane overrides (ParaView custom widget).
    // ---------------------------------------------------------------
    if (this->UseInteractiveCutPlanes)
    {
        std::vector<double> packed;
        if (ParseInteractivePacked(this->InteractiveCutPackedString, packed) &&
            packed.size() == 6u * clips.size())
        {
            for (size_t i = 0; i < clips.size(); ++i)
            {
                const double* o = &packed[6 * i];
                const double* d = &packed[6 * i + 3];
                double        nx = d[0] - o[0];
                double        ny = d[1] - o[1];
                double        nz = d[2] - o[2];
                const double  nn = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nn > 1e-15)
                {
                    clips[i].origin[0] = o[0];
                    clips[i].origin[1] = o[1];
                    clips[i].origin[2] = o[2];
                    clips[i].normal[0] = nx / nn;
                    clips[i].normal[1] = ny / nn;
                    clips[i].normal[2] = nz / nn;
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // 2b. Sync the vtkDataArraySelection with the discovered endpoints.
    // ---------------------------------------------------------------
    std::set<std::string> currentNames;
    for (const auto& ci : clips)
        currentNames.insert(ci.name);

    for (int j = this->EndpointSelection->GetNumberOfArrays() - 1; j >= 0; --j)
    {
        const char* existing = this->EndpointSelection->GetArrayName(j);
        if (currentNames.find(existing) == currentNames.end())
            this->EndpointSelection->RemoveArrayByName(existing);
    }

    for (const auto& ci : clips)
    {
        if (!this->EndpointSelection->ArrayExists(ci.name.c_str()))
            this->EndpointSelection->EnableArray(ci.name.c_str());
    }

    // ---------------------------------------------------------------
    // 2c. Build visualization output (port 1): a line segment at
    //     each endpoint showing the clip plane normal direction,
    //     plus point-data arrays for downstream Glyph / coloring.
    // ---------------------------------------------------------------
    {
        double bbox[6];
        vesselMesh->GetBounds(bbox);
        double diag = std::sqrt(
            (bbox[1] - bbox[0]) * (bbox[1] - bbox[0]) +
            (bbox[3] - bbox[2]) * (bbox[3] - bbox[2]) +
            (bbox[5] - bbox[4]) * (bbox[5] - bbox[4]));
        double arrowLen = diag * 0.04;

        vtkSmartPointer<vtkPoints>    vizPts   = vtkSmartPointer<vtkPoints>::New();
        vtkSmartPointer<vtkCellArray> vizLines = vtkSmartPointer<vtkCellArray>::New();
        vtkSmartPointer<vtkCellArray> vizVerts = vtkSmartPointer<vtkCellArray>::New();

        vtkSmartPointer<vtkDoubleArray> arrNormals = vtkSmartPointer<vtkDoubleArray>::New();
        arrNormals->SetName("ClipNormal");
        arrNormals->SetNumberOfComponents(3);

        vtkSmartPointer<vtkIntArray> arrEnabled = vtkSmartPointer<vtkIntArray>::New();
        arrEnabled->SetName("Enabled");
        arrEnabled->SetNumberOfComponents(1);

        vtkSmartPointer<vtkIntArray> arrIndex = vtkSmartPointer<vtkIntArray>::New();
        arrIndex->SetName("EndpointIndex");
        arrIndex->SetNumberOfComponents(1);

        for (size_t i = 0; i < clips.size(); ++i)
        {
            const double* o = clips[i].origin;
            const double* n = clips[i].normal;
            bool enabled = this->EndpointSelection->ArrayIsEnabled(clips[i].name.c_str());

            vtkIdType ptA = vizPts->InsertNextPoint(o[0], o[1], o[2]);
            vtkIdType ptB = vizPts->InsertNextPoint(
                o[0] + arrowLen * n[0],
                o[1] + arrowLen * n[1],
                o[2] + arrowLen * n[2]);

            vizLines->InsertNextCell(2);
            vizLines->InsertCellPoint(ptA);
            vizLines->InsertCellPoint(ptB);

            vizVerts->InsertNextCell(1);
            vizVerts->InsertCellPoint(ptA);

            for (int p = 0; p < 2; ++p)
            {
                arrNormals->InsertNextTuple3(n[0], n[1], n[2]);
                arrEnabled->InsertNextValue(enabled ? 1 : 0);
                arrIndex->InsertNextValue(static_cast<int>(i));
            }
        }

        vtkSmartPointer<vtkPolyData> viz = vtkSmartPointer<vtkPolyData>::New();
        viz->SetPoints(vizPts);
        viz->SetLines(vizLines);
        viz->SetVerts(vizVerts);
        viz->GetPointData()->AddArray(arrNormals);
        viz->GetPointData()->AddArray(arrEnabled);
        viz->GetPointData()->AddArray(arrIndex);
        // Default scalar coloring / Point Label "active array" follows EndpointIndex on port 1.
        viz->GetPointData()->SetActiveScalars("EndpointIndex");

        if (vizOutput)
            vizOutput->ShallowCopy(viz);
    }

    if (clips.empty())
    {
        vtkWarningMacro("No degree-1 endpoints found in centerline. Passing through input.");
        output->DeepCopy(vesselMesh);
        EnsureCellEndpointIndexArray(output, -1);
        return 1;
    }

    // ---------------------------------------------------------------
    // 3. For each ENABLED endpoint, clip with an infinite plane,
    //    then use CONNECTIVITY ANALYSIS to identify and remove
    //    ONLY the local tip component nearest to the endpoint.
    //    Distant parts that happen to be on the same side of the
    //    plane are topologically separate components — they are
    //    detected and merged back automatically.
    // ---------------------------------------------------------------
    vtkSmartPointer<vtkPolyData> currentMesh = vtkSmartPointer<vtkPolyData>::New();
    currentMesh->DeepCopy(vesselMesh);
    EnsureCellEndpointIndexArray(currentMesh, -1);

    int clippedCount = 0;
    int skippedCount = 0;

    for (size_t i = 0; i < clips.size(); ++i)
    {
        if (!this->EndpointSelection->ArrayIsEnabled(clips[i].name.c_str()))
            continue;

        vtkSmartPointer<vtkPlane> plane = vtkSmartPointer<vtkPlane>::New();
        plane->SetOrigin(clips[i].origin);
        plane->SetNormal(clips[i].normal);

        // Split the mesh into two halves along the infinite plane
        vtkSmartPointer<vtkPolyData> sidePos = clipOneSide(currentMesh, plane, true);
        vtkSmartPointer<vtkPolyData> sideNeg = clipOneSide(currentMesh, plane, false);

        if ((!sidePos || sidePos->GetNumberOfPoints() == 0) &&
            (!sideNeg || sideNeg->GetNumberOfPoints() == 0))
        {
            ++skippedCount;
            continue;
        }

        // On each side, find the connected component closest to
        // the skeleton endpoint.  The SMALLER of the two is the tip.
        vtkSmartPointer<vtkPolyData> closestPos =
            extractClosestComponent(sidePos, clips[i].endPt);
        vtkSmartPointer<vtkPolyData> closestNeg =
            extractClosestComponent(sideNeg, clips[i].endPt);

        vtkIdType nClosestPos = closestPos ? closestPos->GetNumberOfPoints() : 0;
        vtkIdType nClosestNeg = closestNeg ? closestNeg->GetNumberOfPoints() : 0;

        if (nClosestPos == 0 && nClosestNeg == 0)
        {
            ++skippedCount;
            continue;
        }

        bool tipOnPositive;
        if (nClosestPos == 0)
            tipOnPositive = false;
        else if (nClosestNeg == 0)
            tipOnPositive = true;
        else
            tipOnPositive = (nClosestPos <= nClosestNeg);

        vtkPolyData* tipSide  = tipOnPositive ? sidePos  : sideNeg;
        vtkPolyData* bodySide = tipOnPositive ? sideNeg  : sidePos;
        vtkIdType    tipPts   = tipOnPositive ? nClosestPos : nClosestNeg;

        // Informational warning for large tips (but still proceed)
        if (tipPts > currentMesh->GetNumberOfPoints() / 3)
        {
            vtkWarningMacro("Tip at endpoint " << i << " is large ("
                << tipPts << " / " << currentMesh->GetNumberOfPoints()
                << " pts). Uncheck this endpoint if the result is wrong.");
        }

        // Remove ONLY the tip component from its side;
        // any other components on the same side (distant parts
        // accidentally cut by the infinite plane) are kept and
        // merged back with the body side.
        vtkSmartPointer<vtkPolyData> keptFromTipSide =
            removeClosestComponent(tipSide, clips[i].endPt);

        bool hasKept = (keptFromTipSide && keptFromTipSide->GetNumberOfCells() > 0);

        if (!hasKept)
        {
            // Tip side was entirely one component (the tip) → body side is the result
            currentMesh->DeepCopy(bodySide);
        }
        else
        {
            // Merge body side + non-tip fragments from the tip side
            vtkSmartPointer<vtkAppendPolyData> merger =
                vtkSmartPointer<vtkAppendPolyData>::New();
            merger->AddInputData(bodySide);
            merger->AddInputData(keptFromTipSide);
            merger->Update();

            vtkSmartPointer<vtkCleanPolyData> cleaner =
                vtkSmartPointer<vtkCleanPolyData>::New();
            cleaner->SetInputData(merger->GetOutput());
            cleaner->Update();

            currentMesh->DeepCopy(cleaner->GetOutput());
        }

        if (this->CapEndpoints)
        {
            namespace pmp = CGAL::Polygon_mesh_processing;
            using Graph_halfedge = boost::graph_traits<CGAL_Surface>::halfedge_descriptor;

            EnsureCellEndpointIndexArray(currentMesh, -1);

            vtkSmartPointer<vtkTriangleFilter> tri = vtkSmartPointer<vtkTriangleFilter>::New();
            tri->SetInputData(currentMesh);
            tri->Update();
            vtkPolyData* triMesh = tri->GetOutput();

            std::vector<Graph_Faces> vtkCellToCgalFace;
            std::unique_ptr<vtkCGALHelper::Vespa_surface> cgalMesh =
                std::make_unique<vtkCGALHelper::Vespa_surface>();
            if (!vtkCGALHelper::toCGAL(triMesh, cgalMesh.get(), &vtkCellToCgalFace))
            {
                vtkErrorMacro("CGAL endpoint capping at endpoint " << i
                    << ": mesh could not be converted to a CGAL surface mesh. "
                       "Try CapEndpoints off or repair non-manifold geometry.");
                return 0;
            }

            auto epmap = cgalMesh->surface.add_property_map<Graph_Faces, int>("f:epidx", -1).first;

            vtkIntArray* epFromVtk =
                vtkIntArray::SafeDownCast(triMesh->GetCellData()->GetArray("EndpointIndex"));
            const vtkIdType nTriCells = triMesh->GetNumberOfCells();
            if (epFromVtk && epFromVtk->GetNumberOfTuples() == nTriCells &&
                static_cast<vtkIdType>(vtkCellToCgalFace.size()) == nTriCells)
            {
                for (vtkIdType cid = 0; cid < nTriCells; ++cid)
                {
                    put(epmap, vtkCellToCgalFace[static_cast<size_t>(cid)], epFromVtk->GetValue(cid));
                }
            }

            std::vector<Graph_Verts> patch_vertices;
            std::vector<Graph_Faces> patch_facets;
            bool holeOk = true;
            try
            {
                std::vector<Graph_halfedge> borderCycles;
                pmp::extract_boundary_cycles(cgalMesh->surface, std::back_inserter(borderCycles));
                if (borderCycles.size() > 1u)
                {
                    vtkWarningMacro("Endpoint " << i << ": mesh has " << borderCycles.size()
                        << " boundary cycles; all new cap triangles are tagged with EndpointIndex == "
                        << i << ".");
                }
                for (Graph_halfedge h : borderCycles)
                {
                    holeOk &= std::get<0>(pmp::triangulate_refine_and_fair_hole(cgalMesh->surface, h,
                        pmp::parameters::fairing_continuity(this->FairingContinuity)
                            .face_output_iterator(std::back_inserter(patch_facets))
                            .vertex_output_iterator(std::back_inserter(patch_vertices))));
                }
            }
            catch (std::exception& e)
            {
                vtkErrorMacro("CGAL endpoint capping at endpoint " << i << ": " << e.what());
                return 0;
            }

            if (!holeOk)
            {
                vtkWarningMacro("CGAL endpoint capping at endpoint " << i
                    << ": one or more boundary cycles could not be filled cleanly.");
            }

            for (Graph_Faces pf : patch_facets)
            {
                put(epmap, pf, static_cast<int>(i));
            }

            vtkSmartPointer<vtkPolyData> capped = vtkSmartPointer<vtkPolyData>::New();
            ExportVespaSurfaceToVTK(cgalMesh.get(), epmap, capped);
            currentMesh->DeepCopy(capped);
        }

        ++clippedCount;
    }

    if (clippedCount == 0)
    {
        vtkWarningMacro("No endpoints were successfully clipped. Passing through input.");
        output->DeepCopy(vesselMesh);
        EnsureCellEndpointIndexArray(output, -1);
        return 1;
    }

    if (skippedCount > 0)
    {
        vtkWarningMacro("Skipped " << skippedCount << " of " << clips.size()
            << " endpoint clips.");
    }

    if (this->CapEndpoints)
    {
        output->ShallowCopy(currentMesh);
        output->GetCellData()->SetActiveScalars("EndpointIndex");
    }
    else
    {
        vtkSmartPointer<vtkCleanPolyData> finalClean = vtkSmartPointer<vtkCleanPolyData>::New();
        finalClean->SetInputData(currentMesh);
        finalClean->Update();
        vtkPolyData* cleaned = finalClean->GetOutput();
        cleaned->GetCellData()->RemoveArray("EndpointIndex");
        EnsureCellEndpointIndexArray(cleaned, -1);
        output->ShallowCopy(cleaned);
    }

    return 1;
}
