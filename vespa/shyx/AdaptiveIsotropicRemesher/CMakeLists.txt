set(classes
  vtkSHYXAdaptiveIsotropicRemesher)
vtk_module_add_module(vtkSHYXAdaptiveIsotropicRemesher
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXAdaptiveIsotropicRemesher PRIVATE CGAL::Eigen3_support)
