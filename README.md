[![pipeline status](https://gitlab.kitware.com/vtk-cgal/vespa/badges/master/pipeline.svg)](https://gitlab.kitware.com/vtk-cgal/vespa/-/commits/master)

(V)TK (E)nhanced with (S)urface (P)rocessing (A)lgorithms

## Summary

<!--toc:start-->
- [Brief](#brief)
- [How to install](#how-to-install)
    - [VTK](#vtk)
    - [ParaView](#paraview)
    - [CGAL](#cgal)
    - [VESPA](#vespa)
- [How to use](#how-to-use)
  - [VTK Module](#vtk-module)
    - [Cope examples](#cope-examples)
  - [ParaView plugin](#paraview-plugin)
- [How to contribute](#how-to-contribute)
<!--toc:end-->

# Brief

This project contains VTK filters that use CGAL for mesh processing.
The filters can be used either as part of a VTK module or ParaView plugin.

This project is distributed under a BSD-3 License, but as it is linked to
CGAL, any binary generated with it retains the GPLv3 license.

# How to install

The VESPA project requires the following software:

* the [CMake](https://cmake.org/) build system,
* the [VTK library](https://vtk.org/) for the module or [ParaView software](https://www.paraview.org/) for the plugin,
* the [CGAL library](https://www.cgal.org/).

### VTK

VESPA needs VTK >= 9.0. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://vtk.org/Wiki/VTK/Configure_and_Build).

### ParaView

VESPA needs ParaView >= 5.10.0. In order to build plugins, ParaView needs to
be compiled from sources manually using these
[CMake instructions](https://gitlab.kitware.com/paraview/paraview/-/blob/master/Documentation/dev/build.md).

### CGAL

VESPA need CGAL >= 5.3. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://doc.cgal.org/latest/Manual/installation.html#installation_configwithcmake).

If you want to get the [Alpha Wrapping](https://doc.cgal.org/latest/Alpha_wrap_3/index.html#Chapter_3D_Alpha_wrapping)
module, CGAL >= 5.5 is required.

### VESPA

VESPA can be installed using the standard CMake procedure:

1. Create a build folder
1. Launch CMake with this repository as source folder
1. Configure the project. It will need CGAL, as well as VTK or ParaView
depending on whether only the VTK module or the full ParaView plugin should
be built.

If you want to build the ParaView plugin, set the CMake variable `VESPA_BUILD_PV_PLUGIN` to `ON`.
   - If you have installed a library in a custom folder, you can find it in CMake
     by giving the folder: **install_dir**/lib/cmake/**project-version**. For example,
     for VTK: **install_dir**/lib/cmake/vtk-9.1.
   - You can provide a custom `CMAKE_INSTALL_PREFIX` if you do not want to install
     VESPA system wide.
1. Build and install VESPA (please refer to the
   [VTK tutorial](https://vtk.org/Wiki/VTK/Configure_and_Build#Build_VTK)
   if you do not know how to proceed).

# How to use

Except when stated otherwise, filters provided by VESPA require triangulated
surfaces (`vtkPolyData` meshes). These should be watertight and 2-manifold.

### VTK Module

With VTK, you may use the
[vtkTriangleFilter](https://vtk.org/doc/nightly/html/classvtkTriangleFilter.html)
and the
[vtkGeometryFilter](https://vtk.org/doc/nightly/html/classvtkGeometryFilter.html)
to get a valid triangulation. The `vtkCGALAlphaWrapping ` filter can be used
to ensure watertight, 2-manifold mesh then.

#### Cope examples

The testing of the project may be used to get simple examples on how to use each
provided filters. For instance,
for the [Isotropic remeshing](https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__meshing__grp.html#gaa5cc92275df27f0baab2472ecbc4ea3f)
you get a [C++ example](./vespa/PolygonMeshProcessing/Testing/TestPMPIsotropicExecution.cxx)
and a [Python example](./vespa/PolygonMeshProcessing/Testing/execute_IsotropicRemesher.py)

### ParaView plugin

On ParaView, you may apply a `Tetrahedralize` and a `Extract Surface` filters
to get a valid triangulation. The `VESPA Alpha Wrapping` filter can be used
to ensure watertight, 2-manifold mesh then.

# How to contribute

If you want to contribute to this repository, simply open a Merge request
on the [gitlab instance](https://gitlab.kitware.com/vtk-cgal/vespa), we will
gladly review and help you merge your code.
