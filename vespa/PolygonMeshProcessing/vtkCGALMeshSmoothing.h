/**
 * @class   vtkCGALMeshSmoothing
 * @brief   Smoothes a surface mesh by moving its vertices.
 *
 * vtkCGALMeshSmoothing is a filter that smoothes the overall shape of a 3D mesh
 * using the mean curvature.
 * The degree of smoothing can be controlled using the number of iterations as well as
 * the time step. The time step specifies the smoothing speed.
 * A higher time step results in a stronger shape distortion than a higher number of iterations.
 */

#ifndef vtkCGALMeshSmoothing_h
#define vtkCGALMeshSmoothing_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPMPModule.h" // For export macro

class VTKCGALPMP_EXPORT vtkCGALMeshSmoothing : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALMeshSmoothing* New();
  vtkTypeMacro(vtkCGALMeshSmoothing, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(NumberOfIterations, unsigned int);
  vtkSetMacro(NumberOfIterations, unsigned int);
  ///@}

  //@{
  /**
   * Get / Set UseSafetyConstraints.
   * Default is false.
   **/
  vtkGetMacro(UseSafetyConstraints, bool);
  vtkSetMacro(UseSafetyConstraints, bool);
  vtkBooleanMacro(UseSafetyConstraints, bool);
  //@}

protected:
  vtkCGALMeshSmoothing()           = default;
  ~vtkCGALMeshSmoothing() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  unsigned int NumberOfIterations = 10;
  bool UseSafetyConstraints = false;

private:
  vtkCGALMeshSmoothing(const vtkCGALMeshSmoothing&) = delete;
  void operator=(const vtkCGALMeshSmoothing&)       = delete;
};

#endif
