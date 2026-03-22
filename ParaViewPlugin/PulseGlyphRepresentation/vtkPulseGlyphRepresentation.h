// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkPulseGlyphRepresentation_h
#define vtkPulseGlyphRepresentation_h

#include "vtkPulseGlyphRepresentationModule.h"
#include "vtkGlyph3DRepresentation.h"

class vtkDataObject;

/**
 * Glyph representation that drives vtkGlyph3DMapper scaling with a time-varying pulse computed
 * from a point array (default: IntegrationTime) using the same formula as Animated
 * Streamline, evaluated each frame into the \c PulseGlyphScale point array. VTK's OpenGL glyph
 * path then applies that scale in the instancing matrices (GPU instancing), which is equivalent
 * to uniformly scaling each glyph template in the vertex stage.
 */
class VTKPULSEGLYPHREPRESENTATION_EXPORT vtkPulseGlyphRepresentation : public vtkGlyph3DRepresentation
{
public:
  static vtkPulseGlyphRepresentation* New();
  vtkTypeMacro(vtkPulseGlyphRepresentation, vtkGlyph3DRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkSetMacro(Animate, bool);
  vtkGetMacro(Animate, bool);
  vtkBooleanMacro(Animate, bool);

  vtkSetMacro(TimeScale, double);
  vtkGetMacro(TimeScale, double);

  vtkSetMacro(IntegrationScale, double);
  vtkGetMacro(IntegrationScale, double);

  vtkSetMacro(Trunc, double);
  vtkGetMacro(Trunc, double);

  vtkSetMacro(Pow, double);
  vtkGetMacro(Pow, double);

  /** Uniform multiplier applied to the pulse value written to \c PulseGlyphScale (after 0-1 envelope). */
  vtkSetMacro(PulseOverallScale, double);
  vtkGetMacro(PulseOverallScale, double);

  vtkSetStringMacro(AnimationCoordinateArray);
  vtkGetStringMacro(AnimationCoordinateArray);

protected:
  vtkPulseGlyphRepresentation();
  ~vtkPulseGlyphRepresentation() override;

  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;

  void UpdatePulseScaleArrays(vtkDataObject* dobj);
  void FillPolyDataPulseArray(vtkPolyData* pd);

  char* AnimationCoordinateArray = nullptr;
  bool Animate = true;
  double StartTime = 0.0;
  double TimeScale = 0.4;
  double IntegrationScale = 50.0;
  double Trunc = 2.0;
  double Pow = 1.0;
  double PulseOverallScale = 1.0;

private:
  vtkPulseGlyphRepresentation(const vtkPulseGlyphRepresentation&) = delete;
  void operator=(const vtkPulseGlyphRepresentation&) = delete;
};

#endif
