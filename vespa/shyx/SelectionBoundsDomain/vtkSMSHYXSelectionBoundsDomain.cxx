#include "vtkSMSHYXSelectionBoundsDomain.h"

#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPVSHYXSelectionBoundsInformation.h"
#include "vtkPVXMLElement.h"
#include "vtkSMProperty.h"
#include "vtkNew.h"
#include "vtkSMProxy.h"
#include "vtkSMUncheckedPropertyHelper.h"

vtkStandardNewMacro(vtkSMSHYXSelectionBoundsDomain);

vtkSMSHYXSelectionBoundsDomain::vtkSMSHYXSelectionBoundsDomain()
{
  this->DefaultDefaultMode = vtkSMDoubleRangeDomain::MAX;
}

vtkSMSHYXSelectionBoundsDomain::~vtkSMSHYXSelectionBoundsDomain() = default;

void vtkSMSHYXSelectionBoundsDomain::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

int vtkSMSHYXSelectionBoundsDomain::ReadXMLAttributes(vtkSMProperty* prop, vtkPVXMLElement* element)
{
  if (!this->Superclass::ReadXMLAttributes(prop, element))
  {
    return 0;
  }
  if (element->GetAttribute("default_mode") == nullptr)
  {
    this->DefaultDefaultMode = vtkSMDoubleRangeDomain::MAX;
  }
  return 1;
}

void vtkSMSHYXSelectionBoundsDomain::Update(vtkSMProperty* prop)
{
  vtkSMProxy* parent = prop ? prop->GetParent() : nullptr;
  if (!parent)
  {
    if (vtkSMProperty* inputProp = this->GetRequiredProperty("Input"))
    {
      parent = inputProp->GetParent();
    }
  }
  if (!parent)
  {
    return;
  }

  parent->UpdateVTKObjects();

  vtkNew<vtkPVSHYXSelectionBoundsInformation> info;
  if (vtkSMProperty* nameProp = this->GetRequiredProperty("SelectionCellArrayName"))
  {
    vtkSMUncheckedPropertyHelper helper(nameProp);
    if (helper.GetNumberOfElements() > 0)
    {
      info->SetSelectionCellArrayName(helper.GetAsString(0));
    }
  }

  if (!parent->GatherInformation(info.Get()))
  {
    return;
  }
  if (!info->GetValid())
  {
    return;
  }

  double bounds[6];
  info->GetBounds(bounds);
  if (!vtkMath::AreBoundsInitialized(bounds))
  {
    return;
  }
  this->SetDomainValues(bounds);
}
