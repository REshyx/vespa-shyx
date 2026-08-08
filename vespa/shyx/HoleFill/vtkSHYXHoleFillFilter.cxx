#include "vtkSHYXHoleFillFilter.h"

#include "vtkCGALPatchFilling.h"

#include <vtkAlgorithmOutput.h>
#include <vtkDataSet.h>
#include <vtkGeometryFilter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

vtkStandardNewMacro(vtkSHYXHoleFillFilter);

namespace
{

vtkSmartPointer<vtkPolyData> ForceDataSetToPolyData(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    return pd;
  }

  vtkNew<vtkGeometryFilter> geometry;
  geometry->SetInputData(ds);
  geometry->Update();
  vtkPolyData* out = geometry->GetOutput();
  if (!out)
  {
    return nullptr;
  }

  vtkSmartPointer<vtkPolyData> copy = vtkSmartPointer<vtkPolyData>::New();
  copy->ShallowCopy(out);
  return copy;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXHoleFillFilter::vtkSHYXHoleFillFilter()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FairingContinuity: " << this->FairingContinuity << std::endl;
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::SetUpdateAttributes(bool update)
{
  if (update)
  {
    vtkWarningMacro("Unsupported: vtkSHYXHoleFillFilter does not interpolate attributes onto new "
                    "patch geometry (same behavior as vtkCGALPatchFilling).");
  }
  this->Superclass::SetUpdateAttributes(false);
}

//------------------------------------------------------------------------------
void vtkSHYXHoleFillFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
int vtkSHYXHoleFillFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
  }
  else
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkSHYXHoleFillFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!output)
  {
    vtkErrorMacro(<< "Null output.");
    return 0;
  }

  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  if (!input)
  {
    vtkErrorMacro(<< "Missing mesh input on port 0.");
    return 0;
  }

  vtkSmartPointer<vtkPolyData> mesh = ForceDataSetToPolyData(input);
  if (!mesh || mesh->GetNumberOfCells() == 0)
  {
    vtkErrorMacro(<< "Failed to convert input to a non-empty vtkPolyData surface.");
    return 0;
  }

  vtkNew<vtkCGALPatchFilling> fill;
  fill->SetFairingContinuity(this->FairingContinuity);
  fill->SetInputData(0, mesh);
  if (this->GetNumberOfInputConnections(1) > 0)
  {
    fill->SetInputConnection(1, this->GetInputConnection(1, 0));
  }
  fill->Update();
  vtkPolyData* filled = fill->GetOutput();
  if (!filled)
  {
    vtkErrorMacro(<< "vtkCGALPatchFilling produced no output.");
    return 0;
  }
  output->ShallowCopy(filled);
  return 1;
}
