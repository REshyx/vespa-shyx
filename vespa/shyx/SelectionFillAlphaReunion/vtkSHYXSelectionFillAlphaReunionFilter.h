/**
 * @class   vtkSHYXSelectionFillAlphaReunionFilter
 * @brief   Split mesh by selection; hole-fill the rest; hole-fill + Alpha Wrapping the selection;
 *          then CGAL boolean union with face-origin tracking; optionally clean the AW/original bridge.
 *
 * After union, faces imported from the Alpha-Wrapped mesh are marked exactly via a corefinement
 * visitor (no distance heuristic). The mask is dilated by a few face rings across the boolean
 * seam, then a local CGAL isotropic remesh (with relaxation) and constrained smooth_shape (MCF)
 * are applied on that patch.
 *
 * @sa vtkSHYXHoleFillFilter, vtkCGALAlphaWrapping, vtkSHYXBooleanOperationFilter
 */

#ifndef vtkSHYXSelectionFillAlphaReunionFilter_h
#define vtkSHYXSelectionFillAlphaReunionFilter_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXSelectionFillAlphaReunionFilterModule.h"

class VTKSHYXSELECTIONFILLALPHAREUNIONFILTER_EXPORT vtkSHYXSelectionFillAlphaReunionFilter
  : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXSelectionFillAlphaReunionFilter* New();
  vtkTypeMacro(vtkSHYXSelectionFillAlphaReunionFilter, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Port 1: optional vtkSelection (same pattern as other SHYX selection filters). */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  /**
   * When port 1 has no usable selection: name of a cell data array on port 0. A cell is selected
   * if the first component is &gt; 0.5 (float) or != 0 (integral), same as vtkSHYXDeleteSelectedCellsFilter.
   */
  vtkSetStringMacro(SelectionCellArrayName);
  vtkGetStringMacro(SelectionCellArrayName);

  /** Passthrough to vtkSHYXHoleFillFilter (both branches). */
  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);

  /** Parameters for CGAL Alpha Wrapping on the selected part (see vtkCGALAlphaWrapping). */
  vtkGetMacro(AbsoluteThresholds, bool);
  vtkSetMacro(AbsoluteThresholds, bool);
  vtkBooleanMacro(AbsoluteThresholds, bool);

  vtkGetMacro(Alpha, double);
  vtkSetMacro(Alpha, double);

  vtkGetMacro(Offset, double);
  vtkSetMacro(Offset, double);

  /** If true, only hole-fill the selected part (skip Alpha Wrapping). */
  vtkGetMacro(SkipAlphaWrapping, bool);
  vtkSetMacro(SkipAlphaWrapping, bool);
  vtkBooleanMacro(SkipAlphaWrapping, bool);

  /** Same options as vtkSHYXBooleanOperationFilter (union). */
  vtkGetMacro(ThrowOnSelfIntersection, bool);
  vtkSetMacro(ThrowOnSelfIntersection, bool);
  vtkBooleanMacro(ThrowOnSelfIntersection, bool);

  vtkGetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkSetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkBooleanMacro(OrientToBoundVolumeWhenNeeded, bool);

  //@{
  /**
   * After a successful boolean union, mark faces that originated from the Alpha-Wrapped mesh
   * (corefinement visitor), dilate, then locally remesh + smooth that patch. Default ON.
   * No-op when the unselected part is empty (no union).
   */
  vtkGetMacro(EnableBridgeCleanup, bool);
  vtkSetMacro(EnableBridgeCleanup, bool);
  vtkBooleanMacro(EnableBridgeCleanup, bool);
  //@}

  //@{
  /**
   * Face-ring dilation of the precise AW face mark so the boolean seam is included. Default 3.
   * AW faces are those imported from the Alpha-Wrapped operand during CGAL corefinement union.
   */
  vtkGetMacro(BridgeDilateLayers, int);
  vtkSetClampMacro(BridgeDilateLayers, int, 0, 64);
  //@}

  //@{
  /**
   * Target edge length for the local isotropic remesh. &lt;= 0 means auto: mean edge length of the
   * marked patch. Default -1.
   */
  vtkGetMacro(BridgeTargetEdgeLength, double);
  vtkSetMacro(BridgeTargetEdgeLength, double);
  //@}

  //@{
  /** CGAL isotropic_remeshing iterations on the dilated patch. Default 3. */
  vtkGetMacro(BridgeRemeshIterations, int);
  vtkSetClampMacro(BridgeRemeshIterations, int, 1, 50);
  //@}

  //@{
  /**
   * CGAL isotropic_remeshing number_of_relaxation_steps (vertex relocation during remesh).
   * Default 3.
   */
  vtkGetMacro(BridgeRemeshRelaxationSteps, int);
  vtkSetClampMacro(BridgeRemeshRelaxationSteps, int, 0, 50);
  //@}

  //@{
  /**
   * Extra CGAL smooth_shape (mean curvature flow) iterations on the remeshed patch after remesh.
   * 0 disables. Default 8. Unlike angle/area mesh smoothing, this visibly moves the surface.
   */
  vtkGetMacro(BridgeSmoothIterations, int);
  vtkSetClampMacro(BridgeSmoothIterations, int, 0, 50);
  //@}

  //@{
  /**
   * Time step for post-remesh smooth_shape. Larger = stronger motion per iteration (and more
   * shrinkage). Default 0.0025.
   */
  vtkGetMacro(BridgeSmoothTimeStep, double);
  vtkSetMacro(BridgeSmoothTimeStep, double);
  //@}

  //@{
  /**
   * When true, attach cell array SHYXBridgeCleanupMask (1 = cleanup patch). After remesh, the
   * mask tracks the remeshed cleanup region (faces outside the pre-remesh patch stay 0). Default
   * true.
   */
  vtkGetMacro(ExportBridgeMask, bool);
  vtkSetMacro(ExportBridgeMask, bool);
  vtkBooleanMacro(ExportBridgeMask, bool);
  //@}

protected:
  vtkSHYXSelectionFillAlphaReunionFilter();
  ~vtkSHYXSelectionFillAlphaReunionFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  int FairingContinuity = 1;
  bool AbsoluteThresholds = false;
  double Alpha = 4.0;
  double Offset = 0.05;
  bool SkipAlphaWrapping = false;
  bool ThrowOnSelfIntersection = false;
  bool OrientToBoundVolumeWhenNeeded = true;
  char* SelectionCellArrayName = nullptr;

  bool EnableBridgeCleanup = true;
  int BridgeDilateLayers = 3;
  double BridgeTargetEdgeLength = -1.0;
  int BridgeRemeshIterations = 3;
  int BridgeRemeshRelaxationSteps = 3;
  int BridgeSmoothIterations = 8;
  double BridgeSmoothTimeStep = 0.0025;
  bool ExportBridgeMask = true;

private:
  vtkSHYXSelectionFillAlphaReunionFilter(const vtkSHYXSelectionFillAlphaReunionFilter&) = delete;
  void operator=(const vtkSHYXSelectionFillAlphaReunionFilter&) = delete;
};

#endif
