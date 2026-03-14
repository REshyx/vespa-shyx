set(classes
  vtkSurfaceTipExtractor)
# 强制静态链接，不生成单独 dll，与其它 filter 一起打包进 VESPAPlugin.dll
vtk_module_add_module(vtkSurfaceTipExtractor
  FORCE_STATIC
  CLASSES ${classes}
)
