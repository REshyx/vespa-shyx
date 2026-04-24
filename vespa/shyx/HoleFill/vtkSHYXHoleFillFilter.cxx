#include "vtkSHYXHoleFillFilter.h"

#include "vtkCGALPatchFilling.h"

#include <vtkAlgorithmOutput.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

vtkStandardNewMacro(vtkSHYXHoleFillFilter);

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
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
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

  if (this->GetNumberOfInputConnections(0) < 1 || !this->GetInputConnection(0, 0))
  {
    vtkErrorMacro(<< "Missing mesh input on port 0.");
    return 0;
  }

  vtkNew<vtkCGALPatchFilling> fill;
  fill->SetFairingContinuity(this->FairingContinuity);
  fill->SetInputConnection(0, this->GetInputConnection(0, 0));
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
