set(classes
  vtkCGALPointCloudSurfaceSignedDistance)
vtk_module_add_module(vtkSHYXPointCloudSurfaceSDF
  FORCE_STATIC
  CLASSES ${classes})
