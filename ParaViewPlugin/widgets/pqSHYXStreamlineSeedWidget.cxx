#include "pqSHYXStreamlineSeedWidget.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

#include "vtkAlgorithm.h"
#include "vtkActor.h"
#include "vtkCallbackCommand.h"
#include "vtkCamera.h"
#include "vtkCommand.h"
#include "vtkConeSource.h"
#include "vtkDataSet.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkGenericCell.h"
#include "vtkInteractorObserver.h"
#include "vtkLineRepresentation.h"
#include "vtkLineSource.h"
#include "vtkLineWidget2.h"
#include "vtkMath.h"
#include "vtkNew.h"
#include "vtkPlaneSource.h"
#include "vtkPolyDataMapper.h"
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
#include "vtkTransform.h"

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

/** Pipeline node name of a proxy ("(none)" / unknown proxies fall back to the XML label). */
QString ProxyDisplayName(vtkSMProxy* proxy)
{
  if (!proxy)
  {
    return QString();
  }
  if (auto* core = pqApplicationCore::instance())
  {
    if (auto* model = core->getServerManagerModel())
    {
      if (auto* source = model->findItem<pqPipelineSource*>(proxy))
      {
        return source->getSMName();
      }
    }
  }
  const char* label = proxy->GetXMLLabel();
  return label ? QString::fromUtf8(label) : QString();
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
  // vtkSphereRepresentation draws three handle-related props and they are independent:
  //   HandleVisibility -> the ball on the surface,
  //   RadialLine       -> the radius line from the center to that ball,
  //   HandleText       -> the "R = ..." label at the ball.
  // RadialLine / HandleText default to ON, so switching off only HandleVisibility still leaves a
  // radius line. Worse, ComputeInteractionState() picks the (now invisible) handle whenever any of
  // the three is on, so that line's end stayed draggable. Turn all three off: the radius is edited
  // in the panel (right-drag still scales).
  rep->HandleVisibilityOff();
  rep->RadialLineOff();
  rep->HandleTextOff();
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
}

/**
 * vtkSphereRepresentation::SetCenter() rewrites the radius from the handle distance while the
 * handle is visible. The handle is off here, but keep this helper: it makes every center update
 * preserve the radius unconditionally instead of relying on that detail.
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

void StyleRectangleActors(vtkActor* quad, vtkActor* centerHandle, vtkActor* cornerHandle,
  vtkActor* normalCone, vtkActor* normalLine)
{
  if (quad)
  {
    if (auto* p = quad->GetProperty())
    {
      p->SetColor(0.2, 0.65, 1.0);
      p->SetOpacity(0.30);
      p->LightingOff();
      p->SetRepresentationToSurface();
      p->EdgeVisibilityOn();
      p->SetEdgeColor(0.2, 0.65, 1.0);
      p->SetLineWidth(1.0);
    }
  }
  // Center handle: the only thing that snaps, so make it visually distinct.
  if (centerHandle)
  {
    if (auto* p = centerHandle->GetProperty())
    {
      p->SetColor(1.0, 0.85, 0.15);
      p->SetInterpolationToFlat();
    }
  }
  // One corner handle drives Width / Height around the center.
  if (cornerHandle)
  {
    if (auto* p = cornerHandle->GetProperty())
    {
      p->SetColor(0.2, 0.65, 1.0);
      p->SetInterpolationToFlat();
    }
  }
  // Normal handle: drag the cone to rotate the plane.
  if (normalCone)
  {
    if (auto* p = normalCone->GetProperty())
    {
      p->SetColor(0.95, 0.35, 0.25);
      p->SetInterpolationToFlat();
    }
  }
  if (normalLine)
  {
    if (auto* p = normalLine->GetProperty())
    {
      p->SetColor(0.95, 0.35, 0.25);
      p->SetLineWidth(2.0);
      p->LightingOff();
    }
  }
}
} // namespace

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
  this->setStatus(tr("Snap target: %1. Drag the seed handles; snap flags apply on release.")
                    .arg(this->snapTargetLabel()));
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
    &pqSHYXStreamlineSeedWidget::onActiveViewChanged);
  QObject::connect(&pqActiveObjects::instance(), &pqActiveObjects::dataUpdated, this,
    [this]() { this->syncWidgets(); });

  // The Snap target dropdown (SnapSurface input property) is rendered by pqInputSelectorWidget,
  // outside this group widget. Watch it so the snap-surface cache and the status hint follow a
  // change without waiting for the next drag.
  if (proxy)
  {
    if (vtkSMProperty* snapProp = proxy->GetProperty("SnapSurface"))
    {
      this->SnapTargetConnect = vtkSmartPointer<vtkEventQtSlotConnect>::New();
      // The dropdown writes unchecked values (pqPropertyWidget links), so the pending pick shows
      // up as UncheckedPropertyModifiedEvent; Apply/invalidations also raise ModifiedEvent.
      this->SnapTargetConnect->Connect(
        snapProp, vtkCommand::UncheckedPropertyModifiedEvent, this, SLOT(onSnapTargetChanged()));
      this->SnapTargetConnect->Connect(
        snapProp, vtkCommand::ModifiedEvent, this, SLOT(onSnapTargetChanged()));
    }
  }
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
       "Sphere: left-drag the ball to move it; set Radius in the panel (or right-drag to scale). "
       "Rectangle: drag the yellow center to move it (only the center snaps), the blue corner to "
       "resize, and the red cone to rotate the plane. "
       "Snap casts a ray through the cursor onto the snap surface and takes the vertex nearest "
       "that front-most hit (switch to Nearest vertex (3D) to ignore the camera). The snap surface "
       "is the node picked in Snap target above, or the Input when it says (none). "
       "Place in view drops the seed where the camera is looking (Line: 1/3 and 2/3 across the "
       "viewport, Sphere/Rectangle: viewport center). "
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
  this->PlaceFromBounds = new QPushButton(tr("Place in view"), this);
  this->PlaceFromBounds->setObjectName("SHYXStreamlineSeedPlaceAtBounds");
  this->PlaceFromBounds->setToolTip(
    tr("Move the seed to what the camera is looking at, so it always lands inside the view:\n"
       "Line takes the 1/3 and 2/3 positions across the viewport width;\n"
       "Sphere and Rectangle take the viewport center.\n"
       "Each target is ray-picked on the snap surface and snapped to the nearest vertex. "
       "Sizes are a fraction of the viewport, converted to world units at that depth."));
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
    tr("Screen ray: cast a ray through the cursor into the snap surface (the Snap target node, or "
       "the Input when (none)) and snap to the vertex nearest that front-most hit, so the result "
       "matches what you see. Nearest vertex (3D): ignore the camera and take the closest vertex "
       "in space, which can land on the back side of a vessel or on the far wall of a stent."));
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
    this->LineSnap1->setToolTip(tr("On release, move Point 1 to the nearest snap-surface vertex."));
    grid->addWidget(this->LineSnap1, 1, 4);

    grid->addWidget(new QLabel(tr("Point 2"), this->LineGroup), 2, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->LineP2[i] = MakeDoubleSpin(this->LineGroup);
      grid->addWidget(this->LineP2[i], 2, 1 + i);
    }
    this->LineSnap2 = new QCheckBox(tr("Snap"), this->LineGroup);
    this->LineSnap2->setToolTip(tr("On release, move Point 2 to the nearest snap-surface vertex."));
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
      tr("On release, move the sphere center to the nearest snap-surface vertex."));
    grid->addWidget(this->SphereSnap, 1, 4);

    auto* radiusRow = new QHBoxLayout();
    radiusRow->setContentsMargins(0, 0, 0, 0);
    radiusRow->setSpacing(4);
    radiusRow->addWidget(new QLabel(tr("Radius"), this->SphereGroup));
    this->SphereRadius = MakeDoubleSpin(this->SphereGroup);
    this->SphereRadius->setMinimum(0.0);
    this->SphereRadius->setToolTip(
      tr("Ball radius. The 3D widget has no radius handle: edit this value to resize, or "
         "right-drag the ball in the view."));
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

    // Step 1: center (the only snapped point).
    grid->addWidget(new QLabel(tr("Center"), this->RectangleGroup), 1, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->RectangleCenter[i] = MakeDoubleSpin(this->RectangleGroup);
      grid->addWidget(this->RectangleCenter[i], 1, 1 + i);
    }
    this->RectangleSnap = new QCheckBox(tr("Snap center"), this->RectangleGroup);
    this->RectangleSnap->setToolTip(
      tr("On release, move only the rectangle center to the nearest snap-surface vertex. "
         "Width / Height / Normal stay as they are."));
    grid->addWidget(this->RectangleSnap, 1, 4);

    // Step 2: normal (Place in view points it at the camera).
    grid->addWidget(new QLabel(tr("Normal"), this->RectangleGroup), 2, 0);
    for (int i = 0; i < 3; ++i)
    {
      this->RectangleNormal[i] = MakeDoubleSpin(this->RectangleGroup);
      grid->addWidget(this->RectangleNormal[i], 2, 1 + i);
    }
    auto* faceCamera = new QPushButton(tr("Face camera"), this->RectangleGroup);
    faceCamera->setToolTip(tr("Point the plane normal at the camera, so the quad faces the view."));
    grid->addWidget(faceCamera, 2, 4);
    QObject::connect(faceCamera, &QPushButton::clicked, this, [this]() {
      // GetViewPlaneNormal() points from the scene toward the camera, which is the convention
      // vtkImplicitPlaneRepresentation::SetNormalToCamera() uses. DirectionOfProjection would
      // give the opposite sign, so the cone would point away from the viewer.
      double normal[3] = { 0.0, 0.0, 1.0 };
      if (vtkRenderer* ren = this->renderer())
      {
        if (vtkCamera* camera = ren->GetActiveCamera())
        {
          camera->GetViewPlaneNormal(normal);
          if (vtkMath::Normalize(normal) <= 0.0)
          {
            normal[0] = 0.0;
            normal[1] = 0.0;
            normal[2] = 1.0;
          }
        }
      }
      this->UpdatingFromWidget = true;
      for (int i = 0; i < 3; ++i)
      {
        if (this->RectangleNormal[i])
        {
          this->RectangleNormal[i]->setValue(normal[i]);
        }
      }
      this->UpdatingFromWidget = false;
      this->updateRectangleGeometry();
      Q_EMIT this->changeAvailable();
      Q_EMIT this->changeFinished();
      this->setStatus(tr("Rectangle normal now faces the camera. Click Apply to refresh."));
    });

    // Step 3: one corner handle (in the view) controls these two sizes.
    auto* sizeRow = new QHBoxLayout();
    sizeRow->setContentsMargins(0, 0, 0, 0);
    sizeRow->setSpacing(4);
    sizeRow->addWidget(new QLabel(tr("Width"), this->RectangleGroup));
    this->RectangleWidth = MakeDoubleSpin(this->RectangleGroup);
    this->RectangleWidth->setMinimum(0.0);
    this->RectangleWidth->setToolTip(
      tr("Full edge length along the first in-plane axis. Dragging the corner handle in the view "
         "changes Width and Height symmetrically around the center."));
    sizeRow->addWidget(this->RectangleWidth, 1);
    sizeRow->addWidget(new QLabel(tr("Height"), this->RectangleGroup));
    this->RectangleHeight = MakeDoubleSpin(this->RectangleGroup);
    this->RectangleHeight->setMinimum(0.0);
    this->RectangleHeight->setToolTip(
      tr("Full edge length along the second in-plane axis."));
    sizeRow->addWidget(this->RectangleHeight, 1);
    grid->addLayout(sizeRow, 3, 0, 1, 5);

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

    this->RectangleOrientLabel = new QLabel(this->RectangleGroup);
    this->RectangleOrientLabel->setWordWrap(true);
    this->RectangleOrientLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    grid->addWidget(this->RectangleOrientLabel, 5, 0, 1, 5);

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

  linkVector("RectangleCenter", this->RectangleCenter);
  linkVector("RectangleNormal", this->RectangleNormal);
  linkDouble("RectangleWidth", this->RectangleWidth);
  linkDouble("RectangleHeight", this->RectangleHeight);
  linkInt("RectangleResolutionX", this->RectangleResX);
  linkInt("RectangleResolutionY", this->RectangleResY);
  linkBool("RectangleSnapCenter", this->RectangleSnap);
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::connectSeedSignals()
{
  for (QDoubleSpinBox* spin :
    { this->LineP1[0], this->LineP1[1], this->LineP1[2], this->LineP2[0], this->LineP2[1],
      this->LineP2[2], this->SphereCenter[0], this->SphereCenter[1], this->SphereCenter[2],
      this->SphereRadius, this->RectangleCenter[0], this->RectangleCenter[1],
      this->RectangleCenter[2], this->RectangleNormal[0], this->RectangleNormal[1],
      this->RectangleNormal[2], this->RectangleWidth, this->RectangleHeight })
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
void pqSHYXStreamlineSeedWidget::onSnapTargetChanged()
{
  // Drop the cached surface and locators: the next release re-resolves the newly picked node.
  this->SnapSurface = nullptr;
  this->SnapLocator = nullptr;
  this->SnapCellLocator = nullptr;
  this->SnapInput = nullptr;
  this->SnapMTime = 0;
  // Resolve the node here, outside a drag: if its data never reached the client yet this runs
  // UpdatePipeline() once now instead of in the middle of a handle release.
  this->ensureSnapper();
  const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
  const QString message = tr("Snap target: %1.").arg(this->snapTargetLabel());
  this->setStatus(autoApply ? tr("%1 Drag and release to snap.").arg(message)
                            : tr("%1 Drag and release to snap, then click Apply.").arg(message));
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
  vtkRenderer* ren = this->renderer();
  if (!ren || !this->ensureSnapper())
  {
    this->setStatus(tr("No active 3D view with a snap surface; edit the seed values manually."));
    return;
  }

  int* size = ren->GetSize();
  if (!size || size[0] <= 0 || size[1] <= 0)
  {
    this->setStatus(tr("The 3D view has no size yet; edit the seed values manually."));
    return;
  }
  const double width = static_cast<double>(size[0]);
  const double height = static_cast<double>(size[1]);
  const double midX = 0.5 * width;
  const double midY = 0.5 * height;

  // Place from where the camera actually looks, so the seed always lands inside the current view
  // instead of at the dataset bounds center (which can be off-screen after zooming/panning).
  // Line takes the 1/3 and 2/3 points of the viewport width; sphere / rectangle take the center.
  double lineStart[3];
  double lineEnd[3];
  double viewCenter[3];
  const bool haveStart =
    this->surfacePointFromDisplay(width / 3.0, midY, lineStart);
  const bool haveEnd =
    this->surfacePointFromDisplay(2.0 * width / 3.0, midY, lineEnd);
  const bool haveCenter = this->surfacePointFromDisplay(midX, midY, viewCenter);

  // Fallback when a ray misses the surface (point cloud, or the view is aimed off the model).
  double b[6];
  input->GetBounds(b);
  const double boundsCenter[3] = {
    0.5 * (b[0] + b[1]), 0.5 * (b[2] + b[3]), 0.5 * (b[4] + b[5])
  };
  const double boundsLength = std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 });

  const int shape = this->currentShape();
  double anchor[3];
  if (haveCenter)
  {
    std::copy(viewCenter, viewCenter + 3, anchor);
  }
  else
  {
    std::copy(boundsCenter, boundsCenter + 3, anchor);
  }

  // Sizes as a fraction of the viewport, converted to world length at the anchor depth, so the
  // initial gizmo has a sensible on-screen size regardless of dataset units.
  const double minSide = std::min(width, height);
  const double radiusPixels = 0.15 * minSide;
  const double halfSidePixels = 0.20 * minSide;
  double radius = this->worldLengthForPixels(anchor, radiusPixels);
  double halfSide = this->worldLengthForPixels(anchor, halfSidePixels);
  if (!(radius > 0.0) || !std::isfinite(radius))
  {
    radius = 0.15 * boundsLength;
  }
  if (!(halfSide > 0.0) || !std::isfinite(halfSide))
  {
    halfSide = 0.25 * boundsLength;
  }

  // Suppress the per-coordinate sync while the controls are rewritten; one sync + render runs at
  // the end so the 3D widget jumps to the new placement immediately (Apply is not required for the
  // widget itself, only for the streamlines).
  this->UpdatingFromWidget = true;
  if (shape == 0)
  {
    double p1[3];
    double p2[3];
    if (haveStart && haveEnd && !NearlySamePoint(lineStart, lineEnd))
    {
      std::copy(lineStart, lineStart + 3, p1);
      std::copy(lineEnd, lineEnd + 3, p2);
    }
    else
    {
      // At least one screen position missed the surface: keep the line on screen by running it
      // along the camera right vector through the anchor, with the same 1/3 -> 2/3 screen span.
      double right[3] = { 1.0, 0.0, 0.0 };
      double up[3] = { 0.0, 1.0, 0.0 };
      this->cameraRightUp(right, up);
      double span = this->worldLengthForPixels(anchor, width / 3.0);
      if (!(span > 0.0) || !std::isfinite(span))
      {
        span = halfSide;
      }
      p1[0] = anchor[0] - span * right[0];
      p1[1] = anchor[1] - span * right[1];
      p1[2] = anchor[2] - span * right[2];
      p2[0] = anchor[0] + span * right[0];
      p2[1] = anchor[1] + span * right[1];
      p2[2] = anchor[2] + span * right[2];
    }
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
        this->SphereCenter[i]->setValue(anchor[i]);
      }
    }
    if (this->SphereRadius)
    {
      this->SphereRadius->setValue(radius);
    }
  }
  else
  {
    // Step 1: center = ray hit at the viewport center. Step 2: normal = toward the camera
    // (GetViewPlaneNormal, same sign convention as VTK's clip widget), so the quad and its
    // front cone face the viewer. Step 3: Width / Height sized from the viewport.
    double normal[3] = { 0.0, 0.0, 1.0 };
    if (vtkRenderer* ren = this->renderer())
    {
      if (vtkCamera* camera = ren->GetActiveCamera())
      {
        camera->GetViewPlaneNormal(normal);
        if (vtkMath::Normalize(normal) <= 0.0)
        {
          normal[0] = 0.0;
          normal[1] = 0.0;
          normal[2] = 1.0;
        }
      }
    }

    for (int i = 0; i < 3; ++i)
    {
      if (this->RectangleCenter[i])
      {
        this->RectangleCenter[i]->setValue(anchor[i]);
      }
      if (this->RectangleNormal[i])
      {
        this->RectangleNormal[i]->setValue(normal[i]);
      }
    }
    if (this->RectangleWidth)
    {
      this->RectangleWidth->setValue(2.0 * halfSide);
    }
    if (this->RectangleHeight)
    {
      this->RectangleHeight->setValue(2.0 * halfSide);
    }
  }
  this->UpdatingFromWidget = false;

  this->syncWidgets();
  // Same as a drag: park the rotation center on the fresh seed so orbiting stays on it. Place in
  // view rewrites both line endpoints, so there is no single "moved" end: use the midpoint.
  this->MovedLineEndPoint = -1;
  this->updatePickCenterFromWidget();
  Q_EMIT this->changeAvailable();
  Q_EMIT this->changeFinished();
  const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
  const bool placedOnSurface = shape == 0 ? (haveStart || haveEnd) : haveCenter;
  const QString where = placedOnSurface
    ? (shape == 0 ? tr("Seed placed on the surface at the 1/3 and 2/3 view positions.")
                  : tr("Seed placed on the surface at the view center."))
    : tr("Surface was not hit at the target screen position; placed at the dataset bounds center.");
  this->setStatus(autoApply ? tr("%1 Applying streamlines.").arg(where)
                            : tr("%1 Click Apply to refresh.").arg(where));
  this->applyOnRelease();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::cameraRightUp(double right[3], double up[3]) const
{
  right[0] = 1.0;
  right[1] = 0.0;
  right[2] = 0.0;
  up[0] = 0.0;
  up[1] = 1.0;
  up[2] = 0.0;

  vtkRenderer* ren = this->renderer();
  vtkCamera* camera = ren ? ren->GetActiveCamera() : nullptr;
  if (!camera)
  {
    return;
  }

  double viewUp[3];
  double direction[3];
  camera->GetViewUp(viewUp);
  camera->GetDirectionOfProjection(direction);
  if (vtkMath::Normalize(viewUp) <= 0.0 || vtkMath::Normalize(direction) <= 0.0)
  {
    return;
  }
  // Screen right = view direction x up (VTK looks down -direction, so this keeps right-handed
  // screen axes); atan-free and stable as long as the camera is not degenerate.
  double r[3];
  vtkMath::Cross(direction, viewUp, r);
  if (vtkMath::Normalize(r) <= 0.0)
  {
    return;
  }
  right[0] = r[0];
  right[1] = r[1];
  right[2] = r[2];
  up[0] = viewUp[0];
  up[1] = viewUp[1];
  up[2] = viewUp[2];
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
vtkSMProxy* pqSHYXStreamlineSeedWidget::snapTargetProxy() const
{
  vtkSMProxy* proxy = this->proxy();
  auto* snapProp =
    proxy ? vtkSMInputProperty::SafeDownCast(proxy->GetProperty("SnapSurface")) : nullptr;
  if (!snapProp)
  {
    return nullptr;
  }
  // pqPropertyWidget writes its links to *unchecked* values, so the dropdown's current node lives
  // there until the panel copies it into the checked property on Apply. Read the pending value
  // first (GetUncheckedProxy falls back to the checked one when nothing is pending), so the 3D
  // widget snaps to whatever the Snap target row currently shows.
  return snapProp->GetUncheckedProxy(0);
}

//-----------------------------------------------------------------------------
vtkDataSet* pqSHYXStreamlineSeedWidget::snapDataSet() const
{
  auto* proxy = this->proxy();
  auto* snapProp =
    proxy ? vtkSMInputProperty::SafeDownCast(proxy->GetProperty("SnapSurface")) : nullptr;
  if (snapProp)
  {
    if (auto* source = vtkSMSourceProxy::SafeDownCast(snapProp->GetUncheckedProxy(0)))
    {
      const int port = static_cast<int>(snapProp->GetUncheckedOutputPortForConnection(0));
      auto readOutput = [source, port]() -> vtkDataSet* {
        auto* algorithm = vtkAlgorithm::SafeDownCast(source->GetClientSideObject());
        return algorithm ? vtkDataSet::SafeDownCast(algorithm->GetOutputDataObject(port))
                         : nullptr;
      };
      if (vtkDataSet* ds = readOutput())
      {
        if (ds->GetNumberOfPoints() > 0)
        {
          return ds;
        }
      }
      // The picked node need not be shown in a view, so its client-side dataset can still be
      // empty. UpdatePipeline() is a no-op when the source is already up to date.
      source->UpdatePipeline();
      if (vtkDataSet* ds = readOutput())
      {
        if (ds->GetNumberOfPoints() > 0)
        {
          return ds;
        }
      }
    }
  }
  // "(none)", or the picked node has produced no points yet: use the Input (parent) surface, which
  // is exactly the behaviour before the Snap target dropdown existed.
  return this->inputDataSet();
}

//-----------------------------------------------------------------------------
QString pqSHYXStreamlineSeedWidget::snapTargetLabel() const
{
  vtkSMProxy* target = this->snapTargetProxy();
  if (!target)
  {
    vtkSMProxy* proxy = this->proxy();
    auto* input = proxy ? vtkSMInputProperty::SafeDownCast(proxy->GetProperty("Input")) : nullptr;
    target = input && input->GetNumberOfProxies() > 0 ? input->GetProxy(0) : nullptr;
  }
  const QString name = ProxyDisplayName(target);
  return name.isEmpty() ? tr("Input") : name;
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
    if (!this->RectActor)
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
  this->LineWidget = nullptr;
  this->SphereWidget = nullptr;
  this->destroyRectangleWidget();
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
  this->setRectangleEnabled(on);
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
  auto rep = vtkSmartPointer<vtkSphereRepresentation>::New();
  StyleSphereRepresentation(rep);
  widget->SetRepresentation(rep);
  widget->SetInteractor(iren);
  widget->SetPriority(1.0);
  // Left-drag translates; the radius has no handle, so it comes from the panel Radius value
  // (right-drag scaling from vtkSphereWidget2 stays available as a quick 3D adjust).
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

  vtkRenderer* ren = this->renderer();
  if (!ren)
  {
    return;
  }

  // Quad.
  this->RectSource = vtkSmartPointer<vtkPlaneSource>::New();
  this->RectSource->SetXResolution(1);
  this->RectSource->SetYResolution(1);
  this->RectMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectMapper->SetInputConnection(this->RectSource->GetOutputPort());
  this->RectActor = vtkSmartPointer<vtkActor>::New();
  this->RectActor->SetMapper(this->RectMapper);
  this->RectActor->PickableOff();

  // Center handle: the only snapped point.
  this->RectCenterSource = vtkSmartPointer<vtkSphereSource>::New();
  this->RectCenterSource->SetThetaResolution(16);
  this->RectCenterSource->SetPhiResolution(12);
  this->RectCenterMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectCenterMapper->SetInputConnection(this->RectCenterSource->GetOutputPort());
  this->RectCenterActor = vtkSmartPointer<vtkActor>::New();
  this->RectCenterActor->SetMapper(this->RectCenterMapper);
  this->RectCenterActor->PickableOn();

  // Single corner handle: drag it to change Width / Height around the center.
  this->RectCornerSource = vtkSmartPointer<vtkSphereSource>::New();
  this->RectCornerSource->SetThetaResolution(16);
  this->RectCornerSource->SetPhiResolution(12);
  this->RectCornerMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectCornerMapper->SetInputConnection(this->RectCornerSource->GetOutputPort());
  this->RectCornerActor = vtkSmartPointer<vtkActor>::New();
  this->RectCornerActor->SetMapper(this->RectCornerMapper);
  this->RectCornerActor->PickableOn();

  // Normal handle: a stick through the center plus a cone at each end, both draggable. This is
  // what vtkImplicitPlaneRepresentation does (cones at origin +/- 0.3 * diagonal along the
  // normal), so the plane can be rotated no matter which side the camera is on.
  this->RectNormalLineSource = vtkSmartPointer<vtkLineSource>::New();
  this->RectNormalLineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectNormalLineMapper->SetInputConnection(this->RectNormalLineSource->GetOutputPort());
  this->RectNormalLineActor = vtkSmartPointer<vtkActor>::New();
  this->RectNormalLineActor->SetMapper(this->RectNormalLineMapper);
  this->RectNormalLineActor->PickableOff();

  this->RectNormalSource = vtkSmartPointer<vtkConeSource>::New();
  this->RectNormalSource->SetResolution(16);
  this->RectNormalMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectNormalMapper->SetInputConnection(this->RectNormalSource->GetOutputPort());
  this->RectNormalActor = vtkSmartPointer<vtkActor>::New();
  this->RectNormalActor->SetMapper(this->RectNormalMapper);
  this->RectNormalActor->PickableOff(); // picking is resolved in display space

  this->RectNormalSource2 = vtkSmartPointer<vtkConeSource>::New();
  this->RectNormalSource2->SetResolution(16);
  this->RectNormalMapper2 = vtkSmartPointer<vtkPolyDataMapper>::New();
  this->RectNormalMapper2->SetInputConnection(this->RectNormalSource2->GetOutputPort());
  this->RectNormalActor2 = vtkSmartPointer<vtkActor>::New();
  this->RectNormalActor2->SetMapper(this->RectNormalMapper2);
  this->RectNormalActor2->PickableOff();

  StyleRectangleActors(this->RectActor, this->RectCenterActor, this->RectCornerActor,
    this->RectNormalActor, this->RectNormalLineActor);
  if (auto* p = this->RectNormalActor2 ? this->RectNormalActor2->GetProperty() : nullptr)
  {
    p->SetColor(0.95, 0.35, 0.25);
    p->SetInterpolationToFlat();
  }

  ren->AddActor(this->RectActor);
  ren->AddActor(this->RectCenterActor);
  ren->AddActor(this->RectCornerActor);
  ren->AddActor(this->RectNormalLineActor);
  ren->AddActor(this->RectNormalActor);
  ren->AddActor(this->RectNormalActor2);

  this->RectInteractorObserver = vtkSmartPointer<vtkCallbackCommand>::New();
  this->RectInteractorObserver->SetClientData(this);
  this->RectInteractorObserver->SetCallback(
    &pqSHYXStreamlineSeedWidget::ProcessRectangleEvents);
  // Priority above the interactor style so a handle drag does not also orbit the camera.
  iren->AddObserver(vtkCommand::LeftButtonPressEvent, this->RectInteractorObserver, 1.0);
  iren->AddObserver(vtkCommand::LeftButtonReleaseEvent, this->RectInteractorObserver, 1.0);
  iren->AddObserver(vtkCommand::MouseMoveEvent, this->RectInteractorObserver, 1.0);

  this->setRectangleEnabled(true);
  this->updateRectangleGeometry();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::destroyRectangleWidget()
{
  if (this->Interactor && this->RectInteractorObserver)
  {
    this->Interactor->RemoveObserver(this->RectInteractorObserver);
  }
  this->RectInteractorObserver = nullptr;

  if (vtkRenderer* ren = this->renderer())
  {
    if (this->RectActor)
    {
      ren->RemoveActor(this->RectActor);
    }
    if (this->RectCenterActor)
    {
      ren->RemoveActor(this->RectCenterActor);
    }
    if (this->RectCornerActor)
    {
      ren->RemoveActor(this->RectCornerActor);
    }
    if (this->RectNormalActor)
    {
      ren->RemoveActor(this->RectNormalActor);
    }
    if (this->RectNormalActor2)
    {
      ren->RemoveActor(this->RectNormalActor2);
    }
    if (this->RectNormalLineActor)
    {
      ren->RemoveActor(this->RectNormalLineActor);
    }
  }

  this->RectActor = nullptr;
  this->RectMapper = nullptr;
  this->RectSource = nullptr;
  this->RectCenterActor = nullptr;
  this->RectCenterMapper = nullptr;
  this->RectCenterSource = nullptr;
  this->RectCornerActor = nullptr;
  this->RectCornerMapper = nullptr;
  this->RectCornerSource = nullptr;
  this->RectNormalActor = nullptr;
  this->RectNormalMapper = nullptr;
  this->RectNormalSource = nullptr;
  this->RectNormalActor2 = nullptr;
  this->RectNormalMapper2 = nullptr;
  this->RectNormalSource2 = nullptr;
  this->RectNormalLineActor = nullptr;
  this->RectNormalLineMapper = nullptr;
  this->RectNormalLineSource = nullptr;
  this->RectEnabled = false;
  this->RectDrag = RectDragNone;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::setRectangleEnabled(bool on)
{
  this->RectEnabled = on;
  if (vtkRenderer* ren = this->renderer())
  {
    const auto toggle = [on, ren](vtkSmartPointer<vtkActor>& actor) {
      if (!actor)
      {
        return;
      }
      if (on)
      {
        if (!ren->HasViewProp(actor))
        {
          ren->AddActor(actor);
        }
      }
      else if (ren->HasViewProp(actor))
      {
        ren->RemoveActor(actor);
      }
    };
    toggle(this->RectActor);
    toggle(this->RectCenterActor);
    toggle(this->RectCornerActor);
    toggle(this->RectNormalActor);
    toggle(this->RectNormalActor2);
    toggle(this->RectNormalLineActor);
  }
  if (!on)
  {
    this->RectDrag = RectDragNone;
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::readRectangleFromControls(
  double center[3], double normal[3], double& width, double& height) const
{
  center[0] = center[1] = center[2] = 0.0;
  normal[0] = 0.0;
  normal[1] = 0.0;
  normal[2] = 1.0;
  width = 0.0;
  height = 0.0;

  vtkSMProxy* proxy = this->proxy();
  auto readVec = [proxy](const QDoubleSpinBox* const spins[3], const char* name, double out[3]) {
    if (spins[0] && spins[1] && spins[2])
    {
      out[0] = spins[0]->value();
      out[1] = spins[1]->value();
      out[2] = spins[2]->value();
      return;
    }
    if (proxy)
    {
      if (auto* p = proxy->GetProperty(name))
      {
        vtkSMPropertyHelper(p).Get(out, 3);
      }
    }
  };
  readVec(this->RectangleCenter, "RectangleCenter", center);
  readVec(this->RectangleNormal, "RectangleNormal", normal);
  if (this->RectangleWidth)
  {
    width = this->RectangleWidth->value();
  }
  else if (proxy && proxy->GetProperty("RectangleWidth"))
  {
    width = vtkSMPropertyHelper(proxy, "RectangleWidth").GetAsDouble();
  }
  if (this->RectangleHeight)
  {
    height = this->RectangleHeight->value();
  }
  else if (proxy && proxy->GetProperty("RectangleHeight"))
  {
    height = vtkSMPropertyHelper(proxy, "RectangleHeight").GetAsDouble();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::updateRectangleGeometry()
{
  if (!this->RectSource || !this->RectCenterSource || !this->RectCornerSource)
  {
    return;
  }

  double center[3];
  double normal[3];
  double width = 0.0;
  double height = 0.0;
  this->readRectangleFromControls(center, normal, width, height);

  if (!std::isfinite(normal[0]) || !std::isfinite(normal[1]) || !std::isfinite(normal[2]) ||
    vtkMath::Normalize(normal) < 1e-12)
  {
    normal[0] = 0.0;
    normal[1] = 0.0;
    normal[2] = 1.0;
  }
  std::copy(normal, normal + 3, this->RectNormal);

  // Same basis the filter builds, so the editor and the seeds agree.
  vtkMath::Perpendiculars(this->RectNormal, this->RectAxisU, this->RectAxisV, 0.0);
  double cross[3];
  vtkMath::Cross(this->RectAxisU, this->RectAxisV, cross);
  if (vtkMath::Dot(cross, this->RectNormal) < 0.0)
  {
    this->RectAxisV[0] = -this->RectAxisV[0];
    this->RectAxisV[1] = -this->RectAxisV[1];
    this->RectAxisV[2] = -this->RectAxisV[2];
  }

  if (!(width > 0.0) || !std::isfinite(width))
  {
    width = 1.0;
  }
  if (!(height > 0.0) || !std::isfinite(height))
  {
    height = 1.0;
  }

  const double halfU[3] = { 0.5 * width * this->RectAxisU[0], 0.5 * width * this->RectAxisU[1],
    0.5 * width * this->RectAxisU[2] };
  const double halfV[3] = { 0.5 * height * this->RectAxisV[0],
    0.5 * height * this->RectAxisV[1], 0.5 * height * this->RectAxisV[2] };

  double origin[3];
  double point1[3];
  double point2[3];
  for (int i = 0; i < 3; ++i)
  {
    origin[i] = center[i] - halfU[i] - halfV[i];
    point1[i] = origin[i] + 2.0 * halfU[i];
    point2[i] = origin[i] + 2.0 * halfV[i];
  }
  this->RectSource->SetOrigin(origin);
  this->RectSource->SetPoint1(point1);
  this->RectSource->SetPoint2(point2);
  this->RectSource->Update();

  // Handle sizes in screen pixels, converted at the center's depth so they stay grabbable.
  const double handleRadius = std::max(1e-6, this->worldLengthForPixels(center, 6.0));
  this->RectCenterSource->SetCenter(center);
  this->RectCenterSource->SetRadius(handleRadius);
  this->RectCenterSource->Update();

  double corner[3] = { center[0] + halfU[0] + halfV[0], center[1] + halfU[1] + halfV[1],
    center[2] + halfU[2] + halfV[2] };
  this->RectCornerSource->SetCenter(corner);
  this->RectCornerSource->SetRadius(handleRadius * 1.2);
  this->RectCornerSource->Update();

  // Normal handle: one stick through the center, with a draggable cone at each end (mirrors
  // vtkImplicitPlaneRepresentation, which puts cones at origin +/- 0.3 * diagonal).
  if (this->RectNormalSource && this->RectNormalSource2 && this->RectNormalLineSource)
  {
    const double stickPixels = 48.0;
    double length = this->worldLengthForPixels(center, stickPixels);
    if (!(length > 0.0) || !std::isfinite(length))
    {
      // No renderer yet: fall back to a fraction of the quad so the handle is still visible.
      length = 0.5 * std::max({ width, height, 1e-6 });
    }
    this->RectNormalLength = length;

    const double coneHeight = 0.30 * length;
    const double coneRadius = 0.13 * length;

    double tipPlus[3];
    double tipMinus[3];
    double centerPlus[3];
    double centerMinus[3];
    for (int i = 0; i < 3; ++i)
    {
      tipPlus[i] = center[i] + length * this->RectNormal[i];
      tipMinus[i] = center[i] - length * this->RectNormal[i];
      // Cone bodies sit inside the tips, both pointing along +normal like VTK's clip widget.
      centerPlus[i] = tipPlus[i] - 0.5 * coneHeight * this->RectNormal[i];
      centerMinus[i] = tipMinus[i] - 0.5 * coneHeight * this->RectNormal[i];
    }

    this->RectNormalLineSource->SetPoint1(tipMinus);
    this->RectNormalLineSource->SetPoint2(tipPlus);
    this->RectNormalLineSource->Update();

    this->RectNormalSource->SetCenter(centerPlus);
    this->RectNormalSource->SetDirection(this->RectNormal);
    this->RectNormalSource->SetHeight(coneHeight);
    this->RectNormalSource->SetRadius(coneRadius);
    this->RectNormalSource->Update();

    this->RectNormalSource2->SetCenter(centerMinus);
    this->RectNormalSource2->SetDirection(this->RectNormal);
    this->RectNormalSource2->SetHeight(coneHeight);
    this->RectNormalSource2->SetRadius(coneRadius);
    this->RectNormalSource2->Update();
  }

  if (this->RectangleOrientLabel)
  {
    this->RectangleOrientLabel->setText(
      tr("Center %1, %2, %3; normal %4, %5, %6; %7 x %8. Drag the yellow center to move, the blue "
         "corner to resize, the red cone to rotate the plane.")
        .arg(center[0], 0, 'g', 6)
        .arg(center[1], 0, 'g', 6)
        .arg(center[2], 0, 'g', 6)
        .arg(this->RectNormal[0], 0, 'g', 4)
        .arg(this->RectNormal[1], 0, 'g', 4)
        .arg(this->RectNormal[2], 0, 'g', 4)
        .arg(width, 0, 'g', 6)
        .arg(height, 0, 'g', 6));
  }
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
  else if (shape == 2 && this->RectActor)
  {
    // The quad is derived geometry (center + normal + W/H), so rebuild it unconditionally: the
    // plane source update is cheap and this keeps the editor in sync with the panel draft.
    this->updateRectangleGeometry();
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
  // The rectangle has no bounds-relative state: its handle sizes are computed per frame from
  // worldLengthForPixels(), so there is nothing to place here.
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
  // Snap surface = the node picked in Snap target, else the Input flow field. Whatever it is,
  // every vertex of its surface is a candidate (volumes go through vtkDataSetSurfaceFilter).
  vtkDataSet* input = this->snapDataSet();
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
bool pqSHYXStreamlineSeedWidget::rayPickAtDisplay(double displayX, double displayY, double hit[3])
{
  vtkRenderer* ren = this->renderer();
  if (!ren || !this->SnapCellLocator || !std::isfinite(displayX) || !std::isfinite(displayY))
  {
    return false;
  }

  double nearPoint[4];
  double farPoint[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, 0.0, nearPoint);
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, 1.0, farPoint);

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
bool pqSHYXStreamlineSeedWidget::rayPickSurfacePoint(const double world[3], double hit[3])
{
  vtkRenderer* ren = this->renderer();
  if (!ren)
  {
    return false;
  }

  ren->SetWorldPoint(world[0], world[1], world[2], 1.0);
  ren->WorldToDisplay();
  double display[3];
  ren->GetDisplayPoint(display);
  return this->rayPickAtDisplay(display[0], display[1], hit);
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::surfacePointFromDisplay(
  double displayX, double displayY, double out[3])
{
  if (!this->ensureSnapper())
  {
    return false;
  }
  double hit[3];
  if (!this->rayPickAtDisplay(displayX, displayY, hit))
  {
    return false;
  }
  // Same target the Screen ray snap mode uses: the vertex nearest the front-most surface hit.
  const vtkIdType id = this->SnapLocator->FindClosestPoint(hit);
  if (id < 0)
  {
    out[0] = hit[0];
    out[1] = hit[1];
    out[2] = hit[2];
    return true;
  }
  this->SnapSurface->GetPoint(id, out);
  return true;
}

//-----------------------------------------------------------------------------
double pqSHYXStreamlineSeedWidget::worldLengthForPixels(const double at[3], double pixels) const
{
  vtkRenderer* ren = this->renderer();
  if (!ren || !(pixels > 0.0))
  {
    return 0.0;
  }

  ren->SetWorldPoint(at[0], at[1], at[2], 1.0);
  ren->WorldToDisplay();
  double display[3];
  ren->GetDisplayPoint(display);

  double a[4];
  double b[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, display[0], display[1], display[2], a);
  vtkInteractorObserver::ComputeDisplayToWorld(ren, display[0] + pixels, display[1], display[2], b);
  const double dx = a[0] - b[0];
  const double dy = a[1] - b[1];
  const double dz = a[2] - b[2];
  const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
  return std::isfinite(length) ? length : 0.0;
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
        // Remember which handle grabbed the event so the pick center can follow that endpoint:
        // vtkLineRepresentation reports OnP1 / OnP2 for the end points and OnLine for the middle
        // handle. The middle handle only translates, so it has no single end point.
        switch (rep->GetInteractionState())
        {
          case vtkLineRepresentation::OnP1:
          case vtkLineRepresentation::TranslatingP1:
            this->MovedLineEndPoint = 0;
            break;
          case vtkLineRepresentation::OnP2:
          case vtkLineRepresentation::TranslatingP2:
            this->MovedLineEndPoint = 1;
            break;
          default:
            this->MovedLineEndPoint = -1;
            break;
        }
      }
    }
    else if (shape == 1 && this->SphereWidget)
    {
      if (auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation()))
      {
        rep->GetCenter(this->StartSphereCenter);
      }
    }
    // The rectangle is a hand-rolled editor (see ProcessRectangleEvents); it has no widget events.
  }
  else if (eid == vtkCommand::InteractionEvent)
  {
    this->syncControlsFromWidget(false);
  }
  else if (eid == vtkCommand::EndInteractionEvent)
  {
    this->Interacting = false;
    this->syncControlsFromWidget(true);
    // Re-centre the camera's rotation point on what was just moved, so the next orbit/zoom stays
    // around the seed instead of the old center of rotation.
    this->updatePickCenterFromWidget();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::ProcessRectangleEvents(
  vtkObject*, unsigned long eid, void* clientdata, void*)
{
  auto* self = reinterpret_cast<pqSHYXStreamlineSeedWidget*>(clientdata);
  if (self)
  {
    self->onRectangleInteractorEvent(eid);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::onRectangleInteractorEvent(unsigned long eid)
{
  if (!this->WidgetsVisible || !this->RectEnabled || !this->Interactor || !this->RectCenterSource)
  {
    return;
  }
  if (this->currentShape() != 2)
  {
    return;
  }

  int pos[2];
  this->Interactor->GetEventPosition(pos);

  if (eid == vtkCommand::LeftButtonPressEvent)
  {
    if (this->beginRectangleDrag(pos[0], pos[1]))
    {
      // Consume the press so the trackball style does not also start a camera rotate.
      if (this->RectInteractorObserver)
      {
        this->RectInteractorObserver->SetAbortFlag(1);
      }
    }
  }
  else if (eid == vtkCommand::MouseMoveEvent)
  {
    if (this->RectDrag != RectDragNone)
    {
      this->dragRectangle(pos[0], pos[1]);
      if (this->RectInteractorObserver)
      {
        this->RectInteractorObserver->SetAbortFlag(1);
      }
    }
  }
  else if (eid == vtkCommand::LeftButtonReleaseEvent)
  {
    if (this->RectDrag != RectDragNone)
    {
      this->endRectangleDrag();
      if (this->RectInteractorObserver)
      {
        this->RectInteractorObserver->SetAbortFlag(1);
      }
    }
  }
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::displayPointForWorld(
  vtkRenderer* ren, const double world[3], double out[3]) const
{
  out[0] = out[1] = out[2] = 0.0;
  if (!ren)
  {
    return false;
  }
  ren->SetWorldPoint(world[0], world[1], world[2], 1.0);
  ren->WorldToDisplay();
  const double* display = ren->GetDisplayPoint();
  out[0] = display[0];
  out[1] = display[1];
  out[2] = display[2];
  return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

//-----------------------------------------------------------------------------
pqSHYXStreamlineSeedWidget::RectangleDragMode pqSHYXStreamlineSeedWidget::pickRectangleHandle(
  vtkRenderer* ren, int displayX, int displayY, double& depthOut)
{
  depthOut = 0.5;
  if (!ren || !this->RectCenterSource || !this->RectCornerSource || !this->RectNormalSource)
  {
    return RectDragNone;
  }

  // Priority order matters: the normal cones line up with the center handle along the view axis
  // when the camera looks along the normal, so a center click must win before a cone is
  // considered. This is what keeps "drag the center" from turning into "rotate the plane".
  struct Candidate
  {
    RectangleDragMode Mode;
    double Center[3];
  };
  double centerPos[3];
  double cornerPos[3];
  double tipPlus[3];
  double tipMinus[3];
  this->RectCenterSource->GetCenter(centerPos);
  this->RectCornerSource->GetCenter(cornerPos);
  this->RectNormalSource->GetCenter(tipPlus);

  if (this->RectNormalSource2)
  {
    this->RectNormalSource2->GetCenter(tipMinus);
  }
  else
  {
    std::copy(centerPos, centerPos + 3, tipMinus);
  }
  // Use the cone tips rather than the cone centers so the grab point matches the visible tip.
  for (int i = 0; i < 3; ++i)
  {
    tipPlus[i] = centerPos[i] +
      this->RectNormalLength * this->RectNormal[i];
    tipMinus[i] = centerPos[i] -
      this->RectNormalLength * this->RectNormal[i];
  }

  const Candidate candidates[] = {
    { RectDragCenter, { centerPos[0], centerPos[1], centerPos[2] } },
    { RectDragCorner, { cornerPos[0], cornerPos[1], cornerPos[2] } },
    { RectDragNormal, { tipPlus[0], tipPlus[1], tipPlus[2] } },
    { RectDragNormal, { tipMinus[0], tipMinus[1], tipMinus[2] } },
  };

  const double tolerance = static_cast<double>(std::max(4, this->RectHandleTolerance));
  for (const Candidate& candidate : candidates)
  {
    double display[3];
    if (!this->displayPointForWorld(ren, candidate.Center, display))
    {
      continue;
    }
    const double dx = display[0] - static_cast<double>(displayX);
    const double dy = display[1] - static_cast<double>(displayY);
    if (dx * dx + dy * dy <= tolerance * tolerance)
    {
      depthOut = display[2];
      return candidate.Mode;
    }
  }
  return RectDragNone;
}

//-----------------------------------------------------------------------------
bool pqSHYXStreamlineSeedWidget::beginRectangleDrag(int displayX, int displayY)
{
  vtkRenderer* ren = this->renderer();
  if (!ren)
  {
    return false;
  }

  double handleDepth = 0.5;
  const RectangleDragMode mode = this->pickRectangleHandle(ren, displayX, displayY, handleDepth);
  if (mode == RectDragNone)
  {
    // Nothing close: let the interactor style orbit/pan the camera as usual.
    return false;
  }

  double center[3];
  double normal[3];
  double width = 0.0;
  double height = 0.0;
  this->readRectangleFromControls(center, normal, width, height);
  std::copy(center, center + 3, this->RectDragCenterStart);
  this->RectDragStartWidth = width;
  this->RectDragStartHeight = height;
  this->RectDragDepth = handleDepth;

  this->RectDrag = mode;
  if (mode == RectDragCenter)
  {
    double clickWorld[4];
    vtkInteractorObserver::ComputeDisplayToWorld(
      ren, displayX, displayY, this->RectDragDepth, clickWorld);
    this->RectDragGrabOffset[0] = center[0] - clickWorld[0];
    this->RectDragGrabOffset[1] = center[1] - clickWorld[1];
    this->RectDragGrabOffset[2] = center[2] - clickWorld[2];
  }
  else if (mode == RectDragNormal)
  {
    this->RectDragLastDisplay[0] = static_cast<double>(displayX);
    this->RectDragLastDisplay[1] = static_cast<double>(displayY);
  }

  this->Interacting = true;
  return true;
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::dragRectangle(int displayX, int displayY)
{
  vtkRenderer* ren = this->renderer();
  if (!ren || this->RectDrag == RectDragNone)
  {
    return;
  }

  double world[4];
  vtkInteractorObserver::ComputeDisplayToWorld(ren, displayX, displayY, this->RectDragDepth, world);

  double center[3];
  double normal[3];
  double width = this->RectDragStartWidth;
  double height = this->RectDragStartHeight;
  std::copy(this->RectDragCenterStart, this->RectDragCenterStart + 3, center);
  std::copy(this->RectNormal, this->RectNormal + 3, normal);

  if (this->RectDrag == RectDragCenter)
  {
    center[0] = world[0] + this->RectDragGrabOffset[0];
    center[1] = world[1] + this->RectDragGrabOffset[1];
    center[2] = world[2] + this->RectDragGrabOffset[2];
  }
  else if (this->RectDrag == RectDragNormal)
  {
    // Incremental rotation, same scheme as vtkImplicitPlaneRepresentation / vtkPlaneWidget:
    //   axis  = viewPlaneNormal x mouseMotion        (perpendicular to both)
    //   theta = 360 * |motion_pixels| / viewport_diagonal
    // so a small mouse move yields a small angle and the arrow tracks the cursor. Mapping the
    // absolute cursor position onto a direction (the previous version) is discontinuous at the
    // center and made the plane flip on the slightest move.
    vtkCamera* camera = ren->GetActiveCamera();
    double vpn[3] = { 0.0, 0.0, 1.0 };
    if (camera)
    {
      camera->GetViewPlaneNormal(vpn);
    }

    double prevWorld[4];
    double currentWorld[4];
    vtkInteractorObserver::ComputeDisplayToWorld(
      ren, this->RectDragLastDisplay[0], this->RectDragLastDisplay[1], this->RectDragDepth,
      prevWorld);
    vtkInteractorObserver::ComputeDisplayToWorld(
      ren, displayX, displayY, this->RectDragDepth, currentWorld);

    const double motion[3] = { currentWorld[0] - prevWorld[0], currentWorld[1] - prevWorld[1],
      currentWorld[2] - prevWorld[2] };
    double axis[3];
    vtkMath::Cross(vpn, motion, axis);
    const int* size = ren->GetSize();
    const double dx = displayX - this->RectDragLastDisplay[0];
    const double dy = displayY - this->RectDragLastDisplay[1];

    if (size && size[0] > 0 && size[1] > 0 && vtkMath::Normalize(axis) > 0.0)
    {
      const double l2 = dx * dx + dy * dy;
      const double diag2 = static_cast<double>(size[0]) * size[0] +
        static_cast<double>(size[1]) * size[1];
      // A full viewport-diagonal drag sweeps this many degrees. VTK uses 360; a smaller value
      // feels calmer on a trackpad where a few pixels of jitter are easy to produce.
      constexpr double kDegreesPerDiagonal = 240.0;
      const double theta = kDegreesPerDiagonal * std::sqrt(l2 / diag2);

      vtkNew<vtkTransform> rotation;
      rotation->Identity();
      rotation->RotateWXYZ(theta, axis);
      double newNormal[3];
      rotation->TransformNormal(this->RectNormal, newNormal);
      if (vtkMath::Normalize(newNormal) > 0.0)
      {
        std::copy(newNormal, newNormal + 3, normal);
      }
    }

    this->RectDragLastDisplay[0] = static_cast<double>(displayX);
    this->RectDragLastDisplay[1] = static_cast<double>(displayY);
  }
  else
  {
    // Corner drag: project the cursor offset from the center onto the two in-plane axes. The
    // center stays fixed, so width / height grow or shrink symmetrically.
    const double offset[3] = { world[0] - center[0], world[1] - center[1], world[2] - center[2] };
    width = 2.0 * std::fabs(vtkMath::Dot(offset, this->RectAxisU));
    height = 2.0 * std::fabs(vtkMath::Dot(offset, this->RectAxisV));
    const double minSize = std::max(1e-9, this->worldLengthForPixels(center, 2.0));
    width = std::max(width, minSize);
    height = std::max(height, minSize);
  }

  this->UpdatingFromWidget = true;
  SetVec3(this->RectangleCenter, center);
  SetVec3(this->RectangleNormal, normal);
  if (this->RectangleWidth)
  {
    this->RectangleWidth->setValue(width);
  }
  if (this->RectangleHeight)
  {
    this->RectangleHeight->setValue(height);
  }
  this->UpdatingFromWidget = false;

  this->updateRectangleGeometry();
  this->renderSeedView();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::endRectangleDrag()
{
  const RectangleDragMode mode = this->RectDrag;
  this->RectDrag = RectDragNone;
  this->Interacting = false;

  double center[3];
  double normal[3];
  double width = 0.0;
  double height = 0.0;
  this->readRectangleFromControls(center, normal, width, height);

  bool snapped = false;
  // Only the center snaps, and only when it actually moved (or was the handle being dragged).
  const bool centerMoved = !NearlySamePoint(center, this->RectDragCenterStart);
  if (this->RectangleSnap && this->RectangleSnap->isChecked() && mode == RectDragCenter &&
    centerMoved)
  {
    double points[3] = { center[0], center[1], center[2] };
    const int enabled = 1;
    if (this->snapPoints(points, 1, &enabled))
    {
      center[0] = points[0];
      center[1] = points[1];
      center[2] = points[2];
      snapped = true;
    }
  }

  this->UpdatingFromWidget = true;
  SetVec3(this->RectangleCenter, center);
  this->UpdatingFromWidget = false;
  this->updateRectangleGeometry();
  this->updatePickCenterFromWidget();
  this->renderSeedView();

  const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
  const QString snapLabel = this->SnapMode == pqSHYXStreamlineSeedWidget::SnapScreenRay
    ? tr("Center snapped to the %1 surface under the cursor").arg(this->snapTargetLabel())
    : tr("Center snapped to the nearest vertex of %1 in 3D").arg(this->snapTargetLabel());
  if (snapped)
  {
    this->setStatus(autoApply ? tr("%1. Applying streamlines.").arg(snapLabel)
                              : tr("%1. Click Apply to refresh.").arg(snapLabel));
  }
  else
  {
    this->setStatus(autoApply ? tr("Rectangle updated. Applying streamlines.")
                              : tr("Rectangle updated. Click Apply to refresh."));
  }

  Q_EMIT this->changeAvailable();
  Q_EMIT this->changeFinished();
  this->applyOnRelease();
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::setPickCenter(const double p[3])
{
  pqView* view = this->view();
  if (!view)
  {
    view = pqActiveObjects::instance().activeView();
  }
  auto* renderView = qobject_cast<pqRenderView*>(view);
  if (!renderView || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
  {
    return;
  }
  double center[3] = { p[0], p[1], p[2] };
  renderView->setCenterOfRotation(center);
}

//-----------------------------------------------------------------------------
void pqSHYXStreamlineSeedWidget::updatePickCenterFromWidget()
{
  const int shape = this->currentShape();

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

    double target[3];
    if (this->MovedLineEndPoint == 0)
    {
      std::copy(p1, p1 + 3, target);
    }
    else if (this->MovedLineEndPoint == 1)
    {
      std::copy(p2, p2 + 3, target);
    }
    else
    {
      // Whole-line drag (middle handle): the line has no single moved end point, so use its
      // midpoint, which is where the middle handle sits.
      target[0] = 0.5 * (p1[0] + p2[0]);
      target[1] = 0.5 * (p1[1] + p2[1]);
      target[2] = 0.5 * (p1[2] + p2[2]);
    }
    this->setPickCenter(target);
  }
  else if (shape == 1 && this->SphereWidget)
  {
    if (auto* rep = vtkSphereRepresentation::SafeDownCast(this->SphereWidget->GetRepresentation()))
    {
      double center[3];
      rep->GetCenter(center);
      this->setPickCenter(center);
    }
  }
  else if (shape == 2 && this->RectCenterSource)
  {
    double center[3];
    this->RectCenterSource->GetCenter(center);
    this->setPickCenter(center);
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
  else if (shape == 2 && this->RectCenterSource)
  {
    // The rectangle has no VTK widget: it reports through dragRectangle()/endRectangleDrag()
    // instead of the widget Start/Interaction/End events, so nothing to sync here.
  }

  if (release)
  {
    const bool autoApply = this->AutoApply && this->AutoApply->isChecked();
    const QString snapLabel = this->SnapMode == pqSHYXStreamlineSeedWidget::SnapScreenRay
      ? tr("Snapped to the %1 surface under the cursor").arg(this->snapTargetLabel())
      : tr("Snapped to the nearest vertex of %1 in 3D").arg(this->snapTargetLabel());
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
