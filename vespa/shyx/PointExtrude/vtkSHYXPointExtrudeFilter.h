/**
 * @class   vtkSHYXPointExtrudeFilter
 * @brief   Per-point extrusion: offset each vertex along surface normals or a chosen 3D vector array.
 *
 * The signed distance along the direction is `ExtrusionDistance` times an optional per-point scalar
 * multiplier array. Directions from a point array are normalized before applying distance; computed
 * normals from vtkPolyDataNormals are already unit vectors on valid geometry.
 */

#ifndef vtkSHYXPointExtrudeFilter_h
#define vtkSHYXPointExtrudeFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXPointExtrudeFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXPOINTEXTRUDEFILTER_EXPORT vtkSHYXPointExtrudeFilter : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXPointExtrudeFilter* New();
  vtkTypeMacro(vtkSHYXPointExtrudeFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /** Base extrusion length; multiplied by optional per-point scalar array when enabled. */
  vtkSetMacro(ExtrusionDistance, double);
  vtkGetMacro(ExtrusionDistance, double);

  /** When true (default), directions come from vtkPolyDataNormals on the input. When false,
   *  DirectionArrayName must name a point vector array (3 components). */
  vtkSetMacro(UseNormalsForDirection, int);
  vtkGetMacro(UseNormalsForDirection, int);
  vtkBooleanMacro(UseNormalsForDirection, int);

  vtkSetStringMacro(DirectionArrayName);
  vtkGetStringMacro(DirectionArrayName);

  /** When true, each point's displacement is ExtrusionDistance * scalar from DistanceMultiplierArrayName. */
  vtkSetMacro(UseDistanceMultiplierArray, int);
  vtkGetMacro(UseDistanceMultiplierArray, int);
  vtkBooleanMacro(UseDistanceMultiplierArray, int);

  vtkSetStringMacro(DistanceMultiplierArrayName);
  vtkGetStringMacro(DistanceMultiplierArrayName);

  /** Negate the direction before applying distance. */
  vtkSetMacro(FlipDirection, int);
  vtkGetMacro(FlipDirection, int);
  vtkBooleanMacro(FlipDirection, int);

protected:
  vtkSHYXPointExtrudeFilter();
  ~vtkSHYXPointExtrudeFilter() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  double ExtrusionDistance = 1.0;
  int UseNormalsForDirection = 1;
  int UseDistanceMultiplierArray = 0;
  int FlipDirection = 0;
  char* DirectionArrayName = nullptr;
  char* DistanceMultiplierArrayName = nullptr;

private:
  vtkSHYXPointExtrudeFilter(const vtkSHYXPointExtrudeFilter&) = delete;
  void operator=(const vtkSHYXPointExtrudeFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
