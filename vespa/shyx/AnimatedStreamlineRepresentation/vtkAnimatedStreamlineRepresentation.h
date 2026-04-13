// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkAnimatedStreamlineRepresentation_h
#define vtkAnimatedStreamlineRepresentation_h

#include "vtkAnimatedStreamlineRepresentationModule.h"
#include "vtkGeometryRepresentationWithFaces.h"

class vtkCallbackCommand;
class vtkInformation;
class vtkInformationRequestKey;

class VTKANIMATEDSTREAMLINEREPRESENTATION_EXPORT vtkAnimatedStreamlineRepresentation
  : public vtkGeometryRepresentationWithFaces
{
public:
  static vtkAnimatedStreamlineRepresentation* New();
  vtkTypeMacro(vtkAnimatedStreamlineRepresentation, vtkGeometryRepresentationWithFaces);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(Animate, bool);
  vtkGetMacro(Animate, bool);
  vtkBooleanMacro(Animate, bool);

  vtkSetMacro(OpacityScale, double);
  vtkGetMacro(OpacityScale, double);

  vtkSetMacro(TimeScale, double);
  vtkGetMacro(TimeScale, double);

  vtkSetMacro(IntegrationScale, double);
  vtkGetMacro(IntegrationScale, double);

  vtkSetMacro(Trunc, double);
  vtkGetMacro(Trunc, double);

  vtkSetMacro(Pow, double);
  vtkGetMacro(Pow, double);

  vtkSetStringMacro(AnimationCoordinateArray);
  vtkGetStringMacro(AnimationCoordinateArray);

  vtkSetStringMacro(AnimationCoordinateYArray);
  vtkGetStringMacro(AnimationCoordinateYArray);

protected:
  vtkAnimatedStreamlineRepresentation();
  ~vtkAnimatedStreamlineRepresentation() override;

  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;

  bool AddToView(vtkView* view) override;
  bool RemoveFromView(vtkView* view) override;

  void UpdateShaderReplacements();
  void UpdateAnimationMTime();
  void OnUpdateShader(void* calldata);
  void SyncAnimatedMapperAnimationCoordinateArray();
  static void ShaderCallback(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  vtkCallbackCommand* ShaderObserver;
  double StartTime;
  bool Animate;
  double OpacityScale;
  double TimeScale;
  double IntegrationScale;
  double Trunc;
  double Pow;

  /** Point array copied to tcoord.x for the pulse (default IntegrationTime). */
  char* AnimationCoordinateArray = nullptr;

  /**
   * Optional point array for tcoord.y / mixValue denominator. Default is unset (no array):
   * animCoordy is identically 1 (mapper writes tcoord.y = 1, or vertex path sets 1.0).
   */
  char* AnimationCoordinateYArray = nullptr;

  /**
   * True when the selected point array exists on the input; animCoord is scalar value or vector magnitude in tcoord.x.
   */
  bool CachedUsesIntegrationTimeAttribute = false;

private:
  vtkAnimatedStreamlineRepresentation(const vtkAnimatedStreamlineRepresentation&) = delete;
  void operator=(const vtkAnimatedStreamlineRepresentation&) = delete;
};

#endif

