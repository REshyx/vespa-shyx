#ifndef pqSHYXEndClipperEndpointTable_h
#define pqSHYXEndClipperEndpointTable_h

#include "pqPropertyWidget.h"

class QLabel;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class vtkSMPropertyGroup;

/**
 * Endpoints to Clip: one row per leaf, with a clip checkbox and that endpoint's
 * Tangent Depth (1-10). Checkboxes stay on EndpointStatus. Depths are
 * (endpoint id, depth) pairs on EndpointTangentDepths. A row with no stored
 * pair shows TangentDepth.
 *
 * Row selection (not the checkbox) is what the cut-plane widget follows.
 * The view object name is SHYXEndClipperEndpointTable.
 */
class pqSHYXEndClipperEndpointTable : public pqPropertyWidget
{
    Q_OBJECT
    typedef pqPropertyWidget Superclass;

public:
    pqSHYXEndClipperEndpointTable(
        vtkSMProxy* proxy, vtkSMPropertyGroup* smgroup, QWidget* parent = nullptr);
    ~pqSHYXEndClipperEndpointTable() override;

    bool event(QEvent* e) override;
    void apply() override;
    void reset() override;

Q_SIGNALS:
    void endpointStatusChanged();

private Q_SLOTS:
    void onItemChanged(QStandardItem* item);

private:
    void rebuild();
    void writeStatus();
    void writeDepths(bool pushChecked);
    void toggleAllChecks();
    void refreshHeader();
    void updateHint();
    Qt::CheckState columnCheckState() const;
    int fallbackDepth() const;
    int currentEndpointId() const;

    QLabel* Hint = nullptr;
    QStandardItemModel* Model = nullptr;
    QTreeView* View = nullptr;
    QString EndpointPropName;

    bool Updating = false;
    bool UpdatingFromUI = false;
};

#endif
