paraview_add_plugin(VESPAPlugin
  VERSION "1.0"
  REQUIRED_ON_CLIENT
  REQUIRED_ON_SERVER
  SERVER_MANAGER_XML VESPAFilters.xml
  MODULES
    vtkCGALDelaunay
    vtkCGALPMP
  MODULE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../vespa/Delaunay/vtk.module"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vespa/PolygonMeshProcessing/vtk.module"
)

target_link_libraries(VESPAPlugin
  PRIVATE
    vtkCGALDelaunay
    vtkCGALPMP)
