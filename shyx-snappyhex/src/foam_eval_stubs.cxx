#include "evalEntry.H"
#include "primitiveEntry.H"
#include "stringOpsEvaluate.H"
#include "token.H"

namespace Foam
{
namespace stringOps
{
string evaluate(label, const std::string& s, size_t, size_t)
{
    return string(s);
}

string evaluate(const std::string& s, size_t, size_t)
{
    return string(s);
}
} // namespace stringOps

namespace functionEntries
{
tokenList evalEntry::evaluate(const dictionary&, const string&, label, const Istream&)
{
    return tokenList();
}

tokenList evalEntry::evaluate(const dictionary&, Istream&)
{
    return tokenList();
}

bool evalEntry::execute(const dictionary&, primitiveEntry&, Istream&)
{
    return false;
}

bool evalEntry::execute(
    const dictionary&, primitiveEntry&, const string&, label, Istream&)
{
    return false;
}
} // namespace functionEntries
} // namespace Foam
