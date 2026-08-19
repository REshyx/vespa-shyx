set(classes
  vtkSHYXAutoStreamline)
vtk_module_add_module(vtkSHYXAutoStreamline
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXAutoStreamline.xml)
