set(classes
  vtkSHYXImageTeasarCenterline)
vtk_module_add_module(vtkSHYXImageTeasarCenterline
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXImageTeasarCenterline.xml)
