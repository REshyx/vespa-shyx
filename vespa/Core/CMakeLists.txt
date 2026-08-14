set(classes
  vtkVESPAAttributeTransfer)

vtk_module_add_module(vtkVESPACore
  ${FORCE_STATIC_MODULES_STRING}
  CLASSES ${classes}
)
