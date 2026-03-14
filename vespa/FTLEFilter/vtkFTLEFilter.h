/**
 * @class   vtkFTLEFilter
 * @brief   Compute Finite-Time Lyapunov Exponent (FTLE) field
 *
 * vtkFTLEFilter computes the Finite-Time Lyapunov Exponent (FTLE) field
 * from a velocity field. FTLE is used to identify Lagrangian coherent
 * structures in flow fields, such as attracting and repelling structures.
 *
 * @sa
 * vtkStreamTracer vtkRungeKutta2 vtkRungeKutta4
 */

#ifndef vtkFTLEFilter_h
#define vtkFTLEFilter_h

#include "vtkDataSetAlgorithm.h"
#include "vtkSmartPointer.h"
#include "vtkFTLEFilterModule.h"

VTK_ABI_NAMESPACE_BEGIN

class vtkAbstractInterpolatedVelocityField;
class vtkInitialValueProblemSolver;
class vtkDataSet;

class VTKFTLEFILTER_EXPORT vtkFTLEFilter : public vtkDataSetAlgorithm
{
public:
  static vtkFTLEFilter* New();
  vtkTypeMacro(vtkFTLEFilter, vtkDataSetAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(IntegrationTime, double);
  vtkGetMacro(IntegrationTime, double);

  vtkSetMacro(StepSize, double);
  vtkGetMacro(StepSize, double);

  vtkSetStringMacro(VelocityArrayName);
  vtkGetStringMacro(VelocityArrayName);

  vtkSetMacro(StartTime, double);
  vtkGetMacro(StartTime, double);

  enum IntegratorTypeEnum
  {
    INTEGRATOR_RK2 = 0,
    INTEGRATOR_RK4 = 1
  };
  vtkSetClampMacro(IntegratorType, int, INTEGRATOR_RK2, INTEGRATOR_RK4);
  vtkGetMacro(IntegratorType, int);
  void SetIntegratorTypeToRungeKutta2() { this->SetIntegratorType(INTEGRATOR_RK2); }
  void SetIntegratorTypeToRungeKutta4() { this->SetIntegratorType(INTEGRATOR_RK4); }

  enum AdvectionModeEnum
  {
    MODE_STREAMLINE = 0,
    MODE_PATHLINE = 1
  };
  vtkSetClampMacro(AdvectionMode, int, MODE_STREAMLINE, MODE_PATHLINE);
  vtkGetMacro(AdvectionMode, int);
  void SetAdvectionModeToStreamline() { this->SetAdvectionMode(MODE_STREAMLINE); }
  void SetAdvectionModeToPathline() { this->SetAdvectionMode(MODE_PATHLINE); }

protected:
  vtkFTLEFilter();
  ~vtkFTLEFilter() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkFTLEFilter(const vtkFTLEFilter&) = delete;
  void operator=(const vtkFTLEFilter&) = delete;

  double IntegrationTime = 1.0;
  double StepSize = 0.1;
  double StartTime = 0.0;
  int IntegratorType = INTEGRATOR_RK4;
  int AdvectionMode = MODE_STREAMLINE;
  char* VelocityArrayName = nullptr;
};

VTK_ABI_NAMESPACE_END
#endif
