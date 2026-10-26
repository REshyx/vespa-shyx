#include "pqSHYXQuickFilterWheel.h"

#include "pqSHYXQuickFilterWheelOverlay.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqObjectBuilder.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqSelectionManager.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqSHYXDeleteSelectionShortcut.h"
#include "pqSHYXStatusNotifier.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkSMSessionProxyManager.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QList>
#include <QMap>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

namespace
{
/// How long Ctrl has to stay down before the wheel shows. Keeps a plain
/// Ctrl+click (add to selection) from flashing the ring.
constexpr int kHoldDelayMs = 250;

const char* kExtractSurfaceProxy = "DataSetSurfaceFilter";

//-----------------------------------------------------------------------------
/// The port that owns an active point/cell selection, or nullptr.
pqOutputPort* SelectionPort()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* selection = core ? core->selectionManager() : nullptr;
  pqOutputPort* port = selection ? selection->getSelectedPort() : nullptr;
  return (port && port->getSelectionInput()) ? port : nullptr;
}

//-----------------------------------------------------------------------------
/// Filter input: the selected port when there is a selection, otherwise the
/// active pipeline object.
pqOutputPort* ActivePort()
{
  if (pqOutputPort* selected = SelectionPort())
  {
    return selected;
  }
  pqPipelineSource* source = pqActiveObjects::instance().activeSource();
  return source ? source->getOutputPort(0) : nullptr;
}

//-----------------------------------------------------------------------------
bool HasFilterPrototype(pqOutputPort* port, const char* name)
{
  if (!port || !port->getServer())
  {
    return false;
  }
  vtkSMSessionProxyManager* pxm = port->getServer()->proxyManager();
  return pxm && pxm->GetPrototypeProxy("filters", name);
}

//-----------------------------------------------------------------------------
/// True when \a globalPosition is over the render widget of any RenderView.
bool IsOverRenderView(const QPoint& globalPosition)
{
  QWidget* under = QApplication::widgetAt(globalPosition);
  pqApplicationCore* core = pqApplicationCore::instance();
  pqServerManagerModel* model = core ? core->getServerManagerModel() : nullptr;
  if (!under || !model)
  {
    return false;
  }
  const QList<pqView*> views = model->findItems<pqView*>();
  for (pqView* view : views)
  {
    if (!qobject_cast<pqRenderView*>(view) || !view->widget())
    {
      continue;
    }
    for (QWidget* widget = under; widget; widget = widget->parentWidget())
    {
      if (widget == view->widget())
      {
        return true;
      }
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
/// Extract Surface (ParaView proxy DataSetSurfaceFilter) on the active port.
void ApplyExtractSurface()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqOutputPort* port = ActivePort();
  if (!core || !port || !port->getSource() || !port->getServer())
  {
    pqSHYXStatusNotifier::warning(
      QObject::tr("SHYX Quick filters: Extract Surface needs an active pipeline object."));
    return;
  }
  if (!HasFilterPrototype(port, kExtractSurfaceProxy))
  {
    pqSHYXStatusNotifier::error(QObject::tr(
      "SHYX Quick filters: Extract Surface is not available in this ParaView build."));
    return;
  }

  QMap<QString, QList<pqOutputPort*>> namedInputs;
  namedInputs[QStringLiteral("Input")] = QList<pqOutputPort*>() << port;

  BEGIN_UNDO_SET(QObject::tr("SHYX Quick filters: Extract Surface"));
  pqPipelineSource* filter = core->getObjectBuilder()->createFilter(
    QStringLiteral("filters"), QString::fromLatin1(kExtractSurfaceProxy), namedInputs, port->getServer());
  if (!filter)
  {
    END_UNDO_SET();
    pqSHYXStatusNotifier::error(
      QObject::tr("SHYX Quick filters: could not create Extract Surface."));
    return;
  }
  core->applyPipeline();
  END_UNDO_SET();

  const QString name = filter->getSMName();
  pqSHYXStatusNotifier::info(QObject::tr("SHYX Quick filters: applied Extract Surface (%1).")
                               .arg(name.isEmpty() ? QString::fromLatin1(kExtractSurfaceProxy) : name));
}
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheel::pqSHYXQuickFilterWheel(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheel::~pqSHYXQuickFilterWheel()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::onStartup()
{
  if (this->Installed || !qApp)
  {
    return;
  }
  this->ShowTimer = new QTimer(this);
  this->ShowTimer->setSingleShot(true);
  this->ShowTimer->setInterval(kHoldDelayMs);
  QObject::connect(this->ShowTimer, &QTimer::timeout, this,
    [this]() { this->showWheelAt(this->PendingPosition); });

  qApp->installEventFilter(this);
  this->Installed = true;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::onShutdown()
{
  if (!this->Installed && !this->Overlay)
  {
    return;
  }
  if (qApp && this->Installed)
  {
    qApp->removeEventFilter(this);
  }
  this->Installed = false;
  this->dismissWheel();
  if (this->Overlay)
  {
    delete this->Overlay;
    this->Overlay = nullptr;
  }
  if (this->ShowTimer)
  {
    delete this->ShowTimer;
    this->ShowTimer = nullptr;
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXQuickFilterWheel::eventFilter(QObject* watched, QEvent* event)
{
  Q_UNUSED(watched);
  if (!this->Installed)
  {
    return false;
  }

  const bool wheelVisible = this->Overlay && this->Overlay->isVisible();

  switch (event->type())
  {
    case QEvent::KeyPress:
    {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      if (keyEvent->key() == Qt::Key_Control)
      {
        if (!keyEvent->isAutoRepeat())
        {
          this->handleCtrlPressed();
        }
        // Never swallow Ctrl itself: Ctrl+click / Ctrl+X keep working.
        return false;
      }
      if (wheelVisible)
      {
        if (keyEvent->key() == Qt::Key_Escape)
        {
          this->dismissWheel();
          return true;
        }
        // Any other key means the user is not picking a quick filter.
        this->dismissWheel();
      }
      return false;
    }

    case QEvent::KeyRelease:
    {
      auto* keyEvent = static_cast<QKeyEvent*>(event);
      if (keyEvent->key() == Qt::Key_Control && !keyEvent->isAutoRepeat())
      {
        this->handleCtrlReleased();
      }
      return false;
    }

    case QEvent::MouseButtonPress:
    {
      if (!wheelVisible)
      {
        return false;
      }
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const QPoint position = mouseEvent->globalPosition().toPoint();
      if (mouseEvent->button() != Qt::LeftButton || !this->Overlay->containsPoint(position))
      {
        // Let the scene have the click; the wheel is only in the way.
        this->dismissWheel();
        return false;
      }
      const int index = this->Overlay->sectorAt(position);
      this->SwallowNextRelease = true;
      this->activate(index);
      return true;
    }

    case QEvent::MouseButtonRelease:
    {
      if (this->SwallowNextRelease)
      {
        this->SwallowNextRelease = false;
        return true;
      }
      return false;
    }

    case QEvent::MouseMove:
    {
      if (!wheelVisible)
      {
        return false;
      }
      this->updateHover(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
      return false;
    }

    case QEvent::ApplicationDeactivate:
    {
      this->dismissWheel();
      return false;
    }

    default:
      break;
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::handleCtrlPressed()
{
  if (!this->ShowTimer || (this->Overlay && this->Overlay->isVisible()))
  {
    return;
  }
  if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
  {
    return;
  }
  const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
  if ((modifiers & (Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier)) != 0)
  {
    return;
  }
  const QPoint cursor = QCursor::pos();
  if (!IsOverRenderView(cursor))
  {
    return;
  }
  this->PendingPosition = cursor;
  this->ShowTimer->start();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::handleCtrlReleased()
{
  if (this->ShowTimer)
  {
    this->ShowTimer->stop();
  }
  this->dismissWheel();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::showWheelAt(const QPoint& globalPosition)
{
  if ((QApplication::keyboardModifiers() & Qt::ControlModifier) == 0)
  {
    return;
  }
  // Ctrl+drag in the scene is not a wheel gesture.
  if (QApplication::mouseButtons() != Qt::NoButton)
  {
    return;
  }
  if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
  {
    return;
  }

  this->Items = this->buildItems();
  if (this->Items.isEmpty())
  {
    return;
  }

  QStringList labels;
  QVector<bool> enabled;
  for (const Item& item : this->Items)
  {
    labels << item.label;
    enabled << item.enabled;
  }

  if (!this->Overlay)
  {
    this->Overlay = new pqSHYXQuickFilterWheelOverlay(nullptr);
  }
  this->Overlay->setItems(labels, enabled);
  this->Overlay->showAtGlobal(globalPosition);
  this->SwallowNextRelease = false;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::dismissWheel()
{
  if (this->ShowTimer)
  {
    this->ShowTimer->stop();
  }
  if (this->Overlay)
  {
    this->Overlay->hide();
    this->Overlay->setHovered(-1);
  }
  this->Items.clear();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::activate(int index)
{
  const QVector<Item> items = this->Items;
  this->dismissWheel();

  if (index < 0 || index >= items.size())
  {
    return; // center hole: cancel
  }
  const Item item = items.at(index);
  if (!item.enabled)
  {
    pqSHYXStatusNotifier::warning(item.unavailableDetail);
    return;
  }
  if (!item.run)
  {
    return;
  }

  // Let the wheel disappear before the pipeline runs.
  const std::function<void()> run = item.run;
  QTimer::singleShot(0, this, [run]() { run(); });
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::updateHover(const QPoint& globalPosition)
{
  if (this->Overlay && this->Overlay->isVisible())
  {
    this->Overlay->setHovered(this->Overlay->sectorAt(globalPosition));
  }
}

//-----------------------------------------------------------------------------
QVector<pqSHYXQuickFilterWheel::Item> pqSHYXQuickFilterWheel::buildItems() const
{
  QVector<Item> items;

  {
    Item item;
    item.label = tr("Delete");
    item.enabled = SelectionPort() != nullptr;
    item.unavailableDetail = tr(
      "SHYX Quick filters: Delete needs an active point or cell selection in the 3D view.");
    item.run = []() { pqSHYXDeleteSelectionShortcut::deleteActiveSelection(); };
    items.push_back(item);
  }

  {
    Item item;
    item.label = tr("Extract Surface");
    pqOutputPort* port = ActivePort();
    item.enabled = HasFilterPrototype(port, kExtractSurfaceProxy);
    if (!port)
    {
      item.unavailableDetail =
        tr("SHYX Quick filters: Extract Surface needs an active pipeline object.");
    }
    else if (!item.enabled)
    {
      item.unavailableDetail =
        tr("SHYX Quick filters: Extract Surface is not available in this ParaView build.");
    }
    item.run = []() { ApplyExtractSurface(); };
    items.push_back(item);
  }

  return items;
}
