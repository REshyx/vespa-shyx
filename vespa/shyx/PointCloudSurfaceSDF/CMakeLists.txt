set(classes
  vtkSHYXPointCloudSurfaceSignedDistance)
vtk_module_add_module(vtkSHYXPointCloudSurfaceSDF
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXPointCloudSurfaceSDF.xml)
