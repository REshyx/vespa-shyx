/**
 * @class   vtkSHYXAIAssistant
 * @brief   Optional-input pass-through that stores AI assistant text properties.
 *
 * RequestData shallow-copies the input when present, otherwise produces an empty
 * vtkPolyData. ParaView Python in CodeScript is executed by the client panel on
 * Apply, not in RequestData.
 */

#ifndef vtkSHYXAIAssistant_h
#define vtkSHYXAIAssistant_h

#include "vtkPassInputTypeAlgorithm.h"
#include "vtkSHYXAIAssistantModule.h"
#include "vtkSetGet.h"

#include <string>

VTK_ABI_NAMESPACE_BEGIN

class VTKSHYXAIASSISTANT_EXPORT vtkSHYXAIAssistant : public vtkPassInputTypeAlgorithm
{
public:
  static vtkSHYXAIAssistant* New();
  vtkTypeMacro(vtkSHYXAIAssistant, vtkPassInputTypeAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetStdStringFromCharMacro(UserQuestion);
  vtkGetCharFromStdStringMacro(UserQuestion);

  vtkSetStdStringFromCharMacro(CodeScript);
  vtkGetCharFromStdStringMacro(CodeScript);

  vtkSetStdStringFromCharMacro(DialogHistory);
  vtkGetCharFromStdStringMacro(DialogHistory);

  vtkSetStdStringFromCharMacro(EndpointUrl);
  vtkGetCharFromStdStringMacro(EndpointUrl);

  vtkSetStdStringFromCharMacro(ModelName);
  vtkGetCharFromStdStringMacro(ModelName);

protected:
  vtkSHYXAIAssistant();
  ~vtkSHYXAIAssistant() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int RequestDataObject(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  std::string UserQuestion;
  std::string CodeScript;
  std::string DialogHistory;
  std::string EndpointUrl;
  std::string ModelName;

private:
  vtkSHYXAIAssistant(const vtkSHYXAIAssistant&) = delete;
  void operator=(const vtkSHYXAIAssistant&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
