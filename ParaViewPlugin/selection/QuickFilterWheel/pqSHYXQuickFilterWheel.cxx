#include "pqSHYXQuickFilterWheel.h"

#include "pqSHYXQuickFilterWheelConfigDialog.h"
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
#include "pqSHYXStatusNotifier.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkPVProxyDefinitionIterator.h"
#include "vtkPVXMLElement.h"
#include "vtkSMProxyDefinitionManager.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSmartPointer.h"

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
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSet>
#include <QTextEdit>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cstring>

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

/// Where the checked action ids are stored.
constexpr char kSettingsGroup[] = "SHYX/QuickFilterWheel";
constexpr char kSettingsItemsKey[] = "items";

/// One row of the "+" checklist; also the action a checked row runs.
struct CatalogAction
{
  QString id;
  QString label; // full label, used by the dialog
  QString group; // checklist section header
  QString proxyName;
};

/// Checklist group titles.
QString ShyxGroupTitle()
{
  return QObject::tr("SHYX");
}

QString ParaViewGroupTitle()
{
  return QObject::tr("ParaView / VESPA built-in");
}

//-----------------------------------------------------------------------------
/// Wheel sectors are narrow; drop the common "SHYX " prefix.
QString ShortLabel(const QString& label)
{
  return label.startsWith(QLatin1String("SHYX ")) ? label.mid(5) : label;
}

//-----------------------------------------------------------------------------
/// Checked filter ids; empty by default, so a fresh install shows only "+".
QStringList LoadActionIds()
{
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  return settings.value(QLatin1String(kSettingsItemsKey)).toStringList();
}

//-----------------------------------------------------------------------------
void SaveActionIds(const QStringList& ids)
{
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kSettingsItemsKey), ids);
}

//-----------------------------------------------------------------------------
/// True when the proxy definition declares an InputProperty named "Input", i.e.
/// the filter can be created on the active object with a single connection.
bool DefinitionTakesInput(vtkPVXMLElement* definition)
{
  if (!definition)
  {
    return false;
  }
  for (unsigned int i = 0; i < definition->GetNumberOfNestedElements(); ++i)
  {
    vtkPVXMLElement* child = definition->GetNestedElement(i);
    const char* childName = child ? child->GetName() : nullptr;
    if (childName && strcmp(childName, "InputProperty") == 0)
    {
      const char* propertyName = child->GetAttribute("name");
      if (propertyName && strcmp(propertyName, "Input") == 0)
      {
        return true;
      }
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
/// Every filter that can go on the wheel: SHYX first, then the ParaView / VESPA
/// built-ins, each list sorted by label. Filters come from the active session's
/// definitions, so modules that were not compiled never show up.
QVector<CatalogAction> BuildCatalog()
{
  const QString shyxGroup = ShyxGroupTitle();
  const QString builtinGroup = ParaViewGroupTitle();

  QVector<CatalogAction> catalog;
  pqServer* server = pqActiveObjects::instance().activeServer();
  vtkSMSessionProxyManager* pxm = server ? server->proxyManager() : nullptr;
  vtkSMProxyDefinitionManager* definitions = pxm ? pxm->GetProxyDefinitionManager() : nullptr;
  if (!definitions)
  {
    return catalog;
  }

  // Walk the registered definitions instead of instantiating prototypes, which
  // would create every filter just to fill the checklist.
  vtkSmartPointer<vtkPVProxyDefinitionIterator> iter;
  iter.TakeReference(definitions->NewIterator());
  iter->AddTraversalGroupName("filters");
  QVector<CatalogAction> shyxFilters;
  QVector<CatalogAction> builtinFilters;
  for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
  {
    const char* name = iter->GetProxyName();
    if (!name || !*name)
    {
      continue;
    }
    vtkPVXMLElement* definition = iter->GetProxyDefinition();
    if (!DefinitionTakesInput(definition))
    {
      continue;
    }
    const bool shyx = strncmp(name, "SHYX", 4) == 0;
    const char* xmlLabel = definition->GetAttribute("label");
    const QString label =
      (xmlLabel && *xmlLabel) ? QString::fromUtf8(xmlLabel) : QString::fromUtf8(name);
    CatalogAction action{ QStringLiteral("filter:") + QString::fromUtf8(name), label,
      shyx ? shyxGroup : builtinGroup, QString::fromUtf8(name) };
    (shyx ? shyxFilters : builtinFilters).push_back(action);
  }

  const auto byLabel = [](const CatalogAction& a, const CatalogAction& b) {
    return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
  };
  std::sort(shyxFilters.begin(), shyxFilters.end(), byLabel);
  std::sort(builtinFilters.begin(), builtinFilters.end(), byLabel);
  for (const CatalogAction& action : shyxFilters)
  {
    catalog.push_back(action);
  }
  for (const CatalogAction& action : builtinFilters)
  {
    catalog.push_back(action);
  }
  return catalog;
}

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
/// Create a checked filter on the active port and apply it.
void ApplyWheelFilter(const CatalogAction& action)
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqOutputPort* port = ActivePort();
  if (!core || !port || !port->getSource() || !port->getServer())
  {
    pqSHYXStatusNotifier::warning(
      QObject::tr("SHYX Quick filters: %1 needs an active pipeline object.").arg(action.label));
    return;
  }

  const QByteArray proxyName = action.proxyName.toUtf8();
  if (proxyName.isEmpty() || !HasFilterPrototype(port, proxyName.constData()))
  {
    pqSHYXStatusNotifier::error(
      QObject::tr("SHYX Quick filters: %1 is not available in this ParaView build.")
        .arg(action.label));
    return;
  }

  QMap<QString, QList<pqOutputPort*>> namedInputs;
  namedInputs[QStringLiteral("Input")] = QList<pqOutputPort*>() << port;

  BEGIN_UNDO_SET(QObject::tr("SHYX Quick filters: %1").arg(action.label));
  pqPipelineSource* filter = core->getObjectBuilder()->createFilter(
    QStringLiteral("filters"), action.proxyName, namedInputs, port->getServer());
  if (!filter)
  {
    END_UNDO_SET();
    pqSHYXStatusNotifier::error(
      QObject::tr("SHYX Quick filters: could not create %1.").arg(action.label));
    return;
  }
  core->applyPipeline();
  END_UNDO_SET();

  const QString name = filter->getSMName();
  pqSHYXStatusNotifier::info(QObject::tr("SHYX Quick filters: applied %1 (%2).")
                               .arg(action.label, name.isEmpty() ? action.proxyName : name));
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
    // Key is up. A wheel that is still open is released over a sector: running
    // it here keeps the gesture working when this poll sees the release before
    // the Qt key event does.
    if (this->ShowTimer->isActive())
    {
      this->ShowTimer->stop();
    }
    this->HoldArmed = false;
    if (this->Overlay && this->Overlay->isVisible())
    {
      this->commitWheelAtCursor();
    }
    this->SuppressedUntilKeyRelease = false;
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
          // Releasing the key over a sector runs it; anywhere else cancels.
          if (this->Overlay && this->Overlay->isVisible())
          {
            this->commitWheelAtCursor();
          }
          else
          {
            this->updateState();
          }
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
  int specialIndex = -1;
  for (const Item& item : this->Items)
  {
    if (item.isConfig)
    {
      specialIndex = labels.size();
    }
    labels << item.label;
    enabled << item.enabled;
  }

  if (!this->Overlay)
  {
    this->Overlay = new pqSHYXQuickFilterWheelOverlay(nullptr);
  }
  this->Overlay->setItems(labels, enabled, specialIndex);
  this->Overlay->showAtGlobal(globalPosition);
  // Highlight what sits under the cursor already: releasing without moving the
  // mouse must run exactly the sector that is lit up.
  this->updateHover(globalPosition);
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
void pqSHYXQuickFilterWheel::commitWheelAtCursor()
{
  if (!this->Overlay || !this->Overlay->isVisible())
  {
    return;
  }
  // Read the pointer directly: the last mouse-move event may predate the
  // release, and the ring is drawn around the cursor anyway.
  const int index = this->Overlay->sectorAt(QCursor::pos());
  // -1 (center hole / outside the ring) dismisses without running anything.
  this->activate(index);
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
  if (item.isConfig)
  {
    // Let the ring disappear before the modal checklist opens.
    QTimer::singleShot(0, this, [this]() { this->openConfigDialog(); });
    return;
  }
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
void pqSHYXQuickFilterWheel::openConfigDialog()
{
  const QVector<CatalogAction> catalog = BuildCatalog();
  QStringList ids;
  QStringList labels;
  QStringList groups;
  for (const CatalogAction& action : catalog)
  {
    ids.append(action.id);
    labels.append(action.label);
    groups.append(action.group);
  }

  QSet<QString> checked;
  for (const QString& id : LoadActionIds())
  {
    checked.insert(id);
  }

  pqSHYXQuickFilterWheelConfigDialog dialog(
    ids, labels, groups, checked, QApplication::activeWindow());
  if (dialog.exec() == QDialog::Accepted)
  {
    SaveActionIds(dialog.checkedIds());
  }
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

  const QVector<CatalogAction> catalog = BuildCatalog();
  const QStringList selected = LoadActionIds();
  for (const CatalogAction& action : catalog)
  {
    if (!selected.contains(action.id))
    {
      continue;
    }

    // Runs on the active object, or on the selection's port when there is one.
    Item item;
    item.label = ShortLabel(action.label);
    pqOutputPort* port = ActivePort();
    item.enabled = HasFilterPrototype(port, action.proxyName.toUtf8().constData());
    if (!port)
    {
      item.unavailableDetail =
        tr("SHYX Quick filters: %1 needs an active pipeline object.").arg(action.label);
    }
    else if (!item.enabled)
    {
      item.unavailableDetail = tr("SHYX Quick filters: %1 is not available in this ParaView build.")
                                 .arg(action.label);
    }
    item.run = [action]() { ApplyWheelFilter(action); };
    items.push_back(item);
  }

  // Always last: the "+" sector that reopens the checklist.
  {
    Item item;
    item.label = QStringLiteral("+");
    item.isConfig = true;
    item.enabled = true;
    items.push_back(item);
  }

  return items;
}
