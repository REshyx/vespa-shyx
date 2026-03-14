#include "vtkVortexCriteriaFilter.h"

#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkGradientFilter.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkDataSetAttributes.h"
#include "vtkPointData.h"

#include <algorithm>
#include <cmath>
#include <utility>

VTK_ABI_NAMESPACE_BEGIN

namespace
{

inline double FrobeniusNormSq(const double J[9])
{
  double s = 0;
  for (int i = 0; i < 9; i++)
    s += J[i] * J[i];
  return s;
}

void SymmetricAntisymmetricParts(const double J[9], double S[9], double O[9])
{
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
    {
      double jij = J[i * 3 + j];
      double jji = J[j * 3 + i];
      S[i * 3 + j] = 0.5 * (jij + jji);
      O[i * 3 + j] = 0.5 * (jij - jji);
    }
}

void MatMul3x3(const double A[9], const double B[9], double C[9])
{
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
    {
      C[i * 3 + j] = 0;
      for (int k = 0; k < 3; k++)
        C[i * 3 + j] += A[i * 3 + k] * B[k * 3 + j];
    }
}

void EigenvaluesSymmetric3x3(const double M[9], double w[3])
{
  double A[3][3], V[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      A[i][j] = M[i * 3 + j];
  vtkMath::Diagonalize3x3(A, w, V);
  std::sort(w, w + 3);
}

void Eigenvalues3x3(const double J[9], double& lambda_r, double& lambda_cr, double& lambda_ci)
{
  double trace = J[0] + J[4] + J[8];
  double det = J[0] * (J[4] * J[8] - J[5] * J[7]) - J[1] * (J[3] * J[8] - J[5] * J[6]) +
    J[2] * (J[3] * J[7] - J[4] * J[6]);
  double m00 = J[4] * J[8] - J[5] * J[7];
  double m11 = J[0] * J[8] - J[2] * J[6];
  double m22 = J[0] * J[4] - J[1] * J[3];
  double t2 = m00 + m11 + m22;
  double p = t2 - trace * trace / 3.0;
  double q = (2.0 * trace * trace * trace - 9.0 * trace * t2 + 27.0 * det) / 27.0;
  double disc = q * q / 4.0 + p * p * p / 27.0;

  lambda_ci = 0.0;
  if (disc > 1e-20)
  {
    double sqrt_d = std::sqrt(disc);
    double u = -q / 2.0 + sqrt_d;
    double v = -q / 2.0 - sqrt_d;
    u = (u >= 0) ? std::pow(u, 1.0 / 3.0) : -std::pow(-u, 1.0 / 3.0);
    v = (v >= 0) ? std::pow(v, 1.0 / 3.0) : -std::pow(-v, 1.0 / 3.0);
    lambda_r = u + v + trace / 3.0;
    lambda_cr = -(u + v) / 2.0 + trace / 3.0;
    lambda_ci = std::sqrt(3.0) * (u - v) / 2.0;
    if (lambda_ci < 0)
      lambda_ci = -lambda_ci;
  }
  else
  {
    double t = trace / 3.0;
    lambda_ci = 0.0;
    if (p >= -1e-20)
    {
      lambda_r = t;
      lambda_cr = t;
    }
    else
    {
      double r = 2.0 * std::sqrt(-p / 3.0);
      double arg = 1.5 * q / p * std::sqrt(-3.0 / p);
      arg = std::max(-1.0, std::min(1.0, arg));
      double phi = std::acos(arg);
      lambda_r = t + r * std::cos(phi / 3.0);
      lambda_cr = t + r * std::cos((phi + 2.0 * 3.141592653589793) / 3.0);
      double lambda_3 = t + r * std::cos((phi + 4.0 * 3.141592653589793) / 3.0);
      if (lambda_r > lambda_cr)
        std::swap(lambda_r, lambda_cr);
      if (lambda_cr > lambda_3)
        std::swap(lambda_cr, lambda_3);
      if (lambda_r > lambda_cr)
        std::swap(lambda_r, lambda_cr);
    }
  }
}

void RealEigenvector3x3(const double J[9], double lambda, double r[3])
{
  double a0 = J[0] - lambda;
  double a1 = J[1];
  double a2 = J[2];
  double b0 = J[3];
  double b1 = J[4] - lambda;
  double b2 = J[5];
  double c0 = J[6];
  double c1 = J[7];
  double c2 = J[8] - lambda;
  r[0] = a1 * b2 - a2 * b1;
  r[1] = a2 * b0 - a0 * b2;
  r[2] = a0 * b1 - a1 * b0;
  double n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  if (n < 1e-15)
  {
    r[0] = b1 * c2 - b2 * c1;
    r[1] = b2 * c0 - b0 * c2;
    r[2] = b0 * c1 - b1 * c0;
    n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  }
  if (n < 1e-15)
  {
    r[0] = a1 * c2 - a2 * c1;
    r[1] = a2 * c0 - a0 * c2;
    r[2] = a0 * c1 - a1 * c0;
    n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  }
  if (n > 1e-15)
  {
    r[0] /= n;
    r[1] /= n;
    r[2] /= n;
  }
  else
  {
    r[0] = 1;
    r[1] = 0;
    r[2] = 0;
  }
}

}

vtkStandardNewMacro(vtkVortexCriteriaFilter);

vtkVortexCriteriaFilter::vtkVortexCriteriaFilter()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->VelocityArrayName = nullptr;
  this->Epsilon = 1e-10;
  this->ComputeVelocity = 0;
  this->ComputeVorticity = 1;
  this->ComputeGradient = 0;
  this->ComputeStrainRate = 0;
  this->ComputeRotationTensor = 0;
  this->ComputeQCriterion = 1;
  this->ComputeLambda2 = 0;
  this->ComputeSwirlingStrength = 0;
  this->ComputeLiutex = 0;
  this->ComputeOmegaMethod = 0;
  this->ComputeHelicity = 0;
}

vtkVortexCriteriaFilter::~vtkVortexCriteriaFilter()
{
  this->SetVelocityArrayName(nullptr);
}

int vtkVortexCriteriaFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

int vtkVortexCriteriaFilter::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkDataSet* output = vtkDataSet::GetData(outputVector, 0);
  if (!input)
  {
    vtkErrorMacro("No input!");
    return 0;
  }

  vtkDataArray* velocityArray = nullptr;
  if (this->VelocityArrayName && strlen(this->VelocityArrayName) > 0)
  {
    velocityArray = input->GetPointData()->GetArray(this->VelocityArrayName);
    if (!velocityArray)
      velocityArray = input->GetCellData()->GetArray(this->VelocityArrayName);
  }
  else
  {
    velocityArray = input->GetPointData()->GetVectors();
    if (!velocityArray && input->GetPointData()->GetNumberOfArrays() > 0)
    {
      for (int i = 0; i < input->GetPointData()->GetNumberOfArrays(); i++)
      {
        vtkDataArray* a = input->GetPointData()->GetArray(i);
        if (a && a->GetNumberOfComponents() == 3)
        {
          velocityArray = a;
          break;
        }
      }
    }
    if (!velocityArray)
    {
      for (int i = 0; i < input->GetCellData()->GetNumberOfArrays(); i++)
      {
        vtkDataArray* a = input->GetCellData()->GetArray(i);
        if (a && a->GetNumberOfComponents() == 3)
        {
          velocityArray = a;
          break;
        }
      }
    }
  }

  if (!velocityArray || velocityArray->GetNumberOfComponents() != 3)
  {
    vtkErrorMacro("Need a 3-component velocity array.");
    return 0;
  }

  bool isPointData = (input->GetPointData()->GetArray(velocityArray->GetName()) == velocityArray);
  vtkIdType numTuples = velocityArray->GetNumberOfTuples();

  bool needGradient = this->ComputeVorticity || this->ComputeGradient || this->ComputeStrainRate ||
    this->ComputeRotationTensor || this->ComputeQCriterion || this->ComputeLambda2 ||
    this->ComputeSwirlingStrength || this->ComputeLiutex || this->ComputeOmegaMethod ||
    this->ComputeHelicity;

  vtkDataArray* gradientArray = nullptr;
  if (needGradient)
  {
    vtkNew<vtkGradientFilter> gradFilter;
    gradFilter->SetInputData(input);
    gradFilter->SetInputArrayToProcess(
      0, 0, 0, isPointData ? vtkDataObject::FIELD_ASSOCIATION_POINTS
                           : vtkDataObject::FIELD_ASSOCIATION_CELLS,
      velocityArray->GetName());
    gradFilter->ComputeGradientOn();
    gradFilter->ComputeVorticityOff();
    gradFilter->ComputeQCriterionOff();
    gradFilter->SetResultArrayName("Gradients");
    gradFilter->Update();
    vtkDataSet* gradOutput = vtkDataSet::SafeDownCast(gradFilter->GetOutput());
    if (!gradOutput)
    {
      vtkErrorMacro("Gradient filter failed.");
      return 0;
    }
    gradientArray = isPointData ? gradOutput->GetPointData()->GetArray("Gradients")
                                : gradOutput->GetCellData()->GetArray("Gradients");
    if (!gradientArray)
    {
      vtkErrorMacro("Gradient array not produced.");
      return 0;
    }
    output->ShallowCopy(gradOutput);
    gradientArray = isPointData ? output->GetPointData()->GetArray("Gradients")
                                : output->GetCellData()->GetArray("Gradients");
    velocityArray = isPointData ? output->GetPointData()->GetArray(velocityArray->GetName())
                                : output->GetCellData()->GetArray(velocityArray->GetName());
    if (!velocityArray)
    {
      vtkDataSetAttributes* attr =
        (isPointData ? static_cast<vtkDataSetAttributes*>(output->GetPointData())
                     : static_cast<vtkDataSetAttributes*>(output->GetCellData()));
      for (int k = 0; k < attr->GetNumberOfArrays(); k++)
      {
        vtkDataArray* arr = attr->GetArray(k);
        if (arr && arr->GetNumberOfComponents() == 3)
        {
          velocityArray = arr;
          break;
        }
      }
    }
    numTuples = gradientArray->GetNumberOfTuples();
  }
  else
    output->ShallowCopy(input);

  double u[3], grad[9], vorticity[3];
  double S[9], O[9], M[9], S2[9], O2[9];
  double lambda_r, lambda_cr, lambda_ci;
  double r[3];
  const double eps = this->Epsilon;

  if (this->ComputeVelocity)
  {
    vtkNew<vtkDoubleArray> velOut;
    velOut->SetName("Velocity");
    velOut->SetNumberOfComponents(3);
    velOut->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
      velOut->SetTuple(i, velocityArray->GetTuple(i));
    if (isPointData)
      output->GetPointData()->AddArray(velOut);
    else
      output->GetCellData()->AddArray(velOut);
  }

  if (this->ComputeVorticity)
  {
    vtkNew<vtkDoubleArray> vortArray;
    vortArray->SetName("Vorticity");
    vortArray->SetNumberOfComponents(3);
    vortArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      vorticity[0] = grad[7] - grad[5];
      vorticity[1] = grad[2] - grad[6];
      vorticity[2] = grad[3] - grad[1];
      vortArray->SetTuple(i, vorticity);
    }
    if (isPointData)
      output->GetPointData()->AddArray(vortArray);
    else
      output->GetCellData()->AddArray(vortArray);
  }

  if (this->ComputeGradient)
  {
    vtkNew<vtkDoubleArray> gradOut;
    gradOut->SetName("VelocityGradient");
    gradOut->SetNumberOfComponents(9);
    gradOut->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
      gradOut->SetTuple(i, gradientArray->GetTuple(i));
    if (isPointData)
      output->GetPointData()->AddArray(gradOut);
    else
      output->GetCellData()->AddArray(gradOut);
  }

  if (this->ComputeStrainRate)
  {
    vtkNew<vtkDoubleArray> sArray;
    sArray->SetName("StrainRate");
    sArray->SetNumberOfComponents(9);
    sArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      SymmetricAntisymmetricParts(grad, S, O);
      sArray->SetTuple(i, S);
    }
    if (isPointData)
      output->GetPointData()->AddArray(sArray);
    else
      output->GetCellData()->AddArray(sArray);
  }

  if (this->ComputeRotationTensor)
  {
    vtkNew<vtkDoubleArray> oArray;
    oArray->SetName("RotationTensor");
    oArray->SetNumberOfComponents(9);
    oArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      SymmetricAntisymmetricParts(grad, S, O);
      oArray->SetTuple(i, O);
    }
    if (isPointData)
      output->GetPointData()->AddArray(oArray);
    else
      output->GetCellData()->AddArray(oArray);
  }

  if (this->ComputeQCriterion)
  {
    vtkNew<vtkDoubleArray> qArray;
    qArray->SetName("Q-criterion");
    qArray->SetNumberOfComponents(1);
    qArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      SymmetricAntisymmetricParts(grad, S, O);
      double q = 0.5 * (FrobeniusNormSq(O) - FrobeniusNormSq(S));
      qArray->SetValue(i, q);
    }
    if (isPointData)
      output->GetPointData()->AddArray(qArray);
    else
      output->GetCellData()->AddArray(qArray);
  }

  if (this->ComputeLambda2)
  {
    vtkNew<vtkDoubleArray> l2Array;
    l2Array->SetName("Lambda2");
    l2Array->SetNumberOfComponents(1);
    l2Array->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      SymmetricAntisymmetricParts(grad, S, O);
      MatMul3x3(S, S, S2);
      MatMul3x3(O, O, O2);
      for (int k = 0; k < 9; k++)
        M[k] = S2[k] + O2[k];
      double w[3];
      EigenvaluesSymmetric3x3(M, w);
      l2Array->SetValue(i, w[1]);
    }
    if (isPointData)
      output->GetPointData()->AddArray(l2Array);
    else
      output->GetCellData()->AddArray(l2Array);
  }

  if (this->ComputeSwirlingStrength)
  {
    vtkNew<vtkDoubleArray> lciArray;
    lciArray->SetName("SwirlingStrength");
    lciArray->SetNumberOfComponents(1);
    lciArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      Eigenvalues3x3(grad, lambda_r, lambda_cr, lambda_ci);
      lciArray->SetValue(i, lambda_ci);
    }
    if (isPointData)
      output->GetPointData()->AddArray(lciArray);
    else
      output->GetCellData()->AddArray(lciArray);
  }

  if (this->ComputeLiutex)
  {
    vtkNew<vtkDoubleArray> liutexArray;
    liutexArray->SetName("Liutex");
    liutexArray->SetNumberOfComponents(3);
    liutexArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      velocityArray->GetTuple(i, u);
      vorticity[0] = grad[7] - grad[5];
      vorticity[1] = grad[2] - grad[6];
      vorticity[2] = grad[3] - grad[1];
      Eigenvalues3x3(grad, lambda_r, lambda_cr, lambda_ci);
      RealEigenvector3x3(grad, lambda_r, r);
      double omDotR = vorticity[0] * r[0] + vorticity[1] * r[1] + vorticity[2] * r[2];
      if (omDotR < 0)
      {
        r[0] = -r[0];
        r[1] = -r[1];
        r[2] = -r[2];
        omDotR = -omDotR;
      }
      double R = omDotR - std::sqrt(std::max(0.0, omDotR * omDotR - 4.0 * lambda_ci * lambda_ci));
      double liutex[3] = { R * r[0], R * r[1], R * r[2] };
      liutexArray->SetTuple(i, liutex);
    }
    if (isPointData)
      output->GetPointData()->AddArray(liutexArray);
    else
      output->GetCellData()->AddArray(liutexArray);
  }

  if (this->ComputeOmegaMethod)
  {
    vtkNew<vtkDoubleArray> omArray;
    omArray->SetName("Omega");
    omArray->SetNumberOfComponents(1);
    omArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      gradientArray->GetTuple(i, grad);
      SymmetricAntisymmetricParts(grad, S, O);
      double normS2 = FrobeniusNormSq(S);
      double normO2 = FrobeniusNormSq(O);
      double denom = normS2 + normO2 + eps;
      omArray->SetValue(i, denom > 0 ? normO2 / denom : 0.0);
    }
    if (isPointData)
      output->GetPointData()->AddArray(omArray);
    else
      output->GetCellData()->AddArray(omArray);
  }

  if (this->ComputeHelicity)
  {
    vtkNew<vtkDoubleArray> hArray;
    hArray->SetName("Helicity");
    hArray->SetNumberOfComponents(1);
    hArray->SetNumberOfTuples(numTuples);
    vtkNew<vtkDoubleArray> hnArray;
    hnArray->SetName("NormalizedHelicity");
    hnArray->SetNumberOfComponents(1);
    hnArray->SetNumberOfTuples(numTuples);
    for (vtkIdType i = 0; i < numTuples; i++)
    {
      velocityArray->GetTuple(i, u);
      gradientArray->GetTuple(i, grad);
      vorticity[0] = grad[7] - grad[5];
      vorticity[1] = grad[2] - grad[6];
      vorticity[2] = grad[3] - grad[1];
      double H = u[0] * vorticity[0] + u[1] * vorticity[1] + u[2] * vorticity[2];
      hArray->SetValue(i, H);
      double nu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
      double nw = std::sqrt(
        vorticity[0] * vorticity[0] + vorticity[1] * vorticity[1] + vorticity[2] * vorticity[2]);
      double Hn = (nu > eps && nw > eps) ? H / (nu * nw) : 0.0;
      Hn = std::max(-1.0, std::min(1.0, Hn));
      hnArray->SetValue(i, Hn);
    }
    if (isPointData)
    {
      output->GetPointData()->AddArray(hArray);
      output->GetPointData()->AddArray(hnArray);
    }
    else
    {
      output->GetCellData()->AddArray(hArray);
      output->GetCellData()->AddArray(hnArray);
    }
  }

  return 1;
}

void vtkVortexCriteriaFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "VelocityArrayName: "
     << (this->VelocityArrayName ? this->VelocityArrayName : "(none)") << "\n";
  os << indent << "Epsilon: " << this->Epsilon << "\n";
  os << indent << "ComputeVelocity: " << this->ComputeVelocity << "\n";
  os << indent << "ComputeVorticity: " << this->ComputeVorticity << "\n";
  os << indent << "ComputeQCriterion: " << this->ComputeQCriterion << "\n";
  os << indent << "ComputeLambda2: " << this->ComputeLambda2 << "\n";
  os << indent << "ComputeSwirlingStrength: " << this->ComputeSwirlingStrength << "\n";
  os << indent << "ComputeLiutex: " << this->ComputeLiutex << "\n";
  os << indent << "ComputeOmegaMethod: " << this->ComputeOmegaMethod << "\n";
  os << indent << "ComputeHelicity: " << this->ComputeHelicity << "\n";
}

VTK_ABI_NAMESPACE_END
