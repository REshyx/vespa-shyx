#include "vtkSHYXEnhancedRuler.h"

#include "vtkCellArray.h"
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

} // namespace

vtkSHYXEnhancedRuler::vtkSHYXEnhancedRuler()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
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
    if (surface && surface->GetNumberOfPolys() > 0 && id1 != id2)
    {
        vtkNew<vtkTriangleFilter> triangles;
        triangles->SetInputData(surface);
        triangles->Update();
        vtkPolyData* triMesh = triangles->GetOutput();
        const vtkIdType nTriPts = triMesh->GetNumberOfPoints();
        if (id1 >= 0 && id1 < nTriPts && id2 >= 0 && id2 < nTriPts)
        {
            vtkNew<vtkDijkstraGraphGeodesicPath> dijkstra;
            dijkstra->SetInputData(triMesh);
            dijkstra->SetStartVertex(id1);
            dijkstra->SetEndVertex(id2);
            dijkstra->StopWhenEndReachedOn();
            dijkstra->Update();

            pathOut->ShallowCopy(dijkstra->GetOutput());

            vtkNew<vtkDoubleArray> weights;
            dijkstra->GetCumulativeWeights(weights);
            if (id2 < weights->GetNumberOfTuples())
            {
                const double w = weights->GetValue(id2);
                if (w < std::numeric_limits<double>::max() * 0.5)
                {
                    this->GeodesicDistance = w;
                }
            }
            if (this->GeodesicDistance < 0.0 && pathOut->GetNumberOfPoints() >= 2)
            {
                double pathLen = 0.0;
                vtkPoints* pathPts = pathOut->GetPoints();
                for (vtkIdType i = 1; i < pathOut->GetNumberOfPoints(); ++i)
                {
                    double a[3], b[3];
                    pathPts->GetPoint(i - 1, a);
                    pathPts->GetPoint(i, b);
                    pathLen += std::sqrt(vtkMath::Distance2BetweenPoints(a, b));
                }
                this->GeodesicDistance = pathLen;
            }
        }
    }

    WriteGeodesicFieldData(pathOut, this->GeodesicDistance);

    return 1;
}

VTK_ABI_NAMESPACE_END
