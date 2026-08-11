// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkSMAnimatedStreamlineIntegrationScaleDomain.h"

#include "vtkBoundingBox.h"
#include "vtkDataObject.h"
#include "vtkDataSetAttributes.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPVArrayInformation.h"
#include "vtkPVDataInformation.h"
#include "vtkPVDataSetAttributesInformation.h"
#include "vtkPVXMLElement.h"
#include "vtkSMUncheckedPropertyHelper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

vtkStandardNewMacro(vtkSMAnimatedStreamlineIntegrationScaleDomain);

namespace
{
bool NameLooksLikeVelocity(const char* name)
{
  if (!name || !name[0])
  {
    return false;
  }
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower == "u" || lower == "v" || lower.find("veloc") != std::string::npos ||
    lower.find("vector") != std::string::npos;
}
} // namespace

//---------------------------------------------------------------------------
vtkSMAnimatedStreamlineIntegrationScaleDomain::vtkSMAnimatedStreamlineIntegrationScaleDomain()
{
  this->NumPulses = 100.0;
  // Match BoundsDomain scaled_extent: Reset uses domain maximum.
  this->DefaultDefaultMode = vtkSMDoubleRangeDomain::MAX;
}

//---------------------------------------------------------------------------
vtkSMAnimatedStreamlineIntegrationScaleDomain::~vtkSMAnimatedStreamlineIntegrationScaleDomain() =
  default;

//---------------------------------------------------------------------------
void vtkSMAnimatedStreamlineIntegrationScaleDomain::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NumPulses: " << this->NumPulses << "\n";
}

//---------------------------------------------------------------------------
double vtkSMAnimatedStreamlineIntegrationScaleDomain::MagnitudeMax(vtkPVArrayInformation* arrayInfo)
{
  if (!arrayInfo)
  {
    return 0.0;
  }
  // Component -1: vector magnitude range (or scalar range for 1-comp arrays).
  const double* range = arrayInfo->GetComponentRange(-1);
  if (!range)
  {
    return 0.0;
  }
  return std::max(std::abs(range[0]), std::abs(range[1]));
}

//---------------------------------------------------------------------------
vtkPVArrayInformation* vtkSMAnimatedStreamlineIntegrationScaleDomain::ResolveVelocityArray(
  vtkPVDataInformation* info)
{
  if (!info)
  {
    return nullptr;
  }

  // Optional explicit ArraySelection (same 5-tuple convention as ArrayRangeDomain).
  if (vtkSMProperty* arrayProp = this->GetRequiredProperty("ArraySelection"))
  {
    vtkSMUncheckedPropertyHelper arraySelectionHelper(arrayProp);
    if (arraySelectionHelper.GetNumberOfElements() >= 5)
    {
      const char* arrayName = arraySelectionHelper.GetAsString(4);
      const int fieldAssociation = arraySelectionHelper.GetAsInt(3);
      if (arrayName && arrayName[0] && std::strcmp(arrayName, "None") != 0)
      {
        if (auto* ai = info->GetArrayInformation(arrayName, fieldAssociation))
        {
          return ai;
        }
      }
    }
    else if (arraySelectionHelper.GetNumberOfElements() >= 1)
    {
      // Plain string array name (RepresentedArrayListDomain style).
      const char* arrayName = arraySelectionHelper.GetAsString(0);
      if (arrayName && arrayName[0] && std::strcmp(arrayName, "None") != 0)
      {
        if (auto* ai = info->GetArrayInformation(arrayName, vtkDataObject::POINT))
        {
          return ai;
        }
        if (auto* ai = info->GetArrayInformation(arrayName, vtkDataObject::CELL))
        {
          return ai;
        }
      }
    }
  }

  vtkPVDataSetAttributesInformation* pd = info->GetPointDataInformation();
  if (!pd)
  {
    return nullptr;
  }

  if (auto* vectors = pd->GetAttributeInformation(vtkDataSetAttributes::VECTORS))
  {
    return vectors;
  }

  vtkPVArrayInformation* named = nullptr;
  vtkPVArrayInformation* best3 = nullptr;
  double best3Mag = 0.0;
  for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
  {
    vtkPVArrayInformation* ai = pd->GetArrayInformation(i);
    if (!ai || ai->GetNumberOfComponents() < 2)
    {
      continue;
    }
    if (!named && NameLooksLikeVelocity(ai->GetName()))
    {
      named = ai;
    }
    if (ai->GetNumberOfComponents() == 3)
    {
      const double m = MagnitudeMax(ai);
      if (m >= best3Mag)
      {
        best3Mag = m;
        best3 = ai;
      }
    }
  }
  return named ? named : best3;
}

//---------------------------------------------------------------------------
void vtkSMAnimatedStreamlineIntegrationScaleDomain::Update(vtkSMProperty*)
{
  vtkPVDataInformation* info = this->GetInputInformation();
  if (!info)
  {
    return;
  }

  double bounds[6];
  info->GetBounds(bounds);
  if (!vtkMath::AreBoundsInitialized(bounds))
  {
    return;
  }

  vtkBoundingBox box(bounds);
  const double Lmax = box.GetMaxLength();
  if (Lmax <= 0.0 || vtkMath::IsNan(Lmax) || vtkMath::IsInf(Lmax))
  {
    return;
  }

  double Vmax = MagnitudeMax(this->ResolveVelocityArray(info));
  if (Vmax <= 0.0 || vtkMath::IsNan(Vmax) || vtkMath::IsInf(Vmax))
  {
    // No usable velocity: treat as unit speed so Reset still yields num_pulses/L_max.
    Vmax = 1.0;
  }

  const double deltaX = Lmax * Vmax;
  double suggested = this->NumPulses / deltaX;
  if (suggested <= 0.0 || vtkMath::IsNan(suggested) || vtkMath::IsInf(suggested))
  {
    suggested = this->NumPulses;
  }

  // Same shape as BoundsDomain scaled_extent: Reset (default_mode=max) -> suggested.
  std::vector<vtkEntry> entries;
  entries.emplace_back(0.0, suggested);
  this->SetEntries(entries);
}

//---------------------------------------------------------------------------
int vtkSMAnimatedStreamlineIntegrationScaleDomain::ReadXMLAttributes(
  vtkSMProperty* prop, vtkPVXMLElement* element)
{
  if (!this->Superclass::ReadXMLAttributes(prop, element))
  {
    return 0;
  }

  const char* numPulses = element->GetAttribute("num_pulses");
  if (numPulses)
  {
    this->NumPulses = std::atof(numPulses);
    if (this->NumPulses <= 0.0)
    {
      this->NumPulses = 100.0;
    }
  }

  // Prefer max as the Reset target unless XML overrides default_mode.
  if (element->GetAttribute("default_mode") == nullptr)
  {
    this->DefaultDefaultMode = vtkSMDoubleRangeDomain::MAX;
  }
  return 1;
}
