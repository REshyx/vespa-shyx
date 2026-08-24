// clang-cl mangles `char *argv[]` (OpenFOAM main) as QEAPEAD and
// `char**` as PEAPEAD. Wrap in this TU so shyx_snappy.cxx can call
// `int(int, char**)` without a cross-TU ABI mismatch.
#define main shyx_snappyHexMesh_of_main
#include SHYX_SNAPPYHEXMESH_C

#include "error.H"

int shyx_snappyHexMesh_main(int argc, char** argv)
{
    Foam::FatalError.throwExceptions();
    Foam::FatalIOError.throwExceptions();
    return shyx_snappyHexMesh_of_main(argc, argv);
}
