// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class vtkSMAnimatedStreamlineIntegrationScaleDomain
 * @brief Suggest IntegrationScale = num_pulses / (L_max * V_max).
 *
 * Subclass of vtkSMBoundsDomain so ParaView's double-property widget shows the
 * standard Scale/Reset buttons (they only appear for BoundsDomain / ArrayRangeDomain).
 *
 * Approximates animCoordx range DeltaX as the input AABB longest edge times the max
 * velocity magnitude, then suggests IntegrationScale for about num_pulses cycles:
 *   IntegrationScale = num_pulses / (L_max * V_max)
 *
 * Domain range is (0, suggested) with default_mode=max so Reset adopts the suggestion.
 *
 * Required property: Input. Optional: ArraySelection (string vector array pick);
 * if omitted, uses point-data VECTORS attribute, else a name-matched vector, else
 * the largest 3-component point array magnitude max; falls back to V_max = 1.
 *
 * XML attributes: num_pulses (default 100).
 */

#ifndef vtkSMAnimatedStreamlineIntegrationScaleDomain_h
#define vtkSMAnimatedStreamlineIntegrationScaleDomain_h

#include "vtkAnimatedStreamlineRepresentationModule.h"
#include "vtkSMBoundsDomain.h"

class vtkPVDataInformation;
class vtkPVArrayInformation;

class VTKANIMATEDSTREAMLINEREPRESENTATION_EXPORT vtkSMAnimatedStreamlineIntegrationScaleDomain
  : public vtkSMBoundsDomain
{
public:
  static vtkSMAnimatedStreamlineIntegrationScaleDomain* New();
  vtkTypeMacro(vtkSMAnimatedStreamlineIntegrationScaleDomain, vtkSMBoundsDomain);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  void Update(vtkSMProperty* prop) override;

  vtkGetMacro(NumPulses, double);

protected:
  vtkSMAnimatedStreamlineIntegrationScaleDomain();
  ~vtkSMAnimatedStreamlineIntegrationScaleDomain() override;

  int ReadXMLAttributes(vtkSMProperty* prop, vtkPVXMLElement* element) override;

  vtkPVArrayInformation* ResolveVelocityArray(vtkPVDataInformation* info);
  static double MagnitudeMax(vtkPVArrayInformation* arrayInfo);

  double NumPulses;

private:
  vtkSMAnimatedStreamlineIntegrationScaleDomain(
    const vtkSMAnimatedStreamlineIntegrationScaleDomain&) = delete;
  void operator=(const vtkSMAnimatedStreamlineIntegrationScaleDomain&) = delete;
};

#endif
