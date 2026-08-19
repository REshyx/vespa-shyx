#ifndef pqSHYXVascularCategoryAutoStart_h
#define pqSHYXVascularCategoryAutoStart_h

#include <QObject>

/**
 * Keep Filters → Vascular (menu + toolbar) in the plugin XML order.
 *
 * ParaView merges category XML/settings instead of replacing, and
 * pqProxyCategory::convertToXML writes proxies from a QMap (name order) instead
 * of OrderedProxies. This class re-injects the curated list in memory whenever
 * categories or the Filters menu are rebuilt — it does not write settings.
 *
 * Hooks (no wall-clock retries):
 * - plugin auto_start
 * - pqApplicationCore::clientEnvironmentDone (UI finished, after MainWindow restoreState)
 * - pqProxyGroupMenuManager::categoriesUpdated / menuPopulated
 */
class pqSHYXVascularCategoryAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqSHYXVascularCategoryAutoStart(QObject* parent = nullptr);
  ~pqSHYXVascularCategoryAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  void enforceVascularOrder();

  bool Enforcing = false;

  Q_DISABLE_COPY(pqSHYXVascularCategoryAutoStart)
};

#endif
