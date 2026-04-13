set(classes
  vtkSHYXTetGen)
vtk_module_add_module(vtkSHYXTetGen
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXTetGen PRIVATE tet)
target_include_directories(vtkSHYXTetGen PRIVATE ${CMAKE_SOURCE_DIR}/TetGen-1.6.0)
