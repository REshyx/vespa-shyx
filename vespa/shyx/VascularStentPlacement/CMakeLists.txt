set(classes
  vtkSHYXVascularStentPlacement
  vtkSHYXImplicitCylinderRepresentation)
vtk_module_add_module(vtkSHYXVascularStentPlacement
  FORCE_STATIC
  CLASSES ${classes})
