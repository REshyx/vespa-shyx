#include "vtkFTLEFilter.h"

#include "vtkAbstractInterpolatedVelocityField.h"
#include "vtkCellData.h"
#include "vtkClosestPointStrategy.h"
#include "vtkCompositeDataSet.h"
#include "vtkCompositeInterpolatedVelocityField.h"
#include "vtkDataArray.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkGradientFilter.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkInitialValueProblemSolver.h"
#include "vtkMath.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkRungeKutta2.h"
#include "vtkRungeKutta4.h"
#include "vtkSmartPointer.h"

#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkFTLEFilter);

vtkFTLEFilter::vtkFTLEFilter()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->IntegrationTime = 1.0;
  this->StepSize = 0.1;
  this->StartTime = 0.0;
  this->IntegratorType = INTEGRATOR_RK4;
  this->AdvectionMode = MODE_STREAMLINE;
  this->VelocityArrayName = nullptr;
}

vtkFTLEFilter::~vtkFTLEFilter()
{
  this->SetVelocityArrayName(nullptr);
}

int vtkFTLEFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

void vtkFTLEFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Integration Time: " << this->IntegrationTime << "\n";
  os << indent << "Step Size: " << this->StepSize << "\n";
  os << indent << "Start Time: " << this->StartTime << "\n";
  os << indent << "Integrator Type: " << (this->IntegratorType == INTEGRATOR_RK4 ? "RK4" : "RK2") << "\n";
  os << indent << "Advection Mode: " << (this->AdvectionMode == MODE_PATHLINE ? "Pathline" : "Streamline") << "\n";
  os << indent << "Velocity Array Name: "
     << (this->VelocityArrayName ? this->VelocityArrayName : "(auto)") << "\n";
}

int vtkFTLEFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkDataSet* input = vtkDataSet::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));

  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  vtkDataSet* output = vtkDataSet::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (!input)
  {
    vtkErrorMacro("No input!");
    return 0;
  }

  output->CopyStructure(input);

  vtkSmartPointer<vtkCompositeInterpolatedVelocityField> func =
    vtkSmartPointer<vtkCompositeInterpolatedVelocityField>::New();

  vtkNew<vtkClosestPointStrategy> strategy;
  func->SetFindCellStrategy(strategy);

  vtkDataArray* vectors = nullptr;
  const char* vecName = nullptr;

  if (this->VelocityArrayName && strlen(this->VelocityArrayName) > 0)
  {
    vectors = input->GetPointData()->GetArray(this->VelocityArrayName);
    if (!vectors)
      vectors = input->GetCellData()->GetArray(this->VelocityArrayName);
    if (!vectors)
    {
      vtkErrorMacro("Velocity array '" << this->VelocityArrayName << "' not found!");
      return 0;
    }
    vecName = this->VelocityArrayName;
  }
  else
  {
    vectors = input->GetPointData()->GetVectors();
    if (!vectors)
      vectors = input->GetCellData()->GetVectors();
    if (!vectors)
    {
      vtkErrorMacro("No velocity vectors found! Please specify a velocity array.");
      return 0;
    }
    vecName = vectors->GetName();
    if (!vecName || strlen(vecName) == 0)
    {
      vtkErrorMacro("Velocity array has no name!");
      return 0;
    }
  }

  int vecType = (input->GetPointData()->GetArray(vecName) == vectors)
    ? vtkDataObject::FIELD_ASSOCIATION_POINTS
    : vtkDataObject::FIELD_ASSOCIATION_CELLS;

  func->AddDataSet(input);
  func->SelectVectors(vecType, vecName);

  vtkCompositeDataSet* compositeInput = vtkCompositeDataSet::SafeDownCast(input);
  if (compositeInput)
    func->Initialize(compositeInput);
  else
  {
    vtkNew<vtkMultiBlockDataSet> mbds;
    mbds->SetBlock(0, input);
    func->Initialize(mbds);
  }

  if (func->GetInitializationState() == vtkAbstractInterpolatedVelocityField::NOT_INITIALIZED)
  {
    vtkErrorMacro("Failed to initialize velocity field interpolator!");
    return 0;
  }

  vtkSmartPointer<vtkInitialValueProblemSolver> integrator;
  if (this->IntegratorType == INTEGRATOR_RK2)
    integrator = vtkSmartPointer<vtkRungeKutta2>::New();
  else
    integrator = vtkSmartPointer<vtkRungeKutta4>::New();
  integrator->SetFunctionSet(func);

  vtkIdType numPoints = input->GetNumberOfPoints();

  vtkSmartPointer<vtkDoubleArray> flowMapArray = vtkSmartPointer<vtkDoubleArray>::New();
  flowMapArray->SetNumberOfComponents(3);
  flowMapArray->SetNumberOfTuples(numPoints);
  flowMapArray->SetName("FlowMapVectors");

  double timeDir = (this->IntegrationTime >= 0) ? 1.0 : -1.0;
  double absTime = std::abs(this->IntegrationTime);
  double userStep = std::abs(this->StepSize);

  if (userStep < 1e-10)
  {
    vtkErrorMacro("Step size is too small!");
    return 0;
  }

  for (vtkIdType i = 0; i < numPoints; ++i)
  {
    double x[3];
    input->GetPoint(i, x);

    double currentTime = (this->AdvectionMode == MODE_PATHLINE) ? this->StartTime : 0.0;
    double accumulatedT = 0.0;
    double xNext[3];

    while (accumulatedT < absTime)
    {
      double currentStep = userStep;
      if (accumulatedT + userStep > absTime)
        currentStep = absTime - accumulatedT;
      double dt = currentStep * timeDir;

      double delTActual = 0.0;
      double err = 0.0;
      int ret = integrator->ComputeNextStep(x, xNext, currentTime, dt, 1e-6, err);

      if (ret != 1)
        break;

      x[0] = xNext[0];
      x[1] = xNext[1];
      x[2] = xNext[2];
      accumulatedT += std::abs(dt);

      if (this->AdvectionMode == MODE_PATHLINE)
        currentTime += dt;
    }

    flowMapArray->SetTuple(i, x);
  }

  vtkSmartPointer<vtkDataSet> tempGrid;
  tempGrid.TakeReference(input->NewInstance());
  tempGrid->CopyStructure(input);
  tempGrid->GetPointData()->AddArray(flowMapArray);
  tempGrid->GetPointData()->SetActiveVectors("FlowMapVectors");

  vtkSmartPointer<vtkGradientFilter> gradientFilter = vtkSmartPointer<vtkGradientFilter>::New();
  gradientFilter->SetInputData(tempGrid);
  gradientFilter->SetInputScalars(vtkDataObject::FIELD_ASSOCIATION_POINTS, "FlowMapVectors");
  gradientFilter->SetResultArrayName("Jacobian");
  gradientFilter->Update();

  vtkDataArray* jacobianArray =
    gradientFilter->GetOutput()->GetPointData()->GetArray("Jacobian");

  if (!jacobianArray)
  {
    vtkErrorMacro("Failed to compute gradient!");
    return 0;
  }

  vtkSmartPointer<vtkDoubleArray> ftleArray = vtkSmartPointer<vtkDoubleArray>::New();
  ftleArray->SetName("FTLE");
  ftleArray->SetNumberOfTuples(numPoints);

  for (vtkIdType i = 0; i < numPoints; ++i)
  {
    double F[9];
    jacobianArray->GetTuple(i, F);

    double C[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k)
          C[r][c] += F[k * 3 + r] * F[k * 3 + c];

    double eigenvalues[3];
    double* C_rows[3] = { C[0], C[1], C[2] };
    double V[3][3];
    double* V_rows[3] = { V[0], V[1], V[2] };
    vtkMath::Jacobi(C_rows, eigenvalues, V_rows);

    double maxLambda = eigenvalues[0];
    if (eigenvalues[1] > maxLambda)
      maxLambda = eigenvalues[1];
    if (eigenvalues[2] > maxLambda)
      maxLambda = eigenvalues[2];

    double ftle = 0.0;
    if (maxLambda > 0 && absTime > 1e-9)
      ftle = std::log(std::sqrt(maxLambda)) / absTime;
    ftleArray->SetValue(i, ftle);
  }

  output->GetPointData()->AddArray(ftleArray);
  output->GetPointData()->SetActiveScalars("FTLE");

  return 1;
}

VTK_ABI_NAMESPACE_END
