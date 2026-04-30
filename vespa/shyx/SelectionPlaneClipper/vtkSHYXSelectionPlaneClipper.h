/**
 * @class   vtkSHYXSelectionPlaneClipper
 * @brief   Clip a surface mesh using a plane derived from the active selection (centroid + average normal),
 *          with optional interactive plane editing (same packed-string convention as Vessel End Clipper).
 *
 * Port 1 accepts vtkSelection (ParaView Selection port with SelectionInput); vtkExtractSelection is run
 * internally. Alternatively set SelectionCellArrayName on port 0 when no selection is connected.
 * The computed clip plane hint (origin + direction handle) is stored on port 0 field data array
 * SHYX_SelectionPlaneClipper_PlanePacked for the interactive plane widget.
 */

#ifndef vtkSHYXSelectionPlaneClipper_h
#define vtkSHYXSelectionPlaneClipper_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXSelectionPlaneClipperModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXSELECTIONPLANECLIPPER_EXPORT vtkSHYXSelectionPlaneClipper : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXSelectionPlaneClipper* New();
  vtkTypeMacro(vtkSHYXSelectionPlaneClipper, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Selection input (port 1): vtkSelection, same pattern as SHYX Selection Extrude / Region Fairing
   * (ParaView Selection port with Copy Active Selection).
   */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable vtkSelection, optional cell data array on port 0: a cell is treated
   * as selected if scalar &gt; 0.5 or integral non-zero (same as Selection Extrude).
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  vtkGetMacro(ClipOffset, double);
  vtkSetMacro(ClipOffset, double);

  vtkGetMacro(InvertPlane, int);
  vtkSetMacro(InvertPlane, int);
  vtkBooleanMacro(InvertPlane, int);

  /**
   * If true (default), clip like Vessel End Clipper: split by the plane, then remove the smaller
   * connected component near the selection centroid on the side that contains it.
   * If false, remove a single half-space (RemovePositiveHalfSpace controls which side).
   */
  vtkGetMacro(UseTipConnectivity, int);
  vtkSetMacro(UseTipConnectivity, int);
  vtkBooleanMacro(UseTipConnectivity, int);

  /** Used only when UseTipConnectivity is false: if true, remove the plane's positive half-space
   *  (implicit function > 0); if false, remove the negative half-space. */
  vtkGetMacro(RemovePositiveHalfSpace, int);
  vtkSetMacro(RemovePositiveHalfSpace, int);
  vtkBooleanMacro(RemovePositiveHalfSpace, int);

  vtkGetStringMacro(InteractiveCutPackedString);
  vtkSetStringMacro(InteractiveCutPackedString);
  vtkGetMacro(UseInteractiveCutPlanes, int);
  vtkSetMacro(UseInteractiveCutPlanes, int);
  vtkBooleanMacro(UseInteractiveCutPlanes, int);

  /**
   * When true (default), run vtkFillHolesFilter after clipping to triangulate small boundary loops
   * (typical clip opening). Hole size limit is derived from the clipped mesh bounding diagonal unless
   * FillHolesMaximumSize is set positive.
   */
  vtkGetMacro(FillHoles, int);
  vtkSetMacro(FillHoles, int);
  vtkBooleanMacro(FillHoles, int);

  /** If > 0, passed to vtkFillHolesFilter::SetHoleSize; if <= 0, use a fraction of the clipped bounds diagonal. */
  vtkGetMacro(FillHolesMaximumSize, double);
  vtkSetMacro(FillHolesMaximumSize, double);

protected:
  vtkSHYXSelectionPlaneClipper();
  ~vtkSHYXSelectionPlaneClipper() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double ClipOffset = 0.0;
  int InvertPlane = 0;
  int UseTipConnectivity = 1;
  int RemovePositiveHalfSpace = 1;

  char* InteractiveCutPackedString = nullptr;
  int UseInteractiveCutPlanes = 0;

  int FillHoles = 1;
  double FillHolesMaximumSize = 0.0;

  char* SelectionCellArrayName = nullptr;

private:
  vtkSHYXSelectionPlaneClipper(const vtkSHYXSelectionPlaneClipper&) = delete;
  void operator=(const vtkSHYXSelectionPlaneClipper&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
