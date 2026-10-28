// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "pqAnimatedStreamlineAnimationManager.h"

#include "pqApplicationCore.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRepresentationProxy.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr double DefaultFrameRate = 30.0;
constexpr double MinFrameRate = 1e-3;

/**
 * What the animation loop needs from the visible Animated Streamline representations of a view.
 * Only the first animating representation is considered, matching the original loop.
 */
struct AnimationRequest
{
  bool animate = false;
  /** TimeSource == RenderFrames: the shader clock advances once per rendered frame. */
  bool renderFrames = false;
  bool lockToFrameRate = true;
  double frameRate = DefaultFrameRate;
};

/**
 * Sibling lookup that works both on the subproxy (plain names) and on the composed
 * representation proxy, where the same property object is exposed as AS_*.
 */
vtkSMProperty* FindProperty(vtkSMProxy* proxy, const char* exposedName, const char* smName)
{
  if (!proxy)
  {
    return nullptr;
  }
  if (vtkSMProperty* prop = proxy->GetProperty(exposedName))
  {
    return prop;
  }
  return proxy->GetProperty(smName);
}

AnimationRequest QueryAnimation(pqView* view)
{
  AnimationRequest request;

  const QList<pqRepresentation*> reprs = view->getRepresentations();
  for (int i = 0; i < reprs.count(); ++i)
  {
    vtkSMRepresentationProxy* repr = vtkSMRepresentationProxy::SafeDownCast(reprs[i]->getProxy());
    if (!repr || !repr->GetProperty("Representation"))
    {
      continue;
    }

    const char* representation = vtkSMPropertyHelper(repr, "Representation").GetAsString();
    if (!representation || (std::string(representation) != "Animated Streamline") ||
      !vtkSMPropertyHelper(repr, "Visibility").GetAsInt())
    {
      continue;
    }

    vtkSMProperty* animateProp = FindProperty(repr, "AS_Animate", "Animate");
    if (!animateProp || !vtkSMPropertyHelper(animateProp).GetAsInt())
    {
      continue;
    }

    request.animate = true;

    // Time source 1 = Render frames (shader clock = frame counter / Frame rate). Real time (0)
    // already advances with the wall clock, so pacing the loop would not change its speed.
    if (vtkSMProperty* sourceProp = FindProperty(repr, "AS_TimeSource", "TimeSource"))
    {
      request.renderFrames = vtkSMPropertyHelper(sourceProp).GetAsInt() == 1;
    }

    if (vtkSMProperty* lockProp = FindProperty(repr, "AS_LockToFrameRate", "LockToFrameRate"))
    {
      request.lockToFrameRate = vtkSMPropertyHelper(lockProp).GetAsInt() != 0;
    }

    if (vtkSMProperty* rateProp = FindProperty(repr, "AS_FrameRate", "FrameRate"))
    {
      const double rate = vtkSMPropertyHelper(rateProp).GetAsDouble();
      if (rate > MinFrameRate)
      {
        request.frameRate = rate;
      }
    }
    break;
  }

  return request;
}
} // namespace

pqAnimatedStreamlineAnimationManager::pqAnimatedStreamlineAnimationManager(QObject* p)
  : QObject(p)
{
  this->AnimationTimer = new QTimer(this);
  this->AnimationTimer->setSingleShot(true);
  this->AnimationTimer->setTimerType(Qt::PreciseTimer);
  QObject::connect(this->AnimationTimer, SIGNAL(timeout()), this, SLOT(renderPending()));

  pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
  QObject::connect(smmodel, SIGNAL(preViewAdded(pqView*)), this, SLOT(onViewAdded(pqView*)));
  QObject::connect(smmodel, SIGNAL(preViewRemoved(pqView*)), this, SLOT(onViewRemoved(pqView*)));

  Q_FOREACH (pqView* view, smmodel->findItems<pqView*>())
  {
    this->onViewAdded(view);
  }
}

pqAnimatedStreamlineAnimationManager::~pqAnimatedStreamlineAnimationManager() = default;

void pqAnimatedStreamlineAnimationManager::onRenderEnded()
{
  pqView* view = dynamic_cast<pqView*>(sender());
  if (!view)
  {
    return;
  }

  const AnimationRequest request = QueryAnimation(view);
  if (!request.animate)
  {
    this->PendingViews.erase(view);
    if (this->PendingViews.empty())
    {
      this->AnimationTimer->stop();
    }
    return;
  }

  this->PendingViews.insert(view);
  if (!request.lockToFrameRate || !request.renderFrames)
  {
    this->PendingUnlimited = true;
  }
  this->PendingFrameRate = std::max(this->PendingFrameRate, request.frameRate);

  // Locked: keep render starts 1 / Frame rate apart. The Render frames shader clock advances by
  // exactly 1 / Frame rate per rendered frame, so this makes the preview run at the final speed
  // instead of at whatever frame rate the machine happens to reach.
  qint64 waitMs = 0;
  if (!this->PendingUnlimited && this->PendingFrameRate > MinFrameRate)
  {
    const double periodMs = 1000.0 / this->PendingFrameRate;
    const double elapsedMs = this->SinceLastRender.isValid()
      ? static_cast<double>(this->SinceLastRender.elapsed())
      : periodMs; // first frame after a pause: render right away
    waitMs = static_cast<qint64>(std::lround(std::max(0.0, periodMs - elapsedMs)));
  }

  this->AnimationTimer->start(static_cast<int>(waitMs));
}

void pqAnimatedStreamlineAnimationManager::renderPending()
{
  const std::set<pqView*> views = this->PendingViews;
  this->PendingViews.clear();
  this->PendingUnlimited = false;
  this->PendingFrameRate = 0.0;
  if (views.empty())
  {
    return;
  }

  // Each render triggers endRender, which re-populates the pending set for the next tick.
  this->SinceLastRender.start();
  for (pqView* view : views)
  {
    view->render();
  }
}

void pqAnimatedStreamlineAnimationManager::onViewAdded(pqView* view)
{
  if (dynamic_cast<pqRenderView*>(view))
  {
    this->Views.insert(view);
    QObject::connect(view, SIGNAL(endRender()), this, SLOT(onRenderEnded()));
  }
}

void pqAnimatedStreamlineAnimationManager::onViewRemoved(pqView* view)
{
  if (dynamic_cast<pqRenderView*>(view))
  {
    QObject::disconnect(view, SIGNAL(endRender()), this, SLOT(onRenderEnded()));
    this->Views.erase(view);
    this->PendingViews.erase(view);
    if (this->PendingViews.empty())
    {
      this->AnimationTimer->stop();
    }
  }
}
