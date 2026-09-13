set(classes
  vtkPVSHYXSelectionBoundsInformation
  vtkSMSHYXSelectionBoundsDomain)
vtk_module_add_module(vtkSMSHYXSelectionBoundsDomain
  FORCE_STATIC
  CLASSES ${classes})
