// SHYX replacement for meshTools extendedEdgeMeshFormat.C.
// Official read() constructs dummy Time::New() for IOobject; that abort()s
// ParaView after snappy prints "Reading features." OpenFOAM tree stays pristine.

#include "extendedEdgeMeshFormat.H"
#include "IFstream.H"
#include "extendedFeatureEdgeMesh.H"

#include "foam_skip_foamfile.H"

Foam::fileFormats::extendedEdgeMeshFormat::extendedEdgeMeshFormat(const fileName& filename)
{
    read(filename);
}

bool Foam::fileFormats::extendedEdgeMeshFormat::read(const fileName& filename)
{
    clear();

    IFstream is(filename);
    if (!is.good())
    {
        FatalErrorInFunction << "Cannot read file " << filename << exit(FatalError);
    }

    shyx_skip_foamfile_header(is);
    is >> *this;
    return is.good();
}
