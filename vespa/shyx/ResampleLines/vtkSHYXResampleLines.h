/**
 * @class   vtkSHYXResampleLines
 * @brief   Optional line-merge, fuse, then resample of a line network.
 *
 * Three independent steps (all default on). If all are off, the input is passed through.
 *
 * LineMerge pairwise-reduces VTK_LINE / VTK_POLY_LINE cells (binary tree: merge (0,1), (2,3),
 * ... then merge those results). In each pair the left network is kept; each right polyline is
 * compared to that network: a vertex whose distance is within LineMergeTolerance is overlapping.
 * Overlapping interior vertices are dropped; each remaining free run is kept as a branch and
 * its overlapping endpoints are snapped onto the earlier polyline (a vertex is inserted when the
 * closest point is in the interior of a segment). LineMergeTolerance <= 0 (XML default) means
 * 1e-4 times the longest side of the input axis-aligned bounding box.
 *
 * Fuse merges points closer than FuseTolerance so nearly coincident endpoints become one
 * vertex. FuseTolerance <= 0 (XML default) means 1e-6 times the longest side of the input
 * axis-aligned bounding box.
 *
 * Sample builds an undirected graph from VTK_LINE / VTK_POLY_LINE cells. Vertices whose line
 * degree is not 2 (endpoints, junctions, isolated used points) are feature vertices and are
 * copied unchanged. Each branch between two features — and each all-degree-2 loop — is
 * resampled at SampleDistance along arc length. If a branch is shorter than or equal to
 * SampleDistance, both endpoints are kept as a single segment (the branch is never dropped
 * for being too short).
 *
 * After the enabled steps, a 1-component point array named Degree stores each vertex's
 * undirected line degree (number of distinct incident VTK_LINE / VTK_POLY_LINE edges).
 *
 * @sa vtkCleanPolyData vtkSplineFilter
 */

#ifndef vtkSHYXResampleLines_h
#define vtkSHYXResampleLines_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXResampleLinesModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXRESAMPLELINES_EXPORT vtkSHYXResampleLines : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXResampleLines* New();
  vtkTypeMacro(vtkSHYXResampleLines, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * When on (default), pairwise-reduce polylines and snap a vertex onto the left network when it
   * lies on or within LineMergeTolerance. Independent of Fuse and Sample.
   */
  vtkSetMacro(LineMerge, int);
  vtkGetMacro(LineMerge, int);
  vtkBooleanMacro(LineMerge, int);

  /**
   * Absolute point-to-line distance for LineMerge. Values <= 0 (default) use 1e-4 times the
   * longest AABB side (same scale as ParaView BoundsDomain scaled_extent). Ignored when
   * LineMerge is off.
   */
  vtkSetClampMacro(LineMergeTolerance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(LineMergeTolerance, double);

  /** When on (default), merge nearby points with vtkCleanPolyData. Independent of Sample. */
  vtkSetMacro(Fuse, int);
  vtkGetMacro(Fuse, int);
  vtkBooleanMacro(Fuse, int);

  /**
   * Absolute fuse distance. Values <= 0 (default) use 1e-6 times the longest AABB side
   * (same scale as ParaView BoundsDomain scaled_extent). Ignored when Fuse is off.
   */
  vtkSetClampMacro(FuseTolerance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(FuseTolerance, double);

  /** When on (default), resample each branch at SampleDistance. Independent of Fuse. */
  vtkSetMacro(Sample, int);
  vtkGetMacro(Sample, int);
  vtkBooleanMacro(Sample, int);

  /**
   * Spacing along each branch. Values <= 0 use 0.01 times the longest AABB side
   * (BoundsDomain scaled_extent 0.01). Ignored when Sample is off.
   */
  vtkSetClampMacro(SampleDistance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(SampleDistance, double);

protected:
  vtkSHYXResampleLines();
  ~vtkSHYXResampleLines() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int LineMerge = 1;
  double LineMergeTolerance = 0.0;
  int Fuse = 1;
  double FuseTolerance = 0.0;
  int Sample = 1;
  double SampleDistance = 0.0;

private:
  vtkSHYXResampleLines(const vtkSHYXResampleLines&) = delete;
  void operator=(const vtkSHYXResampleLines&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
