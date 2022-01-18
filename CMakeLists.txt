cmake_minimum_required(VERSION 3.8)
project (vtkCGAL)

## Deps

find_package(VTK REQUIRED)
find_package(CGAL REQUIRED)

include(GNUInstallDirs)

## VTK Module

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

vtk_module_build(
  MODULES
    ${VTK_CGAL_PROVIDED_MODULES}
  INSTALL_EXPORT
    vtkCGAL
  INSTALL_HEADERS
    ON
  HEADERS_DESTINATION
    "include"
  CMAKE_DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL"
)

## Python wrapping

option(BUILD_PYTHON_WRAPPERS "Build python wrapping" ON)

if(BUILD_PYTHON_WRAPPERS)
  # find_package(PythonInterp ${VTK_PYTHON_VERSION} QUIET)

  if(NOT DEFINED VTK_CGAL_PYTHON_MODULE_DIR)
    if (IS_ABSOLUTE ${VTK_PYTHONPATH})
      file(RELATIVE_PATH
        PYTHON_SITE_PACKAGES_SUFFIX
        ${VTK_PREFIX_PATH}
        ${VTK_PYTHONPATH}
      )
    else()
      set(PYTHON_SITE_PACKAGES_SUFFIX
        ${VTK_PYTHONPATH}
      )
    endif()
    set(VTK_CGAL_PYTHON_MODULE_DIR
      ${PYTHON_SITE_PACKAGES_SUFFIX}
      CACHE
      PATH
      "Where the VTK_CGAL python module in installed"
    )
  endif()

  vtk_module_wrap_python(
    MODULES
      ${VTK_CGAL_PROVIDED_MODULES}
    INSTALL_EXPORT
      "vtkCGALPython"
    MODULE_DESTINATION
      "${VTK_CGAL_PYTHON_MODULE_DIR}"
    PYTHON_PACKAGE
      "vtkCGAL"
    CMAKE_DESTINATION
      "${CMAKE_INSTALL_LIBDIR}/cmake/vtkCGAL"
  )
endif()

## Install

set(vtkCGALExport
  vtkCGAL
)
if (TARGET vtkCGALPython)
  list(APPEND vtkCGALExport vtkCGALPython)
endif()

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

