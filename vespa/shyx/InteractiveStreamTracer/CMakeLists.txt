set(classes
  vtkSHYXInteractiveStreamTracer)
vtk_module_add_module(vtkSHYXInteractiveStreamTracer
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXInteractiveStreamTracer.xml)
