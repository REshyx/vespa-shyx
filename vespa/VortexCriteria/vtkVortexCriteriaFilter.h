/**
 * @class   vtkVortexCriteriaFilter
 * @brief   Compute flow quantities and vortex identification criteria from velocity field
 *
 * Optional outputs: Velocity, Vorticity, VelocityGradient, StrainRate, RotationTensor,
 * Q-criterion, Lambda2, SwirlingStrength, Liutex, Omega, Helicity, NormalizedHelicity.
 */

#ifndef vtkVortexCriteriaFilter_h
#define vtkVortexCriteriaFilter_h

#include "vtkDataSetAlgorithm.h"
#include "vtkVortexCriteriaFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class VTKVORTEXCRITERIAFILTER_EXPORT vtkVortexCriteriaFilter : public vtkDataSetAlgorithm
{
public:
  static vtkVortexCriteriaFilter* New();
  vtkTypeMacro(vtkVortexCriteriaFilter, vtkDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetStringMacro(VelocityArrayName);
  vtkGetStringMacro(VelocityArrayName);

  vtkSetMacro(Epsilon, double);
  vtkGetMacro(Epsilon, double);

  vtkSetMacro(ComputeVelocity, vtkTypeBool);
  vtkGetMacro(ComputeVelocity, vtkTypeBool);
  vtkBooleanMacro(ComputeVelocity, vtkTypeBool);

  vtkSetMacro(ComputeVorticity, vtkTypeBool);
  vtkGetMacro(ComputeVorticity, vtkTypeBool);
  vtkBooleanMacro(ComputeVorticity, vtkTypeBool);

  vtkSetMacro(ComputeGradient, vtkTypeBool);
  vtkGetMacro(ComputeGradient, vtkTypeBool);
  vtkBooleanMacro(ComputeGradient, vtkTypeBool);

  vtkSetMacro(ComputeStrainRate, vtkTypeBool);
  vtkGetMacro(ComputeStrainRate, vtkTypeBool);
  vtkBooleanMacro(ComputeStrainRate, vtkTypeBool);

  vtkSetMacro(ComputeRotationTensor, vtkTypeBool);
  vtkGetMacro(ComputeRotationTensor, vtkTypeBool);
  vtkBooleanMacro(ComputeRotationTensor, vtkTypeBool);

  vtkSetMacro(ComputeQCriterion, vtkTypeBool);
  vtkGetMacro(ComputeQCriterion, vtkTypeBool);
  vtkBooleanMacro(ComputeQCriterion, vtkTypeBool);

  vtkSetMacro(ComputeLambda2, vtkTypeBool);
  vtkGetMacro(ComputeLambda2, vtkTypeBool);
  vtkBooleanMacro(ComputeLambda2, vtkTypeBool);

  vtkSetMacro(ComputeSwirlingStrength, vtkTypeBool);
  vtkGetMacro(ComputeSwirlingStrength, vtkTypeBool);
  vtkBooleanMacro(ComputeSwirlingStrength, vtkTypeBool);

  vtkSetMacro(ComputeLiutex, vtkTypeBool);
  vtkGetMacro(ComputeLiutex, vtkTypeBool);
  vtkBooleanMacro(ComputeLiutex, vtkTypeBool);

  vtkSetMacro(ComputeOmegaMethod, vtkTypeBool);
  vtkGetMacro(ComputeOmegaMethod, vtkTypeBool);
  vtkBooleanMacro(ComputeOmegaMethod, vtkTypeBool);

  vtkSetMacro(ComputeHelicity, vtkTypeBool);
  vtkGetMacro(ComputeHelicity, vtkTypeBool);
  vtkBooleanMacro(ComputeHelicity, vtkTypeBool);

protected:
  vtkVortexCriteriaFilter();
  ~vtkVortexCriteriaFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

  char* VelocityArrayName;
  double Epsilon;

  vtkTypeBool ComputeVelocity;
  vtkTypeBool ComputeVorticity;
  vtkTypeBool ComputeGradient;
  vtkTypeBool ComputeStrainRate;
  vtkTypeBool ComputeRotationTensor;
  vtkTypeBool ComputeQCriterion;
  vtkTypeBool ComputeLambda2;
  vtkTypeBool ComputeSwirlingStrength;
  vtkTypeBool ComputeLiutex;
  vtkTypeBool ComputeOmegaMethod;
  vtkTypeBool ComputeHelicity;

private:
  vtkVortexCriteriaFilter(const vtkVortexCriteriaFilter&) = delete;
  void operator=(const vtkVortexCriteriaFilter&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
