#include "vtkSHYXTetGenMeshOptimize.h"

#include <vtkCell.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#define TETLIBRARY
#include "tetgen.h"

#include <cstdio>
#include <cstring>

vtkStandardNewMacro(vtkSHYXTetGenMeshOptimize);

namespace
{

bool vtkUnstructuredTetraMeshToTetgenio(vtkUnstructuredGrid* ug, tetgenio& in)
{
    vtkPoints* points = ug->GetPoints();
    if (!points || points->GetNumberOfPoints() < 4)
    {
        return false;
    }

    vtkIdType numTets = 0;
    for (vtkIdType ci = 0; ci < ug->GetNumberOfCells(); ++ci)
    {
        vtkCell* cell = ug->GetCell(ci);
        if (cell && cell->GetCellType() == VTK_TETRA && cell->GetNumberOfPoints() == 4)
        {
            ++numTets;
        }
    }
    if (numTets == 0)
    {
        return false;
    }

    vtkIdType numPoints = points->GetNumberOfPoints();
    in.firstnumber = 0;
    in.mesh_dim = 3;
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

    in.numberofcorners = 4;
    in.numberoftetrahedra = static_cast<int>(numTets);
    in.tetrahedronlist = new int[numTets * 4];
    in.numberoftetrahedronattributes = 0;
    in.tetrahedronattributelist = nullptr;
    in.tetrahedronvolumelist = nullptr;

    int ti = 0;
    for (vtkIdType ci = 0; ci < ug->GetNumberOfCells(); ++ci)
    {
        vtkCell* cell = ug->GetCell(ci);
        if (!cell || cell->GetCellType() != VTK_TETRA || cell->GetNumberOfPoints() != 4)
        {
            continue;
        }
        const int base = ti * 4;
        for (int k = 0; k < 4; ++k)
        {
            in.tetrahedronlist[base + k] = static_cast<int>(cell->GetPointId(k));
        }
        ++ti;
    }
    return true;
}

bool tetgenioToVtk(tetgenio& out, vtkUnstructuredGrid* output)
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

    const int corners = (out.numberofcorners > 0) ? out.numberofcorners : 4;
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

} // namespace

//------------------------------------------------------------------------------
vtkSHYXTetGenMeshOptimize::vtkSHYXTetGenMeshOptimize()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkSHYXTetGenMeshOptimize::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXTetGenMeshOptimize::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkUnstructuredGrid");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXTetGenMeshOptimize::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "OptMaxFlipLevel: " << this->OptMaxFlipLevel << std::endl;
    os << indent << "OptScheme: " << this->OptScheme << std::endl;
    os << indent << "OptMaxAspectRatio: " << this->OptMaxAspectRatio << std::endl;
    os << indent << "OptIterations: " << this->OptIterations << std::endl;
    os << indent << "DoCheck: " << (this->DoCheck ? "ON" : "OFF") << std::endl;
    os << indent << "NoJettison: " << (this->NoJettison ? "ON" : "OFF") << std::endl;
    os << indent << "Epsilon: " << this->Epsilon << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXTetGenMeshOptimize::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkUnstructuredGrid* input = vtkUnstructuredGrid::GetData(inputVector[0]);
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
    if (!vtkUnstructuredTetraMeshToTetgenio(input, in))
    {
        vtkErrorMacro("Input must contain at least one VTK_TETRA cell.");
        return 0;
    }

    tetgenio out;

    // -r reconstruct; -q enables improve_mesh; -S0 forbids Steiner insertion in refinement;
    // -Y skips boundary splits that add vertices; -J keeps unused input nodes if any.
    std::vector<char> switches(512, '\0');
    char* sw = switches.data();
    int len = 0;
    len += std::snprintf(sw + len, 32, "rqS0Y");
    if (this->NoJettison)
    {
        len += std::snprintf(sw + len, 8, "J");
    }
    len += std::snprintf(sw + len, 64, "O%d/%d/%d",
        this->OptMaxFlipLevel,
        this->OptScheme,
        this->OptIterations);
    // TetGen default opt_max_asp_ratio is 1000, so almost no tet is queued for improvement.
    // o//R sets -o//R (skip when ~1000 to restore library default).
    if (this->OptMaxAspectRatio < 999.5)
    {
        len += std::snprintf(sw + len, 48, "o//%g", this->OptMaxAspectRatio);
    }
    if (this->DoCheck)
    {
        len += std::snprintf(sw + len, 8, "C");
    }
    if (this->Epsilon > 0.0 && this->Epsilon != 1e-8)
    {
        len += std::snprintf(sw + len, 32, "T%g", this->Epsilon);
    }
    len += std::snprintf(sw + len, 8, "Q");

    tetrahedralize(sw, &in, &out, nullptr, nullptr);

    if (out.numberoftetrahedra == 0)
    {
        vtkErrorMacro("TetGen produced no tetrahedral cells.");
        return 0;
    }

    if (!tetgenioToVtk(out, output))
    {
        vtkErrorMacro("Failed to convert TetGen output to VTK.");
        return 0;
    }

    return 1;
}
