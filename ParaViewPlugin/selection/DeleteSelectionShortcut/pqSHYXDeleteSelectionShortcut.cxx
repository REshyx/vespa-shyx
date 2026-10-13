#include "pqSHYXDeleteSelectionShortcut.h"

#include "pqSHYXStatusNotifier.h"

#include "pqApplicationCore.h"
#include "pqObjectBuilder.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqSelectionManager.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "pqUndoStack.h"
#include "pqView.h"

#include "vtkPVDataInformation.h"
#include "vtkSMSessionProxyManager.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QList>
#include <QWidget>

//-----------------------------------------------------------------------------
pqSHYXDeleteSelectionShortcut::pqSHYXDeleteSelectionShortcut(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXDeleteSelectionShortcut::~pqSHYXDeleteSelectionShortcut()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXDeleteSelectionShortcut::onStartup()
{
  if (this->Installed || !qApp)
  {
    return;
  }
  qApp->installEventFilter(this);
  this->Installed = true;
}

//-----------------------------------------------------------------------------
void pqSHYXDeleteSelectionShortcut::onShutdown()
{
  if (!this->Installed)
  {
    return;
  }
  if (qApp)
  {
    qApp->removeEventFilter(this);
  }
  this->Installed = false;
}

//-----------------------------------------------------------------------------
bool pqSHYXDeleteSelectionShortcut::renderViewHasFocus()
{
  QWidget* focus = QApplication::focusWidget();
  pqApplicationCore* core = pqApplicationCore::instance();
  pqServerManagerModel* model = core ? core->getServerManagerModel() : nullptr;
  if (!focus || !model)
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
    for (QWidget* widget = focus; widget; widget = widget->parentWidget())
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
bool pqSHYXDeleteSelectionShortcut::eventFilter(QObject*, QEvent* event)
{
  if (event->type() != QEvent::KeyPress)
  {
    return false;
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const Qt::KeyboardModifiers mods = keyEvent->modifiers();
  if (keyEvent->key() != Qt::Key_D ||
    (mods & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier)) !=
      Qt::ControlModifier)
  {
    return false;
  }
  // Leave Ctrl+D to the open context menu (its Delete Selected Cells action).
  if (QApplication::activePopupWidget() || !renderViewHasFocus())
  {
    return false;
  }
  if (!keyEvent->isAutoRepeat())
  {
    deleteActiveSelection();
  }
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXDeleteSelectionShortcut::deleteActiveSelection()
{
  pqPVApplicationCore* core = pqPVApplicationCore::instance();
  pqSelectionManager* selection = core ? core->selectionManager() : nullptr;
  pqOutputPort* port = selection ? selection->getSelectedPort() : nullptr;
  if (!port || !port->getSelectionInput() || !port->getSource() || !port->getServer())
  {
    pqSHYXStatusNotifier::warning(
      QObject::tr("SHYX Delete: no active selection. Select cells or points, then press Ctrl+D."));
    return;
  }

  vtkPVDataInformation* info = port->getDataInformation();
  if (info && info->IsCompositeDataSet())
  {
    pqSHYXStatusNotifier::error(QObject::tr(
      "SHYX Delete: the selection is on composite data. Delete Selected Cells needs a vtkDataSet."));
    return;
  }
  if (info && !info->IsNull() && !info->DataSetTypeIsA("vtkDataSet"))
  {
    pqSHYXStatusNotifier::error(
      QObject::tr("SHYX Delete: the active data is not a vtkDataSet."));
    return;
  }

  pqServer* server = port->getServer();
  vtkSMSessionProxyManager* pxm = server->proxyManager();
  if (!pxm || !pxm->GetPrototypeProxy("filters", "SHYXDeleteSelectedCellsFilter"))
  {
    pqSHYXStatusNotifier::error(
      QObject::tr("SHYX Delete: SHYX Delete Selected Cells is not loaded."));
    return;
  }

  QMap<QString, QList<pqOutputPort*>> namedInputs;
  namedInputs[QStringLiteral("Input")] = QList<pqOutputPort*>() << port;

  BEGIN_UNDO_SET(QObject::tr("SHYX Delete Selected Cells"));
  pqPipelineSource* filter = core->getObjectBuilder()->createFilter(
    QStringLiteral("filters"), QStringLiteral("SHYXDeleteSelectedCellsFilter"), namedInputs, server);
  if (!filter)
  {
    END_UNDO_SET();
    pqSHYXStatusNotifier::error(QObject::tr("SHYX Delete: could not create the filter."));
    return;
  }

  // Same as clicking Apply: copy of the active selection is already on the
  // filter (Extract Selection initialization helper), then the pipeline runs
  // and the input is hidden in the current view.
  core->applyPipeline();
  END_UNDO_SET();

  const QString name = filter->getSMName();
  pqSHYXStatusNotifier::info(
    QObject::tr("SHYX Delete: applied %1.").arg(name.isEmpty() ? QStringLiteral("Delete Selected Cells") : name));
}
