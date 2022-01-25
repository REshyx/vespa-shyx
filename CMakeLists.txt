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
    Python
)
find_package(CGAL REQUIRED)

## VTK Module

vtk_module_find_modules(
  VTKCGAL_MODULE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/vtkCGAL"
)

vtk_module_scan(
  MODULE_FILES
    ${VTKCGAL_MODULE_FILES}
  PROVIDES_MODULES
    VTKCGAL_PROVIDED_MODULES
  WANT_BY_DEFAULT
    ON
  ENABLE_TESTS
    ON
)

vtk_module_python_default_destination(python_destination)

vtk_module_build(
  MODULES
    ${VTKCGAL_PROVIDED_MODULES}
  INSTALL_EXPORT
    vtkCGAL
  INSTALL_HEADERS
    ON
  HEADERS_DESTINATION
    "include"
  CMAKE_DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL"
)

## Install

set(vtkCGALExport
  ${VTKCGAL_PROVIDED_MODULES}
)
export(
  TARGETS
   ${vtkCGALExport}
  NAMESPACE
    VTK::
  FILE
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/vtkCGAL/vtkCGAL-targets.cmake"
)
install(
  EXPORT
    vtkCGAL
  NAMESPACE
    VTK::
  FILE
    vtkCGAL-targets.cmake
  DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL"
)

## Python wrapping (In progress)

option(BUILD_PYTHON_WRAPPERS "Build python wrapping" ON)

if(BUILD_PYTHON_WRAPPERS)

  vtk_module_wrap_python(
    MODULES
      ${VTKCGAL_PROVIDED_MODULES}
    WRAPPED_MODULES
      VTKCGAL_WRAPPED_MODULES
    TARGET
      VTKCGAL::Python
    INSTALL_EXPORT
      VTKCGALPython
    PYTHON_PACKAGE
      "vtkCGAL"
    MODULE_DESTINATION
      "${python_destination}"
    CMAKE_DESTINATION
      "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGALPython"
    LIBRARY_DESTINATION
      "${CMAKE_INSTALL_LIBDIR}"
    INSTALL_HEADERS
      ON
    BUILD_STATIC
      OFF
  )

  # Generate __init__.py
  set(InitContent "from __future__ import absolute_import\n")
  foreach(MODULE ${VTKCGAL_PROVIDED_MODULES})
    string(APPEND InitContent "from .${MODULE} import *\n")
  endforeach()
  file(GENERATE
    OUTPUT
      "${CMAKE_BINARY_DIR}/${python_destination}/vtkCGAL/__init__.py"
    CONTENT
      "${InitContent}"
  )
  install(
    FILES
      "${CMAKE_BINARY_DIR}/${python_destination}/vtkCGAL/__init__.py"
    DESTINATION
      "${python_destination}/vtkCGAL/"
  )

  # Install VTKCGALPython
  export(
    EXPORT
      VTKCGALPython
    NAMESPACE
      VTKCGAL::
    FILE
      "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/vtkCGAL/python/VTKCGALPython-targets.cmake"
  )
  install(
    EXPORT
      VTKCGALPython
    NAMESPACE
      VTKCGAL::
    FILE
      VTKCGALPython-targets.cmake
    DESTINATION
      "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGALPython"
  )
endif()

