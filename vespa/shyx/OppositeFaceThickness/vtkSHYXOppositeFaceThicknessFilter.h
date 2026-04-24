/**
 * @class   vtkSHYXOppositeFaceThicknessFilter
 * @brief   Per-point minimum Euclidean distance to mesh faces whose normals oppose the point normal;
 *          optional displacement along the point normal when that distance falls below a threshold.
 *
 * For each point P with unit normal Np, every 2D cell with unit normal Nc is a candidate if
 * Np·Nc <= OppositeNormalDotMax (default 0: opposite hemisphere including perpendicular boundaries).
 * The thickness at P is the minimum distance from P to such cells, excluding cells that use P as a
 * vertex (to avoid the local sheet). Results are stored in a new point-data scalar array.
 * When EnableThicknessRepair is on, points with finite thickness below ThicknessThreshold are moved
 * along +Np by (ThicknessThreshold - thickness), optionally capped by MaxDisplacement.
 *
 * Complexity is O(nPoints * nCells). Cell topology is read once (serial GetCell); thickness uses
 * vtkSMPTools over points with vtkPoints::GetPoint plus vtkTriangle / thread-local vtkPolygon. Repair
 * displacement uses vtkSMPTools over points.
 */

#ifndef vtkSHYXOppositeFaceThicknessFilter_h
#define vtkSHYXOppositeFaceThicknessFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXOppositeFaceThicknessFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXOPPOSITEFACETHICKNESSFILTER_EXPORT vtkSHYXOppositeFaceThicknessFilter
  : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXOppositeFaceThicknessFilter* New();
  vtkTypeMacro(vtkSHYXOppositeFaceThicknessFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Faces with dot(Np,Nc) <= OppositeNormalDotMax are considered "opposite" (default 0). */
  vtkSetClampMacro(OppositeNormalDotMax, double, -1.0, 1.0);
  vtkGetMacro(OppositeNormalDotMax, double);

  /** Name of the output scalar array (point data). */
  vtkSetStringMacro(ThicknessArrayName);
  vtkGetStringMacro(ThicknessArrayName);

  /** When on, point normals are read from PointNormalArrayName on the input; when off (default), vtkPolyDataNormals computes them. */
  vtkSetMacro(UseInputNormalArray, int);
  vtkGetMacro(UseInputNormalArray, int);
  vtkBooleanMacro(UseInputNormalArray, int);

  vtkSetStringMacro(PointNormalArrayName);
  vtkGetStringMacro(PointNormalArrayName);

  /** When on, points with thickness < ThicknessThreshold are shifted along +pointNormal. */
  vtkSetMacro(EnableThicknessRepair, int);
  vtkGetMacro(EnableThicknessRepair, int);
  vtkBooleanMacro(EnableThicknessRepair, int);

  vtkSetMacro(ThicknessThreshold, double);
  vtkGetMacro(ThicknessThreshold, double);

  /** Upper bound on repair motion along the normal (non-positive = no cap). */
  vtkSetMacro(MaxDisplacement, double);
  vtkGetMacro(MaxDisplacement, double);

  /** Value stored when no opposite-facing cell is found (default -1). */
  vtkSetMacro(InvalidThicknessValue, double);
  vtkGetMacro(InvalidThicknessValue, double);

protected:
  vtkSHYXOppositeFaceThicknessFilter();
  ~vtkSHYXOppositeFaceThicknessFilter() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double OppositeNormalDotMax = 0.0;
  char* ThicknessArrayName = nullptr;
  int UseInputNormalArray = 0;
  char* PointNormalArrayName = nullptr;
  int EnableThicknessRepair = 1;
  double ThicknessThreshold = 0.01;
  double MaxDisplacement = 0.0;
  double InvalidThicknessValue = -1.0;

private:
  vtkSHYXOppositeFaceThicknessFilter(const vtkSHYXOppositeFaceThicknessFilter&) = delete;
  void operator=(const vtkSHYXOppositeFaceThicknessFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
