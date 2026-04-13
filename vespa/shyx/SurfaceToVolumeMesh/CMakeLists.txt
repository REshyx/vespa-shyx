set(classes
  vtkCGALSurfaceToVolumeMesh)
vtk_module_add_module(vtkSHYXSurfaceToVolumeMesh
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSurfaceToVolumeMesh PRIVATE CGAL::Eigen3_support)
