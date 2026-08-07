/**
 * @class   vtkSHYXPartitionedCollectionBoundaryAssignment
 * @brief   Area-sort side sets and classify wall / inlet / outlet for BC map + inlet OPT text.
 *
 * Downstream of vtkSHYXDataSetToPartitionedCollection (or compatible IOSS-style
 * vtkPartitionedDataSetCollection). Sorts side/node pairs by side-set area (descending)
 * for classification via FlowBoundaryMode:
 * - index 0 (largest): wall
 * - index 1: sole inlet (SINGLE_INLET) or sole outlet (SINGLE_OUTLET)
 * - remaining: the other role
 *
 * Optional MergeInletsIntoOneSideSet (default on): in SINGLE_OUTLET mode, after per-inlet OPT
 * stats are collected, append all classified inlet side/node sets into one pair on port 0.
 * BoundaryAssignmentText reflects the post-merge map; InletOptText is a full options file
 * (PV template for SINGLE_INLET, HV template for SINGLE_OUTLET) with pre-merge inlet_* lines.
 *
 * Optional CustomAdapter (default on): keeps the first two BoundaryAssignmentText lines (real
 * ENTITY_IDs), but remaps data-row sideset ids: wall→3, inlet→1, outlets→21,22,... in row order.
 *
 * Ports:
 * - 0: input vtkPartitionedDataSetCollection (passthrough, or with inlets merged) + FieldData stamps
 * - 1: debug vtkPolyData built from the two texts — Point Label for all sideset ids;
 *   AABB/normals for inlets only (from Inlet OPT values)
 *
 * Emits BoundaryAssignmentText / InletOptText. Does not modify boundary field arrays; that remains
 * vtkSHYXPartitionedCollectionBoundaryFields.
 */

#ifndef vtkSHYXPartitionedCollectionBoundaryAssignment_h
#define vtkSHYXPartitionedCollectionBoundaryAssignment_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXPartitionedCollectionBoundaryAssignmentModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkSHYXPartitionedCollectionBoundaryAssignment : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXPartitionedCollectionBoundaryAssignment* New();
  vtkTypeMacro(vtkSHYXPartitionedCollectionBoundaryAssignment, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum FlowBoundaryModeType
  {
    SINGLE_INLET = 0,  /**< Index 1 = inlet; remaining openings = outlets. */
    SINGLE_OUTLET = 1, /**< Index 1 = outlet; remaining openings = inlets. */
  };

  /**
   * After side sets are reordered by area (descending): largest is wall; second is the sole
   * inlet or outlet per this mode; remaining openings take the other role.
   */
  vtkSetClampMacro(FlowBoundaryMode, int, SINGLE_INLET, SINGLE_OUTLET);
  vtkGetMacro(FlowBoundaryMode, int);

  /**
   * When non-zero (default): in SINGLE_OUTLET mode, merge all classified inlets into one side/node
   * pair on port 0 after Inlet OPT stats are computed. Does nothing in SINGLE_INLET mode.
   */
  vtkSetMacro(MergeInletsIntoOneSideSet, int);
  vtkGetMacro(MergeInletsIntoOneSideSet, int);
  vtkBooleanMacro(MergeInletsIntoOneSideSet, int);

  /**
   * When non-zero (default): Boundary assignment data rows use adapter ids (wall=3, inlet=1,
   * outlets from 21). The header and "nodeset: ..." summary keep real ENTITY_IDs.
   */
  vtkSetMacro(CustomAdapter, int);
  vtkGetMacro(CustomAdapter, int);
  vtkBooleanMacro(CustomAdapter, int);

  /**
   * Read-only BC map. Prefer output FieldData (SHYXBoundaryAssignmentText) so ParaView's
   * UpdatePropertyInformation always sees the text from the latest RequestData.
   */
  const char* GetBoundaryAssignmentText();

  /**
   * Read-only full options file text. Prefer output FieldData (SHYXInletOptText).
   * SINGLE_INLET uses the PV template; SINGLE_OUTLET uses the HV template. Inlet_* values are
   * filled from pre-merge classification stats.
   */
  const char* GetInletOptText();

  vtkSetMacro(InletOptNormalScale, double);
  vtkGetMacro(InletOptNormalScale, double);

  vtkSetMacro(InletOptBoundsScale, double);
  vtkGetMacro(InletOptBoundsScale, double);

  vtkSetMacro(InletOptFlowFactorScale, double);
  vtkGetMacro(InletOptFlowFactorScale, double);

  vtkSetMacro(InletOptAreaScale, double);
  vtkGetMacro(InletOptAreaScale, double);

protected:
  vtkSHYXPartitionedCollectionBoundaryAssignment();
  ~vtkSHYXPartitionedCollectionBoundaryAssignment() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  /** Copy text into ivar without Modified() — safe to call from RequestData / Get*. */
  void CopyInfoString(char*& dest, const char* text);

  int FlowBoundaryMode = SINGLE_INLET;
  int MergeInletsIntoOneSideSet = 1;
  int CustomAdapter = 1;
  char* BoundaryAssignmentText = nullptr;
  char* InletOptText = nullptr;
  double InletOptNormalScale = -1.0;
  double InletOptBoundsScale = 0.1;
  double InletOptFlowFactorScale = 1.0;
  double InletOptAreaScale = 1.0;

private:
  vtkSHYXPartitionedCollectionBoundaryAssignment(
    const vtkSHYXPartitionedCollectionBoundaryAssignment&) = delete;
  void operator=(const vtkSHYXPartitionedCollectionBoundaryAssignment&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
