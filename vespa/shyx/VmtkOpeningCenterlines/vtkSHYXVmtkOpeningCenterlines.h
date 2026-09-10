/**
 * @class   vtkSHYXVmtkOpeningCenterlines
 * @brief   Scalar-range patches → connected openings → seed points; optional VMTK centerlines.
 *
 * Single input: tubular vessel surface (vtkPolyData). Select cells whose chosen array magnitude
 * is greater than zero (fixed rule; only the array is user-selectable). Default threshold array is
 * cell-data \c EndpointIndex (e.g. vtkCGALVesselEndClipper); override via VTK
 * \c SetInputArrayToProcess (ParaView array picker).
 * Each resulting connected component yields one representative surface point (closest mesh
 * vertex to the patch centroid). Each opening’s list entry is keyed by **SeedPoint: &lt;SurfacePointId&gt;**
 * (the representative surface vertex id), listed in ascending SurfacePointId order so the panel
 * matches Point Label numbering; duplicate labels get **#2**, **#3** suffixes.
 * vtkDataArraySelection \c InletSelection — checked entries are VMTK source seeds (inlets);
 * unchecked entries are targets (outlets). \c ExcludedOpeningSelection — checked entries are omitted
 * from seed output and from centerline seeds (deleted openings). Changing the threshold array
 * clears prior checks and rebuilds the lists on the next RequestData.
 *
 * CenterlineMethod (when CalculateCenterline is on):
 * - 0 Voronoi: vtkvmtkPolyDataCenterlines on the closed input (source = checked inlets,
 *   target = unchecked outlets). Tree topology; loops keep only the cheaper arm.
 * - 1 Network: delete every cell that passed the threshold (typically EndpointIndex caps),
 *   leaving boundary rings, then vtkvmtkPolyDataNetworkExtraction. No inlet/outlet seeds.
 *
 * Optional post-process (when CalculateCenterline is on), matching the usual VMTK pipe
 * vmtkcenterlineattributes → vmtkbranchextractor → vmtkcenterlinegeometry:
 * - ComputeCenterlineAttributes: Abscissas, ParallelTransportNormals (point data).
 * - ExtractCenterlineBranches: CenterlineIds, TractIds, GroupIds, Blanking (cell data).
 *   Intended for Voronoi overlapping source–target paths; Network Topology is not preserved.
 * - ComputeCenterlineGeometry: Length/Curvature/Torsion/Tortuosity (cell) and Frenet frames (point).
 *
 * Outputs:
 * - Port 0: centerline/network polydata when CalculateCenterline is on and the chosen method
 *   has valid input; otherwise empty. Voronoi shallow-copies the input with point GlobalIds.
 * - Port 1: one vertex per non-excluded opening with PointData OpeningArrayValue (mean magnitude),
 *   OpeningIndex (0-based among emitted openings after SurfacePointId sort), SurfacePointId, IsInlet.
 */

#ifndef vtkSHYXVmtkOpeningCenterlines_h
#define vtkSHYXVmtkOpeningCenterlines_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXVmtkOpeningCenterlinesModule.h"

#include <string>

#include <vtkSmartPointer.h>

class vtkDataArraySelection;

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXVMTKOPENINGCENTERLINES_EXPORT vtkSHYXVmtkOpeningCenterlines : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXVmtkOpeningCenterlines* New();
  vtkTypeMacro(vtkSHYXVmtkOpeningCenterlines, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(CalculateCenterline, int);
  vtkGetMacro(CalculateCenterline, int);
  vtkBooleanMacro(CalculateCenterline, int);

  /** 0 = vtkvmtkPolyDataCenterlines (source–target tree); 1 = punch threshold cells + network. */
  vtkSetClampMacro(CenterlineMethod, int, 0, 1);
  vtkGetMacro(CenterlineMethod, int);

  vtkSetMacro(FlipNormals, int);
  vtkGetMacro(FlipNormals, int);
  vtkBooleanMacro(FlipNormals, int);

  vtkSetMacro(StopFastMarchingOnReachingTarget, int);
  vtkGetMacro(StopFastMarchingOnReachingTarget, int);
  vtkBooleanMacro(StopFastMarchingOnReachingTarget, int);

  vtkSetMacro(AppendEndPointsToCenterlines, int);
  vtkGetMacro(AppendEndPointsToCenterlines, int);
  vtkBooleanMacro(AppendEndPointsToCenterlines, int);

  /** Sphere step / local max radius for vtkvmtkPolyDataNetworkExtraction (must be >= 1). */
  vtkSetClampMacro(AdvancementRatio, double, 1.0, 10.0);
  vtkGetMacro(AdvancementRatio, double);

  /** vtkvmtkCenterlineAttributesFilter: Abscissas + ParallelTransportNormals. */
  vtkSetMacro(ComputeCenterlineAttributes, int);
  vtkGetMacro(ComputeCenterlineAttributes, int);
  vtkBooleanMacro(ComputeCenterlineAttributes, int);

  /** vtkvmtkCenterlineBranchExtractor: CenterlineIds, TractIds, GroupIds, Blanking. */
  vtkSetMacro(ExtractCenterlineBranches, int);
  vtkGetMacro(ExtractCenterlineBranches, int);
  vtkBooleanMacro(ExtractCenterlineBranches, int);

  /** vtkvmtkCenterlineGeometry: Length/Curvature/Torsion/Tortuosity + Frenet frames. */
  vtkSetMacro(ComputeCenterlineGeometry, int);
  vtkGetMacro(ComputeCenterlineGeometry, int);
  vtkBooleanMacro(ComputeCenterlineGeometry, int);

  vtkDataArraySelection* GetInletSelection();
  vtkDataArraySelection* GetExcludedOpeningSelection();

  /** Monotonic counter bumped whenever opening names / selections are rebuilt (ParaView domain refresh). */
  vtkGetMacro(OpeningListRevision, int);

  vtkMTimeType GetMTime() override;

protected:
  vtkSHYXVmtkOpeningCenterlines();
  ~vtkSHYXVmtkOpeningCenterlines() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

private:
  vtkSHYXVmtkOpeningCenterlines(const vtkSHYXVmtkOpeningCenterlines&) = delete;
  void operator=(const vtkSHYXVmtkOpeningCenterlines&) = delete;

  static void ClearAllArrays(vtkDataArraySelection* sel);
  void InvalidateInletSelectionIfOpeningThresholdChanged();
  void ApplyCenterlinePostProcess(vtkPolyData* centerlines);

  int CalculateCenterline = 0;
  int CenterlineMethod = 0;

  int FlipNormals = 0;
  int StopFastMarchingOnReachingTarget = 0;
  int AppendEndPointsToCenterlines = 1;
  double AdvancementRatio = 1.02;

  int ComputeCenterlineAttributes = 0;
  int ExtractCenterlineBranches = 0;
  int ComputeCenterlineGeometry = 0;

  vtkSmartPointer<vtkDataArraySelection> InletSelection;
  vtkSmartPointer<vtkDataArraySelection> ExcludedOpeningSelection;

  /** Last threshold-array fingerprint; used to reset the inlet list when the array changes. */
  std::string CachedOpeningThresholdFingerprint;

  int OpeningListRevision = 0;
};

VTK_ABI_NAMESPACE_END
#endif
