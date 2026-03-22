// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "pqAnimatedStreamlineAnimationManager.h"

#include "pqApplicationCore.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkSMPropertyHelper.h"
#include "vtkSMRepresentationProxy.h"

pqAnimatedStreamlineAnimationManager::pqAnimatedStreamlineAnimationManager(QObject* p)
  : QObject(p)
{
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

  const QList<pqRepresentation*> reprs = view->getRepresentations();
  for (int i = 0; i < reprs.count(); ++i)
  {
    vtkSMRepresentationProxy* repr = vtkSMRepresentationProxy::SafeDownCast(reprs[i]->getProxy());
    if (!repr || !repr->GetProperty("Representation"))
    {
      continue;
    }

    const char* representation = vtkSMPropertyHelper(repr, "Representation").GetAsString();
    const int visible = vtkSMPropertyHelper(repr, "Visibility").GetAsInt();
    const int animate = repr->GetProperty("AS_Animate")
      ? vtkSMPropertyHelper(repr, "AS_Animate").GetAsInt()
      : 0;

    if (representation && (std::string(representation) == "Animated Streamline") && visible &&
      animate)
    {
      view->render();
      break;
    }
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
  }
}

