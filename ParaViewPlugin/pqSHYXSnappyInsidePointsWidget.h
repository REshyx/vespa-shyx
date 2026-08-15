#ifndef pqSHYXSnappyInsidePointsWidget_h
#define pqSHYXSnappyInsidePointsWidget_h

#include "pqInteractivePropertyWidget.h"

#include <vector>

class QPushButton;
class QTableWidget;
class pqView;

/**
 * List of snappyHexMesh keep-points (locationInMesh / locationsInMesh).
 * Add insidePoint places a point at the input AABB centre; selecting a row
 * shows a draggable HandleWidget.
 */
class pqSHYXSnappyInsidePointsWidget : public pqInteractivePropertyWidget
{
    Q_OBJECT
    typedef pqInteractivePropertyWidget Superclass;

public:
    pqSHYXSnappyInsidePointsWidget(
        vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXSnappyInsidePointsWidget() override;

    void select() override;
    void setView(pqView* view) override;
    void apply() override;
    void reset() override;

protected:
    void showEvent(QShowEvent* event) override;

protected Q_SLOTS:
    void placeWidget() override;
    void updateWidgetVisibility() override;

private Q_SLOTS:
    void onAddInsidePoint();
    void onRemoveInsidePoint();
    void onTableSelectionChanged();
    void onTableCellChanged(int row, int column);
    void onHandleInteraction();
    void onHandleEndInteraction();
    void onInsidePointsPropertyChanged();
    void onShowInteractiveAxisToggled(bool on);

private:
    std::vector<double> readPacked() const;
    void writePacked(const std::vector<double>& packed, bool finished);
    void rebuildTableFromProperty();
    void setRowText(int row, const double xyz[3]);
    void syncHandleToActiveRow();
    void styleHandle();
    bool inputCenter(double c[3]) const;
    double inputDiagonal() const;

    QTableWidget* Table = nullptr;
    QPushButton* AddButton = nullptr;
    QPushButton* RemoveButton = nullptr;
    QPushButton* ShowAxisButton = nullptr;
    int ActiveIndex = -1;
    bool Updating = false;
};

#endif
