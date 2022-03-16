cmake_minimum_required(VERSION 3.8)
project (vtkCGAL)

## Config

include(GNUInstallDirs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

option(BUILD_SHARED_LIBS "Build shared library" ON)

include(CTest)

## Deps

find_package(VTK
  COMPONENTS
    CommonCore
    CommonDataModel
    CommonExecutionModel
    FiltersCore
    FiltersExtraction
    FiltersGeometry
    Python
  OPTIONAL_COMPONENTS
    CommonSystem
    IOXML
    TestingCore)

find_package(CGAL REQUIRED)

find_package(Eigen3 REQUIRED 3.2.0)
if (CGAL_VERSION VERSION_GREATER "5.1")
  # this file only exists in CGAL 5.1 and later
  include(CGAL_Eigen3_support)
else()
  include_directories(${EIGEN3_INCLUDE_DIR})
endif()

## VTK Module

vtk_module_find_modules(vtkcgal_module_files "${CMAKE_CURRENT_SOURCE_DIR}/vtkCGAL")

vtk_module_scan(
  MODULE_FILES     ${vtkcgal_module_files}
  PROVIDES_MODULES vtkcgal_provided_modules
  WANT_BY_DEFAULT  ON
  ENABLE_TESTS     ON)

vtk_module_python_default_destination(python_destination)

vtk_module_build(
  MODULES         ${vtkcgal_provided_modules}
  INSTALL_EXPORT  vtkCGAL
  INSTALL_HEADERS ON
  CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL"
  HEADERS_DESTINATION "include")


## Install

set(vtkCGALExport ${vtkcgal_provided_modules})
export(
  TARGETS   ${vtkCGALExport}
  NAMESPACE VTK::
  FILE "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/vtkCGAL/vtkCGAL-targets.cmake")
install(
  EXPORT      vtkCGAL
  NAMESPACE   VTK::
  FILE        vtkCGAL-targets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL")


## Python wrapping

option(BUILD_PYTHON_WRAPPERS "Build python wrapping" ON)

if(BUILD_PYTHON_WRAPPERS)

  vtk_module_wrap_python(
    MODULES         ${vtkcgal_provided_modules}
    WRAPPED_MODULES vtkcgal_wrapped_modules
    TARGET          VTKCGAL::Python
    INSTALL_EXPORT  vtkCGALPython
    PYTHON_PACKAGE  "vtkCGAL"
    MODULE_DESTINATION  "${python_destination}"
    CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGALPython"
    LIBRARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INSTALL_HEADERS ON
    BUILD_STATIC    OFF)

  # Generate __init__.py
  set(python_modules)
  foreach(module ${vtkcgal_wrapped_modules})
    list(APPEND python_modules "'${module}'")
  endforeach()

  set(InitContent "__all__ = ${python_modules}")

  file(GENERATE
    OUTPUT  "${CMAKE_BINARY_DIR}/${python_destination}/vtkCGAL/__init__.py"
    CONTENT "${InitContent}")
  install(
    FILES       "${CMAKE_BINARY_DIR}/${python_destination}/vtkCGAL/__init__.py"
    DESTINATION "${python_destination}/vtkCGAL/")

  # Install vtkCGALPython
  export(
    EXPORT    vtkCGALPython
    NAMESPACE VTKCGAL::
    FILE "${CMAKE_BINARY_DIR}/${python_destination}/vtkCGALPython/vtkCGALPython-targets.cmake")
  install(
    EXPORT    vtkCGALPython
    NAMESPACE VTKCGAL::
    FILE      vtkCGALPython-targets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGALPython")

endif()

