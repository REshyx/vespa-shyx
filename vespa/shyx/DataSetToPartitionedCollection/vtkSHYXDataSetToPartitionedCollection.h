/**
 * @class   vtkSHYXDataSetToPartitionedCollection
 * @brief   Build a vtkPartitionedDataSetCollection from vtkDataSet: tet block + surface partitions.
 *
 * Pipeline: (1) extract VTK_TETRA cells into one vtkUnstructuredGrid partition with contiguous
 * point/cell GlobalIds (1..N) for vtkIOSSWriter; (2) vtkDataSetSurfaceFilter on
 * the tet-only mesh with PassThroughCellIds/PointIds; (3) boundary cell data element_side (volume
 * global cell id, Exodus tet face 1..4); (4) vtkPolyDataNormals splitting by feature angle; (5)
 * vtkPolyDataConnectivityFilter into regions; unreferenced points are stripped per region so
 * side{i} / node{i} only contain points used by that patch. node{i} matches side{i} (1:1 by point
 * id) with one vtkVertex per point. Surface point GlobalIds duplicate the volume mesh node ids so
 * vtkIOSSWriter NodeSet ids resolve to correct coordinates in the merged Exodus node block.
 * vtkDataAssembly IOSS / element_blocks, node_sets (all node{i}), then side_sets (all side{i}).
 *
 * @sa
 * vtkDataSetSurfaceFilter, vtkPolyDataNormals, vtkPolyDataConnectivityFilter
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

  vtkSetMacro(SortByArea, int);
  vtkGetMacro(SortByArea, int);
  vtkBooleanMacro(SortByArea, int);

  vtkSetMacro(CustomPostReorder, int);
  vtkGetMacro(CustomPostReorder, int);
  vtkBooleanMacro(CustomPostReorder, int);

protected:
  vtkSHYXDataSetToPartitionedCollection();
  ~vtkSHYXDataSetToPartitionedCollection() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double FeatureAngle = 70.0;
  /** When non-zero (default), order side / node set patches by total surface area, largest first. */
  int SortByArea = 1;
  /**
   * When non-zero (default), after ordering (e.g. by area), move the 3rd patch to the front and
   * the 1st patch to the end; 2nd stays next, then original 4th…(n-1) in order. No effect if fewer
   * than three patches.
   */
  int CustomPostReorder = 1;

private:
  vtkSHYXDataSetToPartitionedCollection(const vtkSHYXDataSetToPartitionedCollection&) = delete;
  void operator=(const vtkSHYXDataSetToPartitionedCollection&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
