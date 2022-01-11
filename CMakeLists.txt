cmake_minimum_required(VERSION 3.8)
project (VTKCGAL)

# This lists file is ignored when built as part of VTK remote modules.
# Code can be added here to support standalone builds.
#
# find_package(VTK 9.1 REQUIRED)

message(STATUS "VTKCGAL: Building as a Remote VTK module")

set(VTKCGAL_CLASSES
 PolygonMeshProcessing/vtkCGALisotropicRemesh
)
vtk_module_add_module(VTK::VTKCGAL
  CLASSES ${VTKCGAL_CLASSES}
)

vtk_module_find_package(PACKAGE CGAL)

