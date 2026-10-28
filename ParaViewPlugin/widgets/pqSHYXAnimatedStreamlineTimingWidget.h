#ifndef pqSHYXAnimatedStreamlineTimingWidget_h
#define pqSHYXAnimatedStreamlineTimingWidget_h

#include "pqPropertyWidget.h"

class QLabel;
class vtkSMProperty;
class vtkSMProxy;

/**
 * Read-only label showing the pulse period of the Animated Streamline representation.
 *
 * In Render frames mode the shader time term is FrameCount / FrameRate, so one pulse cycle
 * (fract() wrapping once) takes
 *
 *     frames per cycle = FrameRate / TimeScale        (uniform animCoordy = 1)
 *
 * i.e. TimeScale / (FrameRate * animCoordy) of phase per rendered frame. The number is pure
 * arithmetic on two client-side SM properties, so it is recomputed whenever Frame rate, Time
 * scale, Time source, Lock to frame rate or the Animation coordinate Y array changes - no server
 * round trip, and it cannot go stale the way an information-only SM property would.
 *
 * Attached through panel_widget="shyx_animated_streamline_timing" on an information-only
 * FramesPerCycle property (see AnimatedStreamlineRepresentation.xml).
 */
class pqSHYXAnimatedStreamlineTimingWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXAnimatedStreamlineTimingWidget(
    vtkSMProxy* smproxy, vtkSMProperty* smproperty, QWidget* parent = nullptr);
  ~pqSHYXAnimatedStreamlineTimingWidget() override;

private Q_SLOTS:
  void refresh();

private:
  QLabel* Label = nullptr;
  vtkSMProperty* FrameRateProperty = nullptr;
  vtkSMProperty* LockToFrameRateProperty = nullptr;
  vtkSMProperty* TimeScaleProperty = nullptr;
  vtkSMProperty* TimeSourceProperty = nullptr;
  vtkSMProperty* YArrayProperty = nullptr;

  Q_DISABLE_COPY(pqSHYXAnimatedStreamlineTimingWidget)
};

#endif
