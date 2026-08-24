#ifndef SHYX_FOAM_SKIP_FOAMFILE_H
#define SHYX_FOAM_SKIP_FOAMFILE_H

#include "Istream.H"
#include "dictionary.H"
#include "token.H"

// Dummy Time::New() inside edgeMeshFormat / extendedEdgeMeshFormat abort()s
// ParaView when snappyHexMesh is already running. Skip the FoamFile header
// from the real path instead of constructing a second Time.

inline bool shyx_skip_foamfile_header(Foam::Istream& is)
{
    Foam::token first(is);
    if (is.good() && first.isWord("FoamFile"))
    {
        Foam::dictionary header;
        header.read(is, false);
        Foam::word fmt;
        if (header.readIfPresent("format", fmt))
        {
            is.format(fmt);
        }
        return true;
    }
    if (first.good())
    {
        is.putBack(first);
    }
    return false;
}

#endif
