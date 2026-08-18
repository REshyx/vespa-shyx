#include "pqSHYXSelectBlockContextMenu.h"

#include "pqDataRepresentation.h"
#include "pqOutputPort.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqSelectionManager.h"

#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMSelectionHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStringList>

#include <string>

namespace
{
void selectBlocks(pqDataRepresentation* repr, const QStringList& selectors)
{
  if (!repr || selectors.isEmpty())
  {
    return;
  }

  pqOutputPort* port = repr->getOutputPortFromInput();
  if (!port || !port->getSource())
  {
    return;
  }

  vtkSMSourceProxy* producer = vtkSMSourceProxy::SafeDownCast(port->getSource()->getProxy());
  if (!producer)
  {
    return;
  }

  vtkSMSessionProxyManager* pxm = producer->GetSessionProxyManager();
  if (!pxm)
  {
    return;
  }

  vtkSmartPointer<vtkSMSourceProxy> selectionSource;
  selectionSource.TakeReference(
    vtkSMSourceProxy::SafeDownCast(pxm->NewProxy("sources", "BlockSelectorsSelectionSource")));
  if (!selectionSource)
  {
    return;
  }

  std::string assemblyName = "Hierarchy";
  if (vtkSMProxy* reprProxy = repr->getProxy())
  {
    if (reprProxy->GetProperty("Assembly"))
    {
      const char* name = vtkSMPropertyHelper(reprProxy, "Assembly").GetAsString();
      if (name && name[0] != '\0')
      {
        assemblyName = name;
      }
    }
  }

  vtkSMPropertyHelper(selectionSource, "FieldType").Set(vtkSelectionNode::CELL);
  vtkSMPropertyHelper(selectionSource, "BlockSelectorsAssemblyName").Set(assemblyName.c_str());

  vtkSMPropertyHelper selectorsHelper(selectionSource, "BlockSelectors");
  selectorsHelper.SetNumberOfElements(static_cast<unsigned int>(selectors.size()));
  for (int i = 0; i < selectors.size(); ++i)
  {
    selectorsHelper.Set(static_cast<unsigned int>(i), selectors[i].toUtf8().constData());
  }
  selectionSource->UpdateVTKObjects();

  vtkSmartPointer<vtkSMSourceProxy> appendSelections;
  appendSelections.TakeReference(vtkSMSourceProxy::SafeDownCast(
    vtkSMSelectionHelper::NewAppendSelectionsFromSelectionSource(selectionSource)));
  if (!appendSelections)
  {
    return;
  }

  if (auto* selManager = pqPVApplicationCore::instance()->selectionManager())
  {
    selManager->clearSelection();
  }

  port->setSelectionInput(appendSelections, 0);
  if (auto* selManager = pqPVApplicationCore::instance()->selectionManager())
  {
    selManager->select(port);
  }
  repr->renderViewEventually();
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXSelectBlockContextMenu::pqSHYXSelectBlockContextMenu(QObject* parentObject)
  : Superclass(parentObject)
{
}

//-----------------------------------------------------------------------------
pqSHYXSelectBlockContextMenu::~pqSHYXSelectBlockContextMenu() = default;

//-----------------------------------------------------------------------------
bool pqSHYXSelectBlockContextMenu::contextMenu(QMenu* menu, pqView*, const QPoint&,
  pqRepresentation* dataContext, const QStringList& dataBlockContext) const
{
  if (!menu || dataBlockContext.isEmpty())
  {
    return false;
  }

  auto* repr = qobject_cast<pqDataRepresentation*>(dataContext);
  if (!repr || !repr->getOutputPortFromInput())
  {
    return false;
  }

  const QList<QAction*> acts = menu->actions();
  QAction* insertBefore = nullptr;
  if (acts.size() >= 2 && acts[1]->isSeparator())
  {
    insertBefore = acts.size() > 2 ? acts[2] : nullptr;
  }
  else if (!acts.isEmpty())
  {
    insertBefore = acts.first();
  }

  auto* action = new QAction(
    QIcon(":/pqWidgets/Icons/pqSelectBlock.svg"), tr("Select Block"), menu);
  action->setObjectName("actionSHYXSelectBlock");
  action->setToolTip(
    tr("Clear the current selection and select all cells in this block"));
  const QStringList selectors = dataBlockContext;
  QPointer<pqDataRepresentation> reprPtr(repr);
  QObject::connect(action, &QAction::triggered, menu,
    [reprPtr, selectors]()
    {
      if (reprPtr)
      {
        selectBlocks(reprPtr, selectors);
      }
    });
  menu->insertAction(insertBefore, action);

  auto* sep = new QAction(menu);
  sep->setSeparator(true);
  menu->insertAction(insertBefore, sep);

  return false;
}
