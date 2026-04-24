set(classes
  vtkSHYXHoleFillFilter)
vtk_module_add_module(vtkSHYXHoleFillFilter
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXHoleFillFilter PRIVATE CGAL::Eigen3_support)
