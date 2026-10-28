#include "pqSHYXAnimatedStreamlineTimingWidget.h"

#include "pqCoreUtilities.h"
#include "pqPropertiesPanel.h"

#include "vtkCommand.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"

#include <QLabel>
#include <QString>
#include <QVBoxLayout>

#include <cmath>

namespace
{
/**
 * Sibling lookup that works both on the subproxy (plain names) and on the composed
 * GeometryRepresentation, where the same property object is exposed as AS_*.
 */
vtkSMProperty* FindSiblingProperty(
  vtkSMProxy* proxy, const char* exposedName, const char* smName)
{
  if (!proxy)
  {
    return nullptr;
  }
  if (vtkSMProperty* p = proxy->GetProperty(exposedName))
  {
    return p;
  }
  return proxy->GetProperty(smName);
}

bool IsNoneOrEmpty(const QString& s)
{
  return s.isEmpty() || s == QLatin1String("None") || s == QLatin1String("(Uniform)");
}

QString FormatNumber(double v)
{
  const double rounded = std::round(v);
  if (std::abs(v - rounded) < 1e-6)
  {
    return QString::number(static_cast<long long>(rounded));
  }
  return QString::number(v, 'g', 4);
}

QString GetStringValue(vtkSMProperty* prop)
{
  if (!prop)
  {
    return QString();
  }
  vtkSMPropertyHelper helper(prop);
  if (helper.GetNumberOfElements() < 1)
  {
    return QString();
  }
  const char* s = helper.GetAsString(0);
  return s ? QString::fromUtf8(s) : QString();
}
}

//-----------------------------------------------------------------------------
pqSHYXAnimatedStreamlineTimingWidget::pqSHYXAnimatedStreamlineTimingWidget(
  vtkSMProxy* smproxy, vtkSMProperty* smproperty, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
{
  (void)smproperty;
  // Keep the "Frames per cycle" label column: the value widget only shows the number.
  this->setShowLabel(true);

  auto* layout = new QVBoxLayout(this);
  const int margin = pqPropertiesPanel::suggestedMargin();
  layout->setContentsMargins(0, 0, margin, 0);
  layout->setSpacing(0);

  this->Label = new QLabel(this);
  this->Label->setObjectName("PulseCycleInfo");
  this->Label->setWordWrap(true);
  layout->addWidget(this->Label);

  this->FrameRateProperty = FindSiblingProperty(smproxy, "AS_FrameRate", "FrameRate");
  this->LockToFrameRateProperty =
    FindSiblingProperty(smproxy, "AS_LockToFrameRate", "LockToFrameRate");
  this->TimeScaleProperty = FindSiblingProperty(smproxy, "AS_TimeScale", "TimeScale");
  this->TimeSourceProperty = FindSiblingProperty(smproxy, "AS_TimeSource", "TimeSource");
  this->YArrayProperty =
    FindSiblingProperty(smproxy, "AS_AnimationCoordinateYArray", "AnimationCoordinateYArray");

  vtkSMProperty* watched[] = { this->FrameRateProperty, this->LockToFrameRateProperty,
    this->TimeScaleProperty, this->TimeSourceProperty, this->YArrayProperty };
  for (vtkSMProperty* prop : watched)
  {
    if (prop)
    {
      pqCoreUtilities::connect(prop, vtkCommand::ModifiedEvent, this, SLOT(refresh()));
    }
  }

  this->refresh();
}

//-----------------------------------------------------------------------------
pqSHYXAnimatedStreamlineTimingWidget::~pqSHYXAnimatedStreamlineTimingWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXAnimatedStreamlineTimingWidget::refresh()
{
  if (!this->Label)
  {
    return;
  }

  const double frameRate = this->FrameRateProperty
    ? vtkSMPropertyHelper(this->FrameRateProperty).GetAsDouble()
    : 30.0;
  const double timeScale = this->TimeScaleProperty
    ? vtkSMPropertyHelper(this->TimeScaleProperty).GetAsDouble()
    : 0.4;
  const int timeSource = this->TimeSourceProperty
    ? vtkSMPropertyHelper(this->TimeSourceProperty).GetAsInt()
    : 0;
  const bool locked = this->LockToFrameRateProperty
    ? vtkSMPropertyHelper(this->LockToFrameRateProperty).GetAsInt() != 0
    : true;

  const QString yArrayName = GetStringValue(this->YArrayProperty);
  const bool perPointDivisor = !IsNoneOrEmpty(yArrayName);

  const double rate = (frameRate > 1e-9) ? frameRate : 1.0;
  const double ts = std::abs(timeScale);

  if (ts < 1e-9)
  {
    this->Label->setText(tr("none (Time scale = 0 keeps the pulse static)"));
    return;
  }
  if (perPointDivisor)
  {
    this->Label->setText(
      tr("per point (Animation coordinate Y = %1 divides the time term, so each point has its own "
         "period)")
        .arg(yArrayName));
    return;
  }

  const double framesPerCycle = rate / ts;
  const double secondsPerCycle = 1.0 / ts;
  const double phasePerFrame = timeScale / rate;
  const bool whole = std::abs(framesPerCycle - std::round(framesPerCycle)) < 1e-3;

  if (timeSource == 1)
  {
    // Render frames: every image advances the phase by exactly Time scale / Frame rate.
    QString text = tr("%1 frames = %2 s at %3 fps (%4 per frame)")
                     .arg(FormatNumber(framesPerCycle))
                     .arg(FormatNumber(secondsPerCycle))
                     .arg(FormatNumber(rate))
                     .arg(QString::number(phasePerFrame, 'g', 3));
    if (!whole)
    {
      text += tr("  - not a whole number: an exported sequence will not loop seamlessly");
    }
    text += locked
      ? tr("\nPreview locked to %1 fps: the animation runs at the speed of the exported sequence")
          .arg(FormatNumber(rate))
      : tr("\nPreview not locked: the animation runs as fast as this machine renders");
    this->Label->setText(text);
    return;
  }

  this->Label->setText(
    tr("%1 s (~%2 frames at %3 fps); Render frames mode steps a fixed %4 per frame")
      .arg(FormatNumber(secondsPerCycle))
      .arg(FormatNumber(framesPerCycle))
      .arg(FormatNumber(rate))
      .arg(QString::number(phasePerFrame, 'g', 3)));
}
