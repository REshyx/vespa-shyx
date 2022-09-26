vtk_module_find_modules(vespa_vtk_modules "${CMAKE_SOURCE_DIR}/vespa")

paraview_add_plugin(VESPAPlugin
  VERSION "1.0"
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  MODULES ${vtkcgal_provided_modules}
  SERVER_MANAGER_XML VESPAFilters.xml
)

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP)
