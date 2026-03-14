/**
 * @class   vtkSurfaceTipExtractor
 * @brief   Compute a sharpness score for each surface vertex
 *
 * vtkSurfaceTipExtractor identifies tip / sharp points on a polygonal
 * surface by computing, for every vertex, the distance between the vertex
 * and the centroid of its geodesically-connected neighbourhood within a
 * user-specified search radius.
 *
 * A large centroid-offset indicates the point is at a sharp tip (e.g. a
 * tree crown apex), while a small value indicates a flat / smooth region.
 *
 * The output is a copy of the input polydata with an additional point data
 * array "TipScore" containing the computed sharpness values.
 */

#ifndef vtkSurfaceTipExtractor_h
#define vtkSurfaceTipExtractor_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSurfaceTipExtractorModule.h" // for export macro

VTK_ABI_NAMESPACE_BEGIN

class VTKSURFACETIPEXTRACTOR_EXPORT vtkSurfaceTipExtractor : public vtkPolyDataAlgorithm
{
public:
    static vtkSurfaceTipExtractor* New();
    vtkTypeMacro(vtkSurfaceTipExtractor, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    ///@{
    /**
     * Set/Get the search radius used to define each vertex's local
     * neighbourhood. Default is 5.0.
     */
    vtkSetMacro(SearchRadius, double);
    vtkGetMacro(SearchRadius, double);
    ///@}

protected:
    vtkSurfaceTipExtractor();
    ~vtkSurfaceTipExtractor() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    double SearchRadius;

private:
    vtkSurfaceTipExtractor(const vtkSurfaceTipExtractor&) = delete;
    void operator=(const vtkSurfaceTipExtractor&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
