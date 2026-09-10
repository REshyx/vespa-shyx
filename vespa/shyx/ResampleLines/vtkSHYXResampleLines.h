/**
 * @class   vtkSHYXResampleLines
 * @brief   Resample a line network at a fixed spacing, keeping junctions and endpoints.
 *
 * Builds an undirected graph from VTK_LINE / VTK_POLY_LINE cells. Vertices whose line degree
 * is not 2 (endpoints, junctions, isolated used points) are feature vertices and are copied
 * unchanged. Each branch between two features — and each all-degree-2 loop — is resampled at
 * SampleDistance along arc length. If a branch is shorter than or equal to SampleDistance, both
 * endpoints are kept as a single segment (the branch is never dropped for being too short).
 *
 * Optional Fuse (default on) merges points closer than FuseTolerance before walking the graph,
 * so nearly coincident endpoints become one vertex. FuseTolerance <= 0 (XML default) means
 * 1e-6 times the longest side of the input axis-aligned bounding box.
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

  /** When on (default), merge nearby points with vtkCleanPolyData before extracting branches. */
  vtkSetMacro(Fuse, int);
  vtkGetMacro(Fuse, int);
  vtkBooleanMacro(Fuse, int);

  /**
   * Absolute fuse distance. Values <= 0 (default) use 1e-6 times the longest AABB side
   * (same scale as ParaView BoundsDomain scaled_extent).
   */
  vtkSetClampMacro(FuseTolerance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(FuseTolerance, double);

  /**
   * Spacing along each branch. Values <= 0 use 0.01 times the longest AABB side
   * (BoundsDomain scaled_extent 0.01).
   */
  vtkSetClampMacro(SampleDistance, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(SampleDistance, double);

protected:
  vtkSHYXResampleLines();
  ~vtkSHYXResampleLines() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int Fuse = 1;
  double FuseTolerance = 0.0;
  double SampleDistance = 0.0;

private:
  vtkSHYXResampleLines(const vtkSHYXResampleLines&) = delete;
  void operator=(const vtkSHYXResampleLines&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
