// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkPulseGlyphRepresentation_h
#define vtkPulseGlyphRepresentation_h

#include "vtkPulseGlyphRepresentationModule.h"
#include "vtkGlyph3DRepresentation.h"

class vtkDataObject;

/**
 * Glyph representation that drives vtkGlyph3DMapper with a time-varying pulse from a point array
 * (default: IntegrationTime), same envelope as Animated Streamline. \c PulseAffectsScale gates
 * applying that envelope to \c PulseGlyphScale; \c PulseAffectsRotation gates Euler angles in
 * \c PulseGlyphOrientation (\c vtkGlyph3DMapper ROTATION mode). \c ArrayAffectScale gates whether
 * \c PulseGlyphScale uses Overall×(envelope + extra-array magnitude×\c ArrayAffectScaleRatio) when
 * those terms apply; \c ArrayAffectScale gates the array term.
 * \c Shuffle maps phase via \c std::minstd_rand seeded only from \c mixValue; with Shuffle, the
 * time-like term is render-frame × TimeScale (not wall time).
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

  /** Multiplies (envelope + array term) for \c PulseGlyphScale: Overall×(E + |array|×ratio). */
  vtkSetMacro(PulseOverallScale, double);
  vtkGetMacro(PulseOverallScale, double);

  vtkSetStringMacro(AnimationCoordinateArray);
  vtkGetStringMacro(AnimationCoordinateArray);

  /** Point array: magnitude×ratio added to envelope for \c PulseGlyphScale when \c ArrayAffectScale
   * is on (None disables). */
  vtkSetStringMacro(ExtraScaleArray);
  vtkGetStringMacro(ExtraScaleArray);

  vtkSetMacro(ArrayAffectScale, bool);
  vtkGetMacro(ArrayAffectScale, bool);
  vtkBooleanMacro(ArrayAffectScale, bool);

  vtkSetMacro(ArrayAffectScaleRatio, double);
  vtkGetMacro(ArrayAffectScaleRatio, double);

  vtkSetMacro(PulseAffectsScale, bool);
  vtkGetMacro(PulseAffectsScale, bool);
  vtkBooleanMacro(PulseAffectsScale, bool);

  vtkSetMacro(PulseAffectsRotation, bool);
  vtkGetMacro(PulseAffectsRotation, bool);
  vtkBooleanMacro(PulseAffectsRotation, bool);

  vtkSetMacro(Shuffle, bool);
  vtkGetMacro(Shuffle, bool);
  vtkBooleanMacro(Shuffle, bool);

  vtkSetVector3Macro(RotationSweep, double);
  vtkGetVectorMacro(RotationSweep, double, 3);

protected:
  vtkPulseGlyphRepresentation();
  ~vtkPulseGlyphRepresentation() override;

  int ProcessViewRequest(
    vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo) override;

  void UpdatePulseScaleArrays(vtkDataObject* dobj);
  void FillPolyDataPulseArray(vtkPolyData* pd);

  char* AnimationCoordinateArray = nullptr;
  char* ExtraScaleArray = nullptr;
  bool ArrayAffectScale = true;
  double ArrayAffectScaleRatio = 1.0;
  bool PulseAffectsScale = true;
  bool PulseAffectsRotation = false;
  bool Shuffle = false;
  double RotationSweep[3] = { 360.0, 360.0, 360.0 };

  bool Animate = true;
  /** Increments once per render when Shuffle+Animate; drives mixValue time term instead of wall time. */
  unsigned long RenderFrame = 0;
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
