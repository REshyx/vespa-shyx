/**
 * @class   vtkSHYXImageMorphology
 * @brief   Binary / grayscale morphology on vtkImageData point scalars.
 *
 * Structuring element: Box, Cross, or Ellipsoid (default) of odd KernelSize.
 * Operations: dilate, erode, open, close, morphological / internal / external
 * gradient, white / black top-hat, and binary hit-or-miss (foreground SE plus
 * a background shell). Not VTK Gradient Magnitude and not Median.
 */

#ifndef vtkSHYXImageMorphology_h
#define vtkSHYXImageMorphology_h

#include "vtkImageAlgorithm.h"
#include "vtkSHYXImageMorphologyModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXIMAGEMORPHOLOGY_EXPORT vtkSHYXImageMorphology : public vtkImageAlgorithm
{
public:
  static vtkSHYXImageMorphology* New();
  vtkTypeMacro(vtkSHYXImageMorphology, vtkImageAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  enum OperationType
  {
    DILATE = 0,
    ERODE = 1,
    OPEN = 2,
    CLOSE = 3,
    MORPH_GRADIENT = 4,
    INTERNAL_GRADIENT = 5,
    EXTERNAL_GRADIENT = 6,
    WHITE_TOPHAT = 7,
    BLACK_TOPHAT = 8,
    HIT_OR_MISS = 9
  };

  enum ValueModeType
  {
    BINARY = 0,
    GRAYSCALE = 1
  };

  enum KernelShapeType
  {
    BOX = 0,
    CROSS = 1,
    ELLIPSOID = 2
  };

  vtkSetClampMacro(Operation, int, DILATE, HIT_OR_MISS);
  vtkGetMacro(Operation, int);

  vtkSetClampMacro(ValueMode, int, BINARY, GRAYSCALE);
  vtkGetMacro(ValueMode, int);

  vtkSetClampMacro(KernelShape, int, BOX, ELLIPSOID);
  vtkGetMacro(KernelShape, int);

  void SetKernelSize(int sx, int sy, int sz);
  void SetKernelSize(const int sz[3]) { this->SetKernelSize(sz[0], sz[1], sz[2]); }
  vtkGetVector3Macro(KernelSize, int);

  vtkSetClampMacro(NumberOfIterations, int, 1, 64);
  vtkGetMacro(NumberOfIterations, int);

  vtkSetMacro(ForegroundValue, double);
  vtkGetMacro(ForegroundValue, double);

  vtkSetMacro(BackgroundValue, double);
  vtkGetMacro(BackgroundValue, double);

  void SetBackgroundKernelSize(int sx, int sy, int sz);
  void SetBackgroundKernelSize(const int sz[3])
  {
    this->SetBackgroundKernelSize(sz[0], sz[1], sz[2]);
  }
  vtkGetVector3Macro(BackgroundKernelSize, int);

protected:
  vtkSHYXImageMorphology();
  ~vtkSHYXImageMorphology() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestUpdateExtent(vtkInformation* request, vtkInformationVector** inputVector,
    vtkInformationVector* outputVector) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  int Operation = DILATE;
  int ValueMode = BINARY;
  int KernelShape = ELLIPSOID;
  int KernelSize[3] = { 3, 3, 3 };
  int NumberOfIterations = 1;
  double ForegroundValue = 1.0;
  double BackgroundValue = 0.0;
  int BackgroundKernelSize[3] = { 5, 5, 5 };

private:
  vtkSHYXImageMorphology(const vtkSHYXImageMorphology&) = delete;
  void operator=(const vtkSHYXImageMorphology&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
