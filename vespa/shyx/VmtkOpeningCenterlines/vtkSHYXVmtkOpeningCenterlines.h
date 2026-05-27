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
 * (the representative surface vertex id); duplicate labels get **#2**, **#3** suffixes.
 * vtkDataArraySelection \c InletSelection — checked entries are VMTK source seeds (inlets);
 * unchecked entries are targets (outlets). \c ExcludedOpeningSelection — checked entries are omitted
 * from seed output and from centerline seeds (deleted openings). Changing the threshold array
 * clears prior checks and rebuilds the lists on the next RequestData.
 *
 * Outputs:
 * - Port 0: vtkvmtkPolyDataCenterlines result when CalculateCenterline is on and seeds valid;
 *   otherwise empty polydata. (Input surface is shallow-copied internally for VMTK with point GlobalIds.)
 * - Port 1: one vertex per non-excluded opening with PointData OpeningArrayValue (mean magnitude),
 *   OpeningIndex (processing order among emitted openings), SurfacePointId, IsInlet.
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

  vtkSetMacro(FlipNormals, int);
  vtkGetMacro(FlipNormals, int);
  vtkBooleanMacro(FlipNormals, int);

  vtkSetMacro(StopFastMarchingOnReachingTarget, int);
  vtkGetMacro(StopFastMarchingOnReachingTarget, int);
  vtkBooleanMacro(StopFastMarchingOnReachingTarget, int);

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

  int CalculateCenterline = 0;

  int FlipNormals = 0;
  int StopFastMarchingOnReachingTarget = 0;

  vtkSmartPointer<vtkDataArraySelection> InletSelection;
  vtkSmartPointer<vtkDataArraySelection> ExcludedOpeningSelection;

  /** Last threshold-array fingerprint; used to reset the inlet list when the array changes. */
  std::string CachedOpeningThresholdFingerprint;

  int OpeningListRevision = 0;
};

VTK_ABI_NAMESPACE_END
#endif
