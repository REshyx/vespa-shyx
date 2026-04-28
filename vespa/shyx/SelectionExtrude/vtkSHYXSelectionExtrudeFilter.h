/**
 * @class   vtkSHYXSelectionExtrudeFilter
 * @brief   Extrude a selected patch of a triangulated surface along a single average normal or
 *          per-vertex normals on the cap.
 *
 * Input port 0 is the full surface (vtkPolyData, triangles). Input port 1 is optional and follows
 * the same convention as vtkCGALRegionFairing: a vtkSelection wired through ParaView's selection
 * port (SelectionInput). The filter runs vtkExtractSelection internally and uses
 * vtkOriginalCellIds on extracted cells, or falls back to vtkOriginalPointIds (all incident
 * triangles). Alternatively set SelectionCellArrayName on port 0 when no selection is provided.
 *
 * Output keeps unmodified unselected cells, drops the selected triangles from their original
 * positions, adds offset copies of those triangles (top cap), and side quads along the selection
 * boundary (open shell). Field data "SHYX_SelectionExtrude_AvgNormal" stores the unit
 * area-weighted average face normal of the selection (always computed). When AverageNormals is
 * true (default), extrusion uses that single direction; when false, each top-cap vertex moves
 * along its own point normal (area-weighted average of incident *selected* triangle normals).
 */

#ifndef vtkSHYXSelectionExtrudeFilter_h
#define vtkSHYXSelectionExtrudeFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXSelectionExtrudeFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXSELECTIONEXTRUDEFILTER_EXPORT vtkSHYXSelectionExtrudeFilter : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXSelectionExtrudeFilter* New();
    vtkTypeMacro(vtkSHYXSelectionExtrudeFilter, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /**
     * Selection input (port 1): vtkSelection, same pattern as vtkCGALRegionFairing / VESPA Region Fairing.
     */
    void SetSourceConnection(vtkAlgorithmOutput* algOutput);

    vtkSetMacro(ExtrusionDistance, double);
    vtkGetMacro(ExtrusionDistance, double);

    vtkSetMacro(FlipExtrusionDirection, int);
    vtkGetMacro(FlipExtrusionDirection, int);
    vtkBooleanMacro(FlipExtrusionDirection, int);

    /**
     * If true (default), use one area-weighted average normal for the whole selection. If false,
     * each vertex on the top cap is displaced along its own point normal (from selected triangles
     * incident to that vertex).
     */
    vtkSetMacro(AverageNormals, int);
    vtkGetMacro(AverageNormals, int);
    vtkBooleanMacro(AverageNormals, int);

    /**
     * When no vtkSelection on port 1, optional name of a cell data array on port 0. A cell is
     * selected if the first component is > 0.5 (scalar) or != 0 (integral).
     */
    vtkSetStringMacro(SelectionCellArrayName);
    vtkGetStringMacro(SelectionCellArrayName);

    /** Last computed unit average normal (valid after RequestData). */
    vtkGetVector3Macro(LastAverageNormal, double);

protected:
    vtkSHYXSelectionExtrudeFilter();
    ~vtkSHYXSelectionExtrudeFilter() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;

    double ExtrusionDistance = 0.0;
    int FlipExtrusionDirection = 0;
    int AverageNormals = 1;
    char* SelectionCellArrayName = nullptr;
    double LastAverageNormal[3];

private:
    vtkSHYXSelectionExtrudeFilter(const vtkSHYXSelectionExtrudeFilter&) = delete;
    void operator=(const vtkSHYXSelectionExtrudeFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
