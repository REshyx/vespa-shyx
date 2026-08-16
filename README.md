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
    - [Code examples](#code-examples)
  - [ParaView plugin](#paraview-plugin)
- [How to contribute](#how-to-contribute)
<!--toc:end-->

# Brief

This repository is a **VTK / ParaView plugin umbrella**. It started as Kitware
[VESPA](https://gitlab.kitware.com/vtk-cgal/vespa) (VTK filters on top of CGAL).
It now also ships independent modules that use **TetGen, VMTK, MKL, or VTK only**.
CGAL is one backend, not the project identity.

VTK modules live under [`vespa/`](./vespa/README.md), grouped by **author / origin**:

| Path | Origin |
|------|--------|
| `vespa/Algorithm/` | Shared VTK↔CGAL helpers (outside author folders) |
| `vespa/Core/` | Shared VTK-only helpers (`vtkVESPAAttributeTransfer`) |
| `vespa/vespa/` | Original Kitware VESPA (`vtkCGAL*`) |
| `vespa/shyx/` | SHYX vascular / volume / flow / viz filters |

Module ↔ class ↔ XML ↔ menu ↔ icon: [`vespa/INVENTORY.md`](./vespa/INVENTORY.md).

`ParaViewPlugin/` is a thin aggregator (`VESPAPlugin`) of whichever modules were built.
Third-party sources that are not VTK modules (e.g. TetGen) stay at the repository root.

License: BSD-3 for this tree; binaries that link CGAL retain GPLv3.

# How to install

The VESPA project requires the following software:

* the [CMake](https://cmake.org/) build system,
* the [VTK library](https://vtk.org/) for the module or [ParaView software](https://www.paraview.org/) for the plugin,
* the [CGAL library](https://www.cgal.org/) for original VESPA filters and SHYX filters that `DEPENDS` CGAL.

Optional backends (see CMake options): TetGen (vendored), VMTK, Intel MKL.

### VTK

VESPA needs VTK >= 9.0. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://vtk.org/Wiki/VTK/Configure_and_Build).

### ParaView

VESPA needs ParaView >= 5.10.0. In order to build plugins, ParaView needs to
be compiled from sources manually using these
[CMake instructions](https://gitlab.kitware.com/paraview/paraview/-/blob/master/Documentation/dev/build.md).

### CGAL

CGAL-backed modules need CGAL >= 5.3. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://doc.cgal.org/latest/Manual/installation.html#installation_configwithcmake).

If you want to get the [Alpha Wrapping](https://doc.cgal.org/latest/Alpha_wrap_3/index.html#Chapter_3D_Alpha_wrapping)
module, CGAL >= 5.5 is required.

### VESPA

VESPA can be installed using the standard CMake procedure:

1. Create a build folder
1. Launch CMake with this repository as source folder
1. Configure the project with VTK or ParaView. Set `VESPA_USE_CGAL=ON` (the
default) if you need CGAL-backed modules; that is the only path that calls
`find_package(CGAL)`. TetGen / VMTK / VTK-only modules do not require CGAL.

If you want to build the ParaView plugin, set the CMake variable `VESPA_BUILD_PV_PLUGIN` to `ON` (it is already ON by default in this tree).
   - If you have installed a library in a custom folder, you can find it in CMake
     by giving the folder: **install_dir**/lib/cmake/**project-version**. For example,
     for VTK: **install_dir**/lib/cmake/vtk-9.1.
   - You can provide a custom `CMAKE_INSTALL_PREFIX` if you do not want to install
     VESPA system wide.
1. Build and install VESPA (please refer to the
   [VTK tutorial](https://vtk.org/Wiki/VTK/Configure_and_Build#Build_VTK)
   if you do not know how to proceed).

# How to use

Except when stated otherwise, CGAL surface filters require triangulated
surfaces (`vtkPolyData` meshes). These should be watertight and 2-manifold.
Other modules (volume meshing, flow, representations) have their own input types.

### VTK Module

With VTK, you may use the
[vtkTriangleFilter](https://vtk.org/doc/nightly/html/classvtkTriangleFilter.html)
and the
[vtkGeometryFilter](https://vtk.org/doc/nightly/html/classvtkGeometryFilter.html)
to get a valid triangulation. The `vtkCGALAlphaWrapping` filter can be used
to ensure watertight, 2-manifold mesh then.

#### Code examples

The testing of the project may be used to get simple examples on how to use each
provided filters. For instance,
for the [Isotropic remeshing](https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__meshing__grp.html#gaa5cc92275df27f0baab2472ecbc4ea3f)
you get a [C++ example](./vespa/vespa/PolygonMeshProcessing/Testing/TestPMPIsotropicExecution.cxx)
and a [Python example](./vespa/vespa/PolygonMeshProcessing/Testing/execute_IsotropicRemesher.py).

Set the CMake variable `BUILD_TESTING` to `ON`, and pull the data files using `git lfs`.

### ParaView plugin

On ParaView, you may apply a `Tetrahedralize` and a `Extract Surface` filters
to get a valid triangulation. The `VESPA Alpha Wrapping` filter can be used
to ensure watertight, 2-manifold mesh then.

# How to contribute

Add a new first-level folder under `vespa/<your-id>/` with a standard VTK
`vtk.module` (see [`vespa/README.md`](./vespa/README.md)). Do not drop unrelated
filters into `vespa/shyx/` or `vespa/vespa/`. Register ParaView proxies in
`ParaViewPlugin/`.

Upstream Kitware VESPA changes can still go to the
[gitlab instance](https://gitlab.kitware.com/vtk-cgal/vespa).
