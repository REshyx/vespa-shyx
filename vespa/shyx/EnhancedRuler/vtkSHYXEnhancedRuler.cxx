#include "vtkSHYXEnhancedRuler.h"

#include "vtkCellArray.h"
#include "vtkCellType.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDijkstraGraphGeodesicPath.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkGeometryFilter.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointSet.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkTriangleFilter.h"

#include <algorithm>

#include <limits>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXEnhancedRuler);

namespace
{
vtkIdType NearestPointId(vtkPoints* pts, const double pos[3])
{
    if (!pts)
    {
        return 0;
    }
    const vtkIdType n = pts->GetNumberOfPoints();
    if (n <= 0)
    {
        return 0;
    }
    vtkIdType best = 0;
    double bestD2 = VTK_DOUBLE_MAX;
    for (vtkIdType i = 0; i < n; ++i)
    {
        double q[3];
        pts->GetPoint(i, q);
        const double d2 = vtkMath::Distance2BetweenPoints(pos, q);
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = i;
        }
    }
    return best;
}

void SnapPoint(vtkPoints* pts, const double pos[3], double snapped[3], vtkIdType& vertexId)
{
    vertexId = NearestPointId(pts, pos);
    pts->GetPoint(vertexId, snapped);
}

vtkPolyData* ExtractSurfacePolyData(vtkDataSet* input, vtkPolyData* cache)
{
    if (!input)
    {
        return nullptr;
    }
    if (auto* pd = vtkPolyData::SafeDownCast(input))
    {
        return pd;
    }
    vtkNew<vtkGeometryFilter> geometry;
    geometry->SetInputData(input);
    geometry->Update();
    cache->ShallowCopy(geometry->GetOutput());
    return cache;
}

void WriteGeodesicFieldData(vtkDataObject* obj, double geodesic)
{
    if (!obj)
    {
        return;
    }
    vtkFieldData* fd = obj->GetFieldData();
    vtkNew<vtkDoubleArray> geoArr;
    geoArr->SetName("GeodesicDistance");
    geoArr->SetNumberOfComponents(1);
    geoArr->SetNumberOfTuples(1);
    geoArr->SetValue(0, geodesic);
    fd->AddArray(geoArr);
}

double PathLength(vtkPolyData* path)
{
    if (!path || path->GetNumberOfPoints() < 2)
    {
        return -1.0;
    }
    double len = 0.0;
    vtkPoints* pts = path->GetPoints();
    for (vtkIdType i = 1; i < path->GetNumberOfPoints(); ++i)
    {
        double a[3], b[3];
        pts->GetPoint(i - 1, a);
        pts->GetPoint(i, b);
        len += std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
    }
    return len;
}

double DijkstraDistance(vtkPolyData* graph, vtkIdType startId, vtkIdType endId, vtkPolyData* pathOut)
{
    if (!graph || startId < 0 || endId < 0 || startId == endId)
    {
        return -1.0;
    }
    const vtkIdType nPts = graph->GetNumberOfPoints();
    if (startId >= nPts || endId >= nPts)
    {
        return -1.0;
    }

    vtkNew<vtkDijkstraGraphGeodesicPath> dijkstra;
    dijkstra->SetInputData(graph);
    dijkstra->SetStartVertex(startId);
    dijkstra->SetEndVertex(endId);
    dijkstra->StopWhenEndReachedOn();
    dijkstra->Update();

    vtkPolyData* path = dijkstra->GetOutput();
    if (pathOut)
    {
        pathOut->ShallowCopy(path);
    }

    vtkNew<vtkDoubleArray> weights;
    dijkstra->GetCumulativeWeights(weights);
    if (endId < weights->GetNumberOfTuples())
    {
        const double w = weights->GetValue(endId);
        if (w < std::numeric_limits<double>::max() * 0.5)
        {
            return w;
        }
    }
    return PathLength(path);
}

/** vtkDijkstraGraphGeodesicPath only handles VTK_LINE, not VTK_POLY_LINE — expand poly-lines. */
void BuildLineGraphPolyData(vtkPolyData* input, vtkPolyData* output)
{
    output->Initialize();
    if (!input || !input->GetPoints())
    {
        return;
    }
    output->SetPoints(input->GetPoints());

    vtkNew<vtkCellArray> lines;
    const vtkIdType ncells = input->GetNumberOfCells();
    for (vtkIdType i = 0; i < ncells; ++i)
    {
        const int ctype = input->GetCellType(i);
        const vtkIdType* pts = nullptr;
        vtkIdType npts = 0;
        input->GetCellPoints(i, npts, pts);
        if (!pts || npts < 2)
        {
            continue;
        }
        if (ctype == VTK_LINE)
        {
            lines->InsertNextCell(2, pts);
        }
        else if (ctype == VTK_POLY_LINE)
        {
            for (vtkIdType j = 0; j < npts - 1; ++j)
            {
                vtkIdType edge[2] = { pts[j], pts[j + 1] };
                lines->InsertNextCell(2, edge);
            }
        }
    }
    output->SetLines(lines);
}

bool HasPathTopology(vtkPolyData* pd)
{
    return pd && (pd->GetNumberOfPolys() > 0 || pd->GetNumberOfLines() > 0);
}

} // namespace

vtkSHYXEnhancedRuler::vtkSHYXEnhancedRuler()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

double vtkSHYXEnhancedRuler::ComputePathDistance(
    vtkPolyData* topology, vtkIdType startId, vtkIdType endId, vtkPolyData* pathOut)
{
    if (!topology || startId < 0 || endId < 0 || startId == endId)
    {
        return -1.0;
    }

    if (topology->GetNumberOfPolys() > 0)
    {
        vtkNew<vtkTriangleFilter> triangles;
        triangles->SetInputData(topology);
        triangles->Update();
        return DijkstraDistance(triangles->GetOutput(), startId, endId, pathOut);
    }

    if (topology->GetNumberOfLines() > 0)
    {
        vtkNew<vtkPolyData> lineGraph;
        BuildLineGraphPolyData(topology, lineGraph);
        if (lineGraph->GetNumberOfCells() == 0)
        {
            return -1.0;
        }
        return DijkstraDistance(lineGraph, startId, endId, pathOut);
    }

    return -1.0;
}

void vtkSHYXEnhancedRuler::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "Point1: (" << this->Point1[0] << ", " << this->Point1[1] << ", "
       << this->Point1[2] << ")\n";
    os << indent << "Point2: (" << this->Point2[0] << ", " << this->Point2[1] << ", "
       << this->Point2[2] << ")\n";
    os << indent << "Point1VertexId: " << this->Point1VertexId << "\n";
    os << indent << "Point2VertexId: " << this->Point2VertexId << "\n";
    os << indent << "SnapToInputVertices: " << (this->SnapToInputVertices ? "On" : "Off") << "\n";
    os << indent << "GeodesicDistance: " << this->GeodesicDistance << "\n";
}

int vtkSHYXEnhancedRuler::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

int vtkSHYXEnhancedRuler::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

int vtkSHYXEnhancedRuler::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* pathOut = vtkPolyData::GetData(outputVector, 0);
    if (!pathOut)
    {
        vtkErrorMacro(<< "Missing output.");
        return 0;
    }

    pathOut->Initialize();
    this->GeodesicDistance = -1.0;

    vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
    if (!input)
    {
        return 1;
    }

    vtkPointSet* pointSet = vtkPointSet::SafeDownCast(input);
    if (!pointSet || !pointSet->GetPoints() || pointSet->GetNumberOfPoints() == 0)
    {
        return 1;
    }

    vtkPoints* inputPts = pointSet->GetPoints();
    const vtkIdType nPts = inputPts->GetNumberOfPoints();

    if (this->Point2VertexId < 0 || this->Point1VertexId < 0)
    {
        this->Point1VertexId = 0;
        this->Point2VertexId = std::max(vtkIdType(0), nPts - 1);
        inputPts->GetPoint(this->Point1VertexId, this->Point1);
        inputPts->GetPoint(this->Point2VertexId, this->Point2);
    }

    double p1[3] = { this->Point1[0], this->Point1[1], this->Point1[2] };
    double p2[3] = { this->Point2[0], this->Point2[1], this->Point2[2] };
    vtkIdType id1 = this->Point1VertexId;
    vtkIdType id2 = this->Point2VertexId;

    const auto idsInRange = [&](vtkIdType a, vtkIdType b) {
        return a >= 0 && b >= 0 && a < nPts && b < nPts;
    };

    if (idsInRange(id1, id2))
    {
        // Trust vertex ids from the panel / interactive widget (Apply pushes these).
        inputPts->GetPoint(id1, p1);
        inputPts->GetPoint(id2, p2);
    }
    else if (this->SnapToInputVertices)
    {
        SnapPoint(inputPts, p1, p1, id1);
        SnapPoint(inputPts, p2, p2, id2);
    }
    else
    {
        if (id1 < 0 || id1 >= nPts)
        {
            id1 = NearestPointId(inputPts, p1);
        }
        if (id2 < 0 || id2 >= nPts)
        {
            id2 = NearestPointId(inputPts, p2);
        }
        inputPts->GetPoint(id1, p1);
        inputPts->GetPoint(id2, p2);
    }

    this->Point1VertexId = id1;
    this->Point2VertexId = id2;
    this->Point1[0] = p1[0];
    this->Point1[1] = p1[1];
    this->Point1[2] = p1[2];
    this->Point2[0] = p2[0];
    this->Point2[1] = p2[1];
    this->Point2[2] = p2[2];

    this->GeodesicDistance = -1.0;

    vtkNew<vtkPolyData> surfaceCache;
    vtkPolyData* surface = ExtractSurfacePolyData(input, surfaceCache);
    if (surface && HasPathTopology(surface) && id1 != id2)
    {
        this->GeodesicDistance =
            vtkSHYXEnhancedRuler::ComputePathDistance(surface, id1, id2, pathOut);
    }

    WriteGeodesicFieldData(pathOut, this->GeodesicDistance);

    return 1;
}

VTK_ABI_NAMESPACE_END
