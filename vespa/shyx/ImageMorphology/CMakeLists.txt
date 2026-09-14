set(classes
  vtkSHYXImageMorphology)
vtk_module_add_module(vtkSHYXImageMorphology
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXImageMorphology.xml)
