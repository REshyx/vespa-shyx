// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "pqHologramAnimationManager.h"

#include "pqApplicationCore.h"
#include "pqRenderView.h"
#include "pqRepresentation.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkSMPropertyHelper.h"
#include "vtkSMRepresentationProxy.h"

#include <string>

pqHologramAnimationManager::pqHologramAnimationManager(QObject* p)
  : Superclass(p)
{
  pqServerManagerModel* smmodel = pqApplicationCore::instance()->getServerManagerModel();
  QObject::connect(smmodel, SIGNAL(preViewAdded(pqView*)), this, SLOT(onViewAdded(pqView*)));
  QObject::connect(smmodel, SIGNAL(preViewRemoved(pqView*)), this, SLOT(onViewRemoved(pqView*)));

  Q_FOREACH (pqView* view, smmodel->findItems<pqView*>())
  {
    this->onViewAdded(view);
  }
}

pqHologramAnimationManager::~pqHologramAnimationManager() = default;

void pqHologramAnimationManager::onRenderEnded()
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
    const int animate = repr->GetProperty("HG_Animate")
      ? vtkSMPropertyHelper(repr, "HG_Animate").GetAsInt()
      : 0;

    if (representation && (std::string(representation) == "Hologram") && visible && animate)
    {
      view->render();
      break;
    }
  }
}

void pqHologramAnimationManager::onViewAdded(pqView* view)
{
  if (dynamic_cast<pqRenderView*>(view))
  {
    this->Views.insert(view);
    QObject::connect(view, SIGNAL(endRender()), this, SLOT(onRenderEnded()));
  }
}

void pqHologramAnimationManager::onViewRemoved(pqView* view)
{
  if (dynamic_cast<pqRenderView*>(view))
  {
    QObject::disconnect(view, SIGNAL(endRender()), this, SLOT(onRenderEnded()));
    this->Views.erase(view);
  }
}
