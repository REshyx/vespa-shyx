set(classes
  vtkSHYXResampleLines)
vtk_module_add_module(vtkSHYXResampleLines
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXResampleLines.xml)
