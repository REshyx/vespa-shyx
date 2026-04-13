set(classes
  vtkGeodesicDistanceFilter)
# 强制静态链接，打包进 VESPAPlugin.dll
vtk_module_add_module(vtkGeodesicDistanceFilter
  FORCE_STATIC
  CLASSES ${classes}
)
