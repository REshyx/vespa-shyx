/**
 * @class   vtkSHYXSelectionOBBSubtractFilter
 * @brief   Build an OBB from the active selection (or a cell array), optionally transform it,
 *          subtract its volume from a closed surface (CGAL difference), and output the transformed
 *          OBB on a second port. When OBB field data is present, Position/Rotation/Scale are
 *          Interactive-Box *absolute* parameters (same composition as vtkPVTransform on the box widget);
 *          the mesh transform is M(current)*M(baseline)^{-1} so edits are relative to the fitted pose.
 *
 * Port 0: closed triangulated vtkPolyData. Port 1: optional vtkSelection (ParaView Selection input
 * with SelectionInput hint; use Copy Active Selection in the GUI when UseSelectionInput is on).
 * Alternatively set SelectionCellArrayName on port 0. The OBB is computed from all points that
 * belong to the selected region (extracted geometry, or vertices of cells flagged by the array).
 */

#ifndef vtkSHYXSelectionOBBSubtractFilter_h
#define vtkSHYXSelectionOBBSubtractFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXSelectionOBBSubtractFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXSELECTIONOBBSUBTRACTFILTER_EXPORT vtkSHYXSelectionOBBSubtractFilter : public vtkPolyDataAlgorithm
{
public:
    static vtkSHYXSelectionOBBSubtractFilter* New();
    vtkTypeMacro(vtkSHYXSelectionOBBSubtractFilter, vtkPolyDataAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    /** vtkSelection on port 1 (same convention as SHYX Selection Extrude). */
    void SetSourceConnection(vtkAlgorithmOutput* algOutput);

    /**
     * When on (default), port 1 is used when connected (wire the Selection input and use
     * Copy Active Selection in ParaView). When off, port 1 is ignored and only
     * SelectionCellArrayName is considered for defining the region.
     */
    vtkSetMacro(UseSelectionInput, int);
    vtkGetMacro(UseSelectionInput, int);
    vtkBooleanMacro(UseSelectionInput, int);

    /** Forwarded to vtkSHYXMinimumOBBFilter when building the OBB from selected points. */
    vtkSetMacro(CopyInputPointsForOBB, int);
    vtkGetMacro(CopyInputPointsForOBB, int);
    vtkBooleanMacro(CopyInputPointsForOBB, int);

    /** Forwarded to vtkCGALBooleanOperation (interpolate attributes onto the difference mesh). */
    vtkSetMacro(UpdateAttributes, bool);
    vtkGetMacro(UpdateAttributes, bool);
    vtkBooleanMacro(UpdateAttributes, bool);

    /**
     * When no usable vtkSelection on port 1 (or UseSelectionInput is off), optional name of a cell
     * data array on port 0: a cell is selected if its first tuple is > 0.5 (float) or non-zero
     * (integral types). All vertices of selected cells contribute points to the OBB.
     */
    vtkSetStringMacro(SelectionCellArrayName);
    vtkGetStringMacro(SelectionCellArrayName);

    /** Interactive box Position: world image of ref corner (0,0,0) under vtkPVTransform (equals T column). */
    vtkGetVector3Macro(Position, double);
    vtkSetVector3Macro(Position, double);
    /** Degrees: same operator order as ParaView vtkPVTransform (Translate, RotateZ, RotateX, RotateY, Scale). */
    vtkGetVector3Macro(Rotation, double);
    vtkSetVector3Macro(Rotation, double);
    /**
     * Per-axis world edge lengths for the unit reference box (0..1, Interactive Box: Scale). The
     * filter divides by the fitted OBB span (2 * OBB.HalfLengths) before applying vtkTransform::Scale
     * to the raw OBB mesh so values match the box widget.
     */
    vtkGetVector3Macro(Scale, double);
    vtkSetVector3Macro(Scale, double);

    /**
     * Reference bounds in the Interactive Box local frame (typically 0..1 per axis). When the
     * fitted OBB field arrays are present, these are fixed to the unit box and Scale / Rotation /
     * Position encode the oriented box; otherwise they fall back to the axis-aligned bounds of the
     * OBB mesh for placement.
     */
    vtkGetVector6Macro(ReferenceBounds, double);
    vtkSetVector6Macro(ReferenceBounds, double);

    vtkSetMacro(UseReferenceBounds, int);
    vtkGetMacro(UseReferenceBounds, int);
    vtkBooleanMacro(UseReferenceBounds, int);

protected:
    vtkSHYXSelectionOBBSubtractFilter();
    ~vtkSHYXSelectionOBBSubtractFilter() override;

    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
    int FillInputPortInformation(int port, vtkInformation* info) override;
    int FillOutputPortInformation(int port, vtkInformation* info) override;

    int UseSelectionInput = 1;
    int CopyInputPointsForOBB = 1;
    bool UpdateAttributes = true;
    char* SelectionCellArrayName = nullptr;
    double Position[3] = { 0.0, 0.0, 0.0 };
    double Rotation[3] = { 0.0, 0.0, 0.0 };
    double Scale[3] = { 1.0, 1.0, 1.0 };
    double ReferenceBounds[6] = { 0.0, 1.0, 0.0, 1.0, 0.0, 1.0 };
    int UseReferenceBounds = 1;

    /** When the selection-derived OBB changes, we store the Interactive-Box PRS that matches the raw fitted mesh. */
    double BaselinePosition[3] = { 0.0, 0.0, 0.0 };
    double BaselineRotation[3] = { 0.0, 0.0, 0.0 };
    double BaselineScale[3] = { 1.0, 1.0, 1.0 };
    bool ObbBaselineValid = false;
    unsigned long long ObbSelectionFingerprint = 0ULL;

private:
    vtkSHYXSelectionOBBSubtractFilter(const vtkSHYXSelectionOBBSubtractFilter&) = delete;
    void operator=(const vtkSHYXSelectionOBBSubtractFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
