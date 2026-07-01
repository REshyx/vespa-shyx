/**
 * @class   vtkSHYXDataSetToPartitionedCollection
 * @brief   Build a vtkPartitionedDataSetCollection from vtkDataSet: tet block + surface partitions.
 *
 * Pipeline: (1) extract VTK_TETRA cells into one vtkUnstructuredGrid partition with contiguous
 * point/cell GlobalIds (1..N) for vtkIOSSWriter; (2) vtkDataSetSurfaceFilter on
 * the tet-only mesh with PassThroughCellIds/PointIds; (3) boundary cell data element_side (volume
 * global cell id, Exodus tet face 1..4); (4) split the boundary surface into side-set patches
 * using vtkPointData on the extracted surface: \c vtkThreshold keeps cells whose first component
 * lies in (-\infty, 0] (inclusive of 0) with All Scalars off (any corner suffices); vtkAppendPolyData merges
 * all connected parts into one patch. Then cells in [1, +\infty) (inclusive of 1) on the first component,
 * also any corner; \c vtkPolyDataConnectivityFilter splits that sub-mesh into \e n connected regions.
 * A cell whose corners all lie strictly in (0, 1) may belong to neither pass.
 * Side / node sets follow in order: one block for the merged non-positive region (if non-empty), then \e n blocks for
 * the sub-regions at or above 1 (up to \e n+1 pairs total). If the array is missing or the wrong length, or the name is empty, falls
 * back to vtkPolyDataNormals feature-angle splitting plus vtkPolyDataConnectivityFilter on the full
 * surface. Unreferenced points are stripped per region so side{i} / node{i} only contain
 * points used by that patch. node{i} matches side{i} (1:1 by point id) with one vtkVertex per
 * point. Surface point GlobalIds duplicate the volume mesh node ids so vtkIOSSWriter NodeSet ids
 * resolve to correct coordinates in the merged Exodus node block.
 * vtkDataAssembly IOSS / element_blocks, node_sets (all node{i}), then side_sets (all side{i}).
 *
 * @sa
 * vtkDataSetSurfaceFilter, vtkThreshold, vtkGeometryFilter, vtkAppendPolyData, vtkPolyDataNormals, vtkPolyDataConnectivityFilter
 */

#ifndef vtkSHYXDataSetToPartitionedCollection_h
#define vtkSHYXDataSetToPartitionedCollection_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXDataSetToPartitionedCollectionModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkSHYXDataSetToPartitionedCollection : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXDataSetToPartitionedCollection* New();
  vtkTypeMacro(vtkSHYXDataSetToPartitionedCollection, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(FeatureAngle, double);
  vtkGetMacro(FeatureAngle, double);

  /**
   * Point-data array on the boundary surface: vtkThreshold first component in (-\infty, 0] (any
   * corner; vtkThreshold THRESHOLD_BETWEEN with inclusive endpoints) merged to one patch; then
   * [1, +\infty) (any corner) split by vtkPolyDataConnectivityFilter into multiple patches. Values
   * strictly in (0, 1) at all corners of a cell keep that cell out of both passes. Default \c
   * EndpointIndex. Empty string uses FeatureAngle / vtkPolyDataConnectivityFilter on the full
   * surface only.
   */
  vtkSetStringMacro(PartitionPointArrayName);
  vtkGetStringMacro(PartitionPointArrayName);

  vtkSetMacro(SortByArea, int);
  vtkGetMacro(SortByArea, int);
  vtkBooleanMacro(SortByArea, int);

  vtkSetMacro(CustomPostReorder, int);
  vtkGetMacro(CustomPostReorder, int);
  vtkBooleanMacro(CustomPostReorder, int);

  /**
   * When non-zero, BoundaryRadialValueNormal uses BoundaryRadialValue * sideNormal. Otherwise it
   * uses sideNormal. BoundaryRadialValue itself is always computed.
   */
  vtkSetMacro(ComputeBoundaryRadialValue, int);
  vtkGetMacro(ComputeBoundaryRadialValue, int);
  vtkBooleanMacro(ComputeBoundaryRadialValue, int);

  /** Exponent a used to convert raw radial coordinate x to BoundaryRadialValue = 1 - x^a. */
  vtkSetMacro(BoundaryRadialNormalFalloffFactor, double);
  vtkGetMacro(BoundaryRadialNormalFalloffFactor, double);

  /**
   * Newline-separated values aligned with the output block order (tetrahedra, node sets, side
   * sets). Values in each row are tab-separated Variable1, Variable2, ... entries. Only side-set
   * entries are used. Any non-zero value enables writes to volume points. Values are stored in
   * BoundaryVariable1, BoundaryVariable2, ... arrays. BoundaryRadialValueNormal does not multiply
   * these variables. Repeated writes to the same volume node are warned and averaged.
   */
  vtkSetStringMacro(BoundaryVariables);
  vtkGetStringMacro(BoundaryVariables);

  /**
   * Newline-separated names for the output partitioned datasets, in output order:
   * tetrahedra (when present), all node sets, then all side sets. Empty / missing
   * entries fall back to tetrahedra, node{i}, side{i}.
   */
  vtkSetStringMacro(BlockNames);
  vtkGetStringMacro(BlockNames);

protected:
  vtkSHYXDataSetToPartitionedCollection();
  ~vtkSHYXDataSetToPartitionedCollection() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double FeatureAngle = 70.0;
  char* PartitionPointArrayName = nullptr;
  /** When non-zero (default), order side / node set patches by total surface area, largest first. */
  int SortByArea = 1;
  /**
   * When non-zero (default), after ordering (e.g. by area), move the 3rd patch to the front and
   * the 1st patch to the end; 2nd stays next, then original 4th…(n-1) in order. No effect if fewer
   * than three patches.
   */
  int CustomPostReorder = 1;
  int ComputeBoundaryRadialValue = 0;
  double BoundaryRadialNormalFalloffFactor = 1.0;
  char* BoundaryVariables = nullptr;
  char* BlockNames = nullptr;

private:
  vtkSHYXDataSetToPartitionedCollection(const vtkSHYXDataSetToPartitionedCollection&) = delete;
  void operator=(const vtkSHYXDataSetToPartitionedCollection&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
