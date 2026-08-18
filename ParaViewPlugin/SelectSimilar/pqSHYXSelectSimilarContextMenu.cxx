#include "pqSHYXSelectSimilarContextMenu.h"

#include "pqSHYXGrowSelectionWithSimilarController.h"

#include "pqDataRepresentation.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPointer>

//-----------------------------------------------------------------------------
pqSHYXSelectSimilarContextMenu::pqSHYXSelectSimilarContextMenu(QObject* parentObject)
  : Superclass(parentObject)
{
}

//-----------------------------------------------------------------------------
pqSHYXSelectSimilarContextMenu::~pqSHYXSelectSimilarContextMenu() = default;

//-----------------------------------------------------------------------------
bool pqSHYXSelectSimilarContextMenu::contextMenu(QMenu* menu, pqView*, const QPoint&,
  pqRepresentation* dataContext, const QStringList&) const
{
  if (!menu)
  {
    return false;
  }

  auto* repr = qobject_cast<pqDataRepresentation*>(dataContext);
  if (!pqSHYXGrowSelectionWithSimilarController::HasActiveCellSelection(repr))
  {
    return false;
  }

  const QList<QAction*> acts = menu->actions();
  QAction* insertBefore = nullptr;
  QAction* selectBlock = menu->findChild<QAction*>(QStringLiteral("actionSHYXSelectBlock"));
  if (selectBlock)
  {
    const int idx = acts.indexOf(selectBlock);
    if (idx >= 0)
    {
      if (idx + 1 < acts.size() && acts[idx + 1]->isSeparator())
      {
        insertBefore = (idx + 2 < acts.size()) ? acts[idx + 2] : nullptr;
      }
      else if (idx + 1 < acts.size())
      {
        insertBefore = acts[idx + 1];
      }
    }
  }
  else if (acts.size() >= 2 && acts[1]->isSeparator())
  {
    insertBefore = acts.size() > 2 ? acts[2] : nullptr;
  }
  else if (!acts.isEmpty())
  {
    insertBefore = acts.first();
  }

  auto* similarMenu = new QMenu(tr("Select Similar"), menu);
  similarMenu->setObjectName(QStringLiteral("menuSHYXSelectSimilar"));
  similarMenu->setIcon(QIcon(":/VESPA/SHYX_Grow_Selection_With_Similar.svg"));

  const double deg = pqSHYXGrowSelectionWithSimilarController::DihedralThresholdDegrees();
  QAction* byNormal = similarMenu->addAction(tr("By Normal"));
  byNormal->setObjectName(QStringLiteral("actionSHYXSelectSimilarByNormal"));
  byNormal->setToolTip(
    tr("Grow the current cell selection across all adjacent faces with similar "
       "normals (dihedral ≤ %1°) until nothing more can be added.")
      .arg(deg, 0, 'g', 4));

  QPointer<pqDataRepresentation> reprPtr(repr);
  QObject::connect(byNormal, &QAction::triggered, menu,
    [reprPtr]() { pqSHYXGrowSelectionWithSimilarController::GrowUntilCompleteByNormal(reprPtr); });

  QAction* similarRoot = menu->insertMenu(insertBefore, similarMenu);
  similarRoot->setObjectName(QStringLiteral("actionSHYXSelectSimilar"));
  similarRoot->setToolTip(tr("Grow the current selection by a similarity criterion"));

  auto* sep = new QAction(menu);
  sep->setSeparator(true);
  menu->insertAction(insertBefore, sep);

  return false;
}
