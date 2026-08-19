set(classes
  vtkSHYXVectorFieldTopology)
vtk_module_add_module(vtkSHYXVectorFieldTopology
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXVectorFieldTopology.xml)
