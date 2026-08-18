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
  TYPE "shyx_selection_patch_table"
  CLASS_NAME pqSHYXSelectionPatchTableWidget
  INTERFACES shyx_selection_patch_table_interfaces
  SOURCES shyx_selection_patch_table_sources)
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
list(APPEND shyx_selection_patch_table_sources
  pqSHYXSelectionPatchTableWidget.cxx
  pqSHYXSelectionPatchTableWidget.h)
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
set(shyx_snappy_patch_table_interfaces)
set(shyx_snappy_patch_table_sources)
set(shyx_snappy_castellated_interfaces)
set(shyx_snappy_castellated_sources)
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
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "shyx_snappy_patch_table"
    CLASS_NAME pqSHYXSnappyPatchTableWidget
    INTERFACES shyx_snappy_patch_table_interfaces
    SOURCES shyx_snappy_patch_table_sources)
  list(APPEND shyx_snappy_patch_table_sources
    pqSHYXSnappyPatchTableWidget.cxx
    pqSHYXSnappyPatchTableWidget.h)
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "shyx_snappy_castellated"
    CLASS_NAME pqSHYXSnappyCastellatedWidget
    INTERFACES shyx_snappy_castellated_interfaces
    SOURCES shyx_snappy_castellated_sources)
  list(APPEND shyx_snappy_castellated_sources
    pqSHYXSnappyCastellatedWidget.cxx
    pqSHYXSnappyCastellatedWidget.h)
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

  list(APPEND vespa_plugin_qt_auto_interfaces pqSHYXSelectBlockContextMenu)
  list(APPEND vespa_plugin_qt_auto_sources
    SelectBlock/pqSHYXSelectBlockContextMenu.cxx
    SelectBlock/pqSHYXSelectBlockContextMenu.h)

  list(APPEND vespa_plugin_qt_auto_interfaces pqSHYXSelectSimilarContextMenu)
  list(APPEND vespa_plugin_qt_auto_sources
    SelectSimilar/pqSHYXSelectSimilarContextMenu.cxx
    SelectSimilar/pqSHYXSelectSimilarContextMenu.h)

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
    "SHYXSelectionAppendPatches.xml"
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
  UI_INTERFACES ${acm_interfaces} ${vespa_obb_box_interfaces} ${vespa_endclipper_plane_interfaces} ${shyx_selection_plane_clipper_interfaces} ${vespa_stent_cylinder_interfaces} ${vespa_enhanced_ruler_interfaces} ${vespa_endpoint_stent_interfaces} ${shyx_partitioned_block_names_interfaces} ${shyx_selection_patch_table_interfaces} ${shyx_boundary_assignment_info_interfaces} ${shyx_remesh_uncapped_histogram_interfaces} ${shyx_openings_table_interfaces} ${shyx_snappy_inside_points_interfaces} ${shyx_snappy_case_folder_interfaces} ${shyx_snappy_patch_table_interfaces} ${shyx_snappy_castellated_interfaces} ${shyx_ai_assistant_interfaces} ${vespa_plugin_qt_auto_interfaces}
  SOURCES ${acm_sources} ${vespa_obb_box_sources} ${vespa_endclipper_plane_sources} ${shyx_selection_plane_clipper_sources} ${vespa_stent_cylinder_sources} ${vespa_enhanced_ruler_sources} ${vespa_endpoint_stent_sources} ${shyx_partitioned_block_names_sources} ${shyx_selection_patch_table_sources} ${shyx_boundary_assignment_info_sources} ${shyx_remesh_uncapped_histogram_sources} ${shyx_openings_table_sources} ${shyx_snappy_inside_points_sources} ${shyx_snappy_case_folder_sources} ${shyx_snappy_patch_table_sources} ${shyx_snappy_castellated_sources} ${shyx_ai_assistant_sources}
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
    pqSHYXCurlRequest.cxx
    pqSHYXCurlRequest.h
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
    "${CMAKE_CURRENT_SOURCE_DIR}/GrowSelectionWithSimilar"
    "${CMAKE_CURRENT_SOURCE_DIR}/SelectBlock"
    "${CMAKE_CURRENT_SOURCE_DIR}/SelectSimilar")
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
  include(FetchContent)
  # paraview_plugin_build sets BUILD_SHARED_LIBS ON for VESPAPlugin.dll. Curl
  # would then build a DLL; force a static libcurl and restore the caller's value.
  set(_vespa_saved_shared_libs "${BUILD_SHARED_LIBS}")
  set(_vespa_saved_testing "${BUILD_TESTING}")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
  set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
  set(HTTP_ONLY ON CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
  set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
  set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
  set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
  set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
  set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
  if (WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_WINDOWS_SSPI ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  endif ()
  FetchContent_Declare(vespa_curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_21_0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(vespa_curl)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared library" FORCE)
  set(BUILD_SHARED_LIBS "${_vespa_saved_shared_libs}")
  if (_vespa_saved_testing)
    set(BUILD_TESTING ON CACHE BOOL "Build Testing" FORCE)
  else ()
    set(BUILD_TESTING OFF CACHE BOOL "Build Testing" FORCE)
  endif ()
  unset(_vespa_saved_shared_libs)
  unset(_vespa_saved_testing)
  if (NOT TARGET CURL::libcurl AND TARGET libcurl_static)
    add_library(CURL::libcurl ALIAS libcurl_static)
  endif ()

  target_link_libraries(VESPAPlugin
    PRIVATE
      ParaView::RemotingViews
      ParaView::pqApplicationComponents
      CURL::libcurl
      VTK::ChartsCore
      VTK::InteractionWidgets
      VTK::IOImage
      VTK::IOIOSS
      vtkSHYXSelectionPlaneClipper
      vtkSHYXVascularStentPlacement
      vtkSHYXEndpointStentPlacement
      vtkSHYXEnhancedRuler)
  if (WIN32)
    target_link_libraries(VESPAPlugin PRIVATE
      ws2_32 crypt32 secur32 advapi32 bcrypt normaliz iphlpapi)
  endif ()
  if (TARGET ParaView::pqPython)
    target_link_libraries(VESPAPlugin PRIVATE ParaView::pqPython)
  endif ()
  if (VESPA_ADAPTIVE_REMESHING)
    target_link_libraries(VESPAPlugin PRIVATE vtkSHYXAdaptiveIsotropicRemesher)
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
  if (TARGET shyx_snappyhex_ep)
    add_dependencies(VESPAPlugin shyx_snappyhex_ep)
  endif ()
endif ()

if (BUILD_TESTING AND BUILD_SHARED_LIBS)
  add_subdirectory(Testing)
endif()
