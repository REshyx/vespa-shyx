#include "pqSHYXAIImageAnnotator.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

class pqSHYXAIImageAnnotator::Canvas : public QWidget
{
public:
  explicit Canvas(QWidget* parent)
    : QWidget(parent)
  {
    this->setMinimumSize(320, 240);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setCursor(Qt::CrossCursor);
    this->setMouseTracking(false);
  }

  void setSource(const QImage& src)
  {
    this->Original = src.convertToFormat(QImage::Format_RGB32);
    this->Working = this->Original;
    this->update();
  }

  QImage image() const { return this->Working; }

  void setBrushColor(const QColor& c) { this->Color = c; }
  void setBrushWidth(int w) { this->Width = qBound(2, w, 32); }
  void clearMarks()
  {
    this->Working = this->Original;
    this->update();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter p(this);
    p.fillRect(this->rect(), QColor(32, 32, 32));
    if (this->Working.isNull())
    {
      return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(this->imageRect(), this->Working);
  }

  void mousePressEvent(QMouseEvent* event) override
  {
    if (event->button() != Qt::LeftButton)
    {
      return;
    }
    this->Last = this->toImage(event->pos());
    this->Drawing = this->Last.x() >= 0;
    if (this->Drawing)
    {
      this->stroke(this->Last, this->Last);
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override
  {
    if (!this->Drawing || !(event->buttons() & Qt::LeftButton))
    {
      return;
    }
    const QPoint now = this->toImage(event->pos());
    if (now.x() < 0)
    {
      return;
    }
    this->stroke(this->Last, now);
    this->Last = now;
  }

  void mouseReleaseEvent(QMouseEvent* event) override
  {
    if (event->button() == Qt::LeftButton)
    {
      this->Drawing = false;
    }
  }

private:
  QRect imageRect() const
  {
    if (this->Working.isNull() || this->width() < 1 || this->height() < 1)
    {
      return {};
    }
    const QSize fitted =
      this->Working.size().scaled(this->size(), Qt::KeepAspectRatio);
    return QRect((this->width() - fitted.width()) / 2,
      (this->height() - fitted.height()) / 2, fitted.width(), fitted.height());
  }

  QPoint toImage(const QPoint& widgetPos) const
  {
    const QRect r = this->imageRect();
    if (!r.isValid() || !r.contains(widgetPos))
    {
      return { -1, -1 };
    }
    const int x = (widgetPos.x() - r.x()) * this->Working.width() / r.width();
    const int y = (widgetPos.y() - r.y()) * this->Working.height() / r.height();
    return { qBound(0, x, this->Working.width() - 1),
      qBound(0, y, this->Working.height() - 1) };
  }

  void stroke(const QPoint& a, const QPoint& b)
  {
    if (this->Working.isNull())
    {
      return;
    }
    QPainter p(&this->Working);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(this->Color, this->Width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    if (a == b)
    {
      p.drawPoint(a);
    }
    else
    {
      p.drawLine(a, b);
    }
    this->update();
  }

  QImage Original;
  QImage Working;
  QColor Color = Qt::red;
  int Width = 6;
  QPoint Last;
  bool Drawing = false;
};

pqSHYXAIImageAnnotator::pqSHYXAIImageAnnotator(QWidget* parent, const QImage& source)
  : QDialog(parent)
{
  this->setWindowTitle(tr("Mark screenshot"));
  this->resize(900, 640);

  auto* layout = new QVBoxLayout(this);
  this->Pad = new Canvas(this);
  this->Pad->setSource(source);
  layout->addWidget(this->Pad, 1);

  auto* tools = new QHBoxLayout();
  auto addColor = [this, tools](const QString& name, const QColor& c) {
    auto* btn = new QPushButton(name, this);
    btn->setMaximumWidth(72);
    btn->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                         .arg(c.name(), c.lightness() < 140 ? QStringLiteral("white")
                                                            : QStringLiteral("black")));
    QObject::connect(btn, &QPushButton::clicked, this, [this, c]() { this->Pad->setBrushColor(c); });
    tools->addWidget(btn);
  };
  addColor(tr("Red"), QColor(220, 40, 40));
  addColor(tr("Yellow"), QColor(240, 200, 20));
  addColor(tr("Green"), QColor(40, 180, 70));
  addColor(tr("White"), QColor(245, 245, 245));

  tools->addWidget(new QLabel(tr("Size"), this));
  auto* size = new QSlider(Qt::Horizontal, this);
  size->setRange(2, 24);
  size->setValue(6);
  size->setMaximumWidth(140);
  QObject::connect(size, &QSlider::valueChanged, this, [this](int v) { this->Pad->setBrushWidth(v); });
  tools->addWidget(size);

  auto* clearBtn = new QPushButton(tr("Clear marks"), this);
  QObject::connect(clearBtn, &QPushButton::clicked, this, [this]() { this->Pad->clearMarks(); });
  tools->addWidget(clearBtn);
  tools->addStretch(1);
  layout->addLayout(tools);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

QImage pqSHYXAIImageAnnotator::resultImage() const
{
  return this->Pad ? this->Pad->image() : QImage();
}

QImage pqSHYXAIImageAnnotator::annotate(QWidget* parent, const QImage& source)
{
  if (source.isNull())
  {
    return {};
  }
  pqSHYXAIImageAnnotator dlg(parent, source);
  if (dlg.exec() == QDialog::Accepted)
  {
    return dlg.resultImage();
  }
  return {};
}
