/**
 * @class   vtkGeodesicDistanceFilter
 * @brief   Compute geodesic (shortest surface) distance from all vertices to a source vertex
 *
 * vtkGeodesicDistanceFilter computes the shortest path distance along the
 * surface mesh from every vertex to a user-specified source vertex, using
 * Dijkstra's algorithm (via vtkDijkstraGraphGeodesicPath).
 *
 * @sa
 * vtkDijkstraGraphGeodesicPath
 */

#ifndef vtkGeodesicDistanceFilter_h
#define vtkGeodesicDistanceFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkGeodesicDistanceFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKGEODESICDISTANCEFILTER_EXPORT vtkGeodesicDistanceFilter : public vtkPolyDataAlgorithm
{
public:
    static vtkGeodesicDistanceFilter* New();
    vtkTypeMacro(vtkGeodesicDistanceFilter, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkSetMacro(SourceVertexId, vtkIdType);
    vtkGetMacro(SourceVertexId, vtkIdType);

protected:
    vtkGeodesicDistanceFilter();
    ~vtkGeodesicDistanceFilter() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    vtkIdType SourceVertexId;

private:
    vtkGeodesicDistanceFilter(const vtkGeodesicDistanceFilter&) = delete;
    void operator=(const vtkGeodesicDistanceFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
