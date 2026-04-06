#include "vtkSHYXRadiusNeighborCount.h"

#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSMPThreadLocalObject.h>
#include <vtkSMPTools.h>
#include <vtkStaticPointLocator.h>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXRadiusNeighborCount);

vtkSHYXRadiusNeighborCount::vtkSHYXRadiusNeighborCount()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXRadiusNeighborCount::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Radius: " << this->Radius << "\n";
}

int vtkSHYXRadiusNeighborCount::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXRadiusNeighborCount::RequestData(
  vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro(<< "No input.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts == 0)
  {
    vtkWarningMacro(<< "Input has no points.");
    output->ShallowCopy(input);
    return 1;
  }

  if (!(this->Radius > 0.0) || !vtkMath::IsFinite(this->Radius))
  {
    vtkErrorMacro(<< "Radius must be a finite positive value.");
    return 0;
  }

  output->ShallowCopy(input);

  vtkNew<vtkStaticPointLocator> locator;
  locator->SetDataSet(input);
  locator->BuildLocator();

  vtkNew<vtkIdTypeArray> counts;
  counts->SetName("NeighborsInRadius");
  counts->SetNumberOfComponents(1);
  counts->SetNumberOfTuples(nPts);

  vtkPoints* pts = input->GetPoints();
  const double r = this->Radius;

  vtkSMPThreadLocalObject<vtkIdList> idListTLS;
  vtkSMPTools::For(
    0,
    nPts,
    [&](vtkIdType begin, vtkIdType end)
    {
      vtkIdList* ids = idListTLS.Local();
      double x[3];
      for (vtkIdType i = begin; i < end; ++i)
      {
        pts->GetPoint(i, x);
        ids->Reset();
        locator->FindPointsWithinRadius(r, x, ids);
        counts->SetValue(i, ids->GetNumberOfIds());
      }
    });

  vtkPointData* outPD = output->GetPointData();
  outPD->RemoveArray("NeighborsInRadius");
  outPD->AddArray(counts);

  return 1;
}

VTK_ABI_NAMESPACE_END
