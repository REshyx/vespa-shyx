vtk_module_find_modules(vespa_vtk_modules "${CMAKE_SOURCE_DIR}/vespa")

if (${VESPA_ALPHA_WRAPPING})
  set(ALPHA_WRAPPING_SERVER_MANAGER_XML "VESPAAlphaWrapping.xml")
  set(SHYX_SELECTION_FILL_ALPHA_REUNION_XML "SHYXSelectionFillAlphaReunionFilter.xml")
endif()

if (${VESPA_MESH_SMOOTHING})
  set(MESH_SMOOTHING_SERVER_MANAGER_XML "VESPAMeshSmoothingFilter.xml")
endif()

set(ADAPTIVE_REMESHING_SERVER_MANAGER_XML "")
if (${VESPA_ADAPTIVE_REMESHING})
  list(APPEND ADAPTIVE_REMESHING_SERVER_MANAGER_XML
    "SHYXAdaptiveIsotropicRemesher.xml"
    "SHYXRemeshWithEndpoint.xml")
endif()

if(VESPA_USE_VMTK)
  set(VMTK_CENTERLINES_SERVER_MANAGER_XML
    "SHYXVmtkPolyDataCenterlines.xml"
    "SHYXVmtkOpeningCenterlines.xml")
endif()

option(VESPA_SHOW_CGAL_LICENSE_ON_LOAD "Show CGAL License when loading plugin" OFF)
mark_as_advanced(VESPA_SHOW_CGAL_LICENSE_ON_LOAD)
if (${VESPA_SHOW_CGAL_LICENSE_ON_LOAD})
  set(VESPA_CGAL_LICENSE "${CMAKE_SOURCE_DIR}/Data/Resources/CGAL_LICENSE.GPL")
endif()

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "array_curve_mapper_panel"
  CLASS_NAME pqArrayCurveMapperPanel
  INTERFACES acm_interfaces
  SOURCES acm_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_obb_interactive_box"
  CLASS_NAME pqVESPAOBBInteractiveBoxWidget
  INTERFACES vespa_obb_box_interfaces
  SOURCES vespa_obb_box_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_endclipper_plane_handles"
  CLASS_NAME pqVESPAEndClipperPlaneHandlesWidget
  INTERFACES vespa_endclipper_plane_interfaces
  SOURCES vespa_endclipper_plane_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_selection_plane_clipper"
  CLASS_NAME pqVESPASelectionPlaneClipperWidget
  INTERFACES vespa_selection_plane_clipper_interfaces
  SOURCES vespa_selection_plane_clipper_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_shyx_vascular_stent_cylinder"
  CLASS_NAME pqSHYXVascularStentCylinderWidget
  INTERFACES vespa_stent_cylinder_interfaces
  SOURCES vespa_stent_cylinder_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_shyx_enhanced_ruler"
  CLASS_NAME pqSHYXEnhancedRulerWidget
  INTERFACES vespa_enhanced_ruler_interfaces
  SOURCES vespa_enhanced_ruler_sources)
paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "vespa_shyx_endpoint_stent"
  CLASS_NAME pqSHYXEndpointStentWidget
  INTERFACES vespa_endpoint_stent_interfaces
  SOURCES vespa_endpoint_stent_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "shyx_partitioned_block_names"
  CLASS_NAME pqSHYXPartitionedBlockNamesWidget
  INTERFACES shyx_partitioned_block_names_interfaces
  SOURCES shyx_partitioned_block_names_sources)
list(APPEND vespa_stent_cylinder_sources
  pqSHYXVascularStentCylinderWidget.cxx
  pqSHYXVascularStentCylinderWidget.h)
list(APPEND vespa_enhanced_ruler_sources
  pqSHYXEnhancedRulerWidget.cxx
  pqSHYXEnhancedRulerWidget.h)
list(APPEND vespa_endpoint_stent_sources
  pqSHYXEndpointStentWidget.cxx
  pqSHYXEndpointStentWidget.h)
list(APPEND shyx_partitioned_block_names_sources
  pqSHYXPartitionedBlockNamesWidget.cxx
  pqSHYXPartitionedBlockNamesWidget.h)

set(shyx_openings_table_interfaces)
set(shyx_openings_table_sources)
if (VESPA_USE_VMTK)
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "shyx_openings_table"
    CLASS_NAME pqSHYXOpeningTable
    INTERFACES shyx_openings_table_interfaces
    SOURCES shyx_openings_table_sources)
  list(APPEND shyx_openings_table_sources
    pqSHYXOpeningTable.cxx
    pqSHYXOpeningTable.h)
endif()

set(vespa_plugin_qt_auto_interfaces)
set(vespa_plugin_qt_auto_sources)
if (PARAVIEW_USE_QT)
  paraview_plugin_add_auto_start(
    CLASS_NAME "pqPulseGlyphAnimationManager"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES pulse_glyph_autostart_interfaces
    SOURCES pulse_glyph_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${pulse_glyph_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${pulse_glyph_autostart_sources}
    ${CMAKE_SOURCE_DIR}/vespa/shyx/PulseGlyphRepresentation/pqPulseGlyphAnimationManager.cxx
    ${CMAKE_SOURCE_DIR}/vespa/shyx/PulseGlyphRepresentation/pqPulseGlyphAnimationManager.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqAnimatedStreamlineAnimationManager"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES animated_streamline_autostart_interfaces
    SOURCES animated_streamline_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${animated_streamline_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${animated_streamline_autostart_sources}
    ${CMAKE_SOURCE_DIR}/vespa/shyx/AnimatedStreamlineRepresentation/pqAnimatedStreamlineAnimationManager.cxx
    ${CMAKE_SOURCE_DIR}/vespa/shyx/AnimatedStreamlineRepresentation/pqAnimatedStreamlineAnimationManager.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqSHYXSphereSelectionAutoStart"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES sphere_selection_autostart_interfaces
    SOURCES sphere_selection_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${sphere_selection_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${sphere_selection_autostart_sources}
    pqSHYXSphereSelectionAutoStart.cxx
    pqSHYXSphereSelectionAutoStart.h
    pqSHYXSphereSelectionViewFrameActions.cxx
    pqSHYXSphereSelectionViewFrameActions.h
    pqSHYXSphereSelectionController.cxx
    pqSHYXSphereSelectionController.h)
endif ()

paraview_add_plugin(VESPAPlugin
  VERSION "${PROJECT_VERSION}"
  EULA ${VESPA_CGAL_LICENSE}
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  MODULES ${vtkcgal_provided_modules}
  UI_RESOURCES VESPAIcons.qrc
  INITIALIZERS vespa_qrc_init
  EXTRA_INCLUDES "vespa_qrc_init.h"
  SERVER_MANAGER_XML
    "VESPAFilters.xml"
    "SHYXSurfaceTipExtractor.xml"
    "SHYXSurfaceToVolumeMesh.xml"
    "SHYXTetGen.xml"
    "SHYXTetGenMeshOptimize.xml"
    "SHYXTetMeshRegionPartition.xml"
    "SHYXFTLEFilter.xml"
    "SHYXFlipSelectedCellsWinding.xml"
    "SHYXGeodesicDistance.xml"
    "SHYXEnhancedRuler.xml"
    "SHYXHoleFillFilter.xml"
    "SHYXRadiusNeighborCount.xml"
    "SHYXRepairDegeneracies.xml"
    "SHYXSelectionExtrude.xml"
    "SHYXSelectionPlaneClipper.xml"
    "SHYXSelectionOBBSubtract.xml"
    "SHYXMeshChecker.xml"
    "SHYXMinimumOBB.xml"
    "SHYXOppositeFaceThickness.xml"
    "SHYXPointExtrude.xml"
    "SHYXDisconnectedRegionFuse.xml"
    "SHYXEdgeCollapse.xml"
    "SHYXDataSetToPartitionedCollection.xml"
    "SHYXPartitionedCollectionWslSimulation.xml"
    "SHYXDeleteSelectedCells.xml"
    "SHYXClebschMapFilter.xml"
    "SHYXConvexHullFilter.xml"
    "SHYXVortexCriteria.xml"
    "SHYXVortexCoreTest.xml"
    "SHYXVascularStentPlacement.xml"
    "SHYXEndpointStentPlacement.xml"
    "SHYXImplicitCylinderWidgetRepresentation.xml"
    "SHYXEndpointStentWidgetRepresentation.xml"
    "SHYXVectorFieldTopology.xml"
    "SHYXArrayCurveMapper.xml"
    "SHYXAutorefineSelfIntersectionFilter.xml"
    "SHYXBooleanOperationFilter.xml"
    "SHYXShapeSmoothing.xml"
    "PulseGlyphRepresentation.xml"
    "AnimatedStreamlineRepresentation.xml"
    "PointLabelRepresentation.xml"
     ${ALPHA_WRAPPING_SERVER_MANAGER_XML}
     ${SHYX_SELECTION_FILL_ALPHA_REUNION_XML}
     ${MESH_SMOOTHING_SERVER_MANAGER_XML}
     ${ADAPTIVE_REMESHING_SERVER_MANAGER_XML}
     ${VMTK_CENTERLINES_SERVER_MANAGER_XML}
  UI_INTERFACES ${acm_interfaces} ${vespa_obb_box_interfaces} ${vespa_endclipper_plane_interfaces} ${vespa_selection_plane_clipper_interfaces} ${vespa_stent_cylinder_interfaces} ${vespa_enhanced_ruler_interfaces} ${vespa_endpoint_stent_interfaces} ${shyx_partitioned_block_names_interfaces} ${shyx_openings_table_interfaces} ${vespa_plugin_qt_auto_interfaces}
  SOURCES ${acm_sources} ${vespa_obb_box_sources} ${vespa_endclipper_plane_sources} ${vespa_selection_plane_clipper_sources} ${vespa_stent_cylinder_sources} ${vespa_enhanced_ruler_sources} ${vespa_endpoint_stent_sources} ${shyx_partitioned_block_names_sources} ${shyx_openings_table_sources}
    TransferCurve/pqArrayCurveMapperPanel.cxx
    TransferCurve/pqArrayCurveMapperPanel.h
    TransferCurve/pqSHYXTransferCurveWidget.cxx
    TransferCurve/pqSHYXTransferCurveWidget.h
    TransferCurve/vtkSHYXTransferChartXY.cxx
    TransferCurve/vtkSHYXTransferChartXY.h
    TransferCurve/vtkSHYXGrayHistogramPiecewiseFunctionItem.cxx
    TransferCurve/vtkSHYXGrayHistogramPiecewiseFunctionItem.h
    TransferCurve/vtkSHYXCompositeControlPointsItem.cxx
    TransferCurve/vtkSHYXCompositeControlPointsItem.h
    TransferCurve/vtkSHYXInputRangeHandlesItem.cxx
    TransferCurve/vtkSHYXInputRangeHandlesItem.h
    TransferCurve/vtkSHYXOutputRangeHandlesItem.cxx
    TransferCurve/vtkSHYXOutputRangeHandlesItem.h
    pqVESPAOBBInteractiveBoxWidget.cxx
    pqVESPAOBBInteractiveBoxWidget.h
    pqVESPAEndClipperPlaneHandlesWidget.cxx
    pqVESPAEndClipperPlaneHandlesWidget.h
    pqVESPASelectionPlaneClipperWidget.cxx
    pqVESPASelectionPlaneClipperWidget.h
    Resources/UI/pqCylinderPropertyWidget.ui
    vespa_qrc_init.cxx
    vespa_qrc_init.h
    ${vespa_plugin_qt_auto_sources}
)

# pqSHYXVascularStentCylinderWidget includes ui_pqCylinderPropertyWidget.h; the .ui lives under
# Resources/UI/. ParaView plugin targets may not enable AUTOUIC or search that path by default.
if (PARAVIEW_USE_QT)
  set_target_properties(VESPAPlugin PROPERTIES AUTOUIC ON)
  set_property(TARGET VESPAPlugin APPEND PROPERTY
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/UI")
  target_include_directories(VESPAPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/TransferCurve")
endif ()

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP
    VTK::FiltersCore
    VTK::FiltersParallel)

if (PARAVIEW_USE_QT)
  target_link_libraries(VESPAPlugin
    PRIVATE
      ParaView::RemotingViews
      ParaView::pqApplicationComponents
      VTK::ChartsCore
      VTK::InteractionWidgets
      vtkSHYXSelectionPlaneClipper
      vtkSHYXVascularStentPlacement
      vtkSHYXEndpointStentPlacement
      vtkSHYXEnhancedRuler)
endif ()

if (BUILD_TESTING AND BUILD_SHARED_LIBS)
  add_subdirectory(Testing)
endif()
