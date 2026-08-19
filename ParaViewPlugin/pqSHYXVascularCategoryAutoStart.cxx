#include "pqSHYXVascularCategoryAutoStart.h"

#include "pqApplicationCore.h"
#include "pqProxyCategory.h"
#include "pqProxyGroupMenuManager.h"
#include "pqProxyInfo.h"

#include "vtkNew.h"
#include "vtkPVXMLElement.h"
#include "vtkPVXMLParser.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QStringList>
#include <QToolBar>
#include <QWidget>

#include <cstring>

namespace
{
// Keep in sync with ParaViewPlugin/VESPAVascularCategory.xml (order + toolbar).
constexpr const char* kVascularFiltersXml = R"xml(
<ServerManagerConfiguration>
  <ParaViewFilters>
    <Category name="Vascular" menu_label="Vascular" preserve_order="1" show_in_toolbar="1">
      <Proxy group="filters" name="SHYXSkeletonExtraction"
             icon=":/VESPA/SHYX_Skeleton_Extraction.png"/>
      <Proxy group="filters" name="SHYXVesselEndClipper"
             icon=":/VESPA/SHYX_Vessel_End_Clipper.png"/>
      <Proxy group="filters" name="SHYXSelectionPlaneClipper"
             icon=":/VESPA/SHYX_Selection_Plane_Clipper.png"/>
      <Proxy group="filters" name="SHYXRemeshWithEndpoint"
             icon=":/VESPA/SHYX_Remesh_With_Endpoint.png"/>
      <Proxy group="filters" name="SHYXTetGen"
             icon=":/VESPA/SHYX_TetGen.png"/>
      <Proxy group="filters" name="SHYXDataSetToPartitionedCollection"
             icon=":/VESPA/SHYX_DataSet_To_Partitioned_Collection.png"/>
      <Proxy group="filters" name="SHYXPartitionedCollectionBoundaryAssignment"
             icon=":/VESPA/SHYX_Partitioned_Collection_Boundary_Assignment.png"/>
    </Category>
  </ParaViewFilters>
</ServerManagerConfiguration>
)xml";

constexpr auto kVascular = "Vascular";

QStringList expectedVascularProxyNames()
{
  return { QStringLiteral("SHYXSkeletonExtraction"), QStringLiteral("SHYXVesselEndClipper"),
    QStringLiteral("SHYXSelectionPlaneClipper"), QStringLiteral("SHYXRemeshWithEndpoint"),
    QStringLiteral("SHYXTetGen"), QStringLiteral("SHYXDataSetToPartitionedCollection"),
    QStringLiteral("SHYXPartitionedCollectionBoundaryAssignment") };
}

bool isParaViewFiltersMenuManager(pqProxyGroupMenuManager* mgr)
{
  if (!mgr)
  {
    return false;
  }
  if (QMenu* menu = mgr->menu())
  {
    if (menu->objectName() == QLatin1String("menuFilters"))
    {
      return true;
    }
  }
  if (pqProxyCategory* root = mgr->getApplicationCategory())
  {
    for (pqProxyInfo* info : root->getProxiesRecursive())
    {
      if (info && info->group() == QLatin1String("filters"))
      {
        return true;
      }
    }
  }
  return false;
}

QList<pqProxyGroupMenuManager*> findFiltersMenuManagers()
{
  QList<pqProxyGroupMenuManager*> managers;
  auto consider = [&managers](pqProxyGroupMenuManager* mgr) {
    if (mgr && isParaViewFiltersMenuManager(mgr) && !managers.contains(mgr))
    {
      managers.append(mgr);
    }
  };
  for (QWidget* top : QApplication::topLevelWidgets())
  {
    for (pqProxyGroupMenuManager* mgr : top->findChildren<pqProxyGroupMenuManager*>())
    {
      consider(mgr);
    }
  }
  for (pqProxyGroupMenuManager* mgr : qApp->findChildren<pqProxyGroupMenuManager*>())
  {
    consider(mgr);
  }
  return managers;
}

void dropVascularCategory(pqProxyCategory* root)
{
  if (!root)
  {
    return;
  }
  if (pqProxyCategory* vascular = root->findSubCategory(kVascular))
  {
    // clear() drops residual proxies; removeCategory only unmaps the name.
    vascular->clear();
    root->removeCategory(kVascular);
    vascular->deleteLater();
  }
}

vtkPVXMLElement* parseVascularFiltersXml(vtkPVXMLParser* parser)
{
  if (!parser || !parser->Parse(kVascularFiltersXml))
  {
    return nullptr;
  }
  vtkPVXMLElement* root = parser->GetRootElement();
  if (!root || !root->GetName())
  {
    return nullptr;
  }
  if (strcmp(root->GetName(), "ParaViewFilters") == 0)
  {
    return root;
  }
  return root->FindNestedElementByName("ParaViewFilters");
}

/**
 * Replace Filters→Vascular from the plugin XML (no merge of leftovers).
 * Parses directly onto the category tree — do not use loadConfiguration(), which
 * can merge into settings and writeCategoryToSettings() (QMap order, not XML).
 */
void replaceVascularFromPluginXml(pqProxyCategory* root)
{
  if (!root)
  {
    return;
  }
  dropVascularCategory(root);

  vtkNew<vtkPVXMLParser> parser;
  vtkPVXMLElement* filters = parseVascularFiltersXml(parser);
  if (!filters)
  {
    return;
  }
  root->parseXML(filters);

  if (pqProxyCategory* vascular = root->findSubCategory(kVascular))
  {
    vascular->setShowInToolbar(true);
  }
}

bool vascularMatchesPlugin(pqProxyCategory* root)
{
  if (!root)
  {
    return false;
  }
  pqProxyCategory* vascular = root->findSubCategory(kVascular);
  if (!vascular || !vascular->preserveOrder() || !vascular->showInToolbar())
  {
    return false;
  }
  return vascular->getOrderedRootProxiesNames() == expectedVascularProxyNames();
}

bool managerVascularMatchesPlugin(pqProxyGroupMenuManager* mgr)
{
  pqProxyCategory* appRoot = mgr->getApplicationCategory();
  pqProxyCategory* menuRoot = mgr->getMenuCategory();
  if (!vascularMatchesPlugin(appRoot))
  {
    return false;
  }
  if (menuRoot && menuRoot != appRoot && !vascularMatchesPlugin(menuRoot))
  {
    return false;
  }
  return true;
}

/** Show filters.Vascular even if a prior session hid it via View → Toolbars. */
void showVascularToolbar(pqProxyGroupMenuManager* mgr)
{
  pqProxyCategory* menuRoot = mgr->getMenuCategory();
  if (!menuRoot)
  {
    return;
  }
  pqProxyCategory* vascular = menuRoot->findSubCategory(kVascular);
  if (!vascular || !vascular->showInToolbar())
  {
    return;
  }

  const QString toolbarName = mgr->getToolbarName(vascular);
  for (QWidget* top : QApplication::topLevelWidgets())
  {
    auto* mainWindow = qobject_cast<QMainWindow*>(top);
    if (!mainWindow)
    {
      continue;
    }
    for (QToolBar* tb : mainWindow->findChildren<QToolBar*>())
    {
      if (!tb)
      {
        continue;
      }
      // Object name is typically "filters.Vascular"; also match window title.
      if (tb->objectName() == toolbarName || tb->windowTitle() == vascular->label() ||
        tb->objectName().endsWith(QLatin1String(".Vascular")))
      {
        tb->setVisible(true);
      }
    }
  }
}
} // namespace

//-----------------------------------------------------------------------------
pqSHYXVascularCategoryAutoStart::pqSHYXVascularCategoryAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqSHYXVascularCategoryAutoStart::~pqSHYXVascularCategoryAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularCategoryAutoStart::onStartup()
{
  if (pqApplicationCore* core = pqApplicationCore::instance())
  {
    // After MainWindow restoreState and plugin XML/category merge.
    QObject::connect(core, &pqApplicationCore::clientEnvironmentDone, this,
      &pqSHYXVascularCategoryAutoStart::enforceVascularOrder, Qt::UniqueConnection);
  }
  this->enforceVascularOrder();
}

//-----------------------------------------------------------------------------
void pqSHYXVascularCategoryAutoStart::onShutdown() {}

//-----------------------------------------------------------------------------
void pqSHYXVascularCategoryAutoStart::enforceVascularOrder()
{
  if (this->Enforcing)
  {
    return;
  }
  this->Enforcing = true;

  const auto queuedUnique =
    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection);

  for (pqProxyGroupMenuManager* mgr : ::findFiltersMenuManagers())
  {
    QObject::connect(mgr, &pqProxyGroupMenuManager::categoriesUpdated, this,
      &pqSHYXVascularCategoryAutoStart::enforceVascularOrder, queuedUnique);
    QObject::connect(mgr, &pqProxyGroupMenuManager::menuPopulated, this,
      &pqSHYXVascularCategoryAutoStart::enforceVascularOrder, Qt::UniqueConnection);

    pqProxyCategory* appRoot = mgr->getApplicationCategory();
    if (!appRoot)
    {
      continue;
    }

    if (!::managerVascularMatchesPlugin(mgr))
    {
      ::replaceVascularFromPluginXml(appRoot);
      pqProxyCategory* menuRoot = mgr->getMenuCategory();
      if (menuRoot && menuRoot != appRoot)
      {
        ::replaceVascularFromPluginXml(menuRoot);
      }
      mgr->populateMenu();
    }

    ::showVascularToolbar(mgr);
  }

  this->Enforcing = false;
}
