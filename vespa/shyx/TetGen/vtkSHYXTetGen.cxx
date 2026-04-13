#include "vtkSHYXTetGen.h"

#include <vtkCell.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkUnstructuredGrid.h>

#define TETLIBRARY
#include "tetgen.h"

#include <cstdio>
#include <cstring>
#include <vector>

vtkStandardNewMacro(vtkSHYXTetGen);

//------------------------------------------------------------------------------
vtkSHYXTetGen::vtkSHYXTetGen()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
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
    os << indent << "MaxVolume: " << this->MaxVolume << std::endl;
    os << indent << "MaxRadiusEdgeRatio: " << this->MaxRadiusEdgeRatio << std::endl;
    os << indent << "MinDihedralAngle: " << this->MinDihedralAngle << std::endl;
    os << indent << "Nobisect: " << (this->Nobisect ? "ON" : "OFF") << std::endl;
    os << indent << "DoCheck: " << (this->DoCheck ? "ON" : "OFF") << std::endl;
    os << indent << "UseCDT: " << (this->UseCDT ? "ON" : "OFF") << std::endl;
    os << indent << "CDTRefine: " << this->CDTRefine << std::endl;
    os << indent << "Epsilon: " << this->Epsilon << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

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
        deallocateTetgenioFacets(in);
        return 0;
    }

    // Build TetGen command switches: p = PLC, Y = nobisect, q = quality, a = max volume
    // Advanced: C = docheck, D = CDT, D# = cdtrefine, T = epsilon
    std::vector<char> switches(256, '\0');
    char* sw = switches.data();
    int len = 0;
    len += std::snprintf(sw + len, 8, "p");
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
    // 0 value means no limit for that specific parameter. MaxRadiusEdgeRatio must be >= 1.2 when > 0
    if (this->MaxRadiusEdgeRatio > 0.0 || this->MinDihedralAngle > 0.0)
    {
        // Ensure valid ranges: MaxRadiusEdgeRatio >= 1.2 when > 0, MinDihedralAngle > 0 and < 90 when > 0
        // If MaxRadiusEdgeRatio is 0, use 0 (no limit on radius ratio)
        // If MinDihedralAngle is 0, use 0 (no limit on dihedral angle)
        double radiusRatio = this->MaxRadiusEdgeRatio > 0.0 ? 
            (this->MaxRadiusEdgeRatio >= 1.2 ? this->MaxRadiusEdgeRatio : 1.8) : 0.0;
        double dihedralAngle = this->MinDihedralAngle > 0.0 ? 
            (this->MinDihedralAngle > 0.0 && this->MinDihedralAngle < 90.0 ? this->MinDihedralAngle : 0.0) : 0.0;
        len += std::snprintf(sw + len, 32, "q%g/%g",
            static_cast<double>(radiusRatio),
            static_cast<double>(dihedralAngle));
    }
    if (this->MaxVolume > 0.0)
    {
        len += std::snprintf(sw + len, 32, "a%g", this->MaxVolume);
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

    deallocateTetgenioFacets(in);

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

    return 1;
}
