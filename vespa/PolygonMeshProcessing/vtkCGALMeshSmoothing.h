/**
 * @class   vtkCGALMeshSmoothing
 * @brief   Smoothes a surface mesh by moving its vertices.
 *
 * vtkCGALMeshSmoothing is a filter that moves vertices to optimize
 * geometry around each vertex: it can try to equalize the angles between
 * incident edges, or (and) move vertices so that areas of adjacent
 * triangles tend to equalize. Border vertices are considered constrained
 * and do not move at any step of the procedure. No vertices are inserted
 * at any time.
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
