#ifndef pqSHYXAIImageAnnotator_h
#define pqSHYXAIImageAnnotator_h

#include <QImage>
#include <QDialog>

/**
 * Simple brush markup over a screenshot. Cancel returns a null image.
 */
class pqSHYXAIImageAnnotator : public QDialog
{
  Q_OBJECT

public:
  static QImage annotate(QWidget* parent, const QImage& source);

private:
  pqSHYXAIImageAnnotator(QWidget* parent, const QImage& source);
  QImage resultImage() const;

  class Canvas;
  Canvas* Pad = nullptr;
};

#endif
