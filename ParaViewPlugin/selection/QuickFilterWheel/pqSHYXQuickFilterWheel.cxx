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

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

namespace
{
/// How long the key has to stay down before the wheel shows. Short enough to
/// feel immediate, long enough that a quick tap never flashes the ring.
constexpr int kHoldDelayMs = 200;

/// Key / pointer sampling period. Qt key events are not reliable enough here:
/// a focused VTK render window can consume them, which made the wheel depend on
/// whether the user happened to move the mouse.
constexpr int kPollIntervalMs = 30;

/// VK_OEM_3 = the ` ~ / · key left of 1 (US layout; Chinese layout types ·).
constexpr int kTriggerVirtualKey = 0xC0;

const char* kExtractSurfaceProxy = "DataSetSurfaceFilter";

//-----------------------------------------------------------------------------
/// Physical trigger key press, by Qt key code.
bool IsTriggerKey(int key)
{
  return key == Qt::Key_QuoteLeft || key == Qt::Key_Dead_Grave;
}

//-----------------------------------------------------------------------------
/// True for "`", "~" and "·", so layouts that produce the middle dot still work.
bool IsTriggerText(const QString& text)
{
  return text == QLatin1String("`") || text == QLatin1String("~") ||
    text == QString(QChar(0x00B7));
}

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
/// RenderView whose 3D area contains \a globalPosition (nullptr otherwise).
///
/// Pure geometry on purpose: QApplication::widgetAt() is unreliable above the
/// native VTK child window, which made the wheel come and go depending on what
/// exactly was under the cursor. The window also has to be the active one, so
/// the key pressed in another application does not arm the wheel.
pqRenderView* RenderViewAt(const QPoint& globalPosition)
{
  QWidget* activeWindow = QApplication::activeWindow();
  if (!activeWindow)
  {
    return nullptr; // ParaView is not the foreground application
  }
  pqApplicationCore* core = pqApplicationCore::instance();
  pqServerManagerModel* model = core ? core->getServerManagerModel() : nullptr;
  if (!model)
  {
    return nullptr;
  }
  const QList<pqView*> views = model->findItems<pqView*>();
  for (pqView* view : views)
  {
    auto* renderView = qobject_cast<pqRenderView*>(view);
    QWidget* widget = renderView ? renderView->widget() : nullptr;
    if (!widget || !widget->isVisible() || widget->window() != activeWindow)
    {
      continue;
    }
    if (widget->rect().contains(widget->mapFromGlobal(globalPosition)))
    {
      return renderView;
    }
  }
  return nullptr;
}

//-----------------------------------------------------------------------------
/// True when keyboard focus sits in a text input, where the key has to stay a
/// character instead of a gesture.
bool TextInputHasFocus()
{
  QWidget* focus = QApplication::focusWidget();
  if (!focus)
  {
    return false;
  }
  return qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus) ||
    qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus);
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
    [this]() { this->showWheelAt(QCursor::pos()); });

  this->PollTimer = new QTimer(this);
  this->PollTimer->setInterval(kPollIntervalMs);
  QObject::connect(this->PollTimer, &QTimer::timeout, this, [this]() { this->updateState(); });
  this->PollTimer->start();

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
  delete this->ShowTimer;
  this->ShowTimer = nullptr;
  delete this->PollTimer;
  this->PollTimer = nullptr;
}

//-----------------------------------------------------------------------------
bool pqSHYXQuickFilterWheel::triggerKeyDown() const
{
#if defined(Q_OS_WIN)
  // Polled so a render window that eats key events cannot break the gesture.
  return (::GetAsyncKeyState(kTriggerVirtualKey) & 0x8000) != 0;
#else
  return this->TriggerKeyHeld;
#endif
}

//-----------------------------------------------------------------------------
bool pqSHYXQuickFilterWheel::uiBusy() const
{
  return QApplication::mouseButtons() != Qt::NoButton || QApplication::activePopupWidget() ||
    QApplication::activeModalWidget() || TextInputHasFocus() || this->SuppressedUntilKeyRelease;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::updateState()
{
  if (!this->ShowTimer || !this->PollTimer)
  {
    return;
  }
  const bool down = this->triggerKeyDown();
  if (!down)
  {
    // Key is up: whatever the user dismissed may open again on the next press.
    if (this->ShowTimer->isActive())
    {
      this->ShowTimer->stop();
    }
    this->HoldArmed = false;
    this->SuppressedUntilKeyRelease = false;
    if (this->Overlay && this->Overlay->isVisible())
    {
      this->hideOverlay();
    }
    return;
  }

  // Key is held: the wheel already open stays put (the pointer may leave the
  // ring while the user aims at a sector; mouse events handle the hover).
  if (this->Overlay && this->Overlay->isVisible())
  {
    return;
  }

  const QPoint cursor = QCursor::pos();
  if (this->uiBusy() || !RenderViewAt(cursor))
  {
    if (this->ShowTimer->isActive())
    {
      this->ShowTimer->stop();
    }
    this->HoldArmed = false;
    return;
  }

  if (!this->HoldArmed)
  {
    this->HoldArmed = true;
    this->ShowTimer->start();
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
      const bool trigger =
        IsTriggerKey(keyEvent->key()) || IsTriggerText(keyEvent->text());
      if (trigger)
      {
        this->TriggerKeyHeld = true;
        if (!keyEvent->isAutoRepeat())
        {
          this->updateState();
        }
        // Swallow the key only once the wheel owns the gesture, so typing ` in
        // an unrelated field still works.
        return wheelVisible;
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
      const bool trigger =
        IsTriggerKey(keyEvent->key()) || IsTriggerText(keyEvent->text());
      if (trigger)
      {
        // GetAsyncKeyState may already report the release; force the event state.
        this->TriggerKeyHeld = false;
        if (!keyEvent->isAutoRepeat())
        {
          this->updateState();
        }
        return wheelVisible;
      }
      return false;
    }

    case QEvent::MouseButtonPress:
    {
      if (!wheelVisible)
      {
        // A tap that turns into a click or a drag is not a wheel gesture; do not
        // pop the ring when the button comes back up with the key still held.
        if (this->ShowTimer)
        {
          this->ShowTimer->stop();
        }
        this->HoldArmed = false;
        if (this->triggerKeyDown())
        {
          this->SuppressedUntilKeyRelease = true;
        }
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
      if (wheelVisible)
      {
        this->updateHover(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return false;
      }
      // Armed or not, the poll timer picks this up; nothing to do per move.
      return false;
    }

    case QEvent::ApplicationDeactivate:
    {
      // Ignore the deactivation our own overlay may cause on some platforms.
      if (this->Overlay && this->Overlay->isActiveWindow())
      {
        return false;
      }
      this->dismissWheel();
      return false;
    }

    default:
      break;
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::showWheelAt(const QPoint& globalPosition)
{
  this->HoldArmed = false;

  // Re-check right before showing: the pointer may have left the view, a button
  // may have gone down, or the key may already be released.
  if (!this->triggerKeyDown() || this->uiBusy() || !RenderViewAt(globalPosition))
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
void pqSHYXQuickFilterWheel::hideOverlay()
{
  if (this->Overlay)
  {
    this->Overlay->hide();
    this->Overlay->setHovered(-1);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheel::dismissWheel()
{
  if (this->ShowTimer)
  {
    this->ShowTimer->stop();
  }
  this->HoldArmed = false;
  const bool wasVisible = this->Overlay && this->Overlay->isVisible();
  this->hideOverlay();
  this->Items.clear();
  if (wasVisible)
  {
    // Do not reopen just because the key is still held.
    this->SuppressedUntilKeyRelease = true;
  }
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
