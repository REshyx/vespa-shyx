#include "pqVESPAVascularCategoryAutoStart.h"

#include "pqProxyCategory.h"
#include "pqProxyGroupMenuManager.h"
#include "pqProxyInfo.h"

#include "vtkNew.h"
#include "vtkPVXMLParser.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

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
             icon=":/VESPA/SHYX_Vessel_End_Clipper.png"/>
      <Proxy group="filters" name="SHYXRemeshWithEndpoint"
             icon=":/VESPA/SHYX_Adaptive_Isotropic_Remesher.png"/>
      <Proxy group="filters" name="SHYXTetGen"
             icon=":/VESPA/SHYX_TetGen.png"/>
      <Proxy group="filters" name="SHYXDataSetToPartitionedCollection"
             icon=":/VESPA/SHYX_DataSet_To_Partitioned_Collection.png"/>
      <Proxy group="filters" name="SHYXPartitionedCollectionBoundaryAssignment"
             icon=":/VESPA/SHYX_DataSet_To_Partitioned_Collection.png"/>
    </Category>
  </ParaViewFilters>
</ServerManagerConfiguration>
)xml";

constexpr auto kVascular = "Vascular";

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

/** Always replace Filters→Vascular from the plugin XML (no merge of leftovers). */
void ensureApplicationVascular(pqProxyGroupMenuManager* mgr)
{
  pqProxyCategory* appRoot = mgr->getApplicationCategory();
  if (!appRoot)
  {
    return;
  }

  // Application + settings/menu roots both merge on parseXML; drop first.
  dropVascularCategory(appRoot);
  pqProxyCategory* menuRoot = mgr->getMenuCategory();
  if (menuRoot && menuRoot != appRoot)
  {
    dropVascularCategory(menuRoot);
  }

  vtkNew<vtkPVXMLParser> parser;
  if (!parser->Parse(kVascularFiltersXml))
  {
    return;
  }
  mgr->loadConfiguration(parser->GetRootElement());
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
pqVESPAVascularCategoryAutoStart::pqVESPAVascularCategoryAutoStart(QObject* parent)
  : Superclass(parent)
{
}

//-----------------------------------------------------------------------------
pqVESPAVascularCategoryAutoStart::~pqVESPAVascularCategoryAutoStart()
{
  this->onShutdown();
}

//-----------------------------------------------------------------------------
void pqVESPAVascularCategoryAutoStart::onStartup()
{
  // Plugin SM XML / category toolbars may finish after auto_start; refresh on
  // every load so settings residuals cannot stick around.
  QTimer::singleShot(0, this, [this]() { this->enforceVascularOrder(); });
  QTimer::singleShot(200, this, [this]() { this->enforceVascularOrder(); });
  QTimer::singleShot(1000, this, [this]() { this->enforceVascularOrder(); });
}

//-----------------------------------------------------------------------------
void pqVESPAVascularCategoryAutoStart::onShutdown() {}

//-----------------------------------------------------------------------------
void pqVESPAVascularCategoryAutoStart::enforceVascularOrder()
{
  for (pqProxyGroupMenuManager* mgr : ::findFiltersMenuManagers())
  {
    ::ensureApplicationVascular(mgr);

    pqProxyCategory* appRoot = mgr->getApplicationCategory();
    pqProxyCategory* menuRoot = mgr->getMenuCategory();
    if (!appRoot || !menuRoot)
    {
      continue;
    }

    pqProxyCategory* appVascular = appRoot->findSubCategory(kVascular);
    if (!appVascular)
    {
      continue;
    }
    // Always keep the plugin definition's toolbar flag on.
    appVascular->setShowInToolbar(true);

    if (menuRoot != appRoot)
    {
      // Force settings/menu Vascular to match plugin list and persist it.
      ::dropVascularCategory(menuRoot);
      auto* vascular = new pqProxyCategory(nullptr);
      vascular->deepCopy(appVascular);
      vascular->setShowInToolbar(true);
      menuRoot->addCategory(vascular);
      mgr->writeCategoryToSettings();
    }

    mgr->populateMenu();
    ::showVascularToolbar(mgr);
  }
}
