set(classes
  vtkHologramRepresentation)
vtk_module_add_module(vtkHologramRepresentation
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(HologramRepresentation.xml)
