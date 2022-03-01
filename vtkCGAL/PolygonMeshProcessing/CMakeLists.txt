set(vtkcgalpmp_sources
  vtkCGALBooleanOperation.cxx
  vtkCGALIsotropicRemesher.cxx
)
set(vtkcgalpmp_headers
  vtkCGALBooleanOperation.h
  vtkCGALIsotropicRemesher.h
)
vtk_module_add_module(vtkCGALPMP
  SOURCES ${vtkcgalpmp_sources}
  HEADERS ${vtkcgalpmp_headers}
)
