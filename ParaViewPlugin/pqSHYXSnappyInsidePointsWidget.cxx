#include "pqSHYXSnappyInsidePointsWidget.h"

#include "pqCoreUtilities.h"
#include "pqInteractivePropertyWidgetAbstract.h"
#include "pqView.h"

#include "vtk3DWidgetRepresentation.h"
#include "vtkBoundingBox.h"
#include "vtkCommand.h"
#include "vtkMath.h"
#include "vtkPointHandleRepresentation3D.h"
#include "vtkProperty.h"
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMNewWidgetRepresentationProxy.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
QString formatCoord(double v)
{
    return QString::number(v, 'g', 8);
}
}

//-----------------------------------------------------------------------------
pqSHYXSnappyInsidePointsWidget::pqSHYXSnappyInsidePointsWidget(
    vtkSMProxy* smproxy, vtkSMPropertyGroup* smgroup, QWidget* parent)
    : Superclass("representations", "HandleWidgetRepresentation", smproxy, smgroup, parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(4);

    auto* tip = new QLabel(
        tr("Keep-mesh points for castellated meshing. Empty list uses the AABB centre. "
           "Add one point per disconnected region; each point must sit in a cell to keep "
           "(not on a face). Click Show interactive axis, then select a row to drag."),
        this);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    vbox->addWidget(tip);

    this->ShowAxisButton = new QPushButton(tr("Show interactive axis"), this);
    this->ShowAxisButton->setObjectName("SHYXSnappyShowInteractiveAxis");
    this->ShowAxisButton->setCheckable(true);
    this->ShowAxisButton->setChecked(false);
    this->ShowAxisButton->setToolTip(
        tr("Show a draggable 3D axis at the selected insidePoint. Turn off to hide it."));
    vbox->addWidget(this->ShowAxisButton);

    this->Table = new QTableWidget(0, 3, this);
    this->Table->setObjectName("SHYXSnappyInsidePointsTable");
    this->Table->setHorizontalHeaderLabels({ tr("X"), tr("Y"), tr("Z") });
    this->Table->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->Table->setSelectionMode(QAbstractItemView::SingleSelection);
    this->Table->setAlternatingRowColors(true);
    this->Table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    this->Table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    this->Table->setMaximumHeight(160);
    vbox->addWidget(this->Table);

    auto* buttons = new QHBoxLayout();
    this->AddButton = new QPushButton(tr("Add insidePoint"), this);
    this->RemoveButton = new QPushButton(tr("Remove"), this);
    buttons->addWidget(this->AddButton);
    buttons->addWidget(this->RemoveButton);
    buttons->addStretch(1);
    vbox->addLayout(buttons);

    this->styleHandle();
    this->setWidgetVisible(false);

    QObject::connect(this->ShowAxisButton, &QPushButton::toggled, this,
        &pqSHYXSnappyInsidePointsWidget::onShowInteractiveAxisToggled);
    QObject::connect(this->AddButton, &QPushButton::clicked, this,
        &pqSHYXSnappyInsidePointsWidget::onAddInsidePoint);
    QObject::connect(this->RemoveButton, &QPushButton::clicked, this,
        &pqSHYXSnappyInsidePointsWidget::onRemoveInsidePoint);
    QObject::connect(this->Table, &QTableWidget::itemSelectionChanged, this,
        &pqSHYXSnappyInsidePointsWidget::onTableSelectionChanged);
    QObject::connect(this->Table, &QTableWidget::cellChanged, this,
        &pqSHYXSnappyInsidePointsWidget::onTableCellChanged);

    QObject::connect(static_cast<pqInteractivePropertyWidgetAbstract*>(this),
        &pqInteractivePropertyWidgetAbstract::interaction, this,
        &pqSHYXSnappyInsidePointsWidget::onHandleInteraction);
    QObject::connect(static_cast<pqInteractivePropertyWidgetAbstract*>(this),
        &pqInteractivePropertyWidgetAbstract::endInteraction, this,
        &pqSHYXSnappyInsidePointsWidget::onHandleEndInteraction);

    if (vtkSMProperty* prop = smproxy->GetProperty("InsidePoints"))
    {
        if (auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(prop))
        {
            if (dvp->GetNumberOfUncheckedElements() == 0 && dvp->GetNumberOfElements() > 0)
            {
                dvp->SetUncheckedElements(dvp->GetElements(), dvp->GetNumberOfElements());
            }
        }
        pqCoreUtilities::connect(prop, vtkCommand::ModifiedEvent, this,
            SLOT(onInsidePointsPropertyChanged()));
        pqCoreUtilities::connect(prop, vtkCommand::UncheckedPropertyModifiedEvent, this,
            SLOT(onInsidePointsPropertyChanged()));
    }

    this->rebuildTableFromProperty();
}

//-----------------------------------------------------------------------------
pqSHYXSnappyInsidePointsWidget::~pqSHYXSnappyInsidePointsWidget() = default;

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::select()
{
    if (vtkSMProperty* input = this->proxy()->GetProperty("Input"))
    {
        this->setDataSource(vtkSMPropertyHelper(input).GetAsProxy());
    }
    this->setWidgetVisible(this->ShowAxisButton && this->ShowAxisButton->isChecked());
    this->styleHandle();
    this->Superclass::select();
    this->syncHandleToActiveRow();
    this->updateWidgetVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::setView(pqView* view)
{
    this->styleHandle();
    this->Superclass::setView(view);
    this->placeWidget();
    this->updateWidgetVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::showEvent(QShowEvent* event)
{
    this->Superclass::showEvent(event);
    this->setWidgetVisible(this->ShowAxisButton && this->ShowAxisButton->isChecked());
    this->updateWidgetVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::placeWidget()
{
    vtkSMNewWidgetRepresentationProxy* w = this->widgetProxy();
    auto* vtkWdg = w ? vtk3DWidgetRepresentation::SafeDownCast(w->GetClientSideObject()) : nullptr;
    if (!vtkWdg)
    {
        return;
    }

    double bds[6] = { -1.0e6, 1.0e6, -1.0e6, 1.0e6, -1.0e6, 1.0e6 };
    const vtkBoundingBox bbox = this->dataBounds();
    if (bbox.IsValid())
    {
        bbox.GetBounds(bds);
        const double span[3] = { bds[1] - bds[0], bds[3] - bds[2], bds[5] - bds[4] };
        const double pad = std::max(1.0, 2.0 * vtkMath::Norm(span));
        for (int i = 0; i < 3; ++i)
        {
            bds[2 * i] -= pad;
            bds[2 * i + 1] += pad;
        }
    }
    vtkWdg->PlaceWidget(bds);
    this->syncHandleToActiveRow();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::styleHandle()
{
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!wdg)
    {
        return;
    }

    const double yellow[3] = { 1.0, 0.85, 0.15 };
    vtkSMPropertyHelper(wdg, "ForegroundWidgetColor", true).Set(yellow, 3);
    vtkSMPropertyHelper(wdg, "InteractiveWidgetColor", true).Set(yellow, 3);

    if (auto* vtkWdg = vtk3DWidgetRepresentation::SafeDownCast(wdg->GetClientSideObject()))
    {
        // Draw in the overlay renderer so a keep-point inside an opaque surface stays visible.
        vtkWdg->UseNonCompositedRendererOn();
        if (auto* rep = vtkPointHandleRepresentation3D::SafeDownCast(vtkWdg->GetRepresentation()))
        {
            rep->SetPointPlacer(nullptr);
            rep->SetTranslationMode(1);
            rep->SetHandleSize(30.0);
            if (vtkProperty* prop = rep->GetProperty())
            {
                prop->SetColor(yellow[0], yellow[1], yellow[2]);
                prop->SetLineWidth(3.0);
                prop->SetOpacity(1.0);
            }
            if (vtkProperty* sel = rep->GetSelectedProperty())
            {
                sel->SetColor(1.0, 1.0, 0.0);
                sel->SetLineWidth(4.0);
            }
        }
    }
    wdg->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::updateWidgetVisibility()
{
    const bool havePoint = this->ActiveIndex >= 0 && this->ActiveIndex < this->Table->rowCount();
    const bool visible =
        this->isSelected() && this->isWidgetVisible() && this->view() && havePoint;
    if (vtkSMNewWidgetRepresentationProxy* wdgProxy = this->widgetProxy())
    {
        vtkSMPropertyHelper(wdgProxy, "Visibility", true).Set(visible ? 1 : 0);
        vtkSMPropertyHelper(wdgProxy, "Enabled", true).Set(visible ? 1 : 0);
        wdgProxy->UpdateVTKObjects();
    }
    this->render();
    Q_EMIT this->widgetVisibilityUpdated(visible);
}

//-----------------------------------------------------------------------------
std::vector<double> pqSHYXSnappyInsidePointsWidget::readPacked() const
{
    auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InsidePoints"));
    if (!dvp)
    {
        return {};
    }
    unsigned int n = dvp->GetNumberOfUncheckedElements();
    n = (n / 3) * 3;
    std::vector<double> packed(n, 0.0);
    for (unsigned int i = 0; i < n; ++i)
    {
        packed[i] = dvp->GetUncheckedElement(i);
    }
    return packed;
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::writePacked(const std::vector<double>& packed, bool finished)
{
    auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InsidePoints"));
    if (!dvp)
    {
        return;
    }
    const auto n = static_cast<unsigned int>(packed.size());
    this->Updating = true;
    dvp->SetUncheckedElements(n > 0 ? packed.data() : nullptr, n);
    this->Updating = false;
    Q_EMIT this->changeAvailable();
    if (finished)
    {
        Q_EMIT this->changeFinished();
    }
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::apply()
{
    // Drag/table edits only write UncheckedElements. Default apply() only copies
    // addPropertyLink widgets, so InsidePoints never reached VTK and RequestData
    // was skipped (Apply returned immediately, keep-region stayed AABB centre).
    auto* dvp = vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InsidePoints"));
    if (dvp)
    {
        const std::vector<double> packed = this->readPacked();
        this->Updating = true;
        if (packed.empty())
        {
            dvp->SetNumberOfElements(0);
        }
        else
        {
            dvp->SetElements(packed.data(), static_cast<unsigned int>(packed.size()));
        }
        this->Updating = false;
    }
    this->Superclass::apply();
    this->proxy()->UpdateVTKObjects();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::reset()
{
    this->Superclass::reset();
    if (auto* dvp =
            vtkSMDoubleVectorProperty::SafeDownCast(this->proxy()->GetProperty("InsidePoints")))
    {
        this->Updating = true;
        dvp->ClearUncheckedElements();
        this->Updating = false;
    }
    this->rebuildTableFromProperty();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::rebuildTableFromProperty()
{
    if (this->Updating)
    {
        return;
    }
    const std::vector<double> packed = this->readPacked();
    const int n = static_cast<int>(packed.size() / 3);
    const int keep = this->Table->currentRow();

    this->Updating = true;
    QSignalBlocker block(this->Table);
    this->Table->setRowCount(n);
    for (int i = 0; i < n; ++i)
    {
        const double xyz[3] = { packed[static_cast<size_t>(i) * 3],
            packed[static_cast<size_t>(i) * 3 + 1], packed[static_cast<size_t>(i) * 3 + 2] };
        this->setRowText(i, xyz);
    }
    this->Updating = false;

    if (n > 0)
    {
        const int row = std::clamp(keep, 0, n - 1);
        this->Table->selectRow(row);
        this->ActiveIndex = row;
    }
    else
    {
        this->ActiveIndex = -1;
    }
    this->syncHandleToActiveRow();
    this->updateWidgetVisibility();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::setRowText(int row, const double xyz[3])
{
    if (row < 0 || row >= this->Table->rowCount())
    {
        return;
    }
    this->Table->setVerticalHeaderItem(
        row, new QTableWidgetItem(tr("insidePoint %1").arg(row + 1)));
    QSignalBlocker block(this->Table);
    for (int c = 0; c < 3; ++c)
    {
        auto* item = this->Table->item(row, c);
        if (!item)
        {
            item = new QTableWidgetItem();
            this->Table->setItem(row, c, item);
        }
        item->setText(formatCoord(xyz[c]));
    }
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::syncHandleToActiveRow()
{
    if (this->ActiveIndex < 0)
    {
        return;
    }
    const std::vector<double> packed = this->readPacked();
    const int n = static_cast<int>(packed.size() / 3);
    if (this->ActiveIndex >= n)
    {
        return;
    }
    const double p[3] = { packed[static_cast<size_t>(this->ActiveIndex) * 3],
        packed[static_cast<size_t>(this->ActiveIndex) * 3 + 1],
        packed[static_cast<size_t>(this->ActiveIndex) * 3 + 2] };
    if (vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy())
    {
        this->Updating = true;
        vtkSMPropertyHelper(wdg, "WorldPosition").Set(p, 3);
        wdg->UpdateVTKObjects();
        this->Updating = false;
    }
}

//-----------------------------------------------------------------------------
bool pqSHYXSnappyInsidePointsWidget::inputCenter(double c[3]) const
{
    const vtkBoundingBox bbox = this->dataBounds();
    if (!bbox.IsValid())
    {
        c[0] = c[1] = c[2] = 0.0;
        return false;
    }
    bbox.GetCenter(c);
    return true;
}

//-----------------------------------------------------------------------------
double pqSHYXSnappyInsidePointsWidget::inputDiagonal() const
{
    const vtkBoundingBox bbox = this->dataBounds();
    if (!bbox.IsValid())
    {
        return 1.0;
    }
    double b[6];
    bbox.GetBounds(b);
    const double span[3] = { b[1] - b[0], b[3] - b[2], b[5] - b[4] };
    return std::max(1e-6, vtkMath::Norm(span));
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onAddInsidePoint()
{
    if (vtkSMProperty* input = this->proxy()->GetProperty("Input"))
    {
        this->setDataSource(vtkSMPropertyHelper(input).GetAsProxy());
    }
    std::vector<double> packed = this->readPacked();
    double c[3];
    this->inputCenter(c);
    const int n = static_cast<int>(packed.size() / 3);
    if (n > 0)
    {
        c[0] += 0.02 * this->inputDiagonal() * n;
    }
    packed.push_back(c[0]);
    packed.push_back(c[1]);
    packed.push_back(c[2]);
    this->writePacked(packed, true);
    this->rebuildTableFromProperty();
    this->Table->selectRow(n);
    this->ActiveIndex = n;
    this->syncHandleToActiveRow();
    this->updateWidgetVisibility();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onRemoveInsidePoint()
{
    const int row = this->Table->currentRow();
    std::vector<double> packed = this->readPacked();
    const int n = static_cast<int>(packed.size() / 3);
    if (row < 0 || row >= n)
    {
        return;
    }
    packed.erase(packed.begin() + static_cast<std::ptrdiff_t>(row) * 3,
        packed.begin() + static_cast<std::ptrdiff_t>(row + 1) * 3);
    this->writePacked(packed, true);
    this->rebuildTableFromProperty();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onTableSelectionChanged()
{
    if (this->Updating)
    {
        return;
    }
    this->ActiveIndex = this->Table->currentRow();
    this->syncHandleToActiveRow();
    this->updateWidgetVisibility();
    this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onTableCellChanged(int row, int column)
{
    if (this->Updating || row < 0 || column < 0 || column > 2)
    {
        return;
    }
    std::vector<double> packed = this->readPacked();
    const int n = static_cast<int>(packed.size() / 3);
    if (row >= n)
    {
        return;
    }
    bool ok = false;
    const double v = this->Table->item(row, column)->text().toDouble(&ok);
    if (!ok)
    {
        const double xyz[3] = { packed[static_cast<size_t>(row) * 3],
            packed[static_cast<size_t>(row) * 3 + 1], packed[static_cast<size_t>(row) * 3 + 2] };
        this->setRowText(row, xyz);
        return;
    }
    packed[static_cast<size_t>(row) * 3 + static_cast<size_t>(column)] = v;
    this->writePacked(packed, true);
    if (row == this->ActiveIndex)
    {
        this->syncHandleToActiveRow();
        this->render();
    }
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onHandleInteraction()
{
    if (this->Updating || this->ActiveIndex < 0)
    {
        return;
    }
    vtkSMNewWidgetRepresentationProxy* wdg = this->widgetProxy();
    if (!wdg)
    {
        return;
    }
    double p[3] = { 0.0, 0.0, 0.0 };
    vtkSMPropertyHelper(wdg, "WorldPosition").Get(p, 3);
    std::vector<double> packed = this->readPacked();
    const int n = static_cast<int>(packed.size() / 3);
    if (this->ActiveIndex >= n)
    {
        return;
    }
    packed[static_cast<size_t>(this->ActiveIndex) * 3] = p[0];
    packed[static_cast<size_t>(this->ActiveIndex) * 3 + 1] = p[1];
    packed[static_cast<size_t>(this->ActiveIndex) * 3 + 2] = p[2];
    this->writePacked(packed, false);
    this->Updating = true;
    this->setRowText(this->ActiveIndex, p);
    this->Updating = false;
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onHandleEndInteraction()
{
    Q_EMIT this->changeFinished();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onInsidePointsPropertyChanged()
{
    this->rebuildTableFromProperty();
}

//-----------------------------------------------------------------------------
void pqSHYXSnappyInsidePointsWidget::onShowInteractiveAxisToggled(bool on)
{
    this->setWidgetVisible(on);
    if (on)
    {
        this->styleHandle();
        if (this->Table->rowCount() > 0 && this->Table->currentRow() < 0)
        {
            this->Table->selectRow(0);
        }
        this->placeWidget();
        this->syncHandleToActiveRow();
    }
    this->updateWidgetVisibility();
    this->render();
}
