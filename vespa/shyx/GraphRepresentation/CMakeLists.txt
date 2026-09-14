set(classes
  vtkSHYXGraphRepresentation)
vtk_module_add_module(vtkSHYXGraphRepresentation
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(GraphRepresentation.xml)
