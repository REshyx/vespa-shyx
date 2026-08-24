set(classes
  vtkSHYXEMeshReader)
vtk_module_add_module(vtkSHYXEMeshReader
  FORCE_STATIC
  CLASSES ${classes})

vespa_plugin_xml(SHYXEMeshReader.xml)
