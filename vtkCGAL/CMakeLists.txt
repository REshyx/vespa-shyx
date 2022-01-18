
set(VTKCGAL_SOURCES
  PolygonMeshProcessing/vtkCGALIsotropicRemesher.cxx
)
vtk_module_add_module(VTK::vtkCGAL
  SOURCES
    ${VTKCGAL_SOURCES}
)
