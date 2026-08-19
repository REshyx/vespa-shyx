set(classes
  vtkAnimatedStreamlineCompositePolyDataMapper
  vtkAnimatedStreamlineRepresentation
  vtkSMAnimatedStreamlineIntegrationScaleDomain)
vtk_module_add_module(vtkAnimatedStreamlineRepresentation
  FORCE_STATIC
  CLASSES ${classes}
)

vespa_plugin_xml(AnimatedStreamlineRepresentation.xml)
