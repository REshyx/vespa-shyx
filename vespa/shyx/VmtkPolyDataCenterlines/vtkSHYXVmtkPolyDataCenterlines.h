/**
 * @class   vtkSHYXVmtkPolyDataCenterlines
 * @brief   Wrapper around VMTK vtkvmtkPolyDataCenterlines for tubular surface centerlines.
 *
 * Port 0: vessel lumen surface (vtkPolyData). Point GlobalIds are required (same scheme as seeds).
 * Port 1 / 2: seed vtkDataSet (e.g. poly point cloud, vtkUnstructuredGrid vertices only); only
 * point data and point GlobalIds are used. Each point must carry GlobalIds whose values refer to
 * surface points by global id (not by local point index on the seed dataset).
 *
 * Internally sets a non-empty RadiusArrayName for the Voronoi circumsphere field (VMTK
 * requires this name even when Delaunay/Voronoi are generated automatically).
 *
 * @sa vtkvmtkPolyDataCenterlines
 */

#ifndef vtkSHYXVmtkPolyDataCenterlines_h
#define vtkSHYXVmtkPolyDataCenterlines_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXVmtkPolyDataCenterlinesModule.h"

class vtkAlgorithmOutput;

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXVMTKPOLYDATACENTERLINES_EXPORT vtkSHYXVmtkPolyDataCenterlines : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXVmtkPolyDataCenterlines* New();
  vtkTypeMacro(vtkSHYXVmtkPolyDataCenterlines, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  void SetSourceSeedsConnection(vtkAlgorithmOutput* output);
  void SetTargetSeedsConnection(vtkAlgorithmOutput* output);

  vtkSetMacro(FlipNormals, int);
  vtkGetMacro(FlipNormals, int);
  vtkBooleanMacro(FlipNormals, int);

  vtkSetMacro(DelaunayTolerance, double);
  vtkGetMacro(DelaunayTolerance, double);

  vtkSetMacro(CenterlineResampling, int);
  vtkGetMacro(CenterlineResampling, int);
  vtkBooleanMacro(CenterlineResampling, int);

  vtkSetMacro(ResamplingStepLength, double);
  vtkGetMacro(ResamplingStepLength, double);

  vtkSetMacro(AppendEndPointsToCenterlines, int);
  vtkGetMacro(AppendEndPointsToCenterlines, int);
  vtkBooleanMacro(AppendEndPointsToCenterlines, int);

  vtkSetMacro(SimplifyVoronoi, int);
  vtkGetMacro(SimplifyVoronoi, int);
  vtkBooleanMacro(SimplifyVoronoi, int);

  vtkSetMacro(StopFastMarchingOnReachingTarget, int);
  vtkGetMacro(StopFastMarchingOnReachingTarget, int);
  vtkBooleanMacro(StopFastMarchingOnReachingTarget, int);

protected:
  vtkSHYXVmtkPolyDataCenterlines();
  ~vtkSHYXVmtkPolyDataCenterlines() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkSHYXVmtkPolyDataCenterlines(const vtkSHYXVmtkPolyDataCenterlines&) = delete;
  void operator=(const vtkSHYXVmtkPolyDataCenterlines&) = delete;

  int FlipNormals = 0;
  double DelaunayTolerance = 1e-3;
  int CenterlineResampling = 0;
  double ResamplingStepLength = 1.0;
  int AppendEndPointsToCenterlines = 0;
  int SimplifyVoronoi = 0;
  int StopFastMarchingOnReachingTarget = 0;
};

VTK_ABI_NAMESPACE_END
#endif
