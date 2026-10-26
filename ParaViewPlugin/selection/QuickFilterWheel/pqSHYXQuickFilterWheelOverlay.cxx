#include "pqSHYXQuickFilterWheelOverlay.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtMath>

#include <cmath>

namespace
{
constexpr int kOuterRadius = 150;
constexpr int kInnerRadius = 58;
constexpr int kMargin = 4;
constexpr qreal kGapDegrees = 1.6;

//-----------------------------------------------------------------------------
QPointF PolarPoint(const QPointF& center, qreal degrees, qreal radius)
{
  const qreal radians = qDegreesToRadians(degrees);
  return center + QPointF(std::cos(radians) * radius, -std::sin(radians) * radius);
}

//-----------------------------------------------------------------------------
QRectF CircleRect(const QPointF& center, qreal radius)
{
  return QRectF(center.x() - radius, center.y() - radius, 2.0 * radius, 2.0 * radius);
}

//-----------------------------------------------------------------------------
/// Annulus sector between two polar angles (degrees, counter-clockwise).
QPainterPath SectorPath(const QPointF& center, qreal firstEdge, qreal secondEdge)
{
  QPainterPath path;
  path.moveTo(PolarPoint(center, firstEdge, kInnerRadius));
  path.arcTo(CircleRect(center, kOuterRadius), firstEdge, secondEdge - firstEdge);
  path.lineTo(PolarPoint(center, secondEdge, kInnerRadius));
  path.arcTo(CircleRect(center, kInnerRadius), secondEdge, firstEdge - secondEdge);
  path.closeSubpath();
  return path;
}
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheelOverlay::pqSHYXQuickFilterWheelOverlay(QWidget* parent)
  : Superclass(parent,
      Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
        Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus |
        Qt::WindowTransparentForInput)
{
  this->setAttribute(Qt::WA_TranslucentBackground, true);
  this->setAttribute(Qt::WA_ShowWithoutActivating, true);
  this->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  this->setFocusPolicy(Qt::NoFocus);
  this->resize(widgetSize(), widgetSize());
}

//-----------------------------------------------------------------------------
pqSHYXQuickFilterWheelOverlay::~pqSHYXQuickFilterWheelOverlay() = default;

//-----------------------------------------------------------------------------
int pqSHYXQuickFilterWheelOverlay::outerRadius()
{
  return kOuterRadius;
}

//-----------------------------------------------------------------------------
int pqSHYXQuickFilterWheelOverlay::widgetSize()
{
  return 2 * (kOuterRadius + kMargin);
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelOverlay::setItems(const QStringList& labels,
  const QVector<bool>& enabled)
{
  this->Labels = labels;
  this->Enabled = enabled;
  this->Hovered = -1;
  this->update();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelOverlay::setHovered(int index)
{
  const int clamped = (index >= 0 && index < this->Labels.size()) ? index : -1;
  if (clamped == this->Hovered)
  {
    return;
  }
  this->Hovered = clamped;
  this->update();
}

//-----------------------------------------------------------------------------
int pqSHYXQuickFilterWheelOverlay::sectorAt(const QPoint& globalPosition) const
{
  const int count = this->Labels.size();
  if (count <= 0)
  {
    return -1;
  }
  const QPointF delta = QPointF(globalPosition) - QPointF(this->Center);
  const qreal distance = std::hypot(delta.x(), delta.y());
  if (distance < kInnerRadius || distance > kOuterRadius)
  {
    return -1;
  }

  // Clockwise offset from 12 o'clock.
  const qreal angle = qRadiansToDegrees(std::atan2(-delta.y(), delta.x()));
  const qreal span = 360.0 / count;
  const qreal offset = std::fmod(90.0 - angle + 720.0, 360.0);
  const int index = static_cast<int>(offset / span);
  return qBound(0, index, count - 1);
}

//-----------------------------------------------------------------------------
bool pqSHYXQuickFilterWheelOverlay::containsPoint(const QPoint& globalPosition) const
{
  const QPointF delta = QPointF(globalPosition) - QPointF(this->Center);
  return std::hypot(delta.x(), delta.y()) <= kOuterRadius;
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelOverlay::showAtGlobal(const QPoint& globalPosition)
{
  const int size = widgetSize();
  const int half = size / 2;
  QRect geometry(globalPosition.x() - half, globalPosition.y() - half, size, size);

  QScreen* screen = QGuiApplication::screenAt(globalPosition);
  if (!screen)
  {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen)
  {
    const QRect available = screen->availableGeometry();
    const int maxX = qMax(available.left(), available.right() - size + 1);
    const int maxY = qMax(available.top(), available.bottom() - size + 1);
    geometry.moveLeft(qBound(available.left(), geometry.left(), maxX));
    geometry.moveTop(qBound(available.top(), geometry.top(), maxY));
  }

  this->setGeometry(geometry);
  this->Center = geometry.topLeft() + QPoint(half, half);
  this->Hovered = -1;
  this->update();
  this->show();
  this->raise();
}

//-----------------------------------------------------------------------------
void pqSHYXQuickFilterWheelOverlay::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const QPointF center(this->width() / 2.0, this->height() / 2.0);
  const int count = this->Labels.size();
  const qreal span = count > 0 ? 360.0 / count : 360.0;

  // Soft halo so the ring separates from both bright and dark scenes.
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(8, 10, 14, 46));
  painter.drawEllipse(center, kOuterRadius + 3.0, kOuterRadius + 3.0);

  for (int i = 0; i < count; ++i)
  {
    const bool enabled = i < this->Enabled.size() ? this->Enabled.at(i) : true;
    const bool hovered = (i == this->Hovered);
    const qreal firstEdge = 90.0 - i * span - kGapDegrees * 0.5;
    const qreal secondEdge = 90.0 - (i + 1) * span + kGapDegrees * 0.5;

    QColor fill(30, 33, 38, 232);
    QColor text(238, 240, 244);
    if (!enabled)
    {
      fill = hovered ? QColor(62, 55, 42, 235) : QColor(34, 34, 36, 220);
      text = hovered ? QColor(214, 192, 148) : QColor(136, 138, 143);
    }
    else if (hovered)
    {
      fill = QColor(41, 111, 190, 245);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(SectorPath(center, firstEdge, secondEdge));

    const qreal midAngle = 90.0 - (i + 0.5) * span;
    const qreal radius = (kInnerRadius + kOuterRadius) / 2.0;
    const QPointF anchor = PolarPoint(center, midAngle, radius);

    // Tangential label, flipped so it never reads upside down.
    qreal rotation = -(midAngle + 90.0);
    while (rotation <= -90.0)
    {
      rotation += 180.0;
    }
    while (rotation > 90.0)
    {
      rotation -= 180.0;
    }

    QFont font = painter.font();
    font.setBold(hovered);
    font.setPointSizeF(10.0);
    const QFontMetricsF metrics(font);
    const qreal arcLength = qDegreesToRadians(span - kGapDegrees) * radius;
    const QString label =
      metrics.elidedText(this->Labels.at(i), Qt::ElideRight, qMax<qreal>(24.0, arcLength - 18.0));

    painter.save();
    painter.translate(anchor);
    painter.rotate(rotation);
    painter.setFont(font);
    painter.setPen(text);
    painter.drawText(QRectF(-arcLength / 2.0, -12.0, arcLength, 24.0), Qt::AlignCenter, label);
    painter.restore();
  }

  // Center hole: cancel target.
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(18, 20, 24, 238));
  painter.drawEllipse(center, kInnerRadius - 3.0, kInnerRadius - 3.0);

  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(255, 255, 255, 58), 1.2));
  painter.drawEllipse(center, kOuterRadius - 0.5, kOuterRadius - 0.5);
  painter.drawEllipse(center, kInnerRadius - 3.0, kInnerRadius - 3.0);

  QFont hint = painter.font();
  hint.setBold(false);
  hint.setPointSizeF(8.5);
  painter.setFont(hint);
  painter.setPen(QColor(168, 172, 180));
  painter.drawText(QRectF(center.x() - kInnerRadius, center.y() - 10.0, 2.0 * kInnerRadius, 20.0),
    Qt::AlignCenter, tr("esc"));
}
