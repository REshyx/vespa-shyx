set(classes
  vtkVortexCriteriaFilter)
# 强制静态链接，打包进 VESPAPlugin.dll
vtk_module_add_module(vtkVortexCriteriaFilter
  FORCE_STATIC
  CLASSES ${classes}
)
