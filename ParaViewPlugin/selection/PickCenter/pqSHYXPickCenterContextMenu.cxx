#include "pqSHYXPickCenterContextMenu.h"

#include "pqSHYXStatusNotifier.h"

#include "pqRenderView.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMTrace.h"

#include <QAction>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QWidget>

#include <cmath>
#include <limits>

namespace
{
/// viewPoint is the right-button press, in the render widget (origin top-left).
/// Convert it the same way QVTKInteractorAdapter::ProcessEvent feeds
/// vtkRenderWindowInteractor::SetEventInformationFlipY, which is the display
/// position the toolbar Pick Center consumes from a left-click.
bool displayPositionForClick(pqRenderView* view, const QPoint& viewPoint, int posxy[2])
{
  if (!view || !posxy)
  {
    return false;
  }
  QWidget* widget = view->widget();
  vtkSMRenderViewProxy* proxy = view->getRenderViewProxy();
  if (!widget || !proxy)
  {
    return false;
  }

  const double ratio = widget->devicePixelRatioF();
  const double dpr = ratio > 0.0 ? ratio : 1.0;
  constexpr double tol = 1e-5;
  const int xDev = static_cast<int>(viewPoint.x() * dpr + tol);
  const int yDev = static_cast<int>(viewPoint.y() * dpr + tol);

  int width = 0;
  int height = 0;
  if (vtkRenderWindowInteractor* iren = proxy->GetInteractor())
  {
    const int* size = iren->GetSize();
    width = size[0];
    height = size[1];
  }
  if (width <= 0)
  {
    width = static_cast<int>(widget->width() * dpr + tol);
  }
  if (height <= 0)
  {
    height = static_cast<int>(widget->height() * dpr + tol);
  }

  posxy[0] = xDev;
  posxy[1] = height - yDev - 1;
  if (posxy[0] < 0)
  {
    posxy[0] = 0;
  }
  if (posxy[1] < 0)
  {
    posxy[1] = 0;
  }
  if (width > 0 && posxy[0] >= width)
  {
    posxy[0] = width - 1;
  }
  if (height > 0 && posxy[1] >= height)
  {
    posxy[1] = height - 1;
  }
  return true;
}

void pickCenterAtRightClick(pqRenderView* view, const QPoint& viewPoint)
{
  if (!view)
  {
    return;
  }
  vtkSMRenderViewProxy* proxy = view->getRenderViewProxy();
  if (!proxy)
  {
    return;
  }

  int posxy[2] = { 0, 0 };
  if (!displayPositionForClick(view, viewPoint, posxy))
  {
    pqSHYXStatusNotifier::warning(
      QCoreApplication::translate("pqSHYXPickCenterContextMenu",
        "Pick Center could not read the right-click position."));
    return;
  }

  double center[3] = { std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
  double normal[3] = { std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };

  SM_SCOPED_TRACE(PropertiesModified)
    .arg("proxy", proxy)
    .arg("comment",
      qPrintable(QCoreApplication::translate(
        "pqSHYXPickCenterContextMenu", " update center of rotation")));
  proxy->ConvertDisplayToPointOnSurface(posxy, center, normal);
  if (std::isnan(center[0]) && std::isnan(center[1]) && std::isnan(center[2]))
  {
    pqSHYXStatusNotifier::warning(
      QCoreApplication::translate("pqSHYXPickCenterContextMenu",
        "Pick Center found no point at the right-click."));
    return;
  }

  view->setCenterOfRotation(center);
  view->render();
  pqSHYXStatusNotifier::info(QCoreApplication::translate(
    "pqSHYXPickCenterContextMenu", "Pick Center set at the right-click."));
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXPickCenterContextMenu::pqSHYXPickCenterContextMenu(QObject* parentObject)
  : Superclass(parentObject)
{
}

//-----------------------------------------------------------------------------
pqSHYXPickCenterContextMenu::~pqSHYXPickCenterContextMenu() = default;

//-----------------------------------------------------------------------------
bool pqSHYXPickCenterContextMenu::contextMenu(QMenu* menu, pqView* viewContext,
  const QPoint& viewPoint, pqRepresentation*, const QStringList&) const
{
  auto* view = qobject_cast<pqRenderView*>(viewContext);
  if (!menu || !view)
  {
    return false;
  }

  const QString linkLabel =
    QCoreApplication::translate("pqDefaultContextMenu", "Link Camera...");
  QAction* linkCamera = nullptr;
  const QList<QAction*> acts = menu->actions();
  for (QAction* act : acts)
  {
    if (act && act->text() == linkLabel)
    {
      linkCamera = act;
      break;
    }
  }

  if (linkCamera)
  {
    const int idx = acts.indexOf(linkCamera);
    if (idx <= 0 || !acts[idx - 1]->isSeparator())
    {
      auto* sep = new QAction(menu);
      sep->setSeparator(true);
      menu->insertAction(linkCamera, sep);
    }
  }
  else if (!acts.isEmpty() && !acts.last()->isSeparator())
  {
    menu->addSeparator();
  }

  auto* action = new QAction(
    QIcon(":/pqWidgets/Icons/pqPickCenter.svg"), tr("Pick Center"), menu);
  action->setObjectName(QStringLiteral("actionSHYXPickCenter"));
  action->setToolTip(tr(
    "Set the center of rotation to the surface under this right-click. "
    "Does not wait for another left-click."));
  QPointer<pqRenderView> viewPtr(view);
  const QPoint click = viewPoint;
  QObject::connect(action, &QAction::triggered, menu,
    [viewPtr, click]() { pickCenterAtRightClick(viewPtr, click); });

  if (linkCamera)
  {
    menu->insertAction(linkCamera, action);
  }
  else
  {
    menu->addAction(action);
  }

  return false;
}
