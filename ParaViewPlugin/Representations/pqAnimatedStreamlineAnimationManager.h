// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef pqAnimatedStreamlineAnimationManager_h
#define pqAnimatedStreamlineAnimationManager_h

#include <QElapsedTimer>
#include <QObject>

#include <set>

class QTimer;
class pqView;

/**
 * Drives the Animated Streamline shader animation: whenever a render view finishes rendering it
 * asks for the next render, so the pulse keeps moving even though the scene has a single time step.
 *
 * The next render is always scheduled through one single-shot QTimer instead of being issued
 * directly from endRender, so the loop cannot recurse into the render call.
 *
 * Pacing: with Time source = Render frames and the representation's LockToFrameRate on (default),
 * the next render is scheduled so that render start-to-start takes 1 / Frame rate seconds. Render
 * frames advances the shader clock by exactly 1 / Frame rate per rendered frame, so locking the
 * loop makes the preview play at the speed the exported image sequence will play back at, no
 * matter how fast the machine renders. With LockToFrameRate off the loop runs as fast as the
 * machine allows (a faster machine then advances the Render frames clock faster). Real time mode
 * is never throttled: its clock is the wall clock, so its speed does not depend on the frame rate.
 */
class pqAnimatedStreamlineAnimationManager : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqAnimatedStreamlineAnimationManager(QObject* p = nullptr);
  ~pqAnimatedStreamlineAnimationManager() override;

  void onShutdown() {}
  void onStartup() {}

public Q_SLOTS:
  void onViewAdded(pqView*);
  void onViewRemoved(pqView*);

protected Q_SLOTS:
  void onRenderEnded();
  // Renders every view that asked for another animation frame and restarts the pacing clock.
  void renderPending();

protected:
  std::set<pqView*> Views;
  /** Views whose endRender requested the next animation frame. */
  std::set<pqView*> PendingViews;
  QTimer* AnimationTimer = nullptr;
  /** Start time of the last batch of animation renders, used to keep 1 / Frame rate pacing. */
  QElapsedTimer SinceLastRender;
  /** Highest Frame rate among the pending animating representations. */
  double PendingFrameRate = 0.0;
  /** True when one of the pending representations has LockToFrameRate off (render unthrottled). */
  bool PendingUnlimited = false;

private:
  Q_DISABLE_COPY(pqAnimatedStreamlineAnimationManager)
};

#endif
