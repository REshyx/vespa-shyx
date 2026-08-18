#ifndef pqSHYXSelectSimilarContextMenu_h
#define pqSHYXSelectSimilarContextMenu_h

#include "pqContextMenuInterface.h"

#include <QObject>

/**
 * Adds selection actions to the RenderView context menu when a cell selection
 * is active: "Select All" (connected region), "Invert Selection", "Select
 * Similar" (submenu: By Normal), and "Fill Interior". By Normal grows to
 * completion in one shot using the same dihedral threshold as the title-bar
 * Grow tool. Fill Interior adds unselected faces enclosed by the current
 * selection.
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
