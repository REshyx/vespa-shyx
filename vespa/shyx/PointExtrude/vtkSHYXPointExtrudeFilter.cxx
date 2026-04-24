#include "vtkSHYXPointExtrudeFilter.h"

#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSMPTools.h>

#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXPointExtrudeFilter);

namespace
{
void NormalizeOrFallback(double v[3])
{
  const double len = vtkMath::Norm(v);
  if (len > 1e-30)
  {
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }
  else
  {
    v[0] = 0.0;
    v[1] = 0.0;
    v[2] = 1.0;
  }
}

double Tuple1(vtkDataArray* a, vtkIdType i)
{
  if (!a || i < 0 || i >= a->GetNumberOfTuples())
  {
    return 1.0;
  }
  const int nc = a->GetNumberOfComponents();
  if (nc <= 1)
  {
    return a->GetTuple1(i);
  }
  return a->GetComponent(i, 0);
}

void DirectionFromArray(vtkDataArray* vecs, vtkIdType i, double dir[3])
{
  if (!vecs || vecs->GetNumberOfComponents() < 3 || i >= vecs->GetNumberOfTuples())
  {
    dir[0] = 0.0;
    dir[1] = 0.0;
    dir[2] = 1.0;
    return;
  }
  vecs->GetTuple(i, dir);
  NormalizeOrFallback(dir);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXPointExtrudeFilter::vtkSHYXPointExtrudeFilter()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXPointExtrudeFilter::~vtkSHYXPointExtrudeFilter()
{
  this->SetDirectionArrayName(nullptr);
  this->SetDistanceMultiplierArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXPointExtrudeFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ExtrusionDistance: " << this->ExtrusionDistance << "\n";
  os << indent << "UseNormalsForDirection: " << this->UseNormalsForDirection << "\n";
  os << indent << "DirectionArrayName: "
     << (this->DirectionArrayName ? this->DirectionArrayName : "(null)") << "\n";
  os << indent << "UseDistanceMultiplierArray: " << this->UseDistanceMultiplierArray << "\n";
  os << indent << "DistanceMultiplierArrayName: "
     << (this->DistanceMultiplierArrayName ? this->DistanceMultiplierArrayName : "(null)") << "\n";
  os << indent << "FlipDirection: " << this->FlipDirection << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXPointExtrudeFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPointExtrudeFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXPointExtrudeFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro("Null input or output.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts == 0)
  {
    output->Initialize();
    return 1;
  }

  vtkDataArray* normals = nullptr;
  vtkNew<vtkPolyDataNormals> computeNormals;

  if (this->UseNormalsForDirection)
  {
    computeNormals->SetInputData(input);
    computeNormals->ComputePointNormalsOn();
    computeNormals->ComputeCellNormalsOff();
    computeNormals->ConsistencyOn();
    computeNormals->AutoOrientNormalsOn();
    computeNormals->SplittingOff();
    computeNormals->Update();
    vtkPolyData* nOut = computeNormals->GetOutput();
    if (nOut && nOut->GetNumberOfPoints() == nPts)
    {
      normals = nOut->GetPointData()->GetNormals();
    }
    if (!normals || normals->GetNumberOfTuples() != nPts)
    {
      vtkWarningMacro(
        "Could not obtain point normals; vtkPolyDataNormals produced no usable normals. "
        "Provide a Direction Array or ensure the mesh has polygon connectivity.");
      normals = nullptr;
    }
  }

  vtkDataArray* dirArray = nullptr;
  if (!this->UseNormalsForDirection)
  {
    if (!this->DirectionArrayName || this->DirectionArrayName[0] == '\0')
    {
      vtkErrorMacro("Direction Array Name is required when UseNormalsForDirection is off.");
      return 0;
    }
    dirArray = input->GetPointData()->GetArray(this->DirectionArrayName);
    if (!dirArray)
    {
      vtkErrorMacro("Direction array \"" << this->DirectionArrayName << "\" not found on point data.");
      return 0;
    }
    if (dirArray->GetNumberOfComponents() < 3)
    {
      vtkErrorMacro("Direction array must have at least 3 components per tuple.");
      return 0;
    }
    if (dirArray->GetNumberOfTuples() != nPts)
    {
      vtkErrorMacro("Direction array tuple count does not match number of points.");
      return 0;
    }
  }

  vtkDataArray* multArray = nullptr;
  if (this->UseDistanceMultiplierArray)
  {
    if (!this->DistanceMultiplierArrayName || this->DistanceMultiplierArrayName[0] == '\0')
    {
      vtkErrorMacro("Distance Multiplier Array Name is required when UseDistanceMultiplierArray is on.");
      return 0;
    }
    multArray = input->GetPointData()->GetArray(this->DistanceMultiplierArrayName);
    if (!multArray)
    {
      vtkErrorMacro(
        "Distance multiplier array \"" << this->DistanceMultiplierArrayName << "\" not found on point data.");
      return 0;
    }
    if (multArray->GetNumberOfTuples() != nPts)
    {
      vtkErrorMacro("Distance multiplier array tuple count does not match number of points.");
      return 0;
    }
  }

  output->DeepCopy(input);

  vtkPoints* outPts = output->GetPoints();
  if (!outPts || outPts->GetNumberOfPoints() != nPts)
  {
    vtkErrorMacro("Internal error: output points.");
    return 0;
  }

  const double base = this->ExtrusionDistance;
  const int flip = this->FlipDirection ? -1 : 1;

  vtkSMPTools::For(0, nPts, [&](vtkIdType begin, vtkIdType end) {
    double dir[3];
    double p[3];
    for (vtkIdType i = begin; i < end; ++i)
    {
      if (this->UseNormalsForDirection && normals)
      {
        normals->GetTuple(i, dir);
        NormalizeOrFallback(dir);
      }
      else if (dirArray)
      {
        DirectionFromArray(dirArray, i, dir);
      }
      else
      {
        dir[0] = 0.0;
        dir[1] = 0.0;
        dir[2] = 1.0;
      }

      if (flip < 0)
      {
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
      }

      double dist = base;
      if (multArray)
      {
        dist *= Tuple1(multArray, i);
      }

      outPts->GetPoint(i, p);
      outPts->SetPoint(i, p[0] + dist * dir[0], p[1] + dist * dir[1], p[2] + dist * dir[2]);
    }
  });

  return 1;
}

VTK_ABI_NAMESPACE_END
