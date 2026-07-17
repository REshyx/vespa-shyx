/**
 * @class   vtkSHYXPartitionedCollectionBoundaryFields
 * @brief   Modify vtkPartitionedDataSetCollection boundary fields, names, and volume accumulations.
 *
 * Intended downstream of vtkSHYXDataSetToPartitionedCollection (or compatible IOSS-style
 * vtkPartitionedDataSetCollection input). For each side set: optionally compute
 * BoundaryRadialValue, write BoundaryRadialValueNormal and BoundaryVariable* point arrays
 * (overwriting same-named arrays), mirror point data onto paired node sets, and optionally
 * accumulate normals / variables onto the tetrahedra element block. Block names and assembly
 * labels can be edited via the Partitioned block names panel.
 */

#ifndef vtkSHYXPartitionedCollectionBoundaryFields_h
#define vtkSHYXPartitionedCollectionBoundaryFields_h

#include "vtkDataObjectAlgorithm.h"
#include "vtkSHYXPartitionedCollectionBoundaryFieldsModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkSHYXPartitionedCollectionBoundaryFields : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXPartitionedCollectionBoundaryFields* New();
  vtkTypeMacro(vtkSHYXPartitionedCollectionBoundaryFields, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * When non-zero, BoundaryRadialValueNormal uses BoundaryRadialValue * sideNormal. Otherwise it
   * uses sideNormal and BoundaryRadialValue is not computed.
   */
  vtkSetMacro(ComputeBoundaryRadialValue, int);
  vtkGetMacro(ComputeBoundaryRadialValue, int);
  vtkBooleanMacro(ComputeBoundaryRadialValue, int);

  /** Exponent a used to convert raw radial coordinate x to BoundaryRadialValue = 1 - x^a. */
  vtkSetMacro(BoundaryRadialNormalFalloffFactor, double);
  vtkGetMacro(BoundaryRadialNormalFalloffFactor, double);

  /**
   * Newline-separated values aligned with the Partitioned block names panel order (tetrahedra,
   * side sets, mirrored node sets). Values in each row are tab-separated Variable1, Variable2, ...
   * entries. Only side-set entries are used. Finite side values accumulate onto volume points as
   * BoundaryVariable1, BoundaryVariable2, ... (initialized to NaN; NaN / empty cells are not written;
   * existing arrays are overwritten). Repeated writes to the same volume node are warned and
   * averaged, same as BoundaryRadialValueNormal.
   */
  vtkSetStringMacro(BoundaryVariables);
  vtkGetStringMacro(BoundaryVariables);

  /**
   * Newline-separated 0/1 flags aligned with the Partitioned block names panel order. Only side-set
   * rows are used. When non-zero, that side's BoundaryRadialValueNormal is accumulated onto
   * tetrahedra volume points (independent of BoundaryVariables). Volume normals are initialized to
   * NaN; unchecked sides leave those points empty.
   */
  vtkSetStringMacro(BoundaryWriteNormals);
  vtkGetStringMacro(BoundaryWriteNormals);

  /**
   * Newline-separated names aligned with the Partitioned block names panel order:
   * tetrahedra (when present), all side sets, then mirrored node sets. Only side-set names are
   * editable in the custom widget; corresponding node sets use the same base name with a \c node_
   * prefix.
   */
  vtkSetStringMacro(BlockNames);
  vtkGetStringMacro(BlockNames);

protected:
  vtkSHYXPartitionedCollectionBoundaryFields();
  ~vtkSHYXPartitionedCollectionBoundaryFields() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int ComputeBoundaryRadialValue = 0;
  double BoundaryRadialNormalFalloffFactor = 1.0;
  char* BoundaryVariables = nullptr;
  char* BoundaryWriteNormals = nullptr;
  char* BlockNames = nullptr;

private:
  vtkSHYXPartitionedCollectionBoundaryFields(
    const vtkSHYXPartitionedCollectionBoundaryFields&) = delete;
  void operator=(const vtkSHYXPartitionedCollectionBoundaryFields&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
