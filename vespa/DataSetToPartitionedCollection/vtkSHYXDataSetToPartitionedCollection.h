/**
 * @class   vtkSHYXDataSetToPartitionedCollection
 * @brief   Build a vtkPartitionedDataSetCollection from vtkDataSet: tet block + surface partitions.
 *
 * Pipeline: (1) extract VTK_TETRA cells into one vtkUnstructuredGrid partition with object_id = 1
 * and contiguous point/cell GlobalIds (1..N) for vtkIOSSWriter; (2) vtkDataSetSurfaceFilter on
 * the tet-only mesh with PassThroughCellIds/PointIds; (3) boundary cell data element_side (volume
 * global cell id, Exodus tet face 1..4); (4) vtkPolyDataNormals splitting by feature angle; (5)
 * vtkPolyDataConnectivityFilter into regions; unreferenced points are stripped per region so
 * side{i} / node{i} only contain points used by that patch. node{i} matches side{i} (1:1 by point
 * id) with one vtkVertex per point. Surface point GlobalIds duplicate the volume mesh node ids so
 * vtkIOSSWriter NodeSet ids resolve to correct coordinates in the merged Exodus node block.
 * assembly IOSS / element_blocks, side_sets, node_sets.
 * object_id: tetra = 1; sides and nodes follow.
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

protected:
  vtkSHYXDataSetToPartitionedCollection();
  ~vtkSHYXDataSetToPartitionedCollection() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double FeatureAngle = 70.0;

private:
  vtkSHYXDataSetToPartitionedCollection(const vtkSHYXDataSetToPartitionedCollection&) = delete;
  void operator=(const vtkSHYXDataSetToPartitionedCollection&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
