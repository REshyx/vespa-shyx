vtk_module_find_modules(vespa_vtk_modules "${CMAKE_SOURCE_DIR}/vespa")

if (${VESPA_ALPHA_WRAPPING})
  set(ALPHA_WRAPPING_SERVER_MANAGER_XML "VESPAAlphaWrapping.xml")
endif()

if (${VESPA_MESH_SMOOTHING})
  set(MESH_SMOOTHING_SERVER_MANAGER_XML "VESPAMeshSmoothingFilter.xml")
endif()

if (${VESPA_ADAPTIVE_REMESHING})
  set(ADAPTIVE_REMESHING_SERVER_MANAGER_XML "SHYXAdaptiveIsotropicRemesher.xml")
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
    "SHYXFTLEFilter.xml"
    "SHYXGeodesicDistance.xml"
    "SHYXRadiusNeighborCount.xml"
    "SHYXDisconnectedRegionFuse.xml"
    "SHYXDataSetToPartitionedCollection.xml"
    "SHYXClebschMapFilter.xml"
    "SHYXVortexCriteria.xml"
    "SHYXVortexCoreTest.xml"
    "SHYXVectorFieldTopology.xml"
    "SHYXArrayCurveMapper.xml"
    "PulseGlyphRepresentation.xml"
    "AnimatedStreamlineRepresentation.xml"
     ${ALPHA_WRAPPING_SERVER_MANAGER_XML}
     ${MESH_SMOOTHING_SERVER_MANAGER_XML}
     ${ADAPTIVE_REMESHING_SERVER_MANAGER_XML}
  UI_INTERFACES ${acm_interfaces} ${vespa_plugin_qt_auto_interfaces}
  SOURCES ${acm_sources}
    pqArrayCurveMapperPanel.cxx
    pqArrayCurveMapperPanel.h
    vespa_qrc_init.cxx
    vespa_qrc_init.h
    ${vespa_plugin_qt_auto_sources}
)

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP)

if (PARAVIEW_USE_QT)
  target_link_libraries(VESPAPlugin
    PRIVATE
      ParaView::RemotingViews)
endif ()

if (BUILD_TESTING AND BUILD_SHARED_LIBS)
  add_subdirectory(Testing)
endif()
