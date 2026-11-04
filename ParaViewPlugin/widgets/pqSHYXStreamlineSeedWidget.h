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
class vtkConeSource;
class vtkDataSet;
class vtkDataSetSurfaceFilter;
class vtkEventQtSlotConnect;
class vtkLineSource;
class vtkLineWidget2;
class vtkActor;
class vtkPlaneSource;
class vtkPolyData;
class vtkPolyDataMapper;
class vtkRenderer;
class vtkRenderWindowInteractor;
class vtkSMProxy;
class vtkSphereSource;
class vtkSphereWidget2;
class vtkStaticCellLocator;
class vtkStaticPointLocator;
class vtkSMPropertyGroup;

/**
 * Panel widget for SHYX Interactive Stream Tracer.
 *
 * Hosts one of three client-side VTK 3D widgets chosen by the SeedShape property:
 *  - Line: vtkLineWidget2; each endpoint can snap to the nearest snap-surface vertex on release.
 *  - Sphere: vtkSphereWidget2 with middle-button radius scaling; center snaps on release.
 *  - Rectangle: a hand-rolled plane editor; center snaps on release and Width / Height / Normal
 *    stay put.
 *
 * The snap surface is the dataset connected to the optional SnapSurface input (the "Snap target"
 * dropdown, any pipeline node), falling back to the Input flow field when the dropdown is
 * "(none)". A volume dataset is reduced with vtkDataSetSurfaceFilter. Release optionally Applies so
 * the streamlines refresh immediately.
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
  /** SnapSurface (Snap target dropdown) changed: drop the cached surface/locators. */
  void onSnapTargetChanged();

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
  void destroyRectangleWidget();
  void setRectangleEnabled(bool on);  bool syncWidgetFromProperties();
  void placeWidgetInInputBounds();
  void renderSeedView();
  void onWidgetEvent(unsigned long eid);
  void syncControlsFromWidget(bool release);
  void applyOnRelease();
  void setStatus(const QString& text);

  vtkRenderWindowInteractor* interactor() const;
  /** Flow field on input port 0 (bounds, fallback sizing). Not the snap surface. */
  vtkDataSet* inputDataSet() const;
  /** Dataset connected to the optional SnapSurface property, or inputDataSet() when (none). */
  vtkDataSet* snapDataSet() const;
  /** Node picked in the SnapSurface dropdown (pending/unchecked value first), or nullptr. */
  vtkSMProxy* snapTargetProxy() const;
  /** Display name of the current snap target ("(none)" -> the Input node name). */
  QString snapTargetLabel() const;

  bool ensureSnapper();
  bool snapPoints(double* points, int count, const int* enabled);
  bool snapPoint(int mode, const double in[3], double out[3]);
  vtkRenderer* renderer() const;
  bool rayPickAtDisplay(double displayX, double displayY, double hit[3]);
  bool rayPickSurfacePoint(const double world[3], double hit[3]);
  bool surfacePointFromDisplay(double displayX, double displayY, double out[3]);
  double worldLengthForPixels(const double at[3], double pixels) const;
  void cameraRightUp(double right[3], double up[3]) const;
  /** Move the RenderView's center of rotation (ParaView's pick center) to p. */
  void setPickCenter(const double p[3]);
  /** After an interaction ends, park the pick center on whatever the user just moved. */
  void updatePickCenterFromWidget();

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
  QDoubleSpinBox* RectangleCenter[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* RectangleNormal[3] = { nullptr, nullptr, nullptr };
  QDoubleSpinBox* RectangleWidth = nullptr;
  QDoubleSpinBox* RectangleHeight = nullptr;
  QSpinBox* RectangleResX = nullptr;
  QSpinBox* RectangleResY = nullptr;
  QCheckBox* RectangleSnap = nullptr;
  QLabel* RectangleOrientLabel = nullptr;

  QLabel* Status = nullptr;

  vtkSmartPointer<vtkLineWidget2> LineWidget;
  vtkSmartPointer<vtkSphereWidget2> SphereWidget;
  vtkSmartPointer<vtkCallbackCommand> WidgetObserver;
  vtkRenderWindowInteractor* Interactor = nullptr;

  // Rectangle seed: a hand-rolled editor instead of vtkPlaneWidget, because the model is
  // center + normal + width/height (vtkPlaneWidget resizes around a pinned corner and snaps on
  // its corner origin). Actors: the quad, a center handle, one corner handle, and a normal
  // handle (cone + stick) that rotates the plane.
  vtkSmartPointer<vtkPlaneSource> RectSource;
  vtkSmartPointer<vtkPolyDataMapper> RectMapper;
  vtkSmartPointer<vtkActor> RectActor;
  vtkSmartPointer<vtkSphereSource> RectCenterSource;
  vtkSmartPointer<vtkPolyDataMapper> RectCenterMapper;
  vtkSmartPointer<vtkActor> RectCenterActor;
  vtkSmartPointer<vtkSphereSource> RectCornerSource;
  vtkSmartPointer<vtkPolyDataMapper> RectCornerMapper;
  vtkSmartPointer<vtkActor> RectCornerActor;
  vtkSmartPointer<vtkConeSource> RectNormalSource;
  vtkSmartPointer<vtkPolyDataMapper> RectNormalMapper;
  vtkSmartPointer<vtkActor> RectNormalActor;
  vtkSmartPointer<vtkConeSource> RectNormalSource2;
  vtkSmartPointer<vtkPolyDataMapper> RectNormalMapper2;
  vtkSmartPointer<vtkActor> RectNormalActor2;
  vtkSmartPointer<vtkLineSource> RectNormalLineSource;
  vtkSmartPointer<vtkPolyDataMapper> RectNormalLineMapper;
  vtkSmartPointer<vtkActor> RectNormalLineActor;
  vtkSmartPointer<vtkCallbackCommand> RectInteractorObserver;
  bool RectEnabled = false;

  enum RectangleDragMode
  {
    RectDragNone = 0,
    RectDragCenter,
    RectDragCorner,
    RectDragNormal
  };
  RectangleDragMode RectDrag = RectDragNone;
  double RectDragCenterStart[3] = { 0.0, 0.0, 0.0 };
  double RectDragGrabOffset[3] = { 0.0, 0.0, 0.0 };
  double RectDragDepth = 0.5;
  double RectDragStartWidth = 0.0;
  double RectDragStartHeight = 0.0;
  /** Last mouse position (display) used for the incremental normal rotation. */
  double RectDragLastDisplay[2] = { 0.0, 0.0 };
  // Display-space grab radius for the handles, in pixels.
  int RectHandleTolerance = 12;
  // Length of the normal stick / cone in world units, recomputed per frame from screen pixels.
  double RectNormalLength = 1.0;
  // In-plane axes derived from the normal, cached for dragging and for the corner handle.
  double RectAxisU[3] = { 1.0, 0.0, 0.0 };
  double RectAxisV[3] = { 0.0, 1.0, 0.0 };
  double RectNormal[3] = { 0.0, 0.0, 1.0 };

  /** Rebuild the quad / handle geometry from the panel values (center, normal, W, H). */
  void updateRectangleGeometry();
  /** Rectangle interaction lives on the interactor rather than a VTK widget. */
  static void ProcessRectangleEvents(
    vtkObject* caller, unsigned long eid, void* clientdata, void* calldata);
  void onRectangleInteractorEvent(unsigned long eid);
  bool beginRectangleDrag(int displayX, int displayY);
  void dragRectangle(int displayX, int displayY);
  void endRectangleDrag();
  /**
   * Resolve which handle the cursor is over, in display space, with a priority order of
   * center -> corner -> normal cones. Chosen over vtkCellPicker ordering because the cones sit on
   * the same line as the center handle: a world-unit pick tolerance made a center drag start a
   * normal rotation instead. Returns RectangleDragMode (RectDragNone when nothing is close) and
   * the handle's display depth in depthOut.
   */
  RectangleDragMode pickRectangleHandle(
    vtkRenderer* ren, int displayX, int displayY, double& depthOut);
  /** World point -> its display coordinates. False when the renderer cannot project it. */
  bool displayPointForWorld(vtkRenderer* ren, const double world[3], double out[3]) const;
  void readRectangleFromControls(double center[3], double normal[3], double& width,
    double& height) const;

  // Snapping cache: rebuilt when the snap dataset (Input or the Snap target node) changes.
  vtkSmartPointer<vtkDataSetSurfaceFilter> SurfaceFilter;
  vtkSmartPointer<vtkPolyData> SnapSurface;
  vtkSmartPointer<vtkStaticPointLocator> SnapLocator;
  vtkSmartPointer<vtkStaticCellLocator> SnapCellLocator;
  vtkDataSet* SnapInput = nullptr;
  vtkMTimeType SnapMTime = 0;
  /** Watches the SnapSurface property so a dropdown change drops the cache immediately. */
  vtkSmartPointer<vtkEventQtSlotConnect> SnapTargetConnect;

  int SnapMode = SnapScreenRay;

  double StartLineP1[3] = { 0.0, 0.0, 0.0 };
  double StartLineP2[3] = { 0.0, 0.0, 0.0 };
  double StartSphereCenter[3] = { 0.0, 0.0, 0.0 };
  double StartRectangleOrigin[3] = { 0.0, 0.0, 0.0 };

  /** Which line endpoint the current drag moves: 0 = Point 1, 1 = Point 2, -1 = whole line. */
  int MovedLineEndPoint = -1;

  bool WidgetsVisible = false;
  bool Interacting = false;
  bool UpdatingFromWidget = false;
};

#endif
