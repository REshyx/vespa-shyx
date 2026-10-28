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

  /**
   * Where the shader time term (time * timeScale in mixValue) comes from.
   *
   * TimeSourceRealTime (0, default): seconds of wall-clock time since the representation was
   * created. Every rendered frame advances by however long the previous frame took, so a rendered
   * image sequence is not continuous - a slow frame moves the pulse further than a fast one, and
   * the same frame rendered twice gives a different pulse phase.
   *
   * TimeSourceRenderFrames (1): a counter incremented once per rendered frame, divided by
   * FrameRate to give seconds. Every frame advances the phase by exactly 1/FrameRate seconds, so
   * image sequences exported through Save Animation / Save Screenshot are continuous and
   * reproducible regardless of per-frame render cost. Switching to this mode resets the counter
   * to 0 so every export starts at the same frame.
   */
  static constexpr int TimeSourceRealTime = 0;
  static constexpr int TimeSourceRenderFrames = 1;
  void SetTimeSource(int source);
  vtkGetMacro(TimeSource, int);
  void SetTimeSourceToRealTime() { this->SetTimeSource(TimeSourceRealTime); }
  void SetTimeSourceToRenderFrames() { this->SetTimeSource(TimeSourceRenderFrames); }

  /** Frames per second used to convert the render-frame counter into shader time (default 30). */
  vtkSetClampMacro(FrameRate, double, 1e-3, 1e6);
  vtkGetMacro(FrameRate, double);

  /**
   * Render frames mode only. When on (default) the client-side animation loop paces the
   * interactive preview at FrameRate rendered frames per second, so what you see is the speed the
   * exported image sequence will play back at, no matter how fast the machine can render. Off: the
   * loop re-renders as fast as the machine allows, so TimeSourceRenderFrames then plays back faster
   * on a faster machine. Real time mode is unaffected (wall-clock driven). Read by
   * pqAnimatedStreamlineAnimationManager; the shader itself is not affected.
   */
  vtkSetMacro(LockToFrameRate, bool);
  vtkGetMacro(LockToFrameRate, bool);
  vtkBooleanMacro(LockToFrameRate, bool);

  /**
   * Render-frame counter used by TimeSourceRenderFrames: it increments once per rendered frame
   * (only while Animate is on) and is the frame number divided by FrameRate that reaches the
   * shader. Set it to seek (e.g. 0 to restart the sequence at a known phase).
   */
  void SetFrameCount(int count);
  int GetFrameCount();

  /**
   * Rendered frames per pulse cycle: FrameRate / |TimeScale| while the animation coordinate
   * divisor is uniform (animCoordy = 1, i.e. no Animation coordinate Y array). The shader phase
   * advances by TimeScale / (FrameRate * animCoordy) per rendered frame, so this is the number of
   * frames after which fract() wraps once. Returns 0 when TimeScale is 0 (static pulse).
   * Informational only: exposed as the read-only FramesPerCycle property; the client panel widget
   * recomputes it live, so it also reflects an Animation coordinate Y array.
   */
  double GetFramesPerCycle() const;

  vtkSetMacro(IntegrationScale, double);
  vtkGetMacro(IntegrationScale, double);

  vtkSetMacro(Trunc, double);
  vtkGetMacro(Trunc, double);

  vtkSetMacro(Pow, double);
  vtkGetMacro(Pow, double);

  /**
   * Give every connected streamline (polyline or tube) its own random pulse phase, so streamlines
   * whose seed points lie on a line or a plane do not advance as one layered sheet.
   */
  vtkSetMacro(Randomize, bool);
  vtkGetMacro(Randomize, bool);
  vtkBooleanMacro(Randomize, bool);

  /** Random phase spread in pulse cycles (0 = none, >= 1 = fully de-synchronized). */
  vtkSetMacro(RandomizeScale, double);
  vtkGetMacro(RandomizeScale, double);

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
  void SyncAnimatedMapperSettings();
  static void ShaderCallback(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  vtkCallbackCommand* ShaderObserver;
  double StartTime;
  bool Animate;
  double OpacityScale;
  double TimeScale;

  /** 0 = wall clock, 1 = render-frame counter (see SetTimeSource). */
  int TimeSource = TimeSourceRealTime;
  /** Frames per second: converts RenderFrame into shader time. */
  double FrameRate = 30.0;
  /** Caps the interactive render loop at FrameRate (client side only, see SetLockToFrameRate). */
  bool LockToFrameRate = true;
  /** Increments once per rendered frame while TimeSourceRenderFrames + Animate. */
  unsigned long RenderFrame = 0;

  double IntegrationScale;
  double Trunc;
  double Pow;
  bool Randomize;
  double RandomizeScale;

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

