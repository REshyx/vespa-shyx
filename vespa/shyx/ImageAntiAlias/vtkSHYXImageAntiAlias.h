/**
 * @class   vtkSHYXImageAntiAlias
 * @brief   Whitaker anti-alias of a binary vtkImageData (ITK via VMTK).
 *
 * Wraps itk::AntiAliasBinaryImageFilter using the ITK installed with VMTK
 * (same prefix as VMTK_DIR) and vtkvmtkITKFilterUtilities for VTK↔ITK.
 * Optional vtkImageResample first (ResampleFactor: >1 refine, <1 coarsen, 1 skip).
 * Output is a float level set on that grid (inside > 0, outside < 0); extract
 * the surface with Contour at 0. Not morphology and not Gaussian blur.
 */

#ifndef vtkSHYXImageAntiAlias_h
#define vtkSHYXImageAntiAlias_h

#include "vtkImageAlgorithm.h"
#include "vtkSHYXImageAntiAliasModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXIMAGEANTIALIAS_EXPORT vtkSHYXImageAntiAlias : public vtkImageAlgorithm
{
public:
  static vtkSHYXImageAntiAlias* New();
  vtkTypeMacro(vtkSHYXImageAntiAlias, vtkImageAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum BinarizeModeType
  {
    AUTO_MIN_MAX = 0,
    THRESHOLD = 1
  };

  enum ResampleInterpolationType
  {
    NEAREST = 0,
    LINEAR = 1
  };

  vtkSetClampMacro(BinarizeMode, int, AUTO_MIN_MAX, THRESHOLD);
  vtkGetMacro(BinarizeMode, int);

  vtkSetMacro(Threshold, double);
  vtkGetMacro(Threshold, double);

  vtkSetClampMacro(NumberOfIterations, int, 1, 10000);
  vtkGetMacro(NumberOfIterations, int);

  vtkSetClampMacro(MaximumRMSError, double, 1.0e-6, 1.0);
  vtkGetMacro(MaximumRMSError, double);

  vtkSetMacro(UseImageSpacing, vtkTypeBool);
  vtkGetMacro(UseImageSpacing, vtkTypeBool);
  vtkBooleanMacro(UseImageSpacing, vtkTypeBool);

  vtkSetClampMacro(ResampleFactor, double, 0.25, 8.0);
  vtkGetMacro(ResampleFactor, double);

  vtkSetClampMacro(ResampleInterpolation, int, NEAREST, LINEAR);
  vtkGetMacro(ResampleInterpolation, int);

protected:
  vtkSHYXImageAntiAlias();
  ~vtkSHYXImageAntiAlias() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestInformation(vtkInformation* request, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override;
  int RequestUpdateExtent(vtkInformation* request, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int BinarizeMode = THRESHOLD;
  double Threshold = 0.5;
  int NumberOfIterations = 1000;
  double MaximumRMSError = 0.07;
  vtkTypeBool UseImageSpacing = 0;
  double ResampleFactor = 1.0;
  int ResampleInterpolation = NEAREST;

private:
  vtkSHYXImageAntiAlias(const vtkSHYXImageAntiAlias&) = delete;
  void operator=(const vtkSHYXImageAntiAlias&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
