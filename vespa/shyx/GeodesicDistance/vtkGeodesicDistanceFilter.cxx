#include "vtkGeodesicDistanceFilter.h"

#include "vtkCellArray.h"
#include "vtkDijkstraGraphGeodesicPath.h"
#include "vtkDoubleArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"

#include <limits>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkGeodesicDistanceFilter);

vtkGeodesicDistanceFilter::vtkGeodesicDistanceFilter()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
    this->SourceVertexId = 0;
}

vtkGeodesicDistanceFilter::~vtkGeodesicDistanceFilter() = default;

int vtkGeodesicDistanceFilter::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
        return 1;
    }
    return 0;
}

int vtkGeodesicDistanceFilter::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

    if (!input)
    {
        vtkErrorMacro("No input!");
        return 0;
    }

    vtkIdType numPoints = input->GetNumberOfPoints();
    if (numPoints == 0)
    {
        vtkErrorMacro("Input has no points!");
        return 0;
    }

    if (this->SourceVertexId < 0 || this->SourceVertexId >= numPoints)
    {
        vtkErrorMacro("SourceVertexId " << this->SourceVertexId
            << " is out of range [0, " << numPoints - 1 << "].");
        return 0;
    }

    if (input->GetNumberOfPolys() == 0)
    {
        vtkErrorMacro("Input has no polygonal cells. A triangulated surface is required.");
        return 0;
    }

    output->ShallowCopy(input);

    vtkNew<vtkDijkstraGraphGeodesicPath> dijkstra;
    dijkstra->SetInputData(input);
    dijkstra->SetStartVertex(this->SourceVertexId);
    dijkstra->SetEndVertex(this->SourceVertexId == 0 ? 1 : 0);
    dijkstra->StopWhenEndReachedOff();
    dijkstra->Update();

    vtkNew<vtkDoubleArray> cumulativeWeights;
    dijkstra->GetCumulativeWeights(cumulativeWeights);

    vtkNew<vtkDoubleArray> distArray;
    distArray->SetName("GeodesicDistance");
    distArray->SetNumberOfComponents(1);
    distArray->SetNumberOfTuples(numPoints);

    vtkIdType numWeights = cumulativeWeights->GetNumberOfTuples();
    for (vtkIdType i = 0; i < numPoints; i++)
    {
        if (i < numWeights)
        {
            double w = cumulativeWeights->GetValue(i);
            if (w >= std::numeric_limits<double>::max() * 0.5)
                distArray->SetValue(i, -1.0);
            else
                distArray->SetValue(i, w);
        }
        else
            distArray->SetValue(i, -1.0);
    }

    output->GetPointData()->AddArray(distArray);
    output->GetPointData()->SetActiveScalars("GeodesicDistance");

    return 1;
}

void vtkGeodesicDistanceFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "SourceVertexId: " << this->SourceVertexId << "\n";
}

VTK_ABI_NAMESPACE_END
