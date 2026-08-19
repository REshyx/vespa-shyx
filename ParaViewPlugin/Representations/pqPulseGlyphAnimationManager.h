// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef pqPulseGlyphAnimationManager_h
#define pqPulseGlyphAnimationManager_h

#include <QObject>

#include <set>

class pqView;

class pqPulseGlyphAnimationManager : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqPulseGlyphAnimationManager(QObject* p = nullptr);
  ~pqPulseGlyphAnimationManager() override;

  void onShutdown() {}
  void onStartup() {}

public Q_SLOTS:
  void onViewAdded(pqView*);
  void onViewRemoved(pqView*);

protected Q_SLOTS:
  void onRenderEnded();

protected:
  std::set<pqView*> Views;

private:
  Q_DISABLE_COPY(pqPulseGlyphAnimationManager)
};

#endif
