set(classes
  vtkSHYXSelectionFillAlphaReunionFilter)
vtk_module_add_module(vtkSHYXSelectionFillAlphaReunionFilter
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXSelectionFillAlphaReunionFilter PRIVATE CGAL::Eigen3_support)
