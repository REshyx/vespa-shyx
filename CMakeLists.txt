cmake_minimum_required(VERSION 3.8)
project (vtkCGAL)

## Deps

find_package(VTK REQUIRED)
find_package(CGAL REQUIRED)

include(GNUInstallDirs)

## Config

option(BUILD_SHARED_LIBS "Build shared library" ON)

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
)

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

  # find_package(Python3 COMPONENTS Development)
  # include_directories(SYSTEM ${Python3_INCLUDE_DIRS})
  # link_libraries(${Python3_LIBRARIES})

  # message(STATUS "libs : ${Python3_LIBRARIES}")
  # message(STATUS "modules : ${VTKCGAL_PROVIDED_MODULES}")

  vtk_module_python_default_destination(python_destination)

  if(NOT DEFINED VTKCGAL_PYTHON_MODULE_DIR)
    if (IS_ABSOLUTE ${VTK_PYTHONPATH})
      file(RELATIVE_PATH PYTHON_SITE_PACKAGES_SUFFIX
          ${VTK_PREFIX_PATH}
          ${VTK_PYTHONPATH}
      )
    else()
      set(PYTHON_SITE_PACKAGES_SUFFIX
          ${VTK_PYTHONPATH}
      )
    endif()
    set(VTKCGAL_PYTHON_MODULE_DIR
          ${PYTHON_SITE_PACKAGES_SUFFIX}
        CACHE
        PATH
          "Where the VTKCGAL python module in installed"
      )
  endif()
  vtk_module_wrap_python(
    MODULES
      ${VTKCGAL_PROVIDED_MODULES}
    WRAPPED_MODULES
      VTKCGAL_WRAPPED_MODULES
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
    TARGET
      VTKCGAL::Python
    BUILD_STATIC
      OFF
    )

  message(STATUS "modules wrap : ${VTKCGAL_WRAPPED_MODULES}")

  # Generate __init__.py
  # set(InitContent "from __future__ import absolute_import\n")
  # foreach(MODULE ${VTKCGAL_PROVIDED_MODULES})
  #   message(STATUS "Wrap: ${MODULE}")
  #   string(APPEND InitContent "from .${MODULE} import *\n")
  # endforeach()
  # file(GENERATE
  #   OUTPUT
  #     "${CMAKE_BINARY_DIR}/${VTKCGAL_PYTHON_MODULE_DIR}/vtkCGAL/__init__.py"
  #   CONTENT
  #     "${InitContent}"
  #   )
  # install(
  #   FILES
  #     "${CMAKE_BINARY_DIR}/${VTKCGAL_PYTHON_MODULE_DIR}/vtkCGAL/__init__.py"
  #   DESTINATION
  #     "${VTKCGAL_PYTHON_MODULE_DIR}/vtkCGAL/"
  #   )
  # # Install VTKCGALPython
  # export(
  #   EXPORT
  #     VTKCGALPython
  #   NAMESPACE
  #     VTKCGAL::
  #   FILE
  #     "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/vtkCGAL/python/VTKCGALPython-targets.cmake"
  #   )
  # install(
  #   EXPORT
  #     VTKCGALPython
  #   NAMESPACE
  #     VTKCGAL::
  #   FILE
  #     VTKCGALPython-targets.cmake
  #   DESTINATION
  #     "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGALPython"
  #   )
endif()

