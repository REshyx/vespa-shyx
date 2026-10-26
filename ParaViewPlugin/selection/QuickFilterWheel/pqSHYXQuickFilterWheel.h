#ifndef pqSHYXQuickFilterWheel_h
#define pqSHYXQuickFilterWheel_h

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class QEvent;
class QTimer;
class pqSHYXQuickFilterWheelOverlay;

/**
 * Hold Ctrl over a 3D view to get a radial quick-filter wheel under the cursor.
 *
 *   - Ctrl pressed and held (~250 ms) → ring of quick filters appears.
 *   - Move the cursor onto a sector; left-click it to run that filter.
 *   - Left-click the center hole, press Esc, click outside, or release Ctrl to
 *     dismiss without running anything.
 *   - Disabled items stay visible but dimmed; clicking one only reports why.
 *
 * The Ctrl key event itself is never swallowed, so Ctrl+click / Ctrl+key
 * shortcuts in the render view keep working. The overlay window is
 * input-transparent, so the 3D view also keeps receiving mouse events until a
 * click actually lands inside the ring.
 *
 * Only Ctrl without Shift/Alt/Meta is handled, and only while the cursor is
 * over a RenderView.
 */
class pqSHYXQuickFilterWheel : public QObject
{
  Q_OBJECT
  typedef QObject Superclass;

public:
  explicit pqSHYXQuickFilterWheel(QObject* parent = nullptr);
  ~pqSHYXQuickFilterWheel() override;

  void onStartup();
  void onShutdown();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct Item
  {
    QString label;
    bool enabled = true;
    QString unavailableDetail;
    std::function<void()> run;
  };

  QVector<Item> buildItems() const;
  void handleCtrlPressed();
  void handleCtrlReleased();
  void showWheelAt(const QPoint& globalPosition);
  void dismissWheel();
  void activate(int index);
  void updateHover(const QPoint& globalPosition);

  Q_DISABLE_COPY(pqSHYXQuickFilterWheel)

  QPointer<pqSHYXQuickFilterWheelOverlay> Overlay;
  QTimer* ShowTimer = nullptr;
  QVector<Item> Items;
  QPoint PendingPosition;
  bool Installed = false;
  bool SwallowNextRelease = false;
};

#endif
