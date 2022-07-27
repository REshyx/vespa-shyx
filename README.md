[![pipeline status](https://gitlab.kitware.com/vtk-cgal/vespa/badges/master/pipeline.svg)](https://gitlab.kitware.com/vtk-cgal/vespa/-/commits/master)

(V)TK (E)nhanced with (S)urface (P)rocessing (A)lgorithms

# Brief

This project contains VTK filters that use CGAL for mesh processing.
The filters can be used either as part of a VTK module or ParaView plugin.

This project is distributed under a BSD-3 License, but as it is linked to
CGAL, any binary generated with it retains the GPLv3 license.

# How to install

The VESPA project requires on your system:

* the [CMake](https://cmake.org/) build system,
* the [VTK library](https://vtk.org/) for the module or [ParaView software](https://www.paraview.org/) for the plugin,
* the [CGAL library](https://www.cgal.org/).

### VTK

We need VTK >= 9.0. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://vtk.org/Wiki/VTK/Configure_and_Build).

### ParaView

We need ParaView >= 5.10.0. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* by downloading the software [on the official website](https://www.paraview.org/download/),
* manually using [CMake instructions](https://gitlab.kitware.com/paraview/paraview/-/blob/master/Documentation/dev/build.md).

### CGAL

We need CGAL >= 5.3. It can be installed:
* using the package manager of your system (including brew on OSX, or vcpkg on Windows),
* manually using [CMake instructions](https://doc.cgal.org/latest/Manual/installation.html#installation_configwithcmake).

### VESPA

Then, we can install this project using the standard CMake procedure:

1. Create a build folder
1. Launch CMake with this repository as source folder
1. Configure the project. It will need CGAL, as well as VTK or ParaView depending on whether the VTK module or ParaView plugin should be built.
To specify which one to build, set the CMake variable `VESPA_BUILD_MODE` to `VTK` or `PARAVIEW`, respectively.
   - If you have installed a library in a custom folder, you can find it in CMake
     by giving the folder: **install_dir**/lib/cmake/**project-version**. For example,
     for VTK: **install_dir**/lib/cmake/vtk-9.1.
   - You can provide a custom `CMAKE_INSTALL_PREFIX` if you do not want to install
     this project system wide.
1. Build and install this project (please refer to the
   [VTK tutorial](https://vtk.org/Wiki/VTK/Configure_and_Build#Build_VTK)
   if you do not know how to proceed).
