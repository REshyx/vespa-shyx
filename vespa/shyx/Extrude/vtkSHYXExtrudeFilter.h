/**
 * @class   vtkSHYXExtrudeFilter
 * @brief   Point vs Poly extrusion with Threshold mask, vtkSelection, and Front/Side/Back output.
 *
 * Type Point moves shared vertices along point normals (or a point vector array). With only
 * Output Front, coordinates change in place (neighbors stretch on a subset). Turning on Side
 * or Back switches to a connected-patch extrusion: one offset copy per region vertex, optional
 * original faces (Back; reversed if Front is also on), and a single ring of side quads on the
 * patch boundary (Side).
 *
 * Type Poly extrudes each selected polygon independently along its face normal (unique vertices
 * per face). Side walls attach to the original edge vertices; adjacent selected faces do not
 * share cap vertices.
 *
 * Region: vtkSelection on port 1, else a point- or cell-data array with vtkThreshold-style
 * Lower / Between / Upper, else all. Invert flips the resolved set.
 */

#ifndef vtkSHYXExtrudeFilter_h
#define vtkSHYXExtrudeFilter_h

#include "vtkPolyDataAlgorithm.h"
#include "vtkSHYXExtrudeFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXEXTRUDEFILTER_EXPORT vtkSHYXExtrudeFilter : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXExtrudeFilter* New();
  vtkTypeMacro(vtkSHYXExtrudeFilter, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum ExtrudeTypeEnum
  {
    EXTRUDE_POINT = 0,
    EXTRUDE_POLY = 1
  };

  enum ThresholdMethodEnum
  {
    THRESHOLD_BETWEEN = 0,
    THRESHOLD_BELOW_LOWER = 1,
    THRESHOLD_ABOVE_UPPER = 2
  };

  /** Selection input (port 1): vtkSelection, same pattern as SHYX Selection Extrude. */
  void SetSourceConnection(vtkAlgorithmOutput* algOutput);

  vtkSetClampMacro(ExtrudeType, int, EXTRUDE_POINT, EXTRUDE_POLY);
  vtkGetMacro(ExtrudeType, int);

  vtkSetMacro(OutputFront, int);
  vtkGetMacro(OutputFront, int);
  vtkBooleanMacro(OutputFront, int);

  vtkSetMacro(OutputSide, int);
  vtkGetMacro(OutputSide, int);
  vtkBooleanMacro(OutputSide, int);

  vtkSetMacro(OutputBack, int);
  vtkGetMacro(OutputBack, int);
  vtkBooleanMacro(OutputBack, int);

  vtkSetMacro(ExtrusionDistance, double);
  vtkGetMacro(ExtrusionDistance, double);

  vtkSetMacro(FlipDirection, int);
  vtkGetMacro(FlipDirection, int);
  vtkBooleanMacro(FlipDirection, int);

  vtkSetMacro(UseNormalsForDirection, int);
  vtkGetMacro(UseNormalsForDirection, int);
  vtkBooleanMacro(UseNormalsForDirection, int);

  vtkSetStringMacro(DirectionArrayName);
  vtkGetStringMacro(DirectionArrayName);

  vtkSetMacro(UseDistanceMultiplierArray, int);
  vtkGetMacro(UseDistanceMultiplierArray, int);
  vtkBooleanMacro(UseDistanceMultiplierArray, int);

  vtkSetStringMacro(DistanceMultiplierArrayName);
  vtkGetStringMacro(DistanceMultiplierArrayName);

  vtkSetStringMacro(MaskArrayName);
  vtkGetStringMacro(MaskArrayName);

  vtkSetClampMacro(ThresholdMethod, int, THRESHOLD_BETWEEN, THRESHOLD_ABOVE_UPPER);
  vtkGetMacro(ThresholdMethod, int);

  vtkSetMacro(LowerThreshold, double);
  vtkGetMacro(LowerThreshold, double);

  vtkSetMacro(UpperThreshold, double);
  vtkGetMacro(UpperThreshold, double);

  vtkSetMacro(AllScalars, int);
  vtkGetMacro(AllScalars, int);
  vtkBooleanMacro(AllScalars, int);

  vtkSetMacro(Invert, int);
  vtkGetMacro(Invert, int);
  vtkBooleanMacro(Invert, int);

protected:
  vtkSHYXExtrudeFilter();
  ~vtkSHYXExtrudeFilter() override;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int ExtrudeType = EXTRUDE_POINT;
  int OutputFront = 1;
  int OutputSide = 0;
  int OutputBack = 0;
  double ExtrusionDistance = 1.0;
  int FlipDirection = 0;
  int UseNormalsForDirection = 1;
  int UseDistanceMultiplierArray = 0;
  char* DirectionArrayName = nullptr;
  char* DistanceMultiplierArrayName = nullptr;
  char* MaskArrayName = nullptr;
  int ThresholdMethod = THRESHOLD_BETWEEN;
  double LowerThreshold = 0.0;
  double UpperThreshold = 1.0;
  int AllScalars = 0;
  int Invert = 0;

private:
  vtkSHYXExtrudeFilter(const vtkSHYXExtrudeFilter&) = delete;
  void operator=(const vtkSHYXExtrudeFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
