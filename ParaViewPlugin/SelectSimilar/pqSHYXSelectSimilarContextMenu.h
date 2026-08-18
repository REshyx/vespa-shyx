#ifndef pqSHYXSelectSimilarContextMenu_h
#define pqSHYXSelectSimilarContextMenu_h

#include "pqContextMenuInterface.h"

#include <QObject>

/**
 * Adds "Select Similar" (submenu: By Normal) to the RenderView context menu
 * when a cell selection is active. By Normal grows to completion in one shot
 * using the same dihedral threshold as the title-bar Grow tool.
 */
class pqSHYXSelectSimilarContextMenu
  : public QObject
  , public pqContextMenuInterface
{
  Q_OBJECT
  Q_INTERFACES(pqContextMenuInterface)
  typedef QObject Superclass;

public:
  explicit pqSHYXSelectSimilarContextMenu(QObject* parent = nullptr);
  ~pqSHYXSelectSimilarContextMenu() override;

  using pqContextMenuInterface::contextMenu;
  bool contextMenu(QMenu* menu, pqView* viewContext, const QPoint& viewPoint,
    pqRepresentation* dataContext, const QStringList& dataBlockContext) const override;

  /// After Select Block (-1) so this submenu can sit under that action.
  int priority() const override { return -2; }

private:
  Q_DISABLE_COPY(pqSHYXSelectSimilarContextMenu)
};

#endif
