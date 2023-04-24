vtk_module_find_modules(vespa_vtk_modules "${CMAKE_SOURCE_DIR}/vespa")

if (${VESPA_ALPHA_WRAPPING})
  set(ALPHA_WRAPPING_SERVER_MANAGER_XML "VESPAAlphaWrapping.xml")
endif()

if (${VESPA_MESH_SMOOTHING})
  set(MESH_SMOOTHING_SERVER_MANAGER_XML "VESPAMeshSmoothingFilter.xml")
endif()

paraview_add_plugin(VESPAPlugin
  VERSION "1.0"
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  MODULES ${vtkcgal_provided_modules}
  SERVER_MANAGER_XML
    "VESPAFilters.xml"
     ${ALPHA_WRAPPING_SERVER_MANAGER_XML}
     ${MESH_SMOOTHING_SERVER_MANAGER_XML}
)

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP)
