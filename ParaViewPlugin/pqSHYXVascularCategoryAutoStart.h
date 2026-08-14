#ifndef pqVESPAVascularCategoryAutoStart_h
#define pqVESPAVascularCategoryAutoStart_h

#include <QObject>

/**
 * On every plugin load (and delayed retries):
 * 1. Drop any residual Filters → Vascular (settings / prior session) and reload
 *    the curated proxy list from the plugin definition (preserve_order)
 * 2. Force show_in_toolbar (Configure Categories "Use as toolbar")
 * 3. Persist the cleaned category to settings and make filters.Vascular visible
 *
 * Needed because ParaView merges category XML/settings (does not replace), so
 * stale proxies survive reload unless we remove + rewrite on each load.
 */
class pqVESPAVascularCategoryAutoStart : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  pqVESPAVascularCategoryAutoStart(QObject* parent = nullptr);
  ~pqVESPAVascularCategoryAutoStart() override;

  void onStartup();
  void onShutdown();

private:
  void enforceVascularOrder();

  Q_DISABLE_COPY(pqVESPAVascularCategoryAutoStart)
};

#endif
