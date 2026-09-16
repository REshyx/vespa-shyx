/**
 * @class   vtkSHYXVmtkCenterlineMerge
 * @brief   Optional VMTK branch extract, merge, and/or polyball surface from overlapping centerlines.
 *
 * Single input: centerline vtkPolyData (LINE / POLY_LINE) with a point-data radius array
 * (default MaximumInscribedSphereRadius). No vessel surface. Single output port.
 *
 * Three independent steps (all off = passthrough):
 * - ExtractCenterlineBranches: vtkvmtkCenterlineBranchExtractor (tracts + GroupIds / Blanking).
 *   The tract output is cached; toggling Merge / Reconstruct does not re-extract unless the
 *   input polydata or radius array changes.
 * - MergeCenterlines: vtkvmtkMergeCenterlines; UI only when Extract is on.
 * - SplitBySharedEndpoints: partition input polylines by shared endpoints, then extract/merge
 *   each cluster so Appended extra paths cannot glue distant inlet groups.
 * - ReconstructSurface: vtkvmtkPolyBallModeller + isosurface of whatever the current lines are.
 */

#ifndef vtkSHYXVmtkCenterlineMerge_h
#define vtkSHYXVmtkCenterlineMerge_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXVmtkCenterlineMergeModule.h"

#include <string>
#include <vector>

#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

class vtkDataArray;

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXVMTKCENTERLINEMERGE_EXPORT vtkSHYXVmtkCenterlineMerge : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXVmtkCenterlineMerge* New();
  vtkTypeMacro(vtkSHYXVmtkCenterlineMerge, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(ExtractCenterlineBranches, int);
  vtkGetMacro(ExtractCenterlineBranches, int);
  vtkBooleanMacro(ExtractCenterlineBranches, int);

  vtkSetMacro(MergeCenterlines, int);
  vtkGetMacro(MergeCenterlines, int);
  vtkBooleanMacro(MergeCenterlines, int);

  /** Absolute resampling length. <= 0 uses 1% of the input AABB longest side
   *  (same as BoundsDomain scaled_extent Reset). */
  vtkSetClampMacro(ResamplingStepLength, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(ResamplingStepLength, double);

  /** Include blanked bifurcation groups as a shared vertex on adjacent branches (default on). */
  vtkSetMacro(MergeBlanked, int);
  vtkGetMacro(MergeBlanked, int);
  vtkBooleanMacro(MergeBlanked, int);

  /** Partition by shared endpoints, then extract/merge each cluster (default on). */
  vtkSetMacro(SplitBySharedEndpoints, int);
  vtkGetMacro(SplitBySharedEndpoints, int);
  vtkBooleanMacro(SplitBySharedEndpoints, int);

  vtkSetMacro(ReconstructSurface, int);
  vtkGetMacro(ReconstructSurface, int);
  vtkBooleanMacro(ReconstructSurface, int);

  vtkSetVector3Macro(SampleDimensions, int);
  vtkGetVector3Macro(SampleDimensions, int);

  /** Polyball isosurface: dist^2 - r^2, so 0 is the tube wall; positive inflates, negative shrinks. */
  vtkSetMacro(IsoValue, double);
  vtkGetMacro(IsoValue, double);

protected:
  vtkSHYXVmtkCenterlineMerge();
  ~vtkSHYXVmtkCenterlineMerge() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;

  int ExtractCenterlineBranches = 1;
  int MergeCenterlines = 1;
  double ResamplingStepLength = 0.0;
  int MergeBlanked = 1;
  int SplitBySharedEndpoints = 1;
  int ReconstructSurface = 0;
  int SampleDimensions[3] = { 64, 64, 64 };
  double IsoValue = 0.0;

private:
  vtkSHYXVmtkCenterlineMerge(const vtkSHYXVmtkCenterlineMerge&) = delete;
  void operator=(const vtkSHYXVmtkCenterlineMerge&) = delete;

  bool CanReuseExtractCache(
    vtkPolyData* input, vtkDataArray* radiusArr, const char* radiusName) const;
  void CaptureExtractCacheKey(
    vtkPolyData* input, vtkDataArray* radiusArr, const char* radiusName);

  vtkNew<vtkPolyData> ExtractedCache;
  std::vector<vtkSmartPointer<vtkPolyData>> ExtractedClusterCache;
  int ExtractCacheSplitByEndpoints = -1;
  // Do not key on vtkPolyData::GetMTime(): the pipeline ShallowCopy into this
  // filter's input port calls Modified() on the wrapper every execute.
  vtkMTimeType ExtractCachePointsMTime = 0;
  vtkMTimeType ExtractCacheLinesMTime = 0;
  vtkMTimeType ExtractCacheRadiusMTime = 0;
  vtkIdType ExtractCacheInputNPoints = -1;
  vtkIdType ExtractCacheInputNCells = -1;
  std::string ExtractCacheRadiusName;
};

VTK_ABI_NAMESPACE_END
#endif
