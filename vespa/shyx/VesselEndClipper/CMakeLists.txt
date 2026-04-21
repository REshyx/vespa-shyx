set(classes
  vtkCGALVesselEndClipper)
vtk_module_add_module(vtkSHYXVesselEndClipper
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXVesselEndClipper PRIVATE CGAL::Eigen3_support)
