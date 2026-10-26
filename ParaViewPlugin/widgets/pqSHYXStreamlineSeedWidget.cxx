#include "pqSHYXStreamlineSeedWidget.h"

#include "pqActiveObjects.h"
#include "pqPVApplicationCore.h"
#include "pqRenderView.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkCallbackCommand.h"
#include "vtkCamera.h"
#include "vtkCommand.h"
#include "vtkDataSet.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkGenericCell.h"
#include "vtkInteractorObserver.h"
#include "vtkLineRepresentation.h"
#include "vtkLineWidget2.h"
#include "vtkMath.h"
#include "vtkPlaneWidget.h"
#include "vtkPolyData.h"
#include "vtkProperty.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSMInputProperty.h"
#include "vtkSMEnumerationDomain.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyGroup.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSphereRepresentation.h"
#include "vtkSphereSource.h"
#include "vtkSphereWidget2.h"
#include "vtkStaticCellLocator.h"
#include "vtkStaticPointLocator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPointTolerance2 = 1e-20;

bool SamePoint(const double a[3], const double b[3])
{
  return vtkMath::Distance2BetweenPoints(a, b) <= kPointTolerance2;
}

/**
 * Looser test for "the widget already shows this value". Handle representations round-trip
 * world -> display -> world, so exact equality never holds; without a relative tolerance the
 * widget would look changed on every sync and re-render constantly.
 */
bool NearlySamePoint(const double a[3], const double b[3])
{
  const double scale = std::max({ 1.0, std::fabs(a[0]), std::fabs(a[1]), std::fabs(a[2]),
    std::fabs(b[0]), std::fabs(b[1]), std::fabs(b[2]) });
  const double tol = 1e-9 * scale;
  return vtkMath::Distance2BetweenPoints(a, b) <= tol * tol;
}

bool NearlySameValue(double a, double b)
{
  const double scale = std::max({ 1.0, std::fabs(a), std::fabs(b) });
  return std::fabs(a - b) <= 1e-9 * scale;
}

/**
 * Enum combos must round-trip the *entry text*, not the index: pqSMAdaptor::setEnumerationProperty
 * matches EnumerationDomain entry texts, so linking "currentIndex" writes "0"/"1"/"2", matches
 * nothing, and silently leaves the property at its old value. Populating the items from the domain
 * (same text, value kept as item data) keeps both directions in sync.
 */
bool PopulateEnumCombo(QComboBox* combo, vtkSMProperty* prop)
{
  if (!combo || !prop)
  {
    return false;
  }
  auto* domain = prop->FindDomain<vtkSMEnumerationDomain>();
  if (!domain || domain->GetNumberOfEntries() == 0)
  {
    return false;
  }
  QSignalBlocker blocker(combo);
  combo->clear();
  for (unsigned int i = 0; i < domain->GetNumberOfEntries(); ++i)
  {
    const char* text = domain->GetEntryText(i);
    combo->addItem(QString::fromUtf8(text ? text : ""));
    combo->setItemData(static_cast<int>(i), domain->GetEntryValue(i), Qt::UserRole);
  }
  return true;
}

/** Enum value of the current combo item (falls back to the item index). */
int ComboEnumValue(const QComboBox* combo)
{
  if (!combo || combo->count() == 0)
  {
    return 0;
  }
  const QVariant data = combo->currentData();
  return data.isValid() ? data.toInt() : combo->currentIndex();
}

QDoubleSpinBox* MakeDoubleSpin(QWidget* parent)
{
  auto* spin = new QDoubleSpinBox(parent);
  spin->setRange(-1.0e12, 1.0e12);
  spin->setDecimals(6);
  spin->setSingleStep(0.1);
  spin->setKeyboardTracking(false);
  return spin;
}

QSpinBox* MakeIntSpin(QWidget* parent, int minimum, int maximum)
{
  auto* spin = new QSpinBox(parent);
  spin->setRange(minimum, maximum);
  spin->setKeyboardTracking(false);
  return spin;
}

void SetVec3(QDoubleSpinBox* spins[3], const double v[3])
{
  for (int i = 0; i < 3; ++i)
  {
    if (spins[i])
    {
      QSignalBlocker blocker(spins[i]);
      spins[i]->setValue(v[i]);
    }
  }
}

void StyleLineRepresentation(vtkLineRepresentation* rep)
{
  if (!rep)
  {
    return;
  }
  rep->SetLineColor(0.2, 0.65, 1.0);
  rep->SetInteractionColor(1.0, 0.85, 0.15);
  if (auto* p = rep->GetEndPointProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
  if (auto* p = rep->GetEndPoint2Property())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
  rep->DistanceAnnotationVisibilityOn();
}

void StyleSphereRepresentation(vtkSphereRepresentation* rep)
{
  if (!rep)
  {
    return;
  }
  rep->SetRepresentationToSurface();
  rep->SetPhiResolution(24);
  rep->SetThetaResolution(32);
  rep->HandleVisibilityOn();
  if (auto* p = rep->GetSphereProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
    p->SetOpacity(0.35);
  }
  if (auto* p = rep->GetSelectedSphereProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
    p->SetOpacity(0.45);
  }
  if (auto* p = rep->GetHandleProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
}

/**
 * vtkSphereRepresentation::SetCenter() rewrites the radius from the handle distance whenever the
 * handle is visible. That silently discards a radius the user just set, so every center update has
 * to put the radius back.
 */
void SetSphereCenterKeepingRadius(vtkSphereRepresentation* rep, const double center[3], double radius)
{
  if (!rep)
  {
    return;
  }
  const double keep = radius > 0.0 ? radius : rep->GetRadius();
  rep->SetCenter(const_cast<double*>(center));
  if (keep > 0.0)
  {
    rep->SetRadius(keep);
  }
}

void StylePlaneWidget(vtkPlaneWidget* widget)
{
  if (!widget)
  {
    return;
  }
  if (auto* p = widget->GetPlaneProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
    p->SetOpacity(0.35);
  }
  if (auto* p = widget->GetSelectedPlaneProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
    p->SetOpacity(0.45);
  }
  if (auto* p = widget->GetHandleProperty())
  {
    p->SetColor(0.2, 0.65, 1.0);
  }
  if (auto* p = widget->GetSelectedHandleProperty())
  {
    p->SetColor(1.0, 0.85, 0.15);
  }
  widget->SetRepresentationToSurface();
}
}

//-----------------------------------------------------------------------------
// Sphere representation with the interactions the seed widget advertises:
//  - left-drag on the sphere body  -> translate (vtkSphereRepresentation::OnSphere).
//  - left-drag on the surface handle -> change the radius (stock VTK only rotates the handle
//    there: SetHandleDirection() renormalizes to the current radius, so radius never changed).
//  - right-drag -> the stock vtkSphereWidget2 scale path.
//-----------------------------------------------------------------------------
class vtkSHYXSeedSphereRepresentation : public vtkSphereRepresentation
{
public:
  static vtkSHYXSeedSphereRepresentation* New();
  vtkTypeMacro(vtkSHYXSeedSphereRepresentation, vtkSphereRepresentation);

  void WidgetInteraction(double e[2]) override
  {
    if (this->InteractionState != vtkSphereRepresentation::MovingHandle || !this->Renderer ||
      !this->SphereSource)
    {
      this->Superclass::WidgetInteraction(e);
      return;
    }

    vtkCamera* camera = this->Renderer->GetActiveCamera();
    if (!camera)
    {
      return;
    }

    // Drag on the plane that contains the handle and faces the camera, so the cursor distance
    // from the (fixed) center maps directly to the radius.
    double focalPoint[4];
    vtkInteractorObserver::ComputeWorldToDisplay(this->Renderer, this->LastPickPosition[0],
      this->LastPickPosition[1], this->LastPickPosition[2], focalPoint);
    const double z = focalPoint[2];

    double pickPoint[4];
    vtkInteractorObserver::ComputeDisplayToWorld(this->Renderer, e[0], e[1], z, pickPoint);

    double center[3];
    this->SphereSource->GetCenter(center);
    double dir[3] = { pickPoint[0] - center[0], pickPoint[1] - center[1],
      pickPoint[2] - center[2] };
    double radius = vtkMath::Norm(dir);
    if (!std::isfinite(radius) || radius <= 0.0)
    {
      return;
    }

    const double minRadius = 1.0e-06 * (this->InitialLength > 0.0 ? this->InitialLength : 1.0);
    if (radius < minRadius)
    {
      radius = minRadius;
    }

    this->SphereSource->SetRadius(radius);
    // |dir| == radius here, so SetHandleDirection() places the handle exactly under the cursor
    // (its internal factor is radius / |dir| == 1) and updates HandleDirection.
    this->SetHandleDirection(dir);
    this->AdaptCenterCursorBounds();
    this->Modified();

    this->LastEventPosition[0] = e[0];
    this->LastEventPosition[1] = e[1];
    this->LastEventPosition[2] = 0.0;
  }

protected:
  vtkSHYXSeedSphereRepresentation() = default;
  ~vtkSHYXSeedSphereRepresentation() override = default;

private:
  vtkSHYXSeedSphereRepresentation(const vtkSHYXSeedSphereRepresentation&) = delete;
  void operator=(const vtkSHYXSeedSphereRepresentation&) = delete;
};

vtkStandardNewMacro(vtkSHYXSeedSphereRepresentation);

//-----------------------------------------------------------------------------
pqSHYXStreamlineSeedWidget::pqSHYXStreamlineSeedWidget(
  vtkSMProxy* proxy, vtkSMPropertyGroup* group, QWidget* parent)
  : Superclass(proxy, parent)
{
  this->buildUi();
  this->linkProperties(group);

  // Property links are connected before these handlers so a spin-box edit has already been
  // written to the SM property when onSeedPropertyEdited reads it back.
  this->connectSeedSignals();

  this->setChangeAvailableAsChangeFinished(true);

  // The property links already populated the combos from the current SM values; only fix up a
  // selection that the text round-trip could not resolve (e.g. an old state with an unknown text).
  auto selectEnumByValue = [](QComboBox* combo, int value) {
    if (!combo || combo->count() == 0)
    {
      return;
    }
    for (int i = 0; i < combo->count(); ++i)
    {
      if (combo->itemData(i).toInt() == value)
      {
        QSignalBlocker blocker(combo);
        combo->setCurrentIndex(i);
        return;
      }
    }
  };
  if (this->ShapeCombo)
  {
    const int shape = proxy && proxy->GetProperty("SeedShape")
      ? vtkSMPropertyHelper(proxy, "SeedShape").GetAsInt()
      : 0;
    selectEnumByValue(this->ShapeCombo, shape);
  }
  if (this->SphereSampling)
  {
    const int sampling = proxy && proxy->GetProperty("SphereSeedMode")
      ? vtkSMPropertyHelper(proxy, "SphereSeedMode").GetAsInt()
      : 0;
    selectEnumByValue(this->SphereSampling, sampling);
  }
  this->updateShapeGroups();
  this->setStatus(tr("Drag the seed handles. Snap flags are applied when you release the mouse."));
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
    &pqSHYXStreamlineSeedWidget::onActiveViewChanged);
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this,
    [this]() { this->syncWidgets(); });
}

//-----------------------------------------------------------------------------
pqSHYXStreamlineSeedWidget::~pqSHYXStreamlineSeedWidget()
{
  this->destroyWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::buildUi()
{
  auto* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(4);

  auto* tip = new QLabel(
    tr("Line: drag either endpoint; both endpoints can snap. "
       "Sphere: left-drag the ball to move it; left-drag the handle (small ball on its surface) to "
       "change the radius. "
       "Rectangle: drag the corners / normal to resize and rotate. "
       "Snap casts a ray through the cursor onto the Input surface and takes the vertex nearest "
       "that front-most hit (switch to Nearest vertex (3D) to ignore the camera). "
       "Seeds update live; streamlines refresh on Apply (or tick Apply on release)."),
    this);
  tip->setWordWrap(true);
  tip->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(tip);

  auto* topRow = new QHBoxLayout();
  topRow->setContentsMargins(0, 0, 0, 0);
  topRow->setSpacing(6);
  topRow->addWidget(new QLabel(tr("Seed shape:"), this));
  this->ShapeCombo = new QComboBox(this);
  this->ShapeCombo->setObjectName("SHYXStreamlineSeedShape");
  // Items come from the SeedShape EnumerationDomain in linkProperties(); the entry texts are what
  // the SM property round-trips, so they must not be hand-written here.
  topRow->addWidget(this->ShapeCombo, 1);
  this->PlaceFromBounds = new QPushButton(tr("Place at bounds"), this);
  this->PlaceFromBounds->setObjectName("SHYXStreamlineSeedPlaceAtBounds");
  this->PlaceFromBounds->setToolTip(
    tr("Put the current seed at the input bounds center with a size proportional to the bounds."));
  topRow->addWidget(this->PlaceFromBounds);
  this->ShowInteractive = new QCheckBox(tr("Show 3D widget"), this);
  this->ShowInteractive->setObjectName("SHYXStreamlineSeedShowWidget");
  this->ShowInteractive->setChecked(true);
  topRow->addWidget(this->ShowInteractive);
  this->AutoApply = new QCheckBox(tr("Apply on release"), this);
  this->AutoApply->setObjectName("SHYXStreamlineSeedAutoApply");
  this->AutoApply->setChecked(false);
  this->AutoApply->setToolTip(
    tr("Off by default. When off, releasing the mouse only updates the seed properties; click "
       "Apply to refresh the streamlines. Turn on to Apply automatically after each release."));
  topRow->addWidget(this->AutoApply);
  vbox->addLayout(topRow);

  auto* snapRow = new QHBoxLayout();
  snapRow->setContentsMargins(0, 0, 0, 0);
  snapRow->setSpacing(6);
  snapRow->addWidget(new QLabel(tr("Snap to:"), this));
  this->SnapModeCombo = new QComboBox(this);
  this->SnapModeCombo->setObjectName("SHYXStreamlineSeedSnapMode");
  this->SnapModeCombo->addItem(tr("Surface under cursor (screen ray)"));
  this->SnapModeCombo->addItem(tr("Nearest vertex (3D)"));
  this->SnapModeCombo->setToolTip(
    tr("Screen ray: cast a ray through the cursor into the Input surface and snap to the vertex "
       "nearest that front-most hit, so the result matches what you see. Nearest vertex (3D): "
       "ignore the camera and take the closest vertex in space, which can land on the back side "
       "of a vessel or on the far wall of a stent."));
  snapRow->addWidget(this->SnapModeCombo, 1);
  vbox->addLayout(snapRow);

  // ---- Line ----
  this->LineGroup = new QGroupBox(tr("Line seed"), this);
  {
    auto* grid = new QGridLayout(this->LineGroup);
    grid->addWidget(new QLabel(tr("X"), this->LineGroup), 0, 1);
    grid->addWidget(new QLabel(tr("Y"), this->LineGroup), 0, 2);
    grid->addWidget(new QLabel(tr("Z"), this->LineGroup), 0, 3);

    grid->addWidget(new QLabel(tr("Point 1"), this->LineGroup), 1, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->LineP1[i] = MakeDoubleSpin(this->LineGroup);
      grid->addWidget(this->LineP1[i], 1, 1 + i);
    }
    this->LineSnap1 = new QCheckBox(tr("Snap"), this->LineGroup);
    this->LineSnap1->setToolTip(tr("On release, move Point 1 to the nearest input-surface vertex."));
    grid->addWidget(this->LineSnap1, 1, 4);

    grid->addWidget(new QLabel(tr("Point 2"), this->LineGroup), 2, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->LineP2[i] = MakeDoubleSpin(this->LineGroup);
      grid->addWidget(this->LineP2[i], 2, 1 + i);
    }
    this->LineSnap2 = new QCheckBox(tr("Snap"), this->LineGroup);
    this->LineSnap2->setToolTip(tr("On release, move Point 2 to the nearest input-surface vertex."));
    grid->addWidget(this->LineSnap2, 2, 4);

    grid->addWidget(new QLabel(tr("Resolution"), this->LineGroup), 3, 0);
    this->LineResolution = MakeIntSpin(this->LineGroup, 1, 100000);
    grid->addWidget(this->LineResolution, 3, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
  }
  vbox->addWidget(this->LineGroup);

  // ---- Sphere ----
  this->SphereGroup = new QGroupBox(tr("Sphere seed"), this);
  {
    auto* grid = new QGridLayout(this->SphereGroup);
    grid->addWidget(new QLabel(tr("X"), this->SphereGroup), 0, 1);
    grid->addWidget(new QLabel(tr("Y"), this->SphereGroup), 0, 2);
    grid->addWidget(new QLabel(tr("Z"), this->SphereGroup), 0, 3);

    grid->addWidget(new QLabel(tr("Center"), this->SphereGroup), 1, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->SphereCenter[i] = MakeDoubleSpin(this->SphereGroup);
      grid->addWidget(this->SphereCenter[i], 1, 1 + i);
    }
    this->SphereSnap = new QCheckBox(tr("Snap center"), this->SphereGroup);
    this->SphereSnap->setToolTip(
      tr("On release, move the sphere center to the nearest input-surface vertex."));
    grid->addWidget(this->SphereSnap, 1, 4);

    auto* radiusRow = new QHBoxLayout();
    radiusRow->setContentsMargins(0, 0, 0, 0);
    radiusRow->setSpacing(4);
    radiusRow->addWidget(new QLabel(tr("Radius"), this->SphereGroup));
    this->SphereRadius = MakeDoubleSpin(this->SphereGroup);
    this->SphereRadius->setMinimum(0.0);
    radiusRow->addWidget(this->SphereRadius, 1);
    radiusRow->addWidget(new QLabel(tr("Interior points"), this->SphereGroup));
    this->SphereInteriorCount = MakeIntSpin(this->SphereGroup, 1, 100000000);
    this->SphereInteriorCount->setToolTip(
      tr("Exact number of seeds scattered uniformly inside the ball (Interior sampling)."));
    radiusRow->addWidget(this->SphereInteriorCount);
    grid->addLayout(radiusRow, 2, 0, 1, 5);

    auto* samplingRow = new QHBoxLayout();
    samplingRow->setContentsMargins(0, 0, 0, 0);
    samplingRow->setSpacing(4);
    samplingRow->addWidget(new QLabel(tr("Sampling"), this->SphereGroup));
    this->SphereSampling = new QComboBox(this->SphereGroup);
    // Items from the SphereSeedMode domain (see linkProperties()).
    this->SphereSampling->setToolTip(
      tr("Surface: seeds are the sphere mesh vertices, so the count is set by the theta/phi "
         "divisions below. Interior: seeds are Interior points distributed uniformly inside the "
         "ball."));
    samplingRow->addWidget(this->SphereSampling, 1);
    grid->addLayout(samplingRow, 3, 0, 1, 5);

    auto* divisionsRow = new QHBoxLayout();
    divisionsRow->setContentsMargins(0, 0, 0, 0);
    divisionsRow->setSpacing(4);
    divisionsRow->addWidget(new QLabel(tr("Theta (longitude)"), this->SphereGroup));
    this->SphereTheta = MakeIntSpin(this->SphereGroup, 3, 1000);
    this->SphereTheta->setToolTip(
      tr("Points around the sphere axis (vertical slices). Combined with Phi this sets the "
         "surface seed count: Theta x (Phi - 2) + 2."));
    divisionsRow->addWidget(this->SphereTheta);
    divisionsRow->addWidget(new QLabel(tr("Phi (latitude)"), this->SphereGroup));
    this->SpherePhi = MakeIntSpin(this->SphereGroup, 3, 1000);
    this->SpherePhi->setToolTip(
      tr("Points from pole to pole (horizontal rings). The two poles are single points."));
    divisionsRow->addWidget(this->SpherePhi);
    divisionsRow->addWidget(new QLabel(tr("Random seed"), this->SphereGroup));
    this->SphereRandomSeed = MakeIntSpin(this->SphereGroup, -1000000000, 1000000000);
    this->SphereRandomSeed->setToolTip(
      tr("Seed for the interior sampler; the same seed and count give the same positions."));
    divisionsRow->addWidget(this->SphereRandomSeed);
    grid->addLayout(divisionsRow, 4, 0, 1, 5);

    this->SphereCountLabel = new QLabel(this->SphereGroup);
    this->SphereCountLabel->setWordWrap(true);
    this->SphereCountLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    grid->addWidget(this->SphereCountLabel, 5, 0, 1, 5);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
  }
  vbox->addWidget(this->SphereGroup);

  // ---- Rectangle ----
  this->RectangleGroup = new QGroupBox(tr("Rectangle (plane) seed"), this);
  {
    auto* grid = new QGridLayout(this->RectangleGroup);
    grid->addWidget(new QLabel(tr("X"), this->RectangleGroup), 0, 1);
    grid->addWidget(new QLabel(tr("Y"), this->RectangleGroup), 0, 2);
    grid->addWidget(new QLabel(tr("Z"), this->RectangleGroup), 0, 3);

    grid->addWidget(new QLabel(tr("Origin"), this->RectangleGroup), 1, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->RectangleOrigin[i] = MakeDoubleSpin(this->RectangleGroup);
      grid->addWidget(this->RectangleOrigin[i], 1, 1 + i);
    }
    this->RectangleSnap = new QCheckBox(tr("Snap origin"), this->RectangleGroup);
    this->RectangleSnap->setToolTip(
      tr("On release, move the origin to the nearest input-surface vertex; Point 1 / Point 2 "
         "translate with it."));
    grid->addWidget(this->RectangleSnap, 1, 4);

    grid->addWidget(new QLabel(tr("Point 1"), this->RectangleGroup), 2, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->RectangleP1[i] = MakeDoubleSpin(this->RectangleGroup);
      grid->addWidget(this->RectangleP1[i], 2, 1 + i);
    }

    grid->addWidget(new QLabel(tr("Point 2"), this->RectangleGroup), 3, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->RectangleP2[i] = MakeDoubleSpin(this->RectangleGroup);
      grid->addWidget(this->RectangleP2[i], 3, 1 + i);
    }

    auto* resRow = new QHBoxLayout();
    resRow->setContentsMargins(0, 0, 0, 0);
    resRow->setSpacing(4);
    resRow->addWidget(new QLabel(tr("Resolution X"), this->RectangleGroup));
    this->RectangleResX = MakeIntSpin(this->RectangleGroup, 1, 1000);
    resRow->addWidget(this->RectangleResX);
    resRow->addWidget(new QLabel(tr("Y"), this->RectangleGroup));
    this->RectangleResY = MakeIntSpin(this->RectangleGroup, 1, 1000);
    resRow->addWidget(this->RectangleResY);
    resRow->addStretch(1);
    grid->addLayout(resRow, 4, 0, 1, 5);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
  }
  vbox->addWidget(this->RectangleGroup);

  this->Status = new QLabel(this);
  this->Status->setWordWrap(true);
  this->Status->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
  vbox->addWidget(this->Status);

  QObject::connect(this->ShapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    &pqSHYXStreamlineSeedWidget::onShapeChanged);
  QObject::connect(this->SphereSampling, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    &pqSHYXStreamlineSeedWidget::onSphereSamplingChanged);
  QObject::connect(this->PlaceFromBounds, &QPushButton::clicked, this,
    &pqSHYXStreamlineSeedWidget::onPlaceFromBounds);
  QObject::connect(this->ShowInteractive, &QCheckBox::toggled, this,
    &pqSHYXStreamlineSeedWidget::onShowInteractiveToggled);
  QObject::connect(this->SnapModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int index) {
      this->SnapMode = index == pqSHYXStreamlineSeedWidget::SnapNearestVertex
        ? pqSHYXStreamlineSeedWidget::SnapNearestVertex
        : pqSHYXStreamlineSeedWidget::SnapScreenRay;
      this->setStatus(this->SnapMode == pqSHYXStreamlineSeedWidget::SnapScreenRay
          ? tr("Snapping to the surface under the cursor (front-most hit).")
          : tr("Snapping to the nearest vertex in 3D space."));
    });
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::linkProperties(vtkSMPropertyGroup* group)
{
  auto findProperty = [this, group](const char* name) -> vtkSMProperty* {
    if (group)
    {
      if (auto* p = group->GetProperty(name))
      {
        return p;
      }
    }
    return this->proxy() ? this->proxy()->GetProperty(name) : nullptr;
  };

  auto linkVector = [this, &findProperty](const char* name, QDoubleSpinBox* spins[3]) {
    if (auto* p = findProperty(name))
    {
      for (int i = 0; i < 3; ++i)
      {
        if (spins[i])
        {
          this->addPropertyLink(spins[i], "value", SIGNAL(valueChanged(double)), p, i);
        }
      }
    }
  };
  auto linkDouble = [this, &findProperty](const char* name, QDoubleSpinBox* spin) {
    if (auto* p = findProperty(name))
    {
      if (spin)
      {
        this->addPropertyLink(spin, "value", SIGNAL(valueChanged(double)), p);
      }
    }
  };
  auto linkInt = [this, &findProperty](const char* name, QSpinBox* spin) {
    if (auto* p = findProperty(name))
    {
      if (spin)
      {
        this->addPropertyLink(spin, "value", SIGNAL(valueChanged(int)), p);
      }
    }
  };
  auto linkBool = [this, &findProperty](const char* name, QCheckBox* check) {
    if (auto* p = findProperty(name))
    {
      if (check)
      {
        this->addPropertyLink(check, "checked", SIGNAL(toggled(bool)), p);
      }
    }
  };

  if (auto* p = findProperty("SeedShape"))
  {
    if (PopulateEnumCombo(this->ShapeCombo, p))
    {
      // Enumeration properties only accept the entry text (see PopulateEnumCombo).
      this->addPropertyLink(
        this->ShapeCombo, "currentText", SIGNAL(currentTextChanged(QString)), p);
    }
  }

  linkVector("LinePoint1", this->LineP1);
  linkVector("LinePoint2", this->LineP2);
  linkInt("LineResolution", this->LineResolution);
  linkBool("LineSnapPoint1", this->LineSnap1);
  linkBool("LineSnapPoint2", this->LineSnap2);

  linkVector("SphereCenter", this->SphereCenter);
  linkDouble("SphereRadius", this->SphereRadius);
  if (auto* p = findProperty("SphereSeedMode"))
  {
    if (PopulateEnumCombo(this->SphereSampling, p))
    {
      this->addPropertyLink(
        this->SphereSampling, "currentText", SIGNAL(currentTextChanged(QString)), p);
    }
  }
  linkInt("SphereInteriorPointCount", this->SphereInteriorCount);
  linkInt("SphereRandomSeed", this->SphereRandomSeed);
  linkInt("SphereThetaResolution", this->SphereTheta);
  linkInt("SpherePhiResolution", this->SpherePhi);
  linkBool("SphereSnapCenter", this->SphereSnap);

  linkVector("RectangleOrigin", this->RectangleOrigin);
  linkVector("RectanglePoint1", this->RectangleP1);
  linkVector("RectanglePoint2", this->RectangleP2);
  linkInt("RectangleResolutionX", this->RectangleResX);
  linkInt("RectangleResolutionY", this->RectangleResY);
  linkBool("RectangleSnapOrigin", this->RectangleSnap);
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::connectSeedSignals()
{
  for (QDoubleSpinBox* spin :
    { this->LineP1[0], this->LineP1[1], this->LineP1[2], this->LineP2[0], this->LineP2[1],
      this->LineP2[2], this->SphereCenter[0], this->SphereCenter[1], this->SphereCenter[2],
      this->SphereRadius, this->RectangleOrigin[0], this->RectangleOrigin[1],
      this->RectangleOrigin[2], this->RectangleP1[0], this->RectangleP1[1],
      this->RectangleP1[2], this->RectangleP2[0], this->RectangleP2[1],
      this->RectangleP2[2] })
  {
    if (spin)
    {
      QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        &pqSHYXStreamlineSeedWidget::onSeedPropertyEdited);
    }
  }
  for (QSpinBox* spin : { this->LineResolution, this->SphereTheta, this->SpherePhi,
         this->SphereInteriorCount, this->SphereRandomSeed, this->RectangleResX,
         this->RectangleResY })
  {
    if (spin)
    {
      QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
        &pqSHYXStreamlineSeedWidget::onSeedPropertyEdited);
    }
  }
  for (QCheckBox* check : { this->LineSnap1, this->LineSnap2, this->SphereSnap,
         this->RectangleSnap })
  {
    if (check)
    {
      QObject::connect(
        check, &QCheckBox::toggled, this, &pqSHYXStreamlineSeedWidget::onSeedPropertyEdited);
    }
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::select()
{
  this->WidgetsVisible = true;
  this->Superclass::select();
  this->updateShapeGroups();
  this->syncWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::deselect()
{
  this->WidgetsVisible = false;
  this->destroyWidgets();
  this->Superclass::deselect();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::setView(pqView* view)
{
  this->Superclass::setView(view);
  this->syncWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::reset()
{
  this->Superclass::reset();
  // Combos are linked through currentText, so an external (Python) edit updates them silently.
  this->updateShapeGroups();
  this->syncWidgetFromProperties();
}

//-----------------------------------------------------------------------------
int pqSHYXStreamlineSeedWidget::currentShape() const
{
  if (this->ShapeCombo && this->ShapeCombo->count() > 0)
  {
    return ComboEnumValue(this->ShapeCombo);
  }
  if (this->proxy() && this->proxy()->GetProperty("SeedShape"))
  {
    return vtkSMPropertyHelper(this->proxy(), "SeedShape").GetAsInt();
  }
  return 0;
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::sphereSamplesInterior() const
{
  if (this->SphereSampling && this->SphereSampling->count() > 0)
  {
    return ComboEnumValue(this->SphereSampling) == 1;
  }
  if (this->proxy() && this->proxy()->GetProperty("SphereSeedMode"))
  {
    return vtkSMPropertyHelper(this->proxy(), "SphereSeedMode").GetAsInt() == 1;
  }
  return false;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::updateShapeGroups()
{
  const int shape = this->currentShape();
  if (this->LineGroup)
  {
    this->LineGroup->setVisible(shape == 0);
  }
  if (this->SphereGroup)
  {
    this->SphereGroup->setVisible(shape == 1);
  }
  if (this->RectangleGroup)
  {
    this->RectangleGroup->setVisible(shape == 2);
  }
  this->updateSphereSamplingUi();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::updateSphereSamplingUi()
{
  // The chart label text is not the property value: read the combo's enum value instead.
  const bool interior = this->sphereSamplesInterior();
  if (this->SphereInteriorCount)
  {
    this->SphereInteriorCount->setVisible(interior);
    this->SphereInteriorCount->setEnabled(interior);
  }
  if (this->SphereRandomSeed)
  {
    this->SphereRandomSeed->setVisible(interior);
    this->SphereRandomSeed->setEnabled(interior);
  }
  if (this->SphereTheta)
  {
    this->SphereTheta->setEnabled(!interior);
  }
  if (this->SpherePhi)
  {
    this->SpherePhi->setEnabled(!interior);
  }
  if (!this->SphereCountLabel)
  {
    return;
  }
  if (interior)
  {
    const qint64 count = this->SphereInteriorCount ? this->SphereInteriorCount->value() : 0;
    this->SphereCountLabel->setText(
      tr("Interior sampling: %1 seed points uniformly inside the ball.").arg(count));
  }
  else
  {
    const int theta = this->SphereTheta ? this->SphereTheta->value() : 0;
    const int phi = this->SpherePhi ? this->SpherePhi->value() : 0;
    const qint64 count = static_cast<qint64>(theta) * std::max(0, phi - 2) + 2;
    this->SphereCountLabel->setText(
      tr("Surface sampling: Theta x (Phi - 2) + 2 = %1 x %2 + 2 = %3 seed points.")
        .arg(theta)
        .arg(std::max(0, phi - 2))
        .arg(count));
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onSphereSamplingChanged()
{
  this->updateSphereSamplingUi();
  // The 3D widget geometry does not depend on the sampling mode, but the seed count does, so the
  // streamlines (not the widget) need an Apply.
  this->setStatus(tr("Sphere sampling changed. Click Apply to resample the seeds."));
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onShapeChanged()
{
  this->updateShapeGroups();
  this->syncWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onShowInteractiveToggled()
{
  this->syncWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onSeedPropertyEdited()
{
  if (this->UpdatingFromWidget || this->Interacting)
  {
    return;
  }
  this->updateSphereSamplingUi();
  this->syncWidgetFromProperties();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onActiveViewChanged()
{
  this->syncWidgets();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onPlaceFromBounds()
{
  vtkDataSet* input = this->inputDataSet();
  if (!input || input->GetNumberOfPoints() == 0)
  {
    this->setStatus(tr("No client-side input dataset; edit the seed values manually."));
    return;
  }

  double b[6];
  input->GetBounds(b);
  const double center[3] = {
    0.5 * (b[0] + b[1]), 0.5 * (b[2] + b[3]), 0.5 * (b[4] + b[5])
  };
  const double length = std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 });
  const int shape = this->currentShape();

  // Suppress the per-coordinate sync while the controls are rewritten; one sync + render runs at
  // the end so the 3D widget jumps to the new placement immediately (Apply is not required for the
  // widget itself, only for the streamlines).
  this->UpdatingFromWidget = true;
  if (shape == 0)
  {
    const double p1[3] = { center[0] - 0.25 * length, center[1], center[2] };
    const double p2[3] = { center[0] + 0.25 * length, center[1], center[2] };
    for (int i = 0; i < 3; ++i)
    {
      if (this->LineP1[i])
      {
        this->LineP1[i]->setValue(p1[i]);
      }
      if (this->LineP2[i])
      {
        this->LineP2[i]->setValue(p2[i]);
      }
    }
  }
  else if (shape == 1)
  {
    for (int i = 0; i < 3; ++i)
    {
      if (this->SphereCenter[i])
      {
        this->SphereCenter[i]->setValue(center[i]);
      }
    }
    if (this->SphereRadius)
    {
      this->SphereRadius->setValue(0.15 * length);
    }
  }
  else
  {
    const double origin[3] = {
      center[0] - 0.25 * length, center[1] - 0.25 * length, center[2]
    };
    const double p1[3] = { origin[0] + 0.5 * length, origin[1], origin[2] };
    const double p2[3] = { origin[0], origin[1] + 0.5 * length, origin[2] };
    for (int i = 0; i < 3; ++i)
    {
      if (this->RectangleOrigin[i])
      {
        this->RectangleOrigin[i]->setValue(origin[i]);
      }
      if (this->RectangleP1[i])
      {
        this->RectangleP1[i]->setValue(p1[i]);
      }
      if (this->RectangleP2[i])
      {
        this->RectangleP2[i]->setValue(p2[i]);
      }
    }
  }
  this->UpdatingFromWidget = false;

  this->syncWidgets();
  Q_EMIT this->changeAvailable();
  Q_EMIT this->changeFinished();
  const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
  this->setStatus(autoApply ? tr("Seed placed at the input bounds center. Applying streamlines.")
                            : tr("Seed placed at the input bounds center. Click Apply to refresh."));
  this->applyOnRelease();
}

//-----------------------------------------------------------------------------
vtkRenderWindowInteractor* pqSHYXStreamlineSeedWidget::interactor() const
{
  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  auto* renderView = qobject_cast<pqRenderView*>(view);
  if (!renderView || !renderView->getRenderViewProxy())
  {
    return nullptr;
  }
  return renderView->getRenderViewProxy()->GetInteractor();
}

//-----------------------------------------------------------------------------
vtkDataSet* pqSHYXStreamlineSeedWidget::inputDataSet() const
{
  vtkSMProxy* proxy = this->proxy();
  if (!proxy)
  {
    return nullptr;
  }
  auto* input = vtkSMInputProperty::SafeDownCast(proxy->GetProperty("Input"));
  if (!input || input->GetNumberOfProxies() == 0)
  {
    return nullptr;
  }
  auto* source = vtkSMSourceProxy::SafeDownCast(input->GetProxy(0));
  if (!source)
  {
    return nullptr;
  }
  auto* algorithm = vtkAlgorithm::SafeDownCast(source->GetClientSideObject());
  if (!algorithm)
  {
    return nullptr;
  }
  const unsigned int port = input->GetOutputPortForConnection(0);
  return vtkDataSet::SafeDownCast(algorithm->GetOutputDataObject(static_cast<int>(port)));
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::syncWidgets()
{
  if (!this->WidgetsVisible ||
    (this->ShowInteractive && !this->ShowInteractive->isChecked()))
  {
    this->destroyWidgets();
    return;
  }

  vtkRenderWindowInteractor* iren = this->interactor();
  if (!iren)
  {
    this->destroyWidgets();
    return;
  }

  if (this->Interactor != iren)
  {
    this->destroyWidgets();
  }

  const int shape = this->currentShape();
  if (shape == 0)
  {
    if (!this->LineWidget)
    {
      this->createLineWidget(iren);
    }
  }
  else if (shape == 1)
  {
    if (!this->SphereWidget)
    {
      this->createSphereWidget(iren);
    }
  }
  else
  {
    if (!this->PlaneWidget)
    {
      this->createRectangleWidget(iren);
    }
  }

  this->setWidgetsEnabled(true);
  this->updateSphereSamplingUi();
  this->syncWidgetFromProperties();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::destroyWidgets()
{
  if (this->WidgetObserver)
  {
    if (this->LineWidget)
    {
      this->LineWidget->RemoveObserver(this->WidgetObserver);
    }
    if (this->SphereWidget)
    {
      this->SphereWidget->RemoveObserver(this->WidgetObserver);
    }
    if (this->PlaneWidget)
    {
      this->PlaneWidget->RemoveObserver(this->WidgetObserver);
    }
  }
  if (this->LineWidget)
  {
    this->LineWidget->Off();
    this->LineWidget->SetInteractor(nullptr);
  }
  if (this->SphereWidget)
  {
    this->SphereWidget->Off();
    this->SphereWidget->SetInteractor(nullptr);
  }
  if (this->PlaneWidget)
  {
    this->PlaneWidget->Off();
    this->PlaneWidget->SetInteractor(nullptr);
  }
  this->LineWidget = nullptr;
  this->SphereWidget = nullptr;
  this->PlaneWidget = nullptr;
  this->Interactor = nullptr;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::setWidgetsEnabled(bool on)
{
  if (this->LineWidget)
  {
    on ? this->LineWidget->On() : this->LineWidget->Off();
  }
  if (this->SphereWidget)
  {
    on ? this->SphereWidget->On() : this->SphereWidget->Off();
  }
  if (this->PlaneWidget)
  {
    on ? this->PlaneWidget->On() : this->PlaneWidget->Off();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::createLineWidget(vtkRenderWindowInteractor* iren)
{
  this->destroyWidgets();
  this->Interactor = iren;
  if (!this->WidgetObserver)
  {
    this->WidgetObserver = vtkSmartPointer<vtkCallbackCommand>::New();
    this->WidgetObserver->SetClientData(this);
    this->WidgetObserver->SetCallback(&pqSHYXStreamlineSeedWidget::ProcessWidgetEvents);
  }

  auto widget = vtkSmartPointer<vtkLineWidget2>::New();
  auto rep = vtkSmartPointer<vtkLineRepresentation>::New();
  StyleLineRepresentation(rep);
  widget->SetRepresentation(rep);
  widget->SetInteractor(iren);
  widget->SetPriority(1.0);
  widget->AddObserver(vtkCommand::StartInteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::InteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::EndInteractionEvent, this->WidgetObserver, 1.0);
  this->LineWidget = widget;
  widget->On();
  this->placeWidgetInInputBounds();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::createSphereWidget(vtkRenderWindowInteractor* iren)
{
  this->destroyWidgets();
  this->Interactor = iren;
  if (!this->WidgetObserver)
  {
    this->WidgetObserver = vtkSmartPointer<vtkCallbackCommand>::New();
    this->WidgetObserver->SetClientData(this);
    this->WidgetObserver->SetCallback(&pqSHYXStreamlineSeedWidget::ProcessWidgetEvents);
  }

  auto widget = vtkSmartPointer<vtkSphereWidget2>::New();
  auto rep = vtkSmartPointer<vtkSHYXSeedSphereRepresentation>::New();
  StyleSphereRepresentation(rep);
  widget->SetRepresentation(rep);
  widget->SetInteractor(iren);
  widget->SetPriority(1.0);
  widget->TranslationEnabledOn();
  widget->ScalingEnabledOn();
  widget->AddObserver(vtkCommand::StartInteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::InteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::EndInteractionEvent, this->WidgetObserver, 1.0);
  this->SphereWidget = widget;
  widget->On();
  this->placeWidgetInInputBounds();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::createRectangleWidget(vtkRenderWindowInteractor* iren)
{
  this->destroyWidgets();
  this->Interactor = iren;
  if (!this->WidgetObserver)
  {
    this->WidgetObserver = vtkSmartPointer<vtkCallbackCommand>::New();
    this->WidgetObserver->SetClientData(this);
    this->WidgetObserver->SetCallback(&pqSHYXStreamlineSeedWidget::ProcessWidgetEvents);
  }

  auto widget = vtkSmartPointer<vtkPlaneWidget>::New();
  StylePlaneWidget(widget);
  widget->SetInteractor(iren);
  widget->SetPriority(1.0);
  widget->AddObserver(vtkCommand::StartInteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::InteractionEvent, this->WidgetObserver, 1.0);
  widget->AddObserver(vtkCommand::EndInteractionEvent, this->WidgetObserver, 1.0);
  this->PlaneWidget = widget;
  widget->On();
  this->placeWidgetInInputBounds();
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::syncWidgetFromProperties()
{
  if (this->Interacting || this->UpdatingFromWidget)
  {
    return false;
  }

  // The Qt controls hold the current draft: pqPropertyLinks pushes applied SM values into them,
  // and a widget drag / Place at bounds writes them directly. Reading the controls (not the SM
  // properties) keeps the 3D widget live before Apply, which by default is when the pipeline runs.
  vtkSMProxy* proxy = this->proxy();
  auto smVec = [proxy](const char* name, double out[3]) {
    out[0] = out[1] = out[2] = 0.0;
    if (proxy)
    {
      if (auto* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Get(out, 3);
      }
    }
  };
  auto readVec = [&smVec](QDoubleSpinBox* spins[3], const char* name, double out[3]) {
    if (!spins[0] || !spins[1] || !spins[2])
    {
      smVec(name, out);
      return;
    }
    out[0] = spins[0]->value();
    out[1] = spins[1]->value();
    out[2] = spins[2]->value();
  };
  auto readDouble = [proxy](QDoubleSpinBox* spin, const char* name) -> double {
    if (spin)
    {
      return spin->value();
    }
    return proxy && proxy->GetProperty(name)
      ? vtkSMPropertyHelper(proxy, name).GetAsDouble()
      : 0.0;
  };
  auto readInt = [proxy](QSpinBox* spin, const char* name) -> int {
    if (spin)
    {
      return spin->value();
    }
    return proxy && proxy->GetProperty(name) ? vtkSMPropertyHelper(proxy, name).GetAsInt() : 1;
  };

  bool changed = false;
  const int shape = this->currentShape();
  if (shape == 0 && this->LineWidget)
  {
    auto* rep = this->LineWidget->GetLineRepresentation();
    if (rep)
    {
      double p1[3];
      double p2[3];
      double cur1[3];
      double cur2[3];
      readVec(this->LineP1, "LinePoint1", p1);
      readVec(this->LineP2, "LinePoint2", p2);
      rep->GetPoint1WorldPosition(cur1);
      rep->GetPoint2WorldPosition(cur2);
      const int resolution = std::max(1, readInt(this->LineResolution, "LineResolution"));
      if (!NearlySamePoint(cur1, p1) || !NearlySamePoint(cur2, p2) ||
        rep->GetResolution() != resolution)
      {
        rep->SetPoint1WorldPosition(p1);
        rep->SetPoint2WorldPosition(p2);
        rep->SetResolution(resolution);
        rep->BuildRepresentation();
        changed = true;
      }
    }
  }
  else if (shape == 1 && this->SphereWidget)
  {
    auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation());
    if (rep)
    {
      double c[3];
      double currentCenter[3];
      readVec(this->SphereCenter, "SphereCenter", c);
      rep->GetCenter(currentCenter);
      const double radius = std::max(0.0, readDouble(this->SphereRadius, "SphereRadius"));
      const bool radiusDiffers = radius > 0.0 && !NearlySameValue(rep->GetRadius(), radius);
      if (!NearlySamePoint(currentCenter, c) || radiusDiffers)
      {
        // SetCenter() overwrites the radius with the handle distance when handles are visible,
        // so the radius must be written back last (see SetSphereCenterKeepingRadius).
        SetSphereCenterKeepingRadius(rep, c, radius);
        rep->BuildRepresentation();
        changed = true;
      }
    }
  }
  else if (shape == 2 && this->PlaneWidget)
  {
    double origin[3];
    double p1[3];
    double p2[3];
    double curOrigin[3];
    double curP1[3];
    double curP2[3];
    readVec(this->RectangleOrigin, "RectangleOrigin", origin);
    readVec(this->RectangleP1, "RectanglePoint1", p1);
    readVec(this->RectangleP2, "RectanglePoint2", p2);
    this->PlaneWidget->GetOrigin(curOrigin);
    this->PlaneWidget->GetPoint1(curP1);
    this->PlaneWidget->GetPoint2(curP2);
    if (!NearlySamePoint(curOrigin, origin) || !NearlySamePoint(curP1, p1) ||
      !NearlySamePoint(curP2, p2))
    {
      this->PlaneWidget->SetOrigin(origin);
      this->PlaneWidget->SetPoint1(p1);
      this->PlaneWidget->SetPoint2(p2);
      this->PlaneWidget->UpdatePlacement();
      changed = true;
    }
  }

  if (changed)
  {
    // Widget representations only mark themselves Modified; nothing re-renders the view until
    // the pipeline runs. Without this, edits (including Place at bounds) appear stuck until Apply.
    this->renderSeedView();
  }
  return changed;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::placeWidgetInInputBounds()
{
  vtkDataSet* input = this->inputDataSet();
  if (!input || input->GetNumberOfPoints() == 0)
  {
    return;
  }

  double bounds[6];
  input->GetBounds(bounds);
  const double span[3] = { bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[5] - bounds[4] };
  const double pad = std::max(1e-3, 0.05 * std::max({ span[0], span[1], span[2], 1e-6 }));
  for (int i = 0; i < 3; ++i)
  {
    bounds[2 * i] -= pad;
    bounds[2 * i + 1] += pad;
  }

  // PlaceWidget seeds InitialBounds / InitialLength (handle sizing, sphere min radius) and also
  // moves the representation, so syncWidgetFromProperties() must run right after this.
  const int shape = this->currentShape();
  if (shape == 0 && this->LineWidget)
  {
    this->LineWidget->GetLineRepresentation()->PlaceWidget(bounds);
  }
  else if (shape == 1 && this->SphereWidget)
  {
    if (auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation()))
    {
      rep->PlaceWidget(bounds);
    }
  }
  else if (shape == 2 && this->PlaneWidget)
  {
    this->PlaneWidget->PlaceWidget(bounds);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::renderSeedView()
{
  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  if (view)
  {
    view->render();
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::ensureSnapper()
{
  vtkDataSet* input = this->inputDataSet();
  if (!input || input->GetNumberOfPoints() == 0)
  {
    return false;
  }
  const vtkMTimeType mtime = input->GetMTime();
  if (input == this->SnapInput && mtime == this->SnapMTime && this->SnapLocator)
  {
    return true;
  }

  this->SnapSurface = nullptr;
  this->SnapLocator = nullptr;
  this->SnapCellLocator = nullptr;
  if (auto* pd = vtkPolyData::SafeDownCast(input))
  {
    if (pd->GetNumberOfPoints() > 0)
    {
      this->SnapSurface = pd;
    }
  }
  else
  {
    if (!this->SurfaceFilter)
    {
      this->SurfaceFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    }
    this->SurfaceFilter->SetInputData(input);
    this->SurfaceFilter->Update();
    this->SnapSurface = this->SurfaceFilter->GetOutput();
  }

  if (!this->SnapSurface || this->SnapSurface->GetNumberOfPoints() == 0)
  {
    this->SnapInput = nullptr;
    this->SnapMTime = 0;
    return false;
  }

  this->SnapLocator = vtkSmartPointer<vtkStaticPointLocator>::New();
  this->SnapLocator->SetDataSet(this->SnapSurface);
  this->SnapLocator->BuildLocator();

  // Screen-ray mode needs cells; a point cloud (no cells) simply falls back to nearest vertex.
  if (this->SnapSurface->GetNumberOfCells() > 0)
  {
    this->SnapSurface->BuildCells();
    this->SnapCellLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
    this->SnapCellLocator->SetDataSet(this->SnapSurface);
    this->SnapCellLocator->BuildLocator();
  }

  this->SnapInput = input;
  this->SnapMTime = mtime;
  return true;
}

//-----------------------------------------------------------------------------
vtkRenderer* pqSHYXStreamlineSeedWidget::renderer() const
{
  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  auto* renderView = qobject_cast<pqRenderView*>(view);
  if (!renderView || !renderView->getRenderViewProxy())
  {
    return nullptr;
  }
  return renderView->getRenderViewProxy()->GetRenderer();
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::rayPickSurfacePoint(const double world[3], double hit[3])
{
  vtkRenderer* ren = this->renderer();
  if (!ren || !this->SnapCellLocator)
  {
    return false;
  }

  ren->SetWorldPoint(world[0], world[1], world[2], 1.0);
  ren->WorldToDisplay();
  double display[3];
  ren->GetDisplayPoint(display);
  if (!std::isfinite(display[0]) || !std::isfinite(display[1]))
  {
    return false;
  }

  double nearPoint[4];
  double farPoint[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, display[0], display[1], 0.0, nearPoint);
  vtkInteractorObserver::ComputeDisplayToWorld(ren, display[0], display[1], 1.0, farPoint);

  double t = 0.0;
  double x[3] = { 0.0, 0.0, 0.0 };
  double pcoords[3] = { 0.0, 0.0, 0.0 };
  int subId = 0;
  vtkIdType cellId = -1;
  vtkNew<vtkGenericCell> cell;
  if (!this->SnapCellLocator->IntersectWithLine(
        nearPoint, farPoint, 1e-6, t, x, pcoords, subId, cellId, cell))
  {
    return false;
  }
  hit[0] = x[0];
  hit[1] = x[1];
  hit[2] = x[2];
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::snapPoint(int mode, const double in[3], double out[3])
{
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
  if (!this->ensureSnapper())
  {
    return false;
  }

  double target[3] = { in[0], in[1], in[2] };
  if (mode == pqSHYXStreamlineSeedWidget::SnapScreenRay)
  {
    double hit[3];
    if (this->rayPickSurfacePoint(in, hit))
    {
      target[0] = hit[0];
      target[1] = hit[1];
      target[2] = hit[2];
    }
    // Ray missed (handle dragged past the silhouette): fall through to the 3D nearest vertex,
    // which still gives a plausible target instead of silently doing nothing.
  }

  const vtkIdType id = this->SnapLocator->FindClosestPoint(target);
  if (id < 0)
  {
    return false;
  }
  double q[3];
  this->SnapSurface->GetPoint(id, q);
  out[0] = q[0];
  out[1] = q[1];
  out[2] = q[2];
  return true;
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::snapPoints(double* points, int count, const int* enabled)
{
  if (!points || count <= 0)
  {
    return false;
  }

  bool changed = false;
  for (int i = 0; i < count; ++i)
  {
    if (enabled && !enabled[i])
    {
      continue;
    }
    double* p = points + 3 * i;
    double snapped[3];
    if (!this->snapPoint(this->SnapMode, p, snapped))
    {
      continue;
    }
    if (!SamePoint(p, snapped))
    {
      p[0] = snapped[0];
      p[1] = snapped[1];
      p[2] = snapped[2];
      changed = true;
    }
  }
  return changed;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::ProcessWidgetEvents(
  vtkObject*, unsigned long eid, void* clientdata, void*)
{
  auto* self = reinterpret_cast<pqSHYXStreamlineSeedWidget*>(clientdata);
  if (self)
  {
    self->onWidgetEvent(eid);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onWidgetEvent(unsigned long eid)
{
  if (!this->WidgetsVisible)
  {
    return;
  }

  if (eid == vtkCommand::StartInteractionEvent)
  {
    this->Interacting = true;
    const int shape = this->currentShape();
    if (shape == 0 && this->LineWidget)
    {
      if (auto* rep = this->LineWidget->GetLineRepresentation())
      {
        rep->GetPoint1WorldPosition(this->StartLineP1);
        rep->GetPoint2WorldPosition(this->StartLineP2);
      }
    }
    else if (shape == 1 && this->SphereWidget)
    {
      if (auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation()))
      {
        rep->GetCenter(this->StartSphereCenter);
      }
    }
    else if (shape == 2 && this->PlaneWidget)
    {
      this->PlaneWidget->GetOrigin(this->StartRectangleOrigin);
    }
  }
  else if (eid == vtkCommand::InteractionEvent)
  {
    this->syncControlsFromWidget(false);
  }
  else if (eid == vtkCommand::EndInteractionEvent)
  {
    this->Interacting = false;
    this->syncControlsFromWidget(true);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::syncControlsFromWidget(bool release)
{
  const int shape = this->currentShape();
  bool snapped = false;

  if (shape == 0 && this->LineWidget)
  {
    auto* rep = this->LineWidget->GetLineRepresentation();
    if (!rep)
    {
      return;
    }
    double p1[3];
    double p2[3];
    rep->GetPoint1WorldPosition(p1);
    rep->GetPoint2WorldPosition(p2);

    if (release)
    {
      double points[6] = { p1[0], p1[1], p1[2], p2[0], p2[1], p2[2] };
      const int enabled[2] = {
        (this->LineSnap1 && this->LineSnap1->isChecked() && !SamePoint(p1, this->StartLineP1)) ? 1
                                                                                              : 0,
        (this->LineSnap2 && this->LineSnap2->isChecked() && !SamePoint(p2, this->StartLineP2)) ? 1
                                                                                              : 0
      };
      if (enabled[0] || enabled[1])
      {
        snapped = this->snapPoints(points, 2, enabled);
        p1[0] = points[0];
        p1[1] = points[1];
        p1[2] = points[2];
        p2[0] = points[3];
        p2[1] = points[4];
        p2[2] = points[5];
        rep->SetPoint1WorldPosition(p1);
        rep->SetPoint2WorldPosition(p2);
      }
    }

    this->UpdatingFromWidget = true;
    SetVec3(this->LineP1, p1);
    SetVec3(this->LineP2, p2);
    this->UpdatingFromWidget = false;
  }
  else if (shape == 1 && this->SphereWidget)
  {
    auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation());
    if (!rep)
    {
      return;
    }
    double center[3];
    rep->GetCenter(center);
    double radius = rep->GetRadius();

    if (release && this->SphereSnap && this->SphereSnap->isChecked() &&
      !SamePoint(center, this->StartSphereCenter))
    {
      double points[3] = { center[0], center[1], center[2] };
      const int enabled = 1;
      if (this->snapPoints(points, 1, &enabled))
      {
        center[0] = points[0];
        center[1] = points[1];
        center[2] = points[2];
        // Snapping moves the center but not the handle, so SetCenter() alone would reset the
        // radius to |handle - new center|; keep the radius the user just dragged.
        SetSphereCenterKeepingRadius(rep, center, radius);
        radius = rep->GetRadius();
        snapped = true;
      }
    }

    this->UpdatingFromWidget = true;
    SetVec3(this->SphereCenter, center);
    if (this->SphereRadius)
    {
      QSignalBlocker blocker(this->SphereRadius);
      this->SphereRadius->setValue(radius);
    }
    this->UpdatingFromWidget = false;
  }
  else if (shape == 2 && this->PlaneWidget)
  {
    double origin[3];
    double p1[3];
    double p2[3];
    this->PlaneWidget->GetOrigin(origin);
    this->PlaneWidget->GetPoint1(p1);
    this->PlaneWidget->GetPoint2(p2);

    if (release && this->RectangleSnap && this->RectangleSnap->isChecked() &&
      !SamePoint(origin, this->StartRectangleOrigin))
    {
      double points[3] = { origin[0], origin[1], origin[2] };
      const int enabled = 1;
      if (this->snapPoints(points, 1, &enabled))
      {
        const double delta[3] = {
          points[0] - origin[0], points[1] - origin[1], points[2] - origin[2]
        };
        origin[0] = points[0];
        origin[1] = points[1];
        origin[2] = points[2];
        p1[0] += delta[0];
        p1[1] += delta[1];
        p1[2] += delta[2];
        p2[0] += delta[0];
        p2[1] += delta[1];
        p2[2] += delta[2];
        this->PlaneWidget->SetOrigin(origin);
        this->PlaneWidget->SetPoint1(p1);
        this->PlaneWidget->SetPoint2(p2);
        snapped = true;
      }
    }

    this->UpdatingFromWidget = true;
    SetVec3(this->RectangleOrigin, origin);
    SetVec3(this->RectangleP1, p1);
    SetVec3(this->RectangleP2, p2);
    this->UpdatingFromWidget = false;
  }

  if (release)
  {
    const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
    const QString snapLabel = this->SnapMode == pqSHYXStreamlineSeedWidget::SnapScreenRay
      ? tr("Snapped to the surface under the cursor")
      : tr("Snapped to the nearest vertex in 3D");
    if (snapped)
    {
      this->setStatus(autoApply ? tr("%1. Applying streamlines.").arg(snapLabel)
                                : tr("%1. Click Apply to refresh.").arg(snapLabel));
    }
    else
    {
      this->setStatus(autoApply ? tr("Seed updated. Applying streamlines.")
                                : tr("Seed updated. Click Apply to refresh."));
    }
    // Marks the proxy as modified so Apply becomes available, then applies if requested.
    Q_EMIT this->changeAvailable();
    Q_EMIT this->changeFinished();
    this->applyOnRelease();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::applyOnRelease()
{
  if (!this->AutoApply || !this->AutoApply->isChecked())
  {
    return;
  }
  QTimer::singleShot(0, this, []() {
    if (auto* core = pqPVApplicationCore::instance())
    {
      Q_EMIT core->triggerApply();
    }
  });
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::setStatus(const QString& text)
{
  if (this->Status)
  {
    this->Status->setText(text);
  }
}
