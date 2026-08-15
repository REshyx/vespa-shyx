#include "vtkSHYXAIAssistant.h"

#include "vtkAlgorithm.h"
#include "vtkDataObject.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXAIAssistant);

namespace
{
void TruncateForPrint(ostream& os, const std::string& value)
{
  constexpr std::size_t kMax = 80;
  if (value.size() <= kMax)
  {
    os << value;
    return;
  }
  os << value.substr(0, kMax) << "...";
}
}

vtkSHYXAIAssistant::vtkSHYXAIAssistant()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->CodeScript = "# ParaView Python. Apply runs this script.\nfrom paraview.simple import *\n";
  this->EndpointUrl = "https://api.openai.com/v1";
  this->ModelName = "gpt-4o-mini";
}

void vtkSHYXAIAssistant::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "UserQuestion: ";
  TruncateForPrint(os, this->UserQuestion);
  os << "\n";
  os << indent << "CodeScript: ";
  TruncateForPrint(os, this->CodeScript);
  os << "\n";
  os << indent << "DialogHistory: ";
  TruncateForPrint(os, this->DialogHistory);
  os << "\n";
  os << indent << "EndpointUrl: " << this->EndpointUrl << "\n";
  os << indent << "ModelName: " << this->ModelName << "\n";
}

int vtkSHYXAIAssistant::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port != 0)
  {
    return 0;
  }
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataObject");
  info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  return 1;
}

int vtkSHYXAIAssistant::RequestDataObject(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataObject* input = vtkDataObject::GetData(inputVector[0], 0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  vtkDataObject* output = vtkDataObject::GetData(outputVector, 0);

  if (input)
  {
    if (!output || !output->IsA(input->GetClassName()))
    {
      vtkDataObject* newOutput = input->NewInstance();
      outInfo->Set(vtkDataObject::DATA_OBJECT(), newOutput);
      newOutput->Delete();
    }
    return 1;
  }

  if (!vtkPolyData::SafeDownCast(output))
  {
    vtkNew<vtkPolyData> pd;
    outInfo->Set(vtkDataObject::DATA_OBJECT(), pd);
  }
  return 1;
}

int vtkSHYXAIAssistant::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataObject* output = vtkDataObject::GetData(outputVector, 0);
  if (!output)
  {
    vtkErrorMacro(<< "Null output.");
    return 0;
  }

  vtkDataObject* input = vtkDataObject::GetData(inputVector[0], 0);
  if (input)
  {
    output->ShallowCopy(input);
  }
  else
  {
    output->Initialize();
  }
  return 1;
}

VTK_ABI_NAMESPACE_END
