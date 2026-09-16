#ifndef pqSHYXStatusNotifier_h
#define pqSHYXStatusNotifier_h

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QMenu;
class QStatusBar;
class QTimer;
class QToolButton;

/**
 * Status-bar notices on the left of the ParaView window (not Output Messages).
 *
 *   pqSHYXStatusNotifier::info("…");     // green
 *   pqSHYXStatusNotifier::warning("…");  // yellow
 *   pqSHYXStatusNotifier::error("…");    // red
 *
 * A fresh notice expands the chip; after a few seconds it collapses to
 * "SHYX Information". Click the chip for recent notices; click elsewhere
 * to dismiss the popup.
 */
class pqSHYXStatusNotifier : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  enum class Level
  {
    Info,
    Warning,
    Error
  };

  static pqSHYXStatusNotifier* instance();
  static void info(const QString& message);
  static void warning(const QString& message);
  static void error(const QString& message);
  static void show(const QString& message, Level level = Level::Info);
  static void shutdown();

private:
  struct HistoryItem
  {
    QString text;
    Level level = Level::Info;
  };

  explicit pqSHYXStatusNotifier(QObject* parent = nullptr);
  ~pqSHYXStatusNotifier() override;

  Q_DISABLE_COPY(pqSHYXStatusNotifier)

  bool ensureInstalled();
  void showMessage(const QString& message, Level level);
  void applyChipText(const QString& text, bool expanded);
  void applyChipStyle(bool colored);
  void rebuildMenu();
  static QString levelColor(Level level);

private Q_SLOTS:
  void collapseChip();

private:
  QPointer<QStatusBar> StatusBar;
  QPointer<QToolButton> Button;
  QPointer<QMenu> Menu;
  QTimer* CollapseTimer = nullptr;
  QString LastMessage;
  Level LastLevel = Level::Info;
  QList<HistoryItem> History;
};

#endif
