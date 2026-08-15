# Upstream VESPA proxies live in VESPAFilters.xml (CGAL). SHYX filters each have SHYX*.xml.
# Do not register CGAL-backed proxies when those modules were not built.
set(VESPA_UPSTREAM_SERVER_MANAGER_XML "")
set(SHYX_CGAL_SERVER_MANAGER_XML "")
if (VESPA_BUILT_WITH_CGAL)
  set(VESPA_UPSTREAM_SERVER_MANAGER_XML "VESPAFilters.xml")
  set(SHYX_CGAL_SERVER_MANAGER_XML
    "SHYXBooleanOperationFilter.xml"
    "SHYXEdgeCollapse.xml"
    "SHYXHoleFillFilter.xml"
    "SHYXMeshChecker.xml"
    "SHYXMinimumOBB.xml"
    "SHYXRepairDegeneracies.xml"
    "SHYXShapeSmoothing.xml"
    "SHYXSkeletonExtraction.xml"
    "SHYXSurfaceToVolumeMesh.xml"
    "SHYXVesselEndClipper.xml")
endif()

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

set(SNAPPYHEXMESH_SERVER_MANAGER_XML "")
if(VESPA_USE_SNAPPYHEXMESH)
  set(SNAPPYHEXMESH_SERVER_MANAGER_XML "SHYXSnappyHexMesh.xml")
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
  TYPE "shyx_obb_interactive_box"
  CLASS_NAME pqSHYXOBBInteractiveBoxWidget
  INTERFACES vespa_obb_box_interfaces
  SOURCES vespa_obb_box_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "shyx_endclipper_plane_handles"
  CLASS_NAME pqSHYXEndClipperPlaneHandlesWidget
  INTERFACES vespa_endclipper_plane_interfaces
  SOURCES vespa_endclipper_plane_sources)

paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "shyx_selection_plane_clipper"
  CLASS_NAME pqSHYXSelectionPlaneClipperWidget
  INTERFACES shyx_selection_plane_clipper_interfaces
  SOURCES shyx_selection_plane_clipper_sources)

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
paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "shyx_boundary_assignment_info"
  CLASS_NAME pqSHYXBoundaryAssignmentInfoWidget
  INTERFACES shyx_boundary_assignment_info_interfaces
  SOURCES shyx_boundary_assignment_info_sources)
paraview_plugin_add_property_widget(
  KIND GROUP_WIDGET
  TYPE "shyx_remesh_uncapped_histogram"
  CLASS_NAME pqSHYXRemeshUncappedHistogramPanel
  INTERFACES shyx_remesh_uncapped_histogram_interfaces
  SOURCES shyx_remesh_uncapped_histogram_sources)

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
list(APPEND shyx_boundary_assignment_info_sources
  pqSHYXBoundaryAssignmentInfoWidget.cxx
  pqSHYXBoundaryAssignmentInfoWidget.h)
list(APPEND shyx_remesh_uncapped_histogram_sources
  pqSHYXRemeshUncappedHistogramPanel.cxx
  pqSHYXRemeshUncappedHistogramPanel.h)

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

set(shyx_snappy_inside_points_interfaces)
set(shyx_snappy_inside_points_sources)
set(shyx_snappy_case_folder_interfaces)
set(shyx_snappy_case_folder_sources)
if (VESPA_USE_SNAPPYHEXMESH)
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "shyx_snappy_inside_points"
    CLASS_NAME pqSHYXSnappyInsidePointsWidget
    INTERFACES shyx_snappy_inside_points_interfaces
    SOURCES shyx_snappy_inside_points_sources)
  list(APPEND shyx_snappy_inside_points_sources
    pqSHYXSnappyInsidePointsWidget.cxx
    pqSHYXSnappyInsidePointsWidget.h)
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "shyx_snappy_case_folder"
    CLASS_NAME pqSHYXSnappyCaseFolderWidget
    INTERFACES shyx_snappy_case_folder_interfaces
    SOURCES shyx_snappy_case_folder_sources)
  list(APPEND shyx_snappy_case_folder_sources
    pqSHYXSnappyCaseFolderWidget.cxx
    pqSHYXSnappyCaseFolderWidget.h)
endif()

set(vespa_plugin_qt_auto_interfaces)
set(vespa_plugin_qt_auto_sources)
set(shyx_ai_assistant_interfaces)
set(shyx_ai_assistant_sources)
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
    pqPulseGlyphAnimationManager.cxx
    pqPulseGlyphAnimationManager.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqAnimatedStreamlineAnimationManager"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES animated_streamline_autostart_interfaces
    SOURCES animated_streamline_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${animated_streamline_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${animated_streamline_autostart_sources}
    pqAnimatedStreamlineAnimationManager.cxx
    pqAnimatedStreamlineAnimationManager.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqSHYXSphereSelectionAutoStart"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES sphere_selection_autostart_interfaces
    SOURCES sphere_selection_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${sphere_selection_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${sphere_selection_autostart_sources}
    SphereSelection/pqSHYXSphereSelectionAutoStart.cxx
    SphereSelection/pqSHYXSphereSelectionAutoStart.h
    SphereSelection/pqSHYXSphereSelectionViewFrameActions.cxx
    SphereSelection/pqSHYXSphereSelectionViewFrameActions.h
    SphereSelection/pqSHYXSphereSelectionController.cxx
    SphereSelection/pqSHYXSphereSelectionController.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqSHYXGrowSelectionWithSimilarAutoStart"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES grow_selection_similar_autostart_interfaces
    SOURCES grow_selection_similar_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${grow_selection_similar_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${grow_selection_similar_autostart_sources}
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarAutoStart.cxx
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarAutoStart.h
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarViewFrameActions.cxx
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarViewFrameActions.h
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarController.cxx
    GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarController.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqSHYXVascularCategoryAutoStart"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES vascular_category_autostart_interfaces
    SOURCES vascular_category_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${vascular_category_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${vascular_category_autostart_sources}
    pqSHYXVascularCategoryAutoStart.cxx
    pqSHYXVascularCategoryAutoStart.h)

  paraview_plugin_add_auto_start(
    CLASS_NAME "pqSHYXAIAssistantAutoStart"
    STARTUP onStartup
    SHUTDOWN onShutdown
    INTERFACES shyx_ai_autostart_interfaces
    SOURCES shyx_ai_autostart_sources)

  list(APPEND vespa_plugin_qt_auto_interfaces ${shyx_ai_autostart_interfaces})
  list(APPEND vespa_plugin_qt_auto_sources
    ${shyx_ai_autostart_sources}
    pqSHYXAIAssistantAutoStart.cxx
    pqSHYXAIAssistantAutoStart.h)

  paraview_plugin_add_dock_window(
    CLASS_NAME pqSHYXAIAssistantPanel
    DOCK_AREA Right
    INTERFACES shyx_ai_assistant_interfaces
    SOURCES shyx_ai_assistant_sources)
endif ()

paraview_add_plugin(VESPAPlugin
  VERSION "${PROJECT_VERSION}"
  EULA ${VESPA_CGAL_LICENSE}
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  MODULES ${vespa_provided_modules}
  UI_RESOURCES VESPAIcons.qrc
  INITIALIZERS vespa_qrc_init
  EXTRA_INCLUDES "vespa_qrc_init.h"
  SERVER_MANAGER_XML
    ${VESPA_UPSTREAM_SERVER_MANAGER_XML}
    "VESPAVascularCategory.xml"
    "SHYXArrayProbabilityPointCull.xml"
    "SHYXBidirectionalStreamlineMerge.xml"
    "SHYXDensityBasedVolumeSampler.xml"
    "SHYXPointCloudSurfaceSDF.xml"
    "SHYXSurfaceTipExtractor.xml"
    "SHYXTetGen.xml"
    "SHYXTetGenMeshOptimize.xml"
    "SHYXTetMeshRegionPartition.xml"
    "SHYXFTLEFilter.xml"
    "SHYXFlipSelectedCellsWinding.xml"
    "SHYXGeodesicDistance.xml"
    "SHYXEnhancedRuler.xml"
    "SHYXRadiusNeighborCount.xml"
    "SHYXSelectionExtrude.xml"
    "SHYXSelectionPlaneClipper.xml"
    "SHYXPointExtrude.xml"
    "SHYXDisconnectedRegionFuse.xml"
    "SHYXDataSetToPartitionedCollection.xml"
    "SHYXPartitionedCollectionBoundaryAssignment.xml"
    "SHYXPartitionedCollectionBoundaryFields.xml"
    "SHYXPartitionedCollectionWslSimulation.xml"
    "SHYXDeleteSelectedCells.xml"
    "SHYXClebschMapFilter.xml"
    "SHYXConvexHullFilter.xml"
    "SHYXVortexCriteria.xml"
    "SHYXVascularStentPlacement.xml"
    "SHYXEndpointStentPlacement.xml"
    "SHYXImplicitCylinderWidgetRepresentation.xml"
    "SHYXEndpointStentWidgetRepresentation.xml"
    "SHYXVectorFieldTopology.xml"
    "SHYXArrayCurveMapper.xml"
    "SHYXAutoStreamline.xml"
    "PulseGlyphRepresentation.xml"
    "AnimatedStreamlineRepresentation.xml"
    "PointLabelRepresentation.xml"
     ${SHYX_CGAL_SERVER_MANAGER_XML}
     ${ALPHA_WRAPPING_SERVER_MANAGER_XML}
     ${SHYX_SELECTION_FILL_ALPHA_REUNION_XML}
     ${MESH_SMOOTHING_SERVER_MANAGER_XML}
     ${ADAPTIVE_REMESHING_SERVER_MANAGER_XML}
     ${VMTK_CENTERLINES_SERVER_MANAGER_XML}
     ${SNAPPYHEXMESH_SERVER_MANAGER_XML}
  UI_INTERFACES ${acm_interfaces} ${vespa_obb_box_interfaces} ${vespa_endclipper_plane_interfaces} ${shyx_selection_plane_clipper_interfaces} ${vespa_stent_cylinder_interfaces} ${vespa_enhanced_ruler_interfaces} ${vespa_endpoint_stent_interfaces} ${shyx_partitioned_block_names_interfaces} ${shyx_boundary_assignment_info_interfaces} ${shyx_remesh_uncapped_histogram_interfaces} ${shyx_openings_table_interfaces} ${shyx_snappy_inside_points_interfaces} ${shyx_snappy_case_folder_interfaces} ${shyx_ai_assistant_interfaces} ${vespa_plugin_qt_auto_interfaces}
  SOURCES ${acm_sources} ${vespa_obb_box_sources} ${vespa_endclipper_plane_sources} ${shyx_selection_plane_clipper_sources} ${vespa_stent_cylinder_sources} ${vespa_enhanced_ruler_sources} ${vespa_endpoint_stent_sources} ${shyx_partitioned_block_names_sources} ${shyx_boundary_assignment_info_sources} ${shyx_remesh_uncapped_histogram_sources} ${shyx_openings_table_sources} ${shyx_snappy_inside_points_sources} ${shyx_snappy_case_folder_sources} ${shyx_ai_assistant_sources}
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
    pqSHYXOBBInteractiveBoxWidget.cxx
    pqSHYXOBBInteractiveBoxWidget.h
    pqSHYXEndClipperPlaneHandlesWidget.cxx
    pqSHYXEndClipperPlaneHandlesWidget.h
    pqSHYXSelectionPlaneClipperWidget.cxx
    pqSHYXSelectionPlaneClipperWidget.h
    pqSHYXAIAssistantPanel.cxx
    pqSHYXAIAssistantPanel.h
    pqSHYXAIChatView.cxx
    pqSHYXAIChatView.h
    pqSHYXAIImageAnnotator.cxx
    pqSHYXAIImageAnnotator.h
    pqSHYXAIAgentTools.cxx
    pqSHYXAIAgentTools.h
    pqSHYXPythonSyntaxHighlighter.cxx
    pqSHYXPythonSyntaxHighlighter.h
    pqSHYXAIOutputLog.cxx
    pqSHYXAIOutputLog.h
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
    "${CMAKE_CURRENT_SOURCE_DIR}/TransferCurve"
    "${CMAKE_CURRENT_SOURCE_DIR}/SphereSelection"
    "${CMAKE_CURRENT_SOURCE_DIR}/GrowSelectionWithSimilar")
endif ()

target_link_libraries(VESPAPlugin
  PRIVATE
    VTK::FiltersCore
    VTK::FiltersParallel)
if (VESPA_BUILT_WITH_CGAL)
  target_link_libraries(VESPAPlugin
    PRIVATE
      vtkCGALDelaunay
      vtkCGALPMP)
endif()

if (PARAVIEW_USE_QT)
  find_package(Qt6 COMPONENTS Network REQUIRED)
  target_link_libraries(VESPAPlugin
    PRIVATE
      ParaView::RemotingViews
      ParaView::pqApplicationComponents
      Qt6::Network
      VTK::ChartsCore
      VTK::InteractionWidgets
      VTK::IOIOSS
      vtkSHYXSelectionPlaneClipper
      vtkSHYXVascularStentPlacement
      vtkSHYXEndpointStentPlacement
      vtkSHYXEnhancedRuler)
  if (TARGET ParaView::pqPython)
    target_link_libraries(VESPAPlugin PRIVATE ParaView::pqPython)
  endif ()
  if (VESPA_ADAPTIVE_REMESHING)
    target_link_libraries(VESPAPlugin PRIVATE vtkSHYXAdaptiveIsotropicRemesher)
  endif ()

  # Qt TLS backends are MODULE plugins, not static libs. Point at the Qt used to
  # build (QT_PLUGIN_PATH layout: <root>/tls/*.dll). PATH is not searched.
  get_filename_component(_qt6_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  set(_qt6_plugins "${_qt6_prefix}/plugins")
  if (IS_DIRECTORY "${_qt6_plugins}")
    target_compile_definitions(VESPAPlugin PRIVATE "SHYX_QT_PLUGINS_DIR=\"${_qt6_plugins}\"")
  endif ()
  if (WIN32)
    set(_qt6_schannel "${_qt6_plugins}/tls/qschannelbackend.dll")
    if (EXISTS "${_qt6_schannel}")
      add_custom_command(TARGET VESPAPlugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:VESPAPlugin>/tls"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${_qt6_schannel}"
          "$<TARGET_FILE_DIR:VESPAPlugin>/tls/qschannelbackend.dll"
        COMMENT "Copy Qt Schannel TLS plugin next to VESPAPlugin")
    endif ()
  endif ()
endif ()

# MSVC drops RTS-only members of SHYXSnappyHex.lib unless the whole archive is pulled.
if (VESPA_USE_SNAPPYHEXMESH)
  if (COMMAND shyx_snappyhex_whole_archive)
    shyx_snappyhex_whole_archive(VESPAPlugin)
  elseif (MSVC AND TARGET SHYXSnappyHex::SHYXSnappyHex)
    target_link_options(VESPAPlugin PRIVATE
      "/WHOLEARCHIVE:$<TARGET_FILE:SHYXSnappyHex::SHYXSnappyHex>"
      "/OPT:NOICF")
  endif ()
  if (DEFINED SHYXSnappyHex_DIR)
    get_filename_component(_shyx_cli_prefix "${SHYXSnappyHex_DIR}/../../.." ABSOLUTE)
    if (NOT DEFINED SHYXSnappyHex_CLI OR NOT EXISTS "${SHYXSnappyHex_CLI}")
      set(SHYXSnappyHex_CLI "${_shyx_cli_prefix}/bin/snappy_cli.exe")
    endif ()
  endif ()
  if (EXISTS "${SHYXSnappyHex_CLI}")
    add_custom_command(TARGET VESPAPlugin POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SHYXSnappyHex_CLI}"
        "$<TARGET_FILE_DIR:VESPAPlugin>/snappy_cli.exe"
      COMMENT "Copy snappy_cli.exe next to VESPAPlugin")
  endif ()
endif ()

if (BUILD_TESTING AND BUILD_SHARED_LIBS)
  add_subdirectory(Testing)
endif()
