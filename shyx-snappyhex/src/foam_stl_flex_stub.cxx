// Flex-generated STLAsciiParseFlex.C is not built (no wmake OBJECTS_DIR).
// Default STL parser type 0 calls readAsciiFlex; forward to the manual parser.
#include "STLReader.H"

bool Foam::fileFormats::STLReader::readAsciiFlex(const Foam::fileName& filename)
{
    return readAsciiManual(filename);
}

bool Foam::fileFormats::STLReader::readAsciiRagel(const Foam::fileName& filename)
{
    return readAsciiManual(filename);
}
