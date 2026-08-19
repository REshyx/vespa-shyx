# VESPAPlugin: one DLL aggregating vespa VTK modules + client Qt.
# SHYX Server Manager XML is declared on each module via vespa_plugin_xml().
# This file only registers plugin-only XML, property widgets, and client interfaces.

option(VESPA_SHOW_CGAL_LICENSE_ON_LOAD "Show CGAL License when loading plugin" OFF)
mark_as_advanced(VESPA_SHOW_CGAL_LICENSE_ON_LOAD)
if (${VESPA_SHOW_CGAL_LICENSE_ON_LOAD})
  set(VESPA_CGAL_LICENSE "${CMAKE_SOURCE_DIR}/Data/Resources/CGAL_LICENSE.GPL")
endif()

get_property(VESPA_MODULE_SERVER_MANAGER_XML GLOBAL PROPERTY VESPA_PLUGIN_SERVER_MANAGER_XML)
set(VESPA_PLUGIN_ONLY_XML "smxml/VESPAVascularCategory.xml")
if (VESPA_BUILT_WITH_CGAL)
  list(APPEND VESPA_PLUGIN_ONLY_XML "smxml/VESPAFilters.xml")
endif()
list(LENGTH VESPA_MODULE_SERVER_MANAGER_XML _vespa_nxml)
set(_vespa_xml_names)
foreach (_vespa_xml IN LISTS VESPA_MODULE_SERVER_MANAGER_XML)
  get_filename_component(_vespa_xml_name "${_vespa_xml}" NAME)
  list(APPEND _vespa_xml_names "${_vespa_xml_name}")
endforeach ()
message(STATUS "VESPAPlugin: collected ${_vespa_nxml} module Server Manager XML file(s): ${_vespa_xml_names}")
unset(_vespa_nxml)
unset(_vespa_xml_names)
unset(_vespa_xml)
unset(_vespa_xml_name)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PropertyWidgets.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClientInterfaces.cmake")

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
    ${VESPA_MODULE_SERVER_MANAGER_XML}
    ${VESPA_PLUGIN_ONLY_XML}
  UI_INTERFACES
    ${vespa_plugin_widget_interfaces}
    ${shyx_ai_assistant_interfaces}
    ${vespa_plugin_qt_auto_interfaces}
  SOURCES
    ${vespa_plugin_widget_sources}
    ${shyx_ai_assistant_sources}
    ${vespa_plugin_qt_auto_sources}
    Resources/UI/pqCylinderPropertyWidget.ui
    vespa_qrc_init.cxx
    vespa_qrc_init.h
)

# pqSHYXVascularStentCylinderWidget includes ui_pqCylinderPropertyWidget.h; the .ui lives under
# Resources/UI/. ParaView plugin targets may not enable AUTOUIC or search that path by default.
if (PARAVIEW_USE_QT)
  set_target_properties(VESPAPlugin PROPERTIES AUTOUIC ON)
  set_property(TARGET VESPAPlugin APPEND PROPERTY
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/UI")
  target_include_directories(VESPAPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/widgets"
    "${CMAKE_CURRENT_SOURCE_DIR}/widgets/TransferCurve"
    "${CMAKE_CURRENT_SOURCE_DIR}/widgets/SnappyHexMesh"
    "${CMAKE_CURRENT_SOURCE_DIR}/AIAssistant"
    "${CMAKE_CURRENT_SOURCE_DIR}/VascularCategory"
    "${CMAKE_CURRENT_SOURCE_DIR}/Representations"
    "${CMAKE_CURRENT_SOURCE_DIR}/selection/SphereSelection"
    "${CMAKE_CURRENT_SOURCE_DIR}/selection/GrowSelectionWithSimilar"
    "${CMAKE_CURRENT_SOURCE_DIR}/selection/SelectBlock"
    "${CMAKE_CURRENT_SOURCE_DIR}/selection/SelectSimilar")
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
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchLibcurl.cmake")
  target_link_libraries(VESPAPlugin
    PRIVATE
      ParaView::RemotingViews
      ParaView::pqApplicationComponents
      VTK::ChartsCore
      VTK::InteractionWidgets
      VTK::IOImage
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
