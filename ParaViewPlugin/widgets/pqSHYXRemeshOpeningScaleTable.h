#ifndef pqSHYXRemeshOpeningScaleTable_h
#define pqSHYXRemeshOpeningScaleTable_h

#include "pqPropertyWidget.h"

#include <QMetaObject>
#include <vector>

class QLabel;
class QShowEvent;
class QTableWidget;
class QTimer;
class vtkEventQtSlotConnect;

/**
 * One row per open boundary of SHYX Remesh With Endpoint when Anchor boundary size is on
 * and Protect constraints is off. Point is a vertex on that loop. Scale multiplies the
 * whole opening's mean border-edge length. Stored as (centroid x, y, z, scale).
 */
class pqSHYXRemeshOpeningScaleTable : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXRemeshOpeningScaleTable(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  ~pqSHYXRemeshOpeningScaleTable() override;

  void apply() override;
  void reset() override;

protected:
  void showEvent(QShowEvent* event) override;

private Q_SLOTS:
  void scheduleRefresh();
  void refresh();
  void onScaleEdited();

private:
  struct Row
  {
    double Centroid[3] = { 0.0, 0.0, 0.0 };
    double Sample[3] = { 0.0, 0.0, 0.0 };
    double MeanEdge = 0.0;
    int PointCount = 0;
    double Scale = 1.0;
  };

  struct SavedScale
  {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double Scale = 1.0;
  };

  std::vector<SavedScale> readSaved() const;
  void writeRows(bool pushChecked);
  void fillRows(const std::vector<Row>& discovered, const std::vector<SavedScale>& saved);
  void rebuildTable();
  double boundaryScale() const;
  void watchInput();

  QLabel* Status = nullptr;
  QTableWidget* Table = nullptr;
  QTimer* Debounce = nullptr;
  vtkEventQtSlotConnect* PropertyConnect = nullptr;
  QMetaObject::Connection InputDataConnection;
  std::vector<Row> Rows;
  bool Updating = false;
};

#endif
