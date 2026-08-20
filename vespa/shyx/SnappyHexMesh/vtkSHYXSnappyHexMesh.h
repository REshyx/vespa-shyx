/**
 * @class   vtkSHYXSnappyHexMesh
 * @brief   Hex-dominant volume mesh from a surface collection via snappyHexMesh.
 *
 * Input is a vtkPartitionedDataSetCollection (each partition becomes one STL /
 * triSurfaceMesh patch) or a single vtkPolyData (wrapped as partition "geometry").
 * Optional feature-edge vtkPolyData (VTK port 1, Properties pipeline dropdown)
 * is written as .eMesh. The OpenFOAM case is kept; the filter output is the
 * vtkMultiBlockDataSet from vtkOpenFOAMReader (internalMesh plus patches).
 */

#ifndef vtkSHYXSnappyHexMesh_h
#define vtkSHYXSnappyHexMesh_h

#include "vtkSHYXSnappyHexMeshModule.h"
#include "vtkDataObjectAlgorithm.h"

#include <vector>

class vtkAlgorithmOutput;

class VTKSHYXSNAPPYHEXMESH_EXPORT vtkSHYXSnappyHexMesh : public vtkDataObjectAlgorithm
{
public:
  static vtkSHYXSnappyHexMesh* New();
  vtkTypeMacro(vtkSHYXSnappyHexMesh, vtkDataObjectAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Optional feature-edge polydata (port 1). Null / empty is allowed. */
  void SetFeatureEdgesConnection(vtkAlgorithmOutput* algOutput);

  vtkGetMacro(CastellatedMesh, bool);
  vtkSetMacro(CastellatedMesh, bool);
  vtkBooleanMacro(CastellatedMesh, bool);

  vtkGetMacro(Snap, bool);
  vtkSetMacro(Snap, bool);
  vtkBooleanMacro(Snap, bool);

  vtkGetMacro(AddLayers, bool);
  vtkSetMacro(AddLayers, bool);
  vtkBooleanMacro(AddLayers, bool);

  vtkGetMacro(BackgroundCellSize, double);
  vtkSetClampMacro(BackgroundCellSize, double, 0.0, 1e12);

  vtkGetMacro(BoundsMargin, double);
  vtkSetClampMacro(BoundsMargin, double, 0.0, 10.0);

  vtkGetMacro(MaxGlobalCells, int);
  vtkSetClampMacro(MaxGlobalCells, int, 1000, 200000000);

  vtkGetMacro(NCellsBetweenLevels, int);
  vtkSetClampMacro(NCellsBetweenLevels, int, 1, 10);

  vtkGetMacro(RefinementMin, int);
  vtkSetClampMacro(RefinementMin, int, 0, 10);

  vtkGetMacro(RefinementMax, int);
  vtkSetClampMacro(RefinementMax, int, 0, 10);

  vtkGetMacro(NSmoothPatch, int);
  vtkSetClampMacro(NSmoothPatch, int, 0, 100);

  vtkGetMacro(SnapTolerance, double);
  vtkSetClampMacro(SnapTolerance, double, 0.0, 100.0);

  vtkGetMacro(NSolveIter, int);
  vtkSetClampMacro(NSolveIter, int, 0, 1000);

  vtkGetMacro(NRelaxIter, int);
  vtkSetClampMacro(NRelaxIter, int, 0, 100);

  vtkGetMacro(NSurfaceLayers, int);
  vtkSetClampMacro(NSurfaceLayers, int, 0, 50);

  vtkGetMacro(ExpansionRatio, double);
  vtkSetClampMacro(ExpansionRatio, double, 1.0, 10.0);

  vtkGetMacro(FinalLayerThickness, double);
  vtkSetClampMacro(FinalLayerThickness, double, 1e-6, 10.0);

  vtkGetMacro(MinThickness, double);
  vtkSetClampMacro(MinThickness, double, 1e-6, 10.0);

  vtkGetMacro(FeatureAngle, double);
  vtkSetClampMacro(FeatureAngle, double, 0.0, 180.0);

  vtkGetMacro(ImplicitFeatureSnap, bool);
  vtkSetMacro(ImplicitFeatureSnap, bool);
  vtkBooleanMacro(ImplicitFeatureSnap, bool);

  vtkGetMacro(FeatureLevel, int);
  vtkSetClampMacro(FeatureLevel, int, 0, 10);

  /** Optional OpenFOAM case root. Empty = %TEMP%/shyx-snappy-<id>-<mtime>/case. */
  vtkSetStringMacro(CaseDirectory);
  vtkGetStringMacro(CaseDirectory);

  /** Directory actually written this Apply (user Case Directory or auto temp). */
  vtkGetStringMacro(CaseFoamPath);

  /** Newline-separated refinementSurfaces names (table order). Empty = all partitions. */
  vtkSetStringMacro(SurfaceNames);
  vtkGetStringMacro(SurfaceNames);
  vtkSetStringMacro(SurfaceLevelMin);
  vtkGetStringMacro(SurfaceLevelMin);
  vtkSetStringMacro(SurfaceLevelMax);
  vtkGetStringMacro(SurfaceLevelMax);
  vtkSetStringMacro(SurfacePatchTypes);
  vtkGetStringMacro(SurfacePatchTypes);

  /** Newline-separated refinementRegions rows. Empty = none. */
  vtkSetStringMacro(RegionNames);
  vtkGetStringMacro(RegionNames);
  vtkSetStringMacro(RegionModes);
  vtkGetStringMacro(RegionModes);
  vtkSetStringMacro(RegionLevels);
  vtkGetStringMacro(RegionLevels);
  vtkSetStringMacro(RegionDistances);
  vtkGetStringMacro(RegionDistances);

  /** Newline-separated addLayers patch rows. Empty = all surface patches. */
  vtkSetStringMacro(LayerNames);
  vtkGetStringMacro(LayerNames);
  vtkSetStringMacro(LayerNSurfaceLayers);
  vtkGetStringMacro(LayerNSurfaceLayers);

  /** Keep-mesh points (OpenFOAM locationInMesh / locationsInMesh). Empty = AABB centre. */
  void SetNumberOfInsidePoints(int n);
  int GetNumberOfInsidePoints() const;
  void SetInsidePoint(int i, double x, double y, double z);
  void SetInsidePoint(int i, const double xyz[3]);
  double* GetInsidePoint(int i);
  void GetInsidePoint(int i, double xyz[3]);

protected:
  vtkSHYXSnappyHexMesh();
  ~vtkSHYXSnappyHexMesh() override;

  /** Updates CaseFoamPath without calling Modified() (avoids update loops). */
  void SetCaseFoamPathNoModified(const char* msg);

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  bool CastellatedMesh = true;
  bool Snap = true;
  bool AddLayers = true;
  double BackgroundCellSize = 0.0;
  double BoundsMargin = 0.05;
  int MaxGlobalCells = 2000000;
  int NCellsBetweenLevels = 3;
  int RefinementMin = 0;
  int RefinementMax = 2;
  int NSmoothPatch = 3;
  double SnapTolerance = 2.0;
  int NSolveIter = 30;
  int NRelaxIter = 5;
  int NSurfaceLayers = 3;
  double ExpansionRatio = 1.2;
  double FinalLayerThickness = 0.3;
  double MinThickness = 0.1;
  double FeatureAngle = 30.0;
  bool ImplicitFeatureSnap = true;
  int FeatureLevel = 2;
  char* CaseDirectory = nullptr;
  char* CaseFoamPath = nullptr;
  char* SurfaceNames = nullptr;
  char* SurfaceLevelMin = nullptr;
  char* SurfaceLevelMax = nullptr;
  char* SurfacePatchTypes = nullptr;
  char* RegionNames = nullptr;
  char* RegionModes = nullptr;
  char* RegionLevels = nullptr;
  char* RegionDistances = nullptr;
  char* LayerNames = nullptr;
  char* LayerNSurfaceLayers = nullptr;
  std::vector<double> InsidePoints;

private:
  vtkSHYXSnappyHexMesh(const vtkSHYXSnappyHexMesh&) = delete;
  void operator=(const vtkSHYXSnappyHexMesh&) = delete;
};

#endif
