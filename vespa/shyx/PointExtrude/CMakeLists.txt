set(classes
  vtkSHYXPointExtrudeFilter)
vtk_module_add_module(vtkSHYXPointExtrudeFilter
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXPointExtrude.xml)
