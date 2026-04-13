set(classes
  vtkCGALSkeletonExtraction)
vtk_module_add_module(vtkSHYXSkeletonExtraction
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSkeletonExtraction PRIVATE CGAL::Eigen3_support)
