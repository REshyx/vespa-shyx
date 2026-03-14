vtk_module_find_modules(vespa_vtk_modules "${CMAKE_SOURCE_DIR}/vespa")

if (${VESPA_ALPHA_WRAPPING})
  set(ALPHA_WRAPPING_SERVER_MANAGER_XML "VESPAAlphaWrapping.xml")
endif()

if (${VESPA_MESH_SMOOTHING})
  set(MESH_SMOOTHING_SERVER_MANAGER_XML "VESPAMeshSmoothingFilter.xml")
endif()

option(VESPA_SHOW_CGAL_LICENSE_ON_LOAD "Show CGAL License when loading plugin" OFF)
mark_as_advanced(VESPA_SHOW_CGAL_LICENSE_ON_LOAD)
if (${VESPA_SHOW_CGAL_LICENSE_ON_LOAD})
  set(VESPA_CGAL_LICENSE "${CMAKE_SOURCE_DIR}/Data/Resources/CGAL_LICENSE.GPL")
endif()

paraview_add_plugin(VESPAPlugin
  VERSION "${PROJECT_VERSION}"
  EULA ${VESPA_CGAL_LICENSE}
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  MODULES ${vtkcgal_provided_modules}
  SERVER_MANAGER_XML
    "VESPAFilters.xml"
    "SHYXSurfaceTipExtractor.xml"
    "SHYXFTLEFilter.xml"
    "SHYXGeodesicDistance.xml"
    "SHYXVortexCriteria.xml"
     ${ALPHA_WRAPPING_SERVER_MANAGER_XML}
     ${MESH_SMOOTHING_SERVER_MANAGER_XML}
)

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP)

if (BUILD_TESTING AND BUILD_SHARED_LIBS)
  add_subdirectory(Testing)
endif()
