set(classes
  vtkSHYXSurfaceThickness)

vtk_module_add_module(vtkSHYXSurfaceThickness
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXSurfaceThickness.xml)
