#ifndef pqSHYXSelectBlockContextMenu_h
#define pqSHYXSelectBlockContextMenu_h

#include "pqContextMenuInterface.h"

#include <QObject>

/**
 * Adds "Select Block" to the RenderView block context menu so a right-click
 * on a composite part (e.g. Part_1) clears the current selection and then
 * selects every cell in that block.
 */
class pqSHYXSelectBlockContextMenu
  : public QObject
  , public pqContextMenuInterface
{
  Q_OBJECT
  Q_INTERFACES(pqContextMenuInterface)
  typedef QObject Superclass;

public:
  explicit pqSHYXSelectBlockContextMenu(QObject* parent = nullptr);
  ~pqSHYXSelectBlockContextMenu() override;

  using pqContextMenuInterface::contextMenu;
  bool contextMenu(QMenu* menu, pqView* viewContext, const QPoint& viewPoint,
    pqRepresentation* dataContext, const QStringList& dataBlockContext) const override;

  /// Run after pqBlockContextMenu (1) and pqDefaultContextMenu (0) so the
  /// action can be inserted under the existing "Block '...'" header.
  int priority() const override { return -1; }

private:
  Q_DISABLE_COPY(pqSHYXSelectBlockContextMenu)
};

#endif
