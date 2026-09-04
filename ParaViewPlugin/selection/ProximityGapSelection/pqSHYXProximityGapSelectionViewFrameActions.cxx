#include "pqSHYXProximityGapSelectionViewFrameActions.h"

#include "pqSHYXProximityGapSelectionController.h"

#include "pqApplicationCore.h"
#include "pqRenderView.h"
#include "pqServerManagerModel.h"
#include "pqView.h"
#include "pqViewFrame.h"

#include <QAction>
#include <QApplication>
#include <QHash>
#include <QIcon>
#include <QWidget>

namespace
{
constexpr char kActionObjectName[] = "actionSHYXProximityGapSelection";

pqViewFrame* FindViewFrameForWidget(QWidget* widget)
{
  for (QWidget* p = widget ? widget->parentWidget() : nullptr; p; p = p->parentWidget())
  {
    if (auto* frame = qobject_cast<pqViewFrame*>(p))
    {
      return frame;
    }
  }
  return nullptr;
}

bool WidgetRelated(QWidget* a, QWidget* b)
{
  return a && b && (a == b || a->isAncestorOf(b) || b->isAncestorOf(a));
}
}

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionViewFrameActions::pqSHYXProximityGapSelectionViewFrameActions(
  QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXProximityGapSelectionViewFrameActions::
  ~pqSHYXProximityGapSelectionViewFrameActions() = default;

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionViewFrameActions::frameConnected(
  pqViewFrame* frame, pqView* view)
{
  pqRenderView* renderView = qobject_cast<pqRenderView*>(view);
  if (!frame || !renderView)
  {
    return;
  }

  if (frame->findChild<QAction*>(QLatin1String(kActionObjectName)))
  {
    return;
  }

  QAction* action = frame->addTitleBarAction(
    QIcon(":/VESPA/SHYX_Proximity_Gap_Selection.svg"),
    tr("Proximity gap selection"));
  action->setObjectName(QLatin1String(kActionObjectName));
  action->setCheckable(true);

  new pqSHYXProximityGapSelectionController(renderView, frame, action, action);
}

//-----------------------------------------------------------------------------
void pqSHYXProximityGapSelectionViewFrameActions::installOnExistingViews()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  if (!core || !core->getServerManagerModel())
  {
    return;
  }

  const QList<pqRenderView*> views = core->getServerManagerModel()->findItems<pqRenderView*>();
  QHash<QWidget*, pqRenderView*> widgetToView;
  for (pqRenderView* view : views)
  {
    if (view && view->widget())
    {
      widgetToView.insert(view->widget(), view);
    }
  }

  for (auto it = widgetToView.constBegin(); it != widgetToView.constEnd(); ++it)
  {
    if (pqViewFrame* frame = FindViewFrameForWidget(it.key()))
    {
      this->frameConnected(frame, it.value());
    }
  }

  if (!qApp)
  {
    return;
  }
  const QWidgetList all = qApp->allWidgets();
  for (QWidget* w : all)
  {
    auto* frame = qobject_cast<pqViewFrame*>(w);
    if (!frame)
    {
      continue;
    }
    QWidget* central = frame->centralWidget();
    if (!central)
    {
      continue;
    }
    for (auto it = widgetToView.constBegin(); it != widgetToView.constEnd(); ++it)
    {
      if (WidgetRelated(central, it.key()))
      {
        this->frameConnected(frame, it.value());
        break;
      }
    }
  }
}
