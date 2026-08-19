set(classes
  vtkPulseGlyphRepresentation)
vtk_module_add_module(vtkPulseGlyphRepresentation
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(PulseGlyphRepresentation.xml)
