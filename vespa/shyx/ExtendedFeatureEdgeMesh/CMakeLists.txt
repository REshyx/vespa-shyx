set(classes vtkSHYXExtendedFeatureEdgeMesh)
vtk_module_add_module(vtkSHYXExtendedFeatureEdgeMesh
  FORCE_STATIC
  CLASSES ${classes})

vtk_module_link(vtkSHYXExtendedFeatureEdgeMesh PRIVATE SHYXSnappyHex::SHYXSnappyHex)

vespa_plugin_xml(SHYXExtendedFeatureEdgeMesh.xml)
