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
 * Hold the ` key (the · key on a Chinese layout) over a 3D view to get a radial
 * quick-filter wheel under the cursor.
 *
 *   - Hold ` for ~200 ms → ring appears under the cursor. No mouse movement is
 *     needed, and no modifier is involved, so Ctrl+click / Ctrl+key shortcuts
 *     keep their usual meaning.
 *   - Move onto a sector; releasing the key runs it. Left-clicking a sector
 *     works too. Releasing over the center hole or outside the ring cancels.
 *   - The "+" sector opens a checkable list of the filters that can go on the
 *     wheel, in two sections: SHYX and ParaView / VESPA built-in, restricted to
 *     those with an Input in this build. Nothing is checked by default (the
 *     wheel then only shows "+"); the checked set is stored in QSettings and
 *     survives sessions.
 *   - Esc, a click outside the ring, or releasing the key over the center hole
 *     dismisses without running anything.
 *   - Disabled items stay visible but dimmed; committing one only reports why.
 *
 * The key state is polled (GetAsyncKeyState on Windows) rather than inferred
 * from key events, because a focused VTK render window can swallow them; that
 * also makes "hold and wait" work without moving the mouse.
 *
 * Only that one key, only over a RenderView of the active window, and never
 * while a text field has focus.
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
    /// The "+" sector: opens the checklist instead of running a filter.
    bool isConfig = false;
    QString unavailableDetail;
    std::function<void()> run;
  };

  /// Checked filters + the trailing "+" sector.
  QVector<Item> buildItems() const;
  /// The checklist opened by the "+" sector; persists the result in QSettings.
  void openConfigDialog();
  /// Release commit: run the sector under the cursor (or cancel when outside).
  void commitWheelAtCursor();

  /// Sample key / pointer / mouse state and arm or disarm the hold timer.
  void updateState();
  /// The ` key is physically down right now.
  bool triggerKeyDown() const;
  /// Something else (text field, popup, mouse drag) owns the input right now.
  bool uiBusy() const;

  void showWheelAt(const QPoint& globalPosition);
  void hideOverlay();
  void dismissWheel();
  void activate(int index);
  void updateHover(const QPoint& globalPosition);

  Q_DISABLE_COPY(pqSHYXQuickFilterWheel)

  QPointer<pqSHYXQuickFilterWheelOverlay> Overlay;
  /// ~200 ms hold: keeps a quick tap of the key from flashing the ring.
  QTimer* ShowTimer = nullptr;
  /// Polls the key and the pointer so key-event delivery cannot break the gesture.
  QTimer* PollTimer = nullptr;
  QVector<Item> Items;
  bool Installed = false;
  bool HoldArmed = false;
  bool SwallowNextRelease = false;
  /// Event-tracked key state, used where polling is not available.
  bool TriggerKeyHeld = false;
  /// Set after the user dismissed or after a click/drag: no new wheel until the
  /// key is released and pressed again.
  bool SuppressedUntilKeyRelease = false;
};

#endif
