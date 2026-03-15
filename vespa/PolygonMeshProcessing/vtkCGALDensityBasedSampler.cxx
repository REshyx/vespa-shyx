#include "vtkCGALDensityBasedSampler.h"

#include <vtkCellArray.h>
#include <vtkCellLocator.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkGenericCell.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMinimalStandardRandomSequence.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

vtkStandardNewMacro(vtkCGALDensityBasedSampler);

//------------------------------------------------------------------------------
vtkCGALDensityBasedSampler::vtkCGALDensityBasedSampler()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkCGALDensityBasedSampler::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkCGALDensityBasedSampler::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "PreSampleCount: " << this->PreSampleCount << std::endl;
    os << indent << "DensityArrayName: " << this->DensityArrayName << std::endl;
    os << indent << "Seed: " << this->Seed << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
namespace
{

double InterpolateScalarAtPoint(
    vtkCellLocator* locator,
    vtkDataSet* dataset,
    vtkDataArray* arr,
    const double pt[3])
{
    double closestPoint[3];
    vtkIdType cellId = -1;
    int subId = 0;
    double dist2 = 0.0;

    locator->FindClosestPoint(pt, closestPoint, cellId, subId, dist2);

    if (cellId < 0)
    {
        return 0.0;
    }

    vtkCell* cell = dataset->GetCell(cellId);
    if (!cell)
    {
        return 0.0;
    }

    int nPts = cell->GetNumberOfPoints();
    double pcoords[3] = { 0.0, 0.0, 0.0 };
    std::vector<double> weights(static_cast<size_t>(nPts), 0.0);

    int inside = cell->EvaluatePosition(
        pt, closestPoint, subId, pcoords, dist2, weights.data());

    if (inside < 0)
    {
        return 0.0;
    }

    double value = 0.0;
    for (int i = 0; i < nPts; ++i)
    {
        vtkIdType ptId = cell->GetPointId(i);
        value += weights[i] * arr->GetTuple1(ptId);
    }
    return value;
}

bool IsUniformSelection(const std::string& name)
{
    return name.empty() || name == "(Uniform)";
}

} // anonymous namespace

//------------------------------------------------------------------------------
int vtkCGALDensityBasedSampler::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
    vtkPolyData* output = vtkPolyData::GetData(outputVector);

    if (!input || input->GetNumberOfPoints() == 0)
    {
        vtkErrorMacro("Input is empty or null.");
        return 0;
    }

    if (input->GetNumberOfCells() == 0)
    {
        vtkErrorMacro("Input has no cells.");
        return 0;
    }

    // --- Determine whether we have a volume or surface mesh ------------------
    bool isVolumeMesh = (vtkUnstructuredGrid::SafeDownCast(input) != nullptr);

    vtkSmartPointer<vtkPolyData> surface;
    if (isVolumeMesh)
    {
        vtkNew<vtkDataSetSurfaceFilter> surfaceFilter;
        surfaceFilter->SetInputData(input);
        surfaceFilter->Update();
        surface = surfaceFilter->GetOutput();
        if (!surface || surface->GetNumberOfCells() == 0)
        {
            vtkErrorMacro("Failed to extract surface from volume mesh.");
            return 0;
        }
    }
    else
    {
        surface = vtkPolyData::SafeDownCast(input);
        if (!surface)
        {
            vtkErrorMacro("Input is not a supported dataset type "
                          "(expected vtkPolyData or vtkUnstructuredGrid).");
            return 0;
        }
    }

    // --- Resolve the density array -------------------------------------------
    vtkDataArray* densityArray = nullptr;
    bool uniformMode = IsUniformSelection(this->DensityArrayName);

    if (!uniformMode)
    {
        densityArray = input->GetPointData()->GetArray(this->DensityArrayName.c_str());
        if (!densityArray)
        {
            vtkWarningMacro(
                "Density array '" << this->DensityArrayName
                << "' not found on input. Falling back to uniform sampling.");
            uniformMode = true;
        }
    }

    double scalarRange[2] = { 0.0, 1.0 };
    if (!uniformMode && densityArray)
    {
        densityArray->GetRange(scalarRange);
    }

    // --- Build spatial structures --------------------------------------------
    vtkNew<vtkImplicitPolyDataDistance> implicitDist;
    implicitDist->SetInput(surface);

    vtkNew<vtkCellLocator> locator;
    locator->SetDataSet(input);
    locator->BuildLocator();

    // --- Bounding box --------------------------------------------------------
    double bounds[6];
    input->GetBounds(bounds);
    double dx = bounds[1] - bounds[0];
    double dy = bounds[3] - bounds[2];
    double dz = bounds[5] - bounds[4];

    if (dx < 1e-12 || dy < 1e-12 || dz < 1e-12)
    {
        vtkErrorMacro("Degenerate bounding box.");
        return 0;
    }

    // --- Cartesian grid: pre-sample points by aspect ratio -------------------
    vtkIdType preSample = static_cast<vtkIdType>(this->PreSampleCount);

    double vol = dx * dy * dz;
    double scale = std::cbrt(static_cast<double>(preSample) / vol);
    int nx = std::max(1, static_cast<int>(scale * dx));
    int ny = std::max(1, static_cast<int>(scale * dy));
    int nz = std::max(1, static_cast<int>(scale * dz));

    double sx = (nx > 1) ? dx / (nx - 1) : 0.0;
    double sy = (ny > 1) ? dy / (ny - 1) : 0.0;
    double sz = (nz > 1) ? dz / (nz - 1) : 0.0;

    // --- Collect interior points ---------------------------------------------
    std::vector<std::array<double, 3>> interiorPts;
    interiorPts.reserve(std::min(preSample, vtkIdType(nx) * ny * nz));

    for (int k = 0; k < nz; ++k)
    {
        double pz = bounds[4] + (nz > 1 ? k * sz : dz * 0.5);
        for (int j = 0; j < ny; ++j)
        {
            double py = bounds[2] + (ny > 1 ? j * sy : dy * 0.5);
            for (int i = 0; i < nx; ++i)
            {
                double px = bounds[0] + (nx > 1 ? i * sx : dx * 0.5);
                double pt[3] = { px, py, pz };
                if (implicitDist->EvaluateFunction(pt) <= 0.0)
                {
                    interiorPts.push_back({ { pt[0], pt[1], pt[2] } });
                }
            }
        }
    }

    if (interiorPts.empty())
    {
        vtkWarningMacro("No interior points found. Check that the input mesh is closed.");
        return 1;
    }

    // --- Random number generator (for density probability) -------------------
    vtkNew<vtkMinimalStandardRandomSequence> rng;
    rng->SetSeed(this->Seed);

    auto nextRand = [&]() -> double
    {
        rng->Next();
        return rng->GetValue();
    };

    // --- Output: uniform = all interior; density = probability filter ---------
    vtkNew<vtkPoints> outputPoints;
    outputPoints->SetDataTypeToDouble();

    vtkNew<vtkCellArray> outputVerts;

    if (uniformMode)
    {
        for (const auto& pt : interiorPts)
        {
            vtkIdType pid = outputPoints->InsertNextPoint(pt.data());
            outputVerts->InsertNextCell(1, &pid);
        }
    }
    else
    {
        double rangeSpan = scalarRange[1] - scalarRange[0];
        for (const auto& ptArr : interiorPts)
        {
            const double* pt = ptArr.data();
            double rawValue = InterpolateScalarAtPoint(locator, input, densityArray, pt);
            // Automatic linear map: value range -> [0, 100%]
            double density =
                (rangeSpan > 1e-15) ? (rawValue - scalarRange[0]) / rangeSpan : 0.5;
            density = std::max(0.0, std::min(1.0, density));

            if (nextRand() < density)
            {
                vtkIdType pid = outputPoints->InsertNextPoint(pt);
                outputVerts->InsertNextCell(1, &pid);
            }
        }
    }

    output->SetPoints(outputPoints);
    output->SetVerts(outputVerts);

    return 1;
}
