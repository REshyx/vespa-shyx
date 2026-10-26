#ifndef pqSHYXQuickFilterWheelOverlay_h
#define pqSHYXQuickFilterWheelOverlay_h

#include <QPoint>
#include <QStringList>
#include <QVector>
#include <QWidget>

/**
 * Transparent, input-transparent ring drawn around the cursor by
 * pqSHYXQuickFilterWheel.
 *
 * The widget is a top-level Qt::Tool window flagged WindowTransparentForInput,
 * so the 3D view keeps receiving every mouse event; the controller only asks
 * sectorAt() for the sector under the cursor. Nothing here handles events.
 *
 * Layout: sector 0 starts at 12 o'clock and the sectors run clockwise, so two
 * items end up as the right half (0) and the left half (1). Labels are drawn
 * tangentially, which keeps them inside the ring for any item count.
 */
class pqSHYXQuickFilterWheelOverlay : public QWidget
{
  Q_OBJECT
  typedef QWidget Superclass;

public:
  explicit pqSHYXQuickFilterWheelOverlay(QWidget* parent = nullptr);
  ~pqSHYXQuickFilterWheelOverlay() override;

  /// Labels and per-item enabled flags. The item count defines the sector count.
  void setItems(const QStringList& labels, const QVector<bool>& enabled);

  /// -1 = no sector highlighted.
  void setHovered(int index);
  int hovered() const { return this->Hovered; }

  /// Sector index under \a globalPosition, or -1 in the center hole / outside.
  int sectorAt(const QPoint& globalPosition) const;

  /// True inside the outer radius (the center hole counts: it cancels).
  bool containsPoint(const QPoint& globalPosition) const;

  /// Center the wheel on \a globalPosition (clamped to the screen) and show it.
  void showAtGlobal(const QPoint& globalPosition);

  /// Outer ring radius in logical pixels.
  static int outerRadius();
  /// Widget width/height in logical pixels.
  static int widgetSize();

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  Q_DISABLE_COPY(pqSHYXQuickFilterWheelOverlay)

  QStringList Labels;
  QVector<bool> Enabled;
  int Hovered = -1;
  QPoint Center;
};

#endif
