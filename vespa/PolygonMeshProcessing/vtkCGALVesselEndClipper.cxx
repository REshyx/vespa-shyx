#include "vtkCGALVesselEndClipper.h"

#include <vtkAppendPolyData.h>
#include <vtkCellArray.h>
#include <vtkCleanPolyData.h>
#include <vtkClipPolyData.h>
#include <vtkDoubleArray.h>
#include <vtkFillHolesFilter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPoints.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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
    int endpointIndex = 0;

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

        char buf[128];
        snprintf(buf, sizeof(buf), "Endpoint %d (%.2f, %.2f, %.2f)",
                 endpointIndex, leafPt[0], leafPt[1], leafPt[2]);
        ci.name = buf;

        clips.push_back(ci);
        ++endpointIndex;
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

        if (vizOutput)
            vizOutput->ShallowCopy(viz);
    }

    if (clips.empty())
    {
        vtkWarningMacro("No degree-1 endpoints found in centerline. Passing through input.");
        output->ShallowCopy(vesselMesh);
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

        ++clippedCount;
    }

    if (clippedCount == 0)
    {
        vtkWarningMacro("No endpoints were successfully clipped. Passing through input.");
        output->ShallowCopy(vesselMesh);
        return 1;
    }

    if (skippedCount > 0)
    {
        vtkWarningMacro("Skipped " << skippedCount << " of " << clips.size()
            << " endpoint clips.");
    }

    // ---------------------------------------------------------------
    // 4. Cap the holes created by clipping (if requested)
    // ---------------------------------------------------------------
    if (this->CapEndpoints)
    {
        double bounds[6];
        currentMesh->GetBounds(bounds);
        double diag = std::sqrt(
            (bounds[1] - bounds[0]) * (bounds[1] - bounds[0]) +
            (bounds[3] - bounds[2]) * (bounds[3] - bounds[2]) +
            (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));

        vtkSmartPointer<vtkFillHolesFilter> filler =
            vtkSmartPointer<vtkFillHolesFilter>::New();
        filler->SetInputData(currentMesh);
        filler->SetHoleSize(diag * diag);
        filler->Update();
        currentMesh = filler->GetOutput();
    }

    // ---------------------------------------------------------------
    // 5. Final cleanup
    // ---------------------------------------------------------------
    vtkSmartPointer<vtkCleanPolyData> finalClean = vtkSmartPointer<vtkCleanPolyData>::New();
    finalClean->SetInputData(currentMesh);
    finalClean->Update();

    output->ShallowCopy(finalClean->GetOutput());

    return 1;
}
