set(classes
  vtkSHYXRepairDegeneracies)
vtk_module_add_module(vtkSHYXRepairDegeneracies
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXRepairDegeneracies PRIVATE CGAL::Eigen3_support)
