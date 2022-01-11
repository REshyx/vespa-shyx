cmake_minimum_required(VERSION 3.8)
project (VTKCGAL)

message(STATUS "VTKCGAL: Building as a Remote VTK module")

set(VTKCGAL_SOURCES
  PolygonMeshProcessing/vtkCGALisotropicRemesh.cxx
)
vtk_module_add_module(VTK::VTKCGAL
  SOURCES ${VTKCGAL_SOURCES}
)

vtk_module_find_package(PACKAGE CGAL)

