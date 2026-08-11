#include "pqVESPAVascularCategoryAutoStart.h"

#include "pqProxyCategory.h"
#include "pqProxyGroupMenuManager.h"
#include "pqProxyInfo.h"

#include "vtkNew.h"
#include "vtkPVXMLElement.h"
#include "vtkPVXMLParser.h"

#include <QApplication>
#include <QMenu>
#include <QTimer>
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

/**
 * ResourceTagName is protected in ParaView 6; identify Filters managers via public API.
 * Matches the main Filters menu and the Configure-Categories dummy Filters manager.
 */
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

bool sameProxyOrder(pqProxyCategory* a, pqProxyCategory* b)
{
  return a && b && a->getOrderedRootProxiesNames() == b->getOrderedRootProxiesNames();
}

void ensureApplicationVascular(pqProxyGroupMenuManager* mgr)
{
  pqProxyCategory* appRoot = mgr->getApplicationCategory();
  if (!appRoot)
  {
    return;
  }

  pqProxyCategory* appVascular = appRoot->findSubCategory(kVascular);
  if (appVascular && appVascular->preserveOrder() && appVascular->showInToolbar())
  {
    return;
  }

  // Rebuild from canonical XML so PreserveOrder sticks (addCategory early-return
  // would not update the flag on an existing category).
  appRoot->removeCategory(kVascular);

  vtkNew<vtkPVXMLParser> parser;
  if (!parser->Parse(kVascularFiltersXml))
  {
    return;
  }
  mgr->loadConfiguration(parser->GetRootElement());
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
  // Plugin SM XML (incl. ParaViewFilters) may finish loading after auto_start.
  QTimer::singleShot(0, this, [this]() { this->enforceVascularOrder(); });
  QTimer::singleShot(200, this, [this]() { this->enforceVascularOrder(); });
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
    if (!appVascular || !appVascular->preserveOrder())
    {
      continue;
    }

    if (menuRoot == appRoot)
    {
      mgr->populateMenu();
      continue;
    }

    pqProxyCategory* menuVascular = menuRoot->findSubCategory(kVascular);
    const bool needsReplace = !menuVascular || !menuVascular->preserveOrder() ||
      !menuVascular->showInToolbar() || !::sameProxyOrder(menuVascular, appVascular);

    if (!needsReplace)
    {
      continue;
    }

    menuRoot->removeCategory(kVascular);
    auto* vascular = new pqProxyCategory(nullptr);
    vascular->deepCopy(appVascular);
    vascular->setShowInToolbar(true);
    menuRoot->addCategory(vascular);

    mgr->writeCategoryToSettings();
    mgr->populateMenu();
  }
}
