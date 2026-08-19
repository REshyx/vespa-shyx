set(classes
  vtkSHYXSelectionPlaneClipper)
vtk_module_add_module(vtkSHYXSelectionPlaneClipper
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXSelectionPlaneClipper.xml)
