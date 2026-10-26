#ifndef pqSHYXStreamlineSeedWidget_h
#define pqSHYXStreamlineSeedWidget_h

#include "pqPropertyWidget.h"

#include "vtkSmartPointer.h"
#include "vtkType.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class pqView;
class vtkCallbackCommand;
class vtkDataSet;
class vtkDataSetSurfaceFilter;
class vtkLineWidget2;
class vtkPlaneWidget;
class vtkPolyData;
class vtkRenderer;
class vtkRenderWindowInteractor;
class vtkSphereWidget2;
class vtkStaticCellLocator;
class vtkStaticPointLocator;
class vtkSMPropertyGroup;

/**
 * Panel widget for SHYX Interactive Stream Tracer.
 *
 * Hosts one of three client-side VTK 3D widgets chosen by the SeedShape property:
 *  - Line: vtkLineWidget2; each endpoint can snap to the nearest input-surface vertex on release.
 *  - Sphere: vtkSphereWidget2 with middle-button radius scaling; center snaps on release.
 *  - Rectangle: vtkPlaneWidget; origin snaps on release and Point1/Point2 translate with it.
 *
 * The input is a single dataset. Snapping targets every vertex of its surface (a volume input is
 * converted with vtkDataSetSurfaceFilter), so vessel wall and stent vertices in the same dataset
 * are both valid. Release optionally Applies so the streamlines refresh immediately.
 */
class pqSHYXStreamlineSeedWidget : public pqPropertyWidget
{
  Q_OBJECT
  typedef pqPropertyWidget Superclass;

public:
  pqSHYXStreamlineSeedWidget(
    vtkSMProxy* proxy, vtkSMPropertyGroup* group, QWidget* parent = nullptr);
  ~pqSHYXStreamlineSeedWidget() override;

  void select() override;
  void deselect() override;
  void setView(pqView* view) override;
  void reset() override;

private Q_SLOTS:
  void onShapeChanged();
  void onShowInteractiveToggled();
  void onSeedPropertyEdited();
  void onActiveViewChanged();
  void onPlaceFromBounds();
  void onSphereSamplingChanged();

private:
  Q_DISABLE_COPY(pqSHYXStreamlineSeedWidget)

  static void ProcessWidgetEvents(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);

  void buildUi();
  void linkProperties(vtkSMPropertyGroup* group);
  void connectSeedSignals();
  void updateShapeGroups();
  int currentShape() const;
  bool sphereSamplesInterior() const;
  void updateSphereSamplingUi();

  void syncWidgets();
  void destroyWidgets();
  void setWidgetsEnabled(bool on);
  void createLineWidget(vtkRenderWindowInteractor* iren);
  void createSphereWidget(vtkRenderWindowInteractor* iren);
  void createRectangleWidget(vtkRenderWindowInteractor* iren);
  bool syncWidgetFromProperties();
  void placeWidgetInInputBounds();
  void renderSeedView();
  void onWidgetEvent(unsigned long eid);
  void syncControlsFromWidget(bool release);
  void applyOnRelease();
  void setStatus(const QString& text);

  vtkRenderWindowInteractor* interactor() const;
  vtkDataSet* inputDataSet() const;

  bool ensureSnapper();
  bool snapPoints(double* points, int count, const int* enabled);
  bool snapPoint(int mode, const double in[3], double out[3]);
  vtkRenderer* renderer() const;
  bool rayPickSurfacePoint(const double world[3], double hit[3]);

  /**
   * How a released handle resolves its target vertex.
   */
  enum SnapTargetMode
  {
    /** Screen ray: first surface hit under the cursor, then its nearest vertex. */
    SnapScreenRay = 0,
    /** Pure 3D nearest vertex, independent of the camera. */
    SnapNearestVertex = 1
  };

  QComboBox* ShapeCombo = nullptr;
  QComboBox* SnapModeCombo = nullptr;
  QCheckBox* ShowInteractive = nullptr;
  QCheckBox* AutoApply = nullptr;
  QPushButton* PlaceFromBounds = nullptr;

  QGroupBox* LineGroup = nullptr;
  QDoubleSpinBox* LineP1[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* LineP2[3] = { nullptr, nullptr, nullptr };
  QSpinBox* LineResolution = nullptr;
  QCheckBox* LineSnap1 = nullptr;
  QCheckBox* LineSnap2 = nullptr;

  QGroupBox* SphereGroup = nullptr;
  QDoubleSpinBox* SphereCenter[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* SphereRadius = nullptr;
  QComboBox* SphereSampling = nullptr;
  QSpinBox* SphereInteriorCount = nullptr;
  QSpinBox* SphereRandomSeed = nullptr;
  QLabel* SphereCountLabel = nullptr;
  QSpinBox* SphereTheta = nullptr;
  QSpinBox* SpherePhi = nullptr;
  QCheckBox* SphereSnap = nullptr;

  QGroupBox* RectangleGroup = nullptr;
  QDoubleSpinBox* RectangleOrigin[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* RectangleP1[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* RectangleP2[3] = { nullptr, nullptr, nullptr };
  QSpinBox* RectangleResX = nullptr;
  QSpinBox* RectangleResY = nullptr;
  QCheckBox* RectangleSnap = nullptr;

  QLabel* Status = nullptr;

  vtkSmartPointer<vtkLineWidget2> LineWidget;
  vtkSmartPointer<vtkSphereWidget2> SphereWidget;
  vtkSmartPointer<vtkPlaneWidget> PlaneWidget;
  vtkSmartPointer<vtkCallbackCommand> WidgetObserver;
  vtkRenderWindowInteractor* Interactor = nullptr;

  // Snapping cache: rebuilt when the client-side input dataset changes.
  vtkSmartPointer<vtkDataSetSurfaceFilter> SurfaceFilter;
  vtkSmartPointer<vtkPolyData> SnapSurface;
  vtkSmartPointer<vtkStaticPointLocator> SnapLocator;
  vtkSmartPointer<vtkStaticCellLocator> SnapCellLocator;
  vtkDataSet* SnapInput = nullptr;
  vtkMTimeType SnapMTime = 0;

  int SnapMode = SnapScreenRay;

  double StartLineP1[3] = { 0.0, 0.0, 0.0 };
  double StartLineP2[3] = { 0.0, 0.0, 0.0 };
  double StartSphereCenter[3] = { 0.0, 0.0, 0.0 };
  double StartRectangleOrigin[3] = { 0.0, 0.0, 0.0 };

  bool WidgetsVisible = false;
  bool Interacting = false;
  bool UpdatingFromWidget = false;
};

#endif
