#ifndef pqSHYXRemeshUncappedHistogramPanel_h
#define pqSHYXRemeshUncappedHistogramPanel_h

#include "pqPropertyWidget.h"

#include <QPointer>
#include <QVector>

class QLabel;
class QTimer;
class pqPipelineSource;
class vtkEventQtSlotConnect;
class vtkSMPropertyGroup;

/**
 * Read-only ICC target-size histogram for SHYX Remesh With Endpoint.
 * Client-side preview from Input; updates live with AdaptiveTolerance and
 * Min/Max edge length (unchecked values, debounced). Shows sizes after clamp
 * to [Min, Max] (VespaSizeGlobal).
 */
class pqSHYXRemeshUncappedHistogramPanel : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXRemeshUncappedHistogramPanel(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXRemeshUncappedHistogramPanel() override;

private Q_SLOTS:
  void schedulePreview();
  void schedulePreviewDebounced();
  void runPreview();
  void onPipelineDataUpdated();

private:
  class Canvas;

  void setHistogramDisplay(const QVector<double>& counts, double rangeMin, double rangeMax,
    int sampleCount, double uncappedMin, double uncappedMax, bool hasUncappedRange);
  bool computePreviewFromInput();

  QPointer<pqPipelineSource> PipelineSource;
  QLabel* SummaryLabel = nullptr;
  Canvas* Chart = nullptr;
  vtkEventQtSlotConnect* PropertyConnect = nullptr;
  QTimer* DebounceTimer = nullptr;
  bool PreviewPending = false;
};

#endif
