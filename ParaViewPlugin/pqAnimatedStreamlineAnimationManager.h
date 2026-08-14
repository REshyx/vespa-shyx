// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef pqAnimatedStreamlineAnimationManager_h
#define pqAnimatedStreamlineAnimationManager_h

#include <QObject>

#include <set>

class pqView;

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

protected:
  std::set<pqView*> Views;

private:
  Q_DISABLE_COPY(pqAnimatedStreamlineAnimationManager)
};

#endif

