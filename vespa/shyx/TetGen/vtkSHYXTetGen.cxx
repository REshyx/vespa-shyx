#include "vtkSHYXTetGen.h"

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkProbeFilter.h>
#include <vtkUnstructuredGrid.h>

#define TETLIBRARY
#include "tetgen.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

vtkStandardNewMacro(vtkSHYXTetGen);

//------------------------------------------------------------------------------
vtkSHYXTetGen::vtkSHYXTetGen()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
    this->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_NONE, nullptr);
}

//------------------------------------------------------------------------------
const char* vtkSHYXTetGen::GetMaskArrayName()
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
int vtkSHYXTetGen::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXTetGen::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkUnstructuredGrid");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXTetGen::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "LimitMaxVolume: " << (this->LimitMaxVolume ? "ON" : "OFF") << std::endl;
    os << indent << "MaxVolume: " << this->MaxVolume << std::endl;
    os << indent << "MaxRadiusEdgeRatio: " << this->MaxRadiusEdgeRatio << std::endl;
    os << indent << "MinDihedralAngle: " << this->MinDihedralAngle << std::endl;
    os << indent << "Nobisect: " << (this->Nobisect ? "ON" : "OFF") << std::endl;
    os << indent << "DoCheck: " << (this->DoCheck ? "ON" : "OFF") << std::endl;
    os << indent << "UseCDT: " << (this->UseCDT ? "ON" : "OFF") << std::endl;
    os << indent << "CDTRefine: " << this->CDTRefine << std::endl;
    os << indent << "Epsilon: " << this->Epsilon << std::endl;
    os << indent << "UseSurfaceDensitySizing: " << (this->UseSurfaceDensitySizing ? "ON" : "OFF") << std::endl;
    os << indent << "SurfaceSizingScale: " << this->SurfaceSizingScale << std::endl;
    os << indent << "ProbeInputPointData: " << (this->ProbeInputPointData ? "ON" : "OFF") << std::endl;
    os << indent << "MaskArrayEnabled: " << (this->MaskArrayEnabled ? "ON" : "OFF") << std::endl;
    if (const char* const maskName = this->GetMaskArrayName())
    {
        os << indent << "MaskArrayName: " << maskName << std::endl;
    }
    else
    {
        os << indent << "MaskArrayName: (null)" << std::endl;
    }
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
namespace
{
void ApplyBinaryMaskInPlace(vtkDataArray* arr)
{
    if (!arr)
    {
        return;
    }

    const int numComp = arr->GetNumberOfComponents();
    const vtkIdType numTuples = arr->GetNumberOfTuples();
    for (vtkIdType i = 0; i < numTuples; ++i)
    {
        const double v = arr->GetComponent(i, 0);
        const double masked = (v > 0.0) ? 1.0 : 0.0;
        for (int c = 0; c < numComp; ++c)
        {
            arr->SetComponent(i, c, masked);
        }
    }
    arr->Modified();
}

bool ApplyMaskArrayToSurface(vtkPolyData* surface, const char* arrayName)
{
    if (!surface || !arrayName || arrayName[0] == '\0')
    {
        return false;
    }

    vtkDataArray* const cellArr = surface->GetCellData()->GetArray(arrayName);
    vtkDataArray* const ptArr = surface->GetPointData()->GetArray(arrayName);
    const vtkIdType nCells = surface->GetNumberOfCells();
    const vtkIdType nPts = surface->GetNumberOfPoints();

    const bool cellOk = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
    const bool ptOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);

    vtkDataArray* maskArray = nullptr;
    bool isCellData = false;
    if (cellOk && ptOk)
    {
        maskArray = cellArr;
        isCellData = true;
    }
    else if (cellOk)
    {
        maskArray = cellArr;
        isCellData = true;
    }
    else if (ptOk)
    {
        maskArray = ptArr;
        isCellData = false;
    }
    else
    {
        return false;
    }

    ApplyBinaryMaskInPlace(maskArray);

    if (!isCellData)
    {
        return true;
    }

    vtkNew<vtkCellDataToPointData> cellToPoint;
    cellToPoint->SetInputData(surface);
    cellToPoint->PassCellDataOff();
    cellToPoint->Update();

    surface->ShallowCopy(cellToPoint->GetOutput());
    return true;
}

bool ComputeSurfaceVertexSizing(vtkPolyData* input, double scale, std::vector<REAL>& metrics)
{
    vtkPoints* const points = input->GetPoints();
    if (!points || input->GetNumberOfCells() == 0)
    {
        return false;
    }

    const vtkIdType numPoints = points->GetNumberOfPoints();
    std::vector<double> edgeLengthSum(static_cast<size_t>(numPoints), 0.0);
    std::vector<vtkIdType> edgeCount(static_cast<size_t>(numPoints), 0);

    auto accumulateEdge = [&](vtkIdType idA, vtkIdType idB) {
        if (idA < 0 || idB < 0 || idA >= numPoints || idB >= numPoints || idA == idB)
        {
            return;
        }
        double pA[3];
        double pB[3];
        points->GetPoint(idA, pA);
        points->GetPoint(idB, pB);
        const double dx = pA[0] - pB[0];
        const double dy = pA[1] - pB[1];
        const double dz = pA[2] - pB[2];
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len <= 0.0)
        {
            return;
        }
        edgeLengthSum[static_cast<size_t>(idA)] += len;
        edgeCount[static_cast<size_t>(idA)]++;
        edgeLengthSum[static_cast<size_t>(idB)] += len;
        edgeCount[static_cast<size_t>(idB)]++;
    };

    for (vtkIdType cellId = 0; cellId < input->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = input->GetCell(cellId);
        if (!cell || cell->GetCellType() != VTK_TRIANGLE || cell->GetNumberOfPoints() != 3)
        {
            continue;
        }
        const vtkIdType ids[3] = {
            cell->GetPointId(0),
            cell->GetPointId(1),
            cell->GetPointId(2),
        };
        accumulateEdge(ids[0], ids[1]);
        accumulateEdge(ids[1], ids[2]);
        accumulateEdge(ids[2], ids[0]);
    }

    metrics.assign(static_cast<size_t>(numPoints), 0.0);
    double fallbackH = 0.0;
    for (vtkIdType i = 0; i < numPoints; ++i)
    {
        const vtkIdType count = edgeCount[static_cast<size_t>(i)];
        if (count > 0)
        {
            const double h = scale * edgeLengthSum[static_cast<size_t>(i)] / static_cast<double>(count);
            metrics[static_cast<size_t>(i)] = static_cast<REAL>(h);
            if (h > fallbackH)
            {
                fallbackH = h;
            }
        }
    }

    if (fallbackH <= 0.0)
    {
        return false;
    }

    for (vtkIdType i = 0; i < numPoints; ++i)
    {
        if (metrics[static_cast<size_t>(i)] <= 0.0)
        {
            metrics[static_cast<size_t>(i)] = static_cast<REAL>(fallbackH);
        }
    }
    return true;
}
} // namespace

//------------------------------------------------------------------------------
static bool vtkToTetgenio(vtkPolyData* input, tetgenio& in)
{
    vtkPoints* points = input->GetPoints();
    if (!points || points->GetNumberOfPoints() == 0)
    {
        return false;
    }

    vtkIdType numPoints = points->GetNumberOfPoints();
    in.firstnumber = 0;
    in.numberofpoints = static_cast<int>(numPoints);
    in.pointlist = new REAL[numPoints * 3];
    in.numberofpointattributes = 0;
    in.pointattributelist = nullptr;
    in.pointmarkerlist = nullptr;

    for (vtkIdType i = 0; i < numPoints; ++i)
    {
        double pt[3];
        points->GetPoint(i, pt);
        in.pointlist[i * 3 + 0] = pt[0];
        in.pointlist[i * 3 + 1] = pt[1];
        in.pointlist[i * 3 + 2] = pt[2];
    }

    vtkIdType numCells = input->GetNumberOfCells();
    if (numCells == 0)
    {
        return false;
    }

    std::vector<int> triangleIndices;
    triangleIndices.reserve(static_cast<size_t>(numCells));
    for (vtkIdType i = 0; i < numCells; ++i)
    {
        vtkCell* cell = input->GetCell(i);
        if (cell->GetCellType() == VTK_TRIANGLE && cell->GetNumberOfPoints() == 3)
        {
            triangleIndices.push_back(static_cast<int>(i));
        }
    }
    if (triangleIndices.empty())
    {
        return false;
    }

    in.numberoffacets = static_cast<int>(triangleIndices.size());
    in.facetlist = new tetgenio::facet[in.numberoffacets];
    in.facetmarkerlist = nullptr;

    for (int fi = 0; fi < in.numberoffacets; ++fi)
    {
        vtkCell* cell = input->GetCell(triangleIndices[fi]);
        tetgenio::facet& f = in.facetlist[fi];
        f.numberofpolygons = 1;
        f.numberofholes = 0;
        f.holelist = nullptr;
        f.polygonlist = new tetgenio::polygon[1];
        tetgenio::polygon& p = f.polygonlist[0];
        p.numberofvertices = 3;
        p.vertexlist = new int[3];
        p.vertexlist[0] = static_cast<int>(cell->GetPointId(0));
        p.vertexlist[1] = static_cast<int>(cell->GetPointId(1));
        p.vertexlist[2] = static_cast<int>(cell->GetPointId(2));
    }

    in.numberofholes = 0;
    in.holelist = nullptr;
    in.numberofregions = 0;
    in.regionlist = nullptr;
    return true;
}

//------------------------------------------------------------------------------
static bool tetgenioToVtk(tetgenio& out, vtkUnstructuredGrid* output)
{
    if (out.numberofpoints == 0 || out.numberoftetrahedra == 0 || !out.pointlist || !out.tetrahedronlist)
    {
        return false;
    }

    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();
    pts->SetNumberOfPoints(out.numberofpoints);
    for (int i = 0; i < out.numberofpoints; ++i)
    {
        pts->SetPoint(i,
            out.pointlist[i * 3 + 0],
            out.pointlist[i * 3 + 1],
            out.pointlist[i * 3 + 2]);
    }

    output->SetPoints(pts);
    output->Allocate(out.numberoftetrahedra);

    int corners = (out.numberofcorners > 0) ? out.numberofcorners : 4;
    for (int i = 0; i < out.numberoftetrahedra; ++i)
    {
        vtkIdType ids[4];
        ids[0] = static_cast<vtkIdType>(out.tetrahedronlist[i * corners + 0]);
        ids[1] = static_cast<vtkIdType>(out.tetrahedronlist[i * corners + 1]);
        ids[2] = static_cast<vtkIdType>(out.tetrahedronlist[i * corners + 2]);
        ids[3] = static_cast<vtkIdType>(out.tetrahedronlist[i * corners + 3]);
        output->InsertNextCell(VTK_TETRA, 4, ids);
    }
    output->Squeeze();
    return true;
}

//------------------------------------------------------------------------------
static void deallocateTetgenioFacets(tetgenio& in)
{
    if (in.facetlist)
    {
        for (int i = 0; i < in.numberoffacets; ++i)
        {
            tetgenio::facet& f = in.facetlist[i];
            if (f.polygonlist)
            {
                for (int j = 0; j < f.numberofpolygons; ++j)
                {
                    delete[] f.polygonlist[j].vertexlist;
                }
                delete[] f.polygonlist;
            }
        }
        delete[] in.facetlist;
        in.facetlist = nullptr;
    }
    if (in.pointlist)
    {
        delete[] in.pointlist;
        in.pointlist = nullptr;
    }
}

//------------------------------------------------------------------------------
static void deallocateTetgenioInput(tetgenio& in)
{
    deallocateTetgenioFacets(in);
    if (in.pointmtrlist)
    {
        delete[] in.pointmtrlist;
        in.pointmtrlist = nullptr;
    }
    in.numberofpointmtrs = 0;
}

//------------------------------------------------------------------------------
int vtkSHYXTetGen::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
    vtkUnstructuredGrid* output = vtkUnstructuredGrid::GetData(outputVector);

    if (!input || !output)
    {
        vtkErrorMacro("Missing input or output.");
        return 0;
    }

    if (input->GetNumberOfPoints() == 0 || input->GetNumberOfCells() == 0)
    {
        vtkErrorMacro("Input mesh is empty.");
        return 0;
    }

    tetgenio in;
    tetgenio out;
    if (!vtkToTetgenio(input, in))
    {
        vtkErrorMacro("Failed to convert input to TetGen format.");
        deallocateTetgenioInput(in);
        return 0;
    }

    std::vector<REAL> surfaceSizingMetrics;
    if (this->UseSurfaceDensitySizing)
    {
        if (!ComputeSurfaceVertexSizing(input, this->SurfaceSizingScale, surfaceSizingMetrics))
        {
            vtkErrorMacro("Failed to derive surface density sizing from input triangles.");
            deallocateTetgenioInput(in);
            return 0;
        }
        if (static_cast<int>(surfaceSizingMetrics.size()) != in.numberofpoints)
        {
            vtkErrorMacro("Surface sizing metric count does not match input points.");
            deallocateTetgenioInput(in);
            return 0;
        }
        in.numberofpointmtrs = 1;
        in.pointmtrlist = new REAL[in.numberofpoints];
        for (int i = 0; i < in.numberofpoints; ++i)
        {
            in.pointmtrlist[i] = surfaceSizingMetrics[static_cast<size_t>(i)];
        }
    }

    // Build TetGen command switches: p = PLC, Y = nobisect, q = quality, a = max volume, m = sizing
    // Advanced: C = docheck, D = CDT, D# = cdtrefine, T = epsilon
    std::vector<char> switches(256, '\0');
    char* sw = switches.data();
    int len = 0;
    len += std::snprintf(sw + len, 8, "p");

    double effectiveMaxVolume = 0.0;
    if (this->LimitMaxVolume && this->MaxVolume > 0.0)
    {
        effectiveMaxVolume = this->MaxVolume;
    }

    if (this->Nobisect)
    {
        len += std::snprintf(sw + len, 8, "Y");
    }
    if (this->UseCDT && !this->Nobisect)
    {
        len += std::snprintf(sw + len, 8, "D");      // cdt=1
        len += std::snprintf(sw + len, 16, "D%d", this->CDTRefine); // cdtrefine
    }
    // Enable quality mesh if either MaxRadiusEdgeRatio or MinDihedralAngle is set (> 0)
    // Surface density sizing (-m) also requires -q.
    const bool useQualityMesh = this->MaxRadiusEdgeRatio > 0.0 || this->MinDihedralAngle > 0.0 ||
        this->UseSurfaceDensitySizing;
    if (useQualityMesh)
    {
        double radiusRatio = 0.0;
        double dihedralAngle = 0.0;
        if (this->UseSurfaceDensitySizing && this->MaxRadiusEdgeRatio <= 0.0 &&
            this->MinDihedralAngle <= 0.0)
        {
            radiusRatio = 1.8;
        }
        else
        {
            radiusRatio = this->MaxRadiusEdgeRatio > 0.0 ?
                (this->MaxRadiusEdgeRatio >= 1.2 ? this->MaxRadiusEdgeRatio : 1.8) : 0.0;
            dihedralAngle = this->MinDihedralAngle > 0.0 ?
                (this->MinDihedralAngle > 0.0 && this->MinDihedralAngle < 90.0 ? this->MinDihedralAngle : 0.0) : 0.0;
        }
        len += std::snprintf(sw + len, 32, "q%g/%g",
            static_cast<double>(radiusRatio),
            static_cast<double>(dihedralAngle));
    }
    if (this->UseSurfaceDensitySizing)
    {
        len += std::snprintf(sw + len, 8, "m");
    }
    if (effectiveMaxVolume > 0.0)
    {
        len += std::snprintf(sw + len, 32, "a%g", effectiveMaxVolume);
    }
    if (this->DoCheck)
    {
        len += std::snprintf(sw + len, 8, "C");
    }
    if (this->Epsilon > 0.0 && this->Epsilon != 1e-8)
    {
        len += std::snprintf(sw + len, 32, "T%g", this->Epsilon);
    }
    len += std::snprintf(sw + len, 8, "Q"); // quiet

    tetrahedralize(sw, &in, &out, nullptr, nullptr);

    deallocateTetgenioInput(in);

    if (out.numberoftetrahedra == 0)
    {
        vtkErrorMacro("TetGen produced no tetrahedral cells. Check input mesh (must be closed and watertight).");
        return 0;
    }

    if (!tetgenioToVtk(out, output))
    {
        vtkErrorMacro("Failed to convert TetGen output to VTK.");
        return 0;
    }

    // Point-data probe (optional mask binarization) after volume meshing.
    if (this->ProbeInputPointData)
    {
        vtkPointData* const inPD = input->GetPointData();
        const bool hasPointArrays = inPD && inPD->GetNumberOfArrays() > 0;
        bool applyMask = this->MaskArrayEnabled;
        if (hasPointArrays || applyMask)
        {
            vtkNew<vtkPolyData> probeSource;
            if (applyMask)
            {
                vtkDataArray* const picked = this->GetInputArrayToProcess(0, inputVector);
                const char* arrayName = picked ? picked->GetName() : this->GetMaskArrayName();
                if (!arrayName || arrayName[0] == '\0')
                {
                    vtkWarningMacro("Mask array enabled but no array selected; probing without mask.");
                    applyMask = false;
                }
            }
            if (applyMask)
            {
                probeSource->DeepCopy(input);

                vtkDataArray* const picked = this->GetInputArrayToProcess(0, inputVector);
                const char* arrayName = picked ? picked->GetName() : this->GetMaskArrayName();
                if (!ApplyMaskArrayToSurface(probeSource, arrayName))
                {
                    vtkErrorMacro("Failed to apply mask array \"" << arrayName << "\".");
                    return 0;
                }
            }
            else
            {
                probeSource->CopyStructure(input);
                probeSource->GetPointData()->ShallowCopy(inPD);
            }

            probeSource->GetCellData()->Initialize();

            vtkNew<vtkProbeFilter> probe;
            probe->SetInputData(output);
            probe->SetSourceData(probeSource);
            probe->PassCellArraysOff();
            probe->Update();
            output->ShallowCopy(probe->GetOutput());
        }
    }

    return 1;
}
