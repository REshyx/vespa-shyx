/**
 * @class   vtkSHYXExtendedFeatureEdgeMesh
 * @brief   OpenFOAM extendedFeatureEdgeMesh extraction (statically linked).
 *
 * Input surface triangles are classified with Foam::surfaceFeatures and
 * Foam::extendedFeatureEdgeMesh (same path as surfaceFeatureExtract). Output is
 * vtkPolyData lines (no extra VERTEX cells) with OpenFOAM edge/point
 * status arrays and a FoamFile blob so SHYX SnappyHexMesh can write
 * constant/extendedFeatureEdgeMesh.
 *
 * Optional port 1 adds extra polylines (merged with extendedEdgeMesh::add).
 */

#ifndef vtkSHYXExtendedFeatureEdgeMesh_h
#define vtkSHYXExtendedFeatureEdgeMesh_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXExtendedFeatureEdgeMeshModule.h"

class vtkAlgorithmOutput;

class VTKSHYXEXTENDEDFEATUREEDGEMESH_EXPORT vtkSHYXExtendedFeatureEdgeMesh
  : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXExtendedFeatureEdgeMesh* New();
  vtkTypeMacro(vtkSHYXExtendedFeatureEdgeMesh, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetClampMacro(IncludedAngle, double, 0.0, 180.0);
  vtkGetMacro(IncludedAngle, double);

  vtkSetMacro(GeometricTestOnly, int);
  vtkGetMacro(GeometricTestOnly, int);
  vtkBooleanMacro(GeometricTestOnly, int);

  vtkSetClampMacro(TrimMinLength, double, 0.0, VTK_DOUBLE_MAX);
  vtkGetMacro(TrimMinLength, double);

  vtkSetClampMacro(TrimMinElements, int, 0, VTK_INT_MAX);
  vtkGetMacro(TrimMinElements, int);

  vtkSetMacro(KeepOpenEdges, int);
  vtkGetMacro(KeepOpenEdges, int);
  vtkBooleanMacro(KeepOpenEdges, int);

  vtkSetMacro(KeepNonManifoldEdges, int);
  vtkGetMacro(KeepNonManifoldEdges, int);
  vtkBooleanMacro(KeepNonManifoldEdges, int);

  vtkSetMacro(KeepRegionEdges, int);
  vtkGetMacro(KeepRegionEdges, int);
  vtkBooleanMacro(KeepRegionEdges, int);

  vtkSetMacro(BaffleAllRegions, int);
  vtkGetMacro(BaffleAllRegions, int);
  vtkBooleanMacro(BaffleAllRegions, int);

  vtkSetStringMacro(RegionArrayName);
  vtkGetStringMacro(RegionArrayName);

  void SetExtraFeatureEdgesConnection(vtkAlgorithmOutput* algOutput);

protected:
  vtkSHYXExtendedFeatureEdgeMesh();
  ~vtkSHYXExtendedFeatureEdgeMesh() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double IncludedAngle = 150.0;
  int GeometricTestOnly = 0;
  double TrimMinLength = 0.0;
  int TrimMinElements = 0;
  int KeepOpenEdges = 1;
  int KeepNonManifoldEdges = 1;
  int KeepRegionEdges = 1;
  int BaffleAllRegions = 0;
  char* RegionArrayName = nullptr;

private:
  vtkSHYXExtendedFeatureEdgeMesh(const vtkSHYXExtendedFeatureEdgeMesh&) = delete;
  void operator=(const vtkSHYXExtendedFeatureEdgeMesh&) = delete;
};

#endif
