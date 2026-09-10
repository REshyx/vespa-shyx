/**
 * @class   vtkSHYXAutoMeshRepair
 * @brief   Mesh Checker diagnostics plus optional two-stage local Alpha Wrap of self-intersections.
 *
 * Front half matches vtkSHYXMeshChecker (soup / boundary / self-intersection diagnostics and
 * combinatorial soup repair). Local self-intersection repair is off by default. When
 * RepairSelfIntersections is on, RepairStage splits the old one-shot fill + Alpha Wrap + union
 * (CGAL corefinement of a small wrap against the full remainder can hang):
 *
 * - EXTRACT_AND_WRAP (default): cluster intersecting faces, dilate, hole-fill + Alpha Wrap each
 *   patch. Port 0 is the hole-filled remainder; port 2 is the wrapped patches.
 * - UNION: boolean-union Input (remainder) with WrappedPatches (input port 1), then optional
 *   bridge remesh/smooth. Typical pipeline: a second Auto Mesh Repair whose Input is port 0 of
 *   the extract filter and WrappedPatches is port 2.
 * - EXTRACT_WRAP_AND_UNION: legacy one-shot (one cluster per pass, re-detect, up to MaxPasses).
 *
 * - Port 0: remainder (extract) or union / one-shot result.
 * - Port 1: Mesh Checker illegal primitives (SHYX_CheckReason 1/2/3).
 * - Port 2: Alpha-wrapped patches (extract stage); empty otherwise.
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

  enum RepairStageType
  {
    EXTRACT_AND_WRAP = 0,
    UNION = 1,
    EXTRACT_WRAP_AND_UNION = 2
  };

  vtkGetMacro(CheckSoupEdges, bool);
  vtkSetMacro(CheckSoupEdges, bool);
  vtkBooleanMacro(CheckSoupEdges, bool);

  vtkGetMacro(CheckBoundary, bool);
  vtkSetMacro(CheckBoundary, bool);
  vtkBooleanMacro(CheckBoundary, bool);

  vtkGetMacro(CheckSelfIntersection, bool);
  vtkSetMacro(CheckSelfIntersection, bool);
  vtkBooleanMacro(CheckSelfIntersection, bool);

  vtkGetMacro(CheckOrient, bool);
  vtkSetMacro(CheckOrient, bool);
  vtkBooleanMacro(CheckOrient, bool);

  vtkGetMacro(AttemptOrientRepair, bool);
  vtkSetMacro(AttemptOrientRepair, bool);
  vtkBooleanMacro(AttemptOrientRepair, bool);

  //@{
  /**
   * If on, cluster self-intersecting faces, dilate, and run RepairStage (extract/wrap and/or union).
   * Default off: same as vtkSHYXMeshChecker (diagnose + soup repair only).
   */
  vtkGetMacro(RepairSelfIntersections, bool);
  vtkSetMacro(RepairSelfIntersections, bool);
  vtkBooleanMacro(RepairSelfIntersections, bool);
  //@}

  //@{
  /**
   * 0 = extract clusters + Alpha Wrap (port 0 remainder, port 2 wrapped).
   * 1 = union Input remainder with WrappedPatches (input port 1).
   * 2 = legacy one-shot extract + wrap + union per cluster.
   * Default 0. Ignored when RepairSelfIntersections is off.
   */
  vtkGetMacro(RepairStage, int);
  vtkSetClampMacro(RepairStage, int, 0, 2);
  //@}

  //@{
  /**
   * Face-ring expansion of each intersecting-face cluster before local Alpha Wrap. Default 3.
   */
  vtkGetMacro(DilateLayers, int);
  vtkSetClampMacro(DilateLayers, int, 0, 64);
  //@}

  //@{
  /**
   * Extract stage: maximum number of dilated clusters to wrap (largest first).
   * One-shot stage: maximum cluster-repair passes (one cluster per pass, then re-detect).
   * Default 32.
   */
  vtkGetMacro(MaxPasses, int);
  vtkSetClampMacro(MaxPasses, int, 1, 256);
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

  vtkGetMacro(EnableBridgeRemesh, bool);
  vtkSetMacro(EnableBridgeRemesh, bool);
  vtkBooleanMacro(EnableBridgeRemesh, bool);

  vtkGetMacro(EnableBridgeSmooth, bool);
  vtkSetMacro(EnableBridgeSmooth, bool);
  vtkBooleanMacro(EnableBridgeSmooth, bool);

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
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  bool CheckSoupEdges = true;
  bool CheckBoundary = true;
  bool CheckSelfIntersection = true;
  bool CheckOrient = true;
  bool AttemptOrientRepair = true;
  bool RepairSelfIntersections = false;
  int RepairStage = EXTRACT_AND_WRAP;
  int DilateLayers = 3;
  int MaxPasses = 32;
  bool LogSteps = true;

  int FairingContinuity = 1;
  bool AbsoluteThresholds = false;
  double Alpha = 4.0;
  double Offset = 0.05;
  bool SkipAlphaWrapping = false;
  bool ThrowOnSelfIntersection = false;
  bool OrientToBoundVolumeWhenNeeded = true;

  bool EnableBridgeCleanup = true;
  bool EnableBridgeRemesh = true;
  bool EnableBridgeSmooth = true;
  int BridgeDilateLayers = 2;
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
