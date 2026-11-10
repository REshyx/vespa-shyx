#ifndef pqSHYXPickCenterContextMenu_h
#define pqSHYXPickCenterContextMenu_h

#include "pqContextMenuInterface.h"

#include <QObject>

/**
 * Adds "Pick Center" to the RenderView context menu. Choosing it runs the
 * same surface pick as the axes-toolbar Pick Center, at the right-click that
 * opened the menu. It does not arm a second left-click.
 */
class pqSHYXPickCenterContextMenu
  : public QObject
  , public pqContextMenuInterface
{
  Q_OBJECT
  Q_INTERFACES(pqContextMenuInterface)
  typedef QObject Superclass;

public:
  explicit pqSHYXPickCenterContextMenu(QObject* parent = nullptr);
  ~pqSHYXPickCenterContextMenu() override;

  using pqContextMenuInterface::contextMenu;
  bool contextMenu(QMenu* menu, pqView* viewContext, const QPoint& viewPoint,
    pqRepresentation* dataContext, const QStringList& dataBlockContext) const override;

  /// After Select Similar (-2) so Link Camera already exists and this item
  /// can sit with the other camera actions.
  int priority() const override { return -3; }

private:
  Q_DISABLE_COPY(pqSHYXPickCenterContextMenu)
};

#endif
