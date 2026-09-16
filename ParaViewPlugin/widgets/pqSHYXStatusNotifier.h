#ifndef pqSHYXStatusNotifier_h
#define pqSHYXStatusNotifier_h

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QMenu;
class QStatusBar;
class QTimer;
class QToolButton;

/**
 * Injects a compact SHYX notice chip immediately left of ParaView's progress
 * bar. Informational client-tool messages (Select Connected, Grow, …) go here
 * instead of vtkOutputWindow / Output Messages.
 *
 * A fresh notice expands the chip to elided text; after a few seconds it
 * collapses to "SHYX". Click the chip to open a popup of recent notices;
 * click elsewhere to dismiss it.
 */
class pqSHYXStatusNotifier : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  static pqSHYXStatusNotifier* instance();
  static void show(const QString& message);
  static void shutdown();

private:
  explicit pqSHYXStatusNotifier(QObject* parent = nullptr);
  ~pqSHYXStatusNotifier() override;

  Q_DISABLE_COPY(pqSHYXStatusNotifier)

  bool ensureInstalled();
  void showMessage(const QString& message);
  void applyChipText(const QString& text, bool expanded);
  void rebuildMenu();

private Q_SLOTS:
  void collapseChip();

private:
  QPointer<QStatusBar> StatusBar;
  QPointer<QToolButton> Button;
  QPointer<QMenu> Menu;
  QTimer* CollapseTimer = nullptr;
  QString LastMessage;
  QStringList History;
};

#endif
