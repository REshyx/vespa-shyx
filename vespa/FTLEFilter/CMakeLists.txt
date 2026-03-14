set(classes
  vtkFTLEFilter)
# 强制静态链接，打包进 VESPAPlugin.dll
vtk_module_add_module(vtkFTLEFilter
  FORCE_STATIC
  CLASSES ${classes}
)
