set(classes
  vtkSHYXSelectionExtrudeFilter)
vtk_module_add_module(vtkSHYXSelectionExtrudeFilter
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(SHYXSelectionExtrude.xml)
