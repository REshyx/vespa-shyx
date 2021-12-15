message(STATUS "VTKCGAL: Building as a Remote VTK module")

find_package(CGAL REQUIRED)

set(VTKCGAL_SOURCES
 PolygonMeshProcessing/vtkCGALisotropicRemesh.cxx
)

set(VTKCGAL_HEADERS
 PolygonMeshProcessing/vtkCGALisotropicRemesh.h
)
vtk_module_library(VTK::CGAL
  SOURCES ${VTKCGAL_SOURCES}
  HEADERS ${VTKCGAL_HEADERS}
)
