set(classes
  vtkAnimatedStreamlineCompositePolyDataMapper
  vtkAnimatedStreamlineRepresentation)
vtk_module_add_module(vtkAnimatedStreamlineRepresentation
  FORCE_STATIC
  CLASSES ${classes}
)
