// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef pqHologramAnimationManager_h
#define pqHologramAnimationManager_h

#include <QObject>

#include <set>

class pqView;

class pqHologramAnimationManager : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqHologramAnimationManager(QObject* p = nullptr);
  ~pqHologramAnimationManager() override;

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
  Q_DISABLE_COPY(pqHologramAnimationManager)
};

#endif
