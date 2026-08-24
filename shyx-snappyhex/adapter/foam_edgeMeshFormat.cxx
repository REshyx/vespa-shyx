// SHYX replacement for meshTools edgeMeshFormat.C.
// Official read() constructs dummy Time::New() for IOobject; that abort()s
// ParaView after snappy prints "Reading features." OpenFOAM tree stays pristine.

#include "edgeMeshFormat.H"
#include "IFstream.H"
#include "IOobject.H"
#include "OFstream.H"

#include "foam_skip_foamfile.H"

Foam::fileFormats::edgeMeshFormat::edgeMeshFormat(const fileName& filename)
{
    read(filename);
}

bool Foam::fileFormats::edgeMeshFormat::read(const fileName& filename)
{
    clear();

    IFstream is(filename);
    if (!is.good())
    {
        FatalErrorInFunction << "Cannot read file " << filename << exit(FatalError);
    }

    shyx_skip_foamfile_header(is);
    return read(is, this->storedPoints(), this->storedEdges());
}

bool Foam::fileFormats::edgeMeshFormat::read(
    Istream& is, pointField& pointLst, edgeList& edgeLst)
{
    if (!is.good())
    {
        FatalErrorInFunction << "read error " << exit(FatalError);
    }

    is >> pointLst;
    is >> edgeLst;
    return true;
}

Foam::Ostream& Foam::fileFormats::edgeMeshFormat::write(
    Ostream& os, const pointField& pointLst, const edgeList& edgeLst)
{
    if (!os.good())
    {
        FatalErrorInFunction << "bad output stream " << os.name() << exit(FatalError);
    }

    os << "\n// points:" << nl << pointLst << nl << "\n// edges:" << nl << edgeLst << nl;
    IOobject::writeDivider(os);
    os.check(FUNCTION_NAME);
    return os;
}

void Foam::fileFormats::edgeMeshFormat::write(const fileName& filename,
    const edgeMesh& mesh, IOstreamOption streamOpt, const dictionary&)
{
    OFstream os(filename, streamOpt);
    if (!os.good())
    {
        FatalIOErrorInFunction(os) << "Cannot open file for writing " << filename
                                   << exit(FatalIOError);
    }

    os << "FoamFile\n{\n    version     2.0;\n    format      ascii;\n"
          "    class       featureEdgeMesh;\n    object      features;\n}\n";
    write(os, mesh.points(), mesh.edges());
    os.check(FUNCTION_NAME);
}
