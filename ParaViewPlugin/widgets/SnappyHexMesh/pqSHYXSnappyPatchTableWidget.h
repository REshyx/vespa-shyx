#ifndef pqSHYXSnappyPatchTableWidget_h
#define pqSHYXSnappyPatchTableWidget_h

#include "pqPropertyWidget.h"

#include <QString>
#include <QStringList>

class QLabel;
class QStandardItem;
class QStandardItemModel;
class pqTreeView;
class vtkSMPropertyGroup;

/**
 * Editable patch table for vtkSHYXSnappyHexMesh: Surface patches, Region patches, or
 * Layer patches. Add picks an Input partition name (not a 3D selection).
 */
class pqSHYXSnappyPatchTableWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  enum Kind
  {
    Surfaces,
    Regions,
    Layers
  };

  struct Row
  {
    QString Name;
    int LevelMin = 0;
    int LevelMax = 2;
    QString PatchType = QStringLiteral("wall");
    QString Mode = QStringLiteral("inside");
    int Level = 2;
    double Distance = 0.0;
    int NSurfaceLayers = 3;
  };

  pqSHYXSnappyPatchTableWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
  pqSHYXSnappyPatchTableWidget(vtkSMProxy* proxy, Kind kind, QWidget* parent = nullptr);
  ~pqSHYXSnappyPatchTableWidget() override;

  bool event(QEvent* e) override;
  void apply() override;
  void reset() override;

Q_SIGNALS:
  void patchesChanged();

private Q_SLOTS:
  void onItemChanged(QStandardItem* item);
  void onAddClicked();
  void onAddName(const QString& name);
  void onAddAllRemaining();
  void onRemoveSelected();

private:
  void initialize(vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup);
  void rebuildFromProperty();
  void rebuildRows(const QList<Row>& rows);
  void writeBackProperty();
  QList<Row> collectRows() const;
  QStringList linesFromProperty(const QString& propertyName) const;
  void setStatus(const QString& text, bool error);
  QStringList inputPartitionNames() const;
  QStringList unusedPartitionNames() const;
  int defaultLevelMin() const;
  int defaultLevelMax() const;
  int defaultNSurfaceLayers() const;
  Row defaultRow(const QString& name) const;

  Kind TableKind = Surfaces;
  QStandardItemModel* Model = nullptr;
  pqTreeView* View = nullptr;
  QLabel* Status = nullptr;
  QString NamesPropertyName;
  QString LevelMinPropertyName;
  QString LevelMaxPropertyName;
  QString PatchTypesPropertyName;
  QString ModesPropertyName;
  QString LevelsPropertyName;
  QString DistancesPropertyName;
  QString NLayersPropertyName;
  bool UpdatingFromProperty = false;
  bool UpdatingFromUI = false;
};

#endif
