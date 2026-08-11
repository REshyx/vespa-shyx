#include "pqSHYXGrowSelectionWithSimilarViewFrameActions.h"

#include "pqSHYXGrowSelectionWithSimilarController.h"

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
constexpr char kActionObjectName[] = "actionSHYXGrowSelectionWithSimilar";

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
pqSHYXGrowSelectionWithSimilarViewFrameActions::pqSHYXGrowSelectionWithSimilarViewFrameActions(
  QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXGrowSelectionWithSimilarViewFrameActions::
  ~pqSHYXGrowSelectionWithSimilarViewFrameActions() = default;

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarViewFrameActions::frameConnected(
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
    QIcon(":/VESPA/SHYX_Grow_Selection_With_Similar.svg"),
    tr("Grow selection with similar normals"));
  action->setObjectName(QLatin1String(kActionObjectName));
  action->setCheckable(false);

  new pqSHYXGrowSelectionWithSimilarController(renderView, frame, action, action);
}

//-----------------------------------------------------------------------------
void pqSHYXGrowSelectionWithSimilarViewFrameActions::installOnExistingViews()
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
