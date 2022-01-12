cmake_minimum_required(VERSION 3.8)
project (vtkCGAL)

find_package(VTK REQUIRED)
find_package(CGAL REQUIRED)

include(GNUInstallDirs)

vtk_module_find_modules(
  VTK_CGAL_MODULE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/vtkCGAL"
)

vtk_module_scan(
  MODULE_FILES
    ${VTK_CGAL_MODULE_FILES}
  PROVIDES_MODULES
    VTK_CGAL_PROVIDED_MODULES
  WANT_BY_DEFAULT
    ON
)

message(STATUS "Modules: ${VTK_CGAL_PROVIDED_MODULES} ")

vtk_module_build(
  MODULES
    ${VTK_CGAL_PROVIDED_MODULES}
)


