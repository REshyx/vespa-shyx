cmake_minimum_required(VERSION 3.8)
project(vespa VERSION 0.2.0)

## Testing
option(BUILD_TESTING "Build Testing" OFF)
if (BUILD_TESTING)
  enable_testing()
endif()

## Config

set(CMAKE_CXX_STANDARD 11)


set(Boost_DIR "C:/SoftWare/boost_1_82_0/lib64-msvc-14.2/cmake/Boost-1.82.0")
set(CGAL_DIR C:/SoftWare/CGAL-6.1.1)
set(ParaView_DIR C:/SoftWare/Paraview/install-qt6-tbb/lib/cmake/paraview-6.0)
set(Qt6_DIR C:/SoftWare/QT/msvc2022_64/lib/cmake/Qt6)
set(TBB_DIR "C:/SoftWare/oneapi-tbb-2021.12.0/lib/cmake/tbb")
set(MKL_DIR "C:/Program Files (x86)/Intel/oneAPI/mkl/2025.3/lib/cmake/mkl")
set(Eigen3_DIR C:/SoftWare/eigen-3.4.0/install/x64-Release/share/eigen3/cmake)


include(GNUInstallDirs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

get_property(generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT CMAKE_BUILD_TYPE AND NOT generator_is_multi_config)
  message(STATUS "Setting build type to 'Release'.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif ()

option(BUILD_SHARED_LIBS "Build shared library" ON)
set(VESPA_BUILD_PV_PLUGIN ON CACHE BOOL "Build VESPA ParaView plugin")
# SMP (vtkSMPTools): ON for TBB ParaView, OFF for official ParaView (avoids crash)
if(ParaView_DIR MATCHES "tbb")
  set(VESPA_USE_SMP_DEFAULT ON)
else()
  set(VESPA_USE_SMP_DEFAULT OFF)
endif()
option(VESPA_USE_SMP "Use VTK SMP in DensityBasedSampler (disable if plugin crashes with official ParaView)" ${VESPA_USE_SMP_DEFAULT})

option(VESPA_USE_VMTK "Build SHYX VMTK centerline filter (VMTK must match ParaView VTK)" ON)
set(VMTK_DIR "C:/SoftWare/vmtk-install" CACHE PATH
  "VMTK install prefix: include/vmtk and lib (e.g. vtkvmtk*.lib) live beneath this directory")

# Plugin 模式下将 vespa 模块全部编译为静态库并链接进 VESPAPlugin.dll，无需再配置 build\bin\Release 到 PATH
if (VESPA_BUILD_PV_PLUGIN)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries; OFF when building ParaView plugin so all modules are linked into VESPAPlugin.dll" FORCE)
    message(STATUS "VESPA: Building ParaView plugin with static vespa modules (all linked into VESPAPlugin.dll)")
endif()

include(CTest)

## Deps

find_package(Ceres 2.0 QUIET)
message(STATUS "Found Ceres ${Ceres_VERSION}")
option(USE_CERES "Use Ceres library" ${CERES_FOUND})
if (${USE_CERES} AND NOT ${CERES_FOUND})
  message(FATAL_ERROR "USE_CERES is set to ON, but Ceres in considered NOT found.")
endif()

find_package(Eigen3 3.2.0 REQUIRED)
message(STATUS "Found Eigen3 ${Eigen3_VERSION}")

option(VESPA_USE_MKL "Use Intel MKL for Clebsch Map Filter (Eigen BLAS/LAPACK + Pardiso)" ON)
# MKL 默认线程层为 intel_thread（依赖 libiomp5md）。设为 tbb_thread 则与 oneTBB 链接，需 TBB_DIR 可被 find_package(TBB CONFIG) 找到。
option(VESPA_MKL_THREADING_TBB "Use oneTBB as Intel MKL threading layer (avoids Intel OpenMP runtime for MKL)" ON)
if (VESPA_USE_MKL)
  set(MKL_LINK "static" CACHE STRING "MKL link type: static, dynamic, or sdl")
  set(MKL_INTERFACE "lp64" CACHE STRING "MKL interface: lp64 or ilp64")
  if (VESPA_MKL_THREADING_TBB)
    set(MKL_THREADING "tbb_thread" CACHE STRING "oneMKL threading: intel_thread, gnu_thread, tbb_thread, sequential" FORCE)
    message(STATUS "VESPA: MKL threading layer set to tbb_thread (oneTBB)")
  else ()
    set(MKL_THREADING "intel_thread" CACHE STRING "oneMKL threading: intel_thread, gnu_thread, tbb_thread, sequential" FORCE)
    message(STATUS "VESPA: MKL threading layer set to intel_thread (Intel OpenMP)")
  endif ()
  find_package(MKL CONFIG QUIET PATHS ${MKL_DIR})
  if (MKL_FOUND)
    message(STATUS "VESPA_USE_MKL: Intel MKL found, Clebsch filter will use MKL acceleration")
  else()
    message(WARNING "VESPA_USE_MKL is ON but MKL not found. Set MKL_DIR (e.g. ${MKL_DIR})")
    set(VESPA_USE_MKL OFF)
  endif()
endif()

find_package(CGAL 5.3.0 REQUIRED)
message(STATUS "Found CGAL ${CGAL_VERSION}")
include(CGAL_Eigen3_support)
# CGAL TBB parallel support (for pca_estimate_normals etc.)
find_package(TBB QUIET)
include(CGAL_TBB_support)
if(TARGET CGAL::TBB_support)
  message(STATUS "CGAL TBB parallel support: ENABLED")
else()
  message(STATUS "CGAL TBB parallel support: disabled (TBB not found)")
endif()

# TetGen library for tetrahedral meshing (SHYX TetGen filter)
add_subdirectory(${CMAKE_SOURCE_DIR}/TetGen-1.6.0 ${CMAKE_BINARY_DIR}/TetGen-1.6.0)
message(STATUS "TetGen: enabled for SHYX surface-to-volume meshing")
# tet must be in export set since vtkCGALPMP (exported) links to it
install(TARGETS tet EXPORT vespa
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT development
)

## VTK Module
## ----------


if (VESPA_BUILD_PV_PLUGIN)
  if(VTK_DIR AND NOT ParaView_DIR)
    # old VTK_DIR
    unset(VTK_DIR CACHE)
  endif()
  # Qt6 不再提供 xmlpatterns；ParaView 6 生成插件帮助文档时仍需要 Qt5 的 xmlpatterns.exe。
  # 见 ParaView 安装树中 ParaViewClient.cmake（paraview_client_documentation）。
  # 须为「Qt5 根目录」：ParaView 会在 ${hint}/msvc2017_64/bin 等路径下查找 xmlpatterns。
  set(_vespa_qt5_xmlpatterns_root "C:/SoftWare/QT")
  if (EXISTS "${_vespa_qt5_xmlpatterns_root}/msvc2017_64/bin/xmlpatterns.exe")
    set(ParaView_QT5_XMLPATTERNS_HINT "${_vespa_qt5_xmlpatterns_root}" CACHE PATH
      "Qt5 安装根目录（其下 msvc*_64/bin 含 xmlpatterns.exe，供 Qt6 版 ParaView 的插件文档生成使用）" FORCE)
  elseif (NOT DEFINED ParaView_QT5_XMLPATTERNS_HINT)
    set(ParaView_QT5_XMLPATTERNS_HINT "${_vespa_qt5_xmlpatterns_root}" CACHE PATH
      "Qt5 安装根目录（其下 msvc*_64/bin 含 xmlpatterns.exe，供 Qt6 版 ParaView 的插件文档生成使用）")
  endif()
  unset(_vespa_qt5_xmlpatterns_root)
  find_package(ParaView REQUIRED)
  message(STATUS "Found ParaView ${ParaView_VERSION}")
else()
  find_package(VTK
    COMPONENTS
      CommonCore
      CommonDataModel
      CommonExecutionModel
      FiltersCore
      FiltersExtraction
      FiltersGeometry
      freetype
      Python
    OPTIONAL_COMPONENTS
      CommonSystem
      IOXML
      TestingCore)
  message(STATUS "Found VTK ${VTK_VERSION}")
endif()

## Options
option(FORCE_STATIC_MODULES "Build VTK modules statically" OFF)
mark_as_advanced(FORCE_STATIC_MODULES)
if (FORCE_STATIC_MODULES)
  set(FORCE_STATIC_MODULES_STRING FORCE_STATIC)
else ()
  set(FORCE_STATIC_MODULES_STRING)
endif()

option(BUILD_PYTHON_WRAPPERS "Build python wrapping" OFF)
if(BUILD_PYTHON_WRAPPERS)
  vtk_module_python_default_destination(python_destination)
endif()

## Module build
vtk_module_find_modules(vtkcgal_module_files "${CMAKE_CURRENT_SOURCE_DIR}/vespa")
if(NOT VESPA_USE_VMTK)
  list(FILTER vtkcgal_module_files EXCLUDE REGEX "VmtkPolyDataCenterlines")
endif()
if(VESPA_USE_VMTK)
  message(STATUS "VESPA: VMTK centerlines enabled (VMTK_DIR=${VMTK_DIR})")
endif()
# CGAL 6.0+ required for Adaptive Isotropic Remesher; exclude its module on older CGAL
if (NOT CGAL_VERSION VERSION_GREATER_EQUAL 6.0)
  list(FILTER vtkcgal_module_files EXCLUDE REGEX "AdaptiveIsotropicRemesher")
  set(VESPA_ADAPTIVE_REMESHING OFF CACHE INTERNAL "vtkSHYXAdaptiveIsotropicRemesher state" FORCE)
else()
  set(VESPA_ADAPTIVE_REMESHING ON CACHE INTERNAL "vtkSHYXAdaptiveIsotropicRemesher state" FORCE)
endif()
# CGAL 5.5+ required: vtkCGALAlphaWrapping and SHYX Selection / Fill / Alpha / Union pipeline
if (NOT CGAL_VERSION VERSION_GREATER_EQUAL 5.5)
  list(FILTER vtkcgal_module_files EXCLUDE REGEX "SelectionFillAlphaReunion")
endif()
# ParaView-only: Representation modules depend on ParaView::RemotingViews (skip when building VTK-only)
if (NOT VESPA_BUILD_PV_PLUGIN)
  list(FILTER vtkcgal_module_files EXCLUDE REGEX "Representation")
endif ()

if(BUILD_TESTING)
  set(_vespa_enable_tests "ON")
else()
  set(_vespa_enable_tests "OFF")
endif()

vtk_module_scan(
  MODULE_FILES     ${vtkcgal_module_files}
  PROVIDES_MODULES vtkcgal_provided_modules
  WANT_BY_DEFAULT  ON
  ENABLE_TESTS     ${_vespa_enable_tests})

vtk_module_build(
  MODULES         ${vtkcgal_provided_modules}
  INSTALL_EXPORT  vespa
  INSTALL_HEADERS ON
  CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vespa"
  HEADERS_DESTINATION "include")

## Install
set(vespaExport ${vtkcgal_provided_modules})

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/CMake/vespa.cmake.in
    ${PROJECT_BINARY_DIR}/vespa-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vespa
)

write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/vespa-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
  FILES       ${CMAKE_BINARY_DIR}/vespa-config.cmake
              ${CMAKE_BINARY_DIR}/vespa-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vespa
  COMPONENT   development)

## ParaView plugin
## ---------------

if (VESPA_BUILD_PV_PLUGIN)

  # disable unsupported python wrapping
  if (NOT TARGET VTK::PythonUsed)
    message(STATUS "ParaView has python disabled, no python wrapping available.")
    set(BUILD_PYTHON_WRAPPERS OFF CACHE BOOL "Python wrapping (disabled)" FORCE)
  endif()

  set("_paraview_plugin_default_${CMAKE_PROJECT_NAME}" ON)
  paraview_plugin_scan(
    PLUGIN_FILES      "${CMAKE_SOURCE_DIR}/ParaViewPlugin/paraview.plugin"
    ENABLE_BY_DEFAULT ON
    PROVIDES_PLUGINS  vespa_plugins
    REQUIRES_MODULES  vespa_required_modules)

  foreach (module IN LISTS vespa_required_modules)
    if (NOT TARGET "${module}")
      message("${CMAKE_PROJECT_NAME}: missing required module ${module}")
      return ()
    endif ()
  endforeach ()

  # 插件目标必须是 SHARED(DLL) 供 ParaView 加载；vespa 模块已在上面 vtk_module_build 时建为静态库，会链接进此 DLL
  set(BUILD_SHARED_LIBS ON)

  paraview_plugin_build(
    TARGET VESPAPVPLUGIN
    PLUGINS ${vespa_plugins}
    INSTALL_HEADERS OFF
    RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY_SUBDIRECTORY "${PARAVIEW_PLUGIN_SUBDIR}"
  )

endif()


## Python wrapping
## ---------------

if(BUILD_PYTHON_WRAPPERS)
  vtk_module_wrap_python(
    MODULES         ${vtkcgal_provided_modules}
    WRAPPED_MODULES vtkcgal_wrapped_modules
    TARGET          VESPA::Python
    INSTALL_EXPORT  vespaPython
    PYTHON_PACKAGE  "vespa"
    MODULE_DESTINATION  "${python_destination}"
    CMAKE_DESTINATION   "${CMAKE_INSTALL_LIBDIR}/cmake/vespaPython"
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
    OUTPUT  "${CMAKE_BINARY_DIR}/${python_destination}/vespa/__init__.py"
    CONTENT "${InitContent}")
  install(
    FILES       "${CMAKE_BINARY_DIR}/${python_destination}/vespa/__init__.py"
    DESTINATION "${python_destination}/vespa/")

  # Install vespaPython
  export(
    EXPORT    vespaPython
    NAMESPACE VESPA::
    FILE "${CMAKE_BINARY_DIR}/${python_destination}/vespaPython/vespaPython-targets.cmake")
  install(
    EXPORT    vespaPython
    NAMESPACE VESPA::
    FILE      vespaPython-targets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vespaPython")

endif()
