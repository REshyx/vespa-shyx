#ifndef pqVESPAVascularCategoryAutoStart_h
#define pqVESPAVascularCategoryAutoStart_h

#include <QObject>

/**
 * Ensures Filters → Vascular uses the plugin's preserve_order + proxy list.
 *
 * ParaView stores customized filter categories in settings. When that key is
 * present, the UI uses SettingsCategory and ignores ApplicationCategory's
 * preserve_order for an already-existing "Vascular" entry (addCategory early-
 * returns without updating the flag). Result: toolbar/menu stay alphabetical.
 *
 * On startup, replace menu Vascular with a deep copy from ApplicationCategory
 * (the ParaViewFilters definition in VESPAVascularCategory.xml) and write
 * settings so the toolbar keeps the curated order.
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
