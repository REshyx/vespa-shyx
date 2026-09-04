/**
 * @class   vtkSHYXAutoMeshRepair
 * @brief   Cluster self-intersecting faces, dilate each cluster, then local fill + Alpha Wrap + union.
 *
 * Further step after vtkSHYXMeshChecker for self-intersections. Each connected location of
 * intersecting faces is treated as its own selection, expanded by a few face rings, then repaired
 * with the same pipeline as vtkSHYXSelectionFillAlphaReunionFilter (hole-fill both sides, Alpha
 * Wrap the patch, CGAL union, local remesh + smooth/fair).
 *
 * - Port 0: repaired vtkPolyData.
 * - Port 1: first-pass intersecting triangles with cell array SHYX_ClusterId (1-based).
 *
 * Requires CGAL &gt;= 5.5 (same as Alpha Wrapping / Selection Fill Alpha Reunion).
 *
 * @sa vtkSHYXMeshChecker, vtkSHYXSelectionFillAlphaReunionFilter
 */

#ifndef vtkSHYXAutoMeshRepair_h
#define vtkSHYXAutoMeshRepair_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkSHYXAutoMeshRepairModule.h"

class VTKSHYXAUTOMESHREPAIR_EXPORT vtkSHYXAutoMeshRepair : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkSHYXAutoMeshRepair* New();
  vtkTypeMacro(vtkSHYXAutoMeshRepair, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  //@{
  /**
   * Face-ring expansion of each intersecting-face cluster before local Alpha Wrap. Default 3.
   */
  vtkGetMacro(DilateLayers, int);
  vtkSetClampMacro(DilateLayers, int, 0, 64);
  //@}

  //@{
  /**
   * Maximum number of cluster-repair passes (one cluster per pass, then re-detect). Default 32.
   */
  vtkGetMacro(MaxPasses, int);
  vtkSetClampMacro(MaxPasses, int, 1, 256);
  //@}

  //@{
  /**
   * If on (default), run orient_polygon_soup + repair_polygon_soup before intersection detection,
   * same combinatorial soup repair as vtkSHYXMeshChecker.
   */
  vtkGetMacro(AttemptSoupRepair, bool);
  vtkSetMacro(AttemptSoupRepair, bool);
  vtkBooleanMacro(AttemptSoupRepair, bool);
  //@}

  vtkGetMacro(LogSteps, bool);
  vtkSetMacro(LogSteps, bool);
  vtkBooleanMacro(LogSteps, bool);

  /** Passthrough to vtkSHYXHoleFillFilter (both branches of each local repair). */
  vtkGetMacro(FairingContinuity, int);
  vtkSetClampMacro(FairingContinuity, int, 0, 2);

  vtkGetMacro(AbsoluteThresholds, bool);
  vtkSetMacro(AbsoluteThresholds, bool);
  vtkBooleanMacro(AbsoluteThresholds, bool);

  vtkGetMacro(Alpha, double);
  vtkSetMacro(Alpha, double);

  vtkGetMacro(Offset, double);
  vtkSetMacro(Offset, double);

  vtkGetMacro(SkipAlphaWrapping, bool);
  vtkSetMacro(SkipAlphaWrapping, bool);
  vtkBooleanMacro(SkipAlphaWrapping, bool);

  vtkGetMacro(ThrowOnSelfIntersection, bool);
  vtkSetMacro(ThrowOnSelfIntersection, bool);
  vtkBooleanMacro(ThrowOnSelfIntersection, bool);

  vtkGetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkSetMacro(OrientToBoundVolumeWhenNeeded, bool);
  vtkBooleanMacro(OrientToBoundVolumeWhenNeeded, bool);

  vtkGetMacro(EnableBridgeCleanup, bool);
  vtkSetMacro(EnableBridgeCleanup, bool);
  vtkBooleanMacro(EnableBridgeCleanup, bool);

  vtkGetMacro(BridgeDilateLayers, int);
  vtkSetClampMacro(BridgeDilateLayers, int, 0, 64);

  vtkGetMacro(BridgeDilateFromSeam, bool);
  vtkSetMacro(BridgeDilateFromSeam, bool);
  vtkBooleanMacro(BridgeDilateFromSeam, bool);

  vtkGetMacro(BridgeTargetEdgeLength, double);
  vtkSetMacro(BridgeTargetEdgeLength, double);

  vtkGetMacro(BridgeRemeshIterations, int);
  vtkSetClampMacro(BridgeRemeshIterations, int, 1, 50);

  vtkGetMacro(BridgeRemeshRelaxationSteps, int);
  vtkSetClampMacro(BridgeRemeshRelaxationSteps, int, 0, 50);

  vtkGetMacro(BridgeSmoothMethod, int);
  vtkSetClampMacro(BridgeSmoothMethod, int, 0, 2);

  vtkGetMacro(BridgeSmoothIterations, int);
  vtkSetClampMacro(BridgeSmoothIterations, int, 0, 50);

  vtkGetMacro(BridgeSmoothTimeStep, double);
  vtkSetMacro(BridgeSmoothTimeStep, double);

  vtkGetMacro(BridgeFairContinuity, int);
  vtkSetClampMacro(BridgeFairContinuity, int, 0, 2);

protected:
  vtkSHYXAutoMeshRepair();
  ~vtkSHYXAutoMeshRepair() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  int DilateLayers = 3;
  int MaxPasses = 32;
  bool AttemptSoupRepair = true;
  bool LogSteps = true;

  int FairingContinuity = 1;
  bool AbsoluteThresholds = false;
  double Alpha = 4.0;
  double Offset = 0.05;
  bool SkipAlphaWrapping = false;
  bool ThrowOnSelfIntersection = false;
  bool OrientToBoundVolumeWhenNeeded = true;

  bool EnableBridgeCleanup = true;
  int BridgeDilateLayers = 3;
  bool BridgeDilateFromSeam = false;
  double BridgeTargetEdgeLength = -1.0;
  int BridgeRemeshIterations = 3;
  int BridgeRemeshRelaxationSteps = 3;
  int BridgeSmoothMethod = 2;
  int BridgeSmoothIterations = 8;
  double BridgeSmoothTimeStep = 0.0025;
  int BridgeFairContinuity = 1;

private:
  vtkSHYXAutoMeshRepair(const vtkSHYXAutoMeshRepair&) = delete;
  void operator=(const vtkSHYXAutoMeshRepair&) = delete;
};

#endif
