set(vtkcgalpmp_sources
  vtkCGALIsotropicRemesher.cxx
)
set(vtkcgalpmp_headers
  vtkCGALIsotropicRemesher.h
)
vtk_module_add_module(vtkCGALPMP
  SOURCES ${vtkcgalpmp_sources}
  HEADERS ${vtkcgalpmp_headers}
)
