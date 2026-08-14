/**
 * @class   vtkSHYXSnappyHexMesh
 * @brief   Hex-dominant volume mesh from a closed surface via snappyHexMesh.
 *
 * Input vtkPolyData (closed triangulated surface). Output vtkUnstructuredGrid
 * (hexahedra / polyhedra / prisms after snap and layer addition).
 */

#ifndef vtkSHYXSnappyHexMesh_h
#define vtkSHYXSnappyHexMesh_h

#include "vtkSHYXSnappyHexMeshModule.h"
#include "vtkDataSetAlgorithm.h"

class VTKSHYXSNAPPYHEXMESH_EXPORT vtkSHYXSnappyHexMesh : public vtkDataSetAlgorithm
{
public:
  static vtkSHYXSnappyHexMesh* New();
  vtkTypeMacro(vtkSHYXSnappyHexMesh, vtkDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

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

protected:
  vtkSHYXSnappyHexMesh();
  ~vtkSHYXSnappyHexMesh() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
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

private:
  vtkSHYXSnappyHexMesh(const vtkSHYXSnappyHexMesh&) = delete;
  void operator=(const vtkSHYXSnappyHexMesh&) = delete;
};

#endif
