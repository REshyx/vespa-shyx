set(classes
  vtkSHYXExtrudeFilter)
vtk_module_add_module(vtkSHYXExtrudeFilter
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXExtrude.xml)
