#include "pqSHYXSphereSelectionViewFrameActions.h"

#include "pqSHYXSphereSelectionController.h"

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
constexpr char kActionObjectName[] = "actionSHYXSphereSelection";

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
pqSHYXSphereSelectionViewFrameActions::pqSHYXSphereSelectionViewFrameActions(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXSphereSelectionViewFrameActions::~pqSHYXSphereSelectionViewFrameActions() = default;

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionViewFrameActions::frameConnected(pqViewFrame* frame, pqView* view)
{
  pqRenderView* renderView = qobject_cast<pqRenderView*>(view);
  if (!frame || !renderView)
  {
    return;
  }

  // Avoid duplicate buttons when installOnExistingViews() races with newFrame().
  if (frame->findChild<QAction*>(QLatin1String(kActionObjectName)))
  {
    return;
  }

  QAction* action = frame->addTitleBarAction(
    QIcon(":/VESPA/SHYX_Sphere_Selection.svg"), tr("Sphere cell selection"));
  action->setObjectName(QLatin1String(kActionObjectName));
  action->setCheckable(true);

  // Owned by the action so it dies with the frame/action.
  new pqSHYXSphereSelectionController(renderView, frame, action, action);
}

//-----------------------------------------------------------------------------
void pqSHYXSphereSelectionViewFrameActions::installOnExistingViews()
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

  // Path 1: walk parent chain from each view widget (usual case).
  for (auto it = widgetToView.constBegin(); it != widgetToView.constEnd(); ++it)
  {
    if (pqViewFrame* frame = FindViewFrameForWidget(it.key()))
    {
      this->frameConnected(frame, it.value());
    }
  }

  // Path 2: scan all pqViewFrame instances (covers odd parenting / deferred layout).
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
