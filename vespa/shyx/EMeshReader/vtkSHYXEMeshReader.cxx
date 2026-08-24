#include "vtkSHYXEMeshReader.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDataObject.h"
#include "vtkFieldData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkStringArray.h"

#include <vtksys/FStream.hxx>
#include <vtksys/SystemTools.hxx>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXEMeshReader);

namespace
{
enum
{
  EDGE_EXTERNAL = 0,
  EDGE_INTERNAL = 1,
  EDGE_FLAT = 2,
  EDGE_OPEN = 3,
  EDGE_MULTIPLE = 4
};

enum
{
  POINT_CONVEX = 0,
  POINT_CONCAVE = 1,
  POINT_MIXED = 2,
  POINT_NONFEATURE = 3
};

bool EqualCI(const std::string& a, const char* b)
{
  if (!b)
  {
    return false;
  }
  const size_t n = std::strlen(b);
  if (a.size() != n)
  {
    return false;
  }
  for (size_t i = 0; i < n; ++i)
  {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
      std::tolower(static_cast<unsigned char>(b[i])))
    {
      return false;
    }
  }
  return true;
}

bool IsEMeshClass(const std::string& cls)
{
  return EqualCI(cls, "featureEdgeMesh") || EqualCI(cls, "edgeMesh") ||
    EqualCI(cls, "extendedFeatureEdgeMesh") || EqualCI(cls, "extendedEdgeMesh");
}

bool IsExtendedClass(const std::string& cls)
{
  return EqualCI(cls, "extendedFeatureEdgeMesh") || EqualCI(cls, "extendedEdgeMesh");
}

const char* EdgeStatusName(int s)
{
  switch (s)
  {
    case EDGE_EXTERNAL:
      return "external";
    case EDGE_INTERNAL:
      return "internal";
    case EDGE_FLAT:
      return "flat";
    case EDGE_OPEN:
      return "open";
    case EDGE_MULTIPLE:
      return "multiple";
    default:
      return "none";
  }
}

const char* PointStatusName(int s)
{
  switch (s)
  {
    case POINT_CONVEX:
      return "convex";
    case POINT_CONCAVE:
      return "concave";
    case POINT_MIXED:
      return "mixed";
    default:
      return "nonFeature";
  }
}

bool SuffixCI(const std::string& path, const char* suffix)
{
  const size_t n = std::strlen(suffix);
  if (path.size() < n)
  {
    return false;
  }
  for (size_t i = 0; i < n; ++i)
  {
    const char a = static_cast<char>(
      std::tolower(static_cast<unsigned char>(path[path.size() - n + i])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
    if (a != b)
    {
      return false;
    }
  }
  return true;
}

bool PathLooksLikeEMesh(const char* path)
{
  if (!path || !path[0])
  {
    return false;
  }
  const std::string p(path);
  return SuffixCI(p, ".eMesh") || SuffixCI(p, ".extendedFeatureEdgeMesh") ||
    SuffixCI(p, ".extendedEdgeMesh");
}

bool LoadFileText(const char* path, std::string* text, std::string* err)
{
  vtksys::ifstream is(path, std::ios::binary);
  if (!is)
  {
    *err = std::string("Cannot open ") + path;
    return false;
  }
  is.seekg(0, std::ios::end);
  const std::streamoff n = is.tellg();
  is.seekg(0, std::ios::beg);
  if (n < 0)
  {
    *err = "Cannot determine file size";
    return false;
  }
  std::vector<char> raw(static_cast<size_t>(n));
  if (n > 0 && !is.read(raw.data(), static_cast<std::streamsize>(n)))
  {
    *err = "Cannot read file";
    return false;
  }
  const bool gzip = raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0x1f &&
    static_cast<unsigned char>(raw[1]) == 0x8b;
  if (gzip)
  {
    *err = "Gzip eMesh is not supported (no vtkzlib). Decompress the file first "
           "(gunzip / 7-Zip) and open the ASCII .eMesh.";
    return false;
  }
  text->assign(raw.begin(), raw.end());
  if (text->size() >= 3 && static_cast<unsigned char>((*text)[0]) == 0xef &&
    static_cast<unsigned char>((*text)[1]) == 0xbb &&
    static_cast<unsigned char>((*text)[2]) == 0xbf)
  {
    text->erase(0, 3);
  }
  return true;
}

struct Lexer
{
  const char* s = nullptr;
  size_t n = 0;
  size_t i = 0;
  std::string err;

  void Init(const std::string& t)
  {
    s = t.c_str();
    n = t.size();
    i = 0;
    err.clear();
  }

  bool Fail(const char* msg)
  {
    err = msg;
    return false;
  }

  void Skip()
  {
    while (i < n)
    {
      const char c = s[i];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      {
        ++i;
        continue;
      }
      if (c == '/' && i + 1 < n && s[i + 1] == '/')
      {
        i += 2;
        while (i < n && s[i] != '\n')
        {
          ++i;
        }
        continue;
      }
      if (c == '/' && i + 1 < n && s[i + 1] == '*')
      {
        i += 2;
        while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/'))
        {
          ++i;
        }
        i = (i + 1 < n) ? i + 2 : n;
        continue;
      }
      break;
    }
  }

  bool AtEnd()
  {
    Skip();
    return i >= n;
  }

  char Peek()
  {
    Skip();
    return i < n ? s[i] : '\0';
  }

  bool Take(char c)
  {
    if (Peek() != c)
    {
      err = std::string("expected '") + c + "'";
      return false;
    }
    ++i;
    return true;
  }

  bool ReadQuoted(std::string* w)
  {
    if (!Take('"'))
    {
      return false;
    }
    w->clear();
    while (i < n && s[i] != '"')
    {
      if (s[i] == '\\' && i + 1 < n)
      {
        w->push_back(s[i + 1]);
        i += 2;
        continue;
      }
      w->push_back(s[i]);
      ++i;
    }
    if (i >= n)
    {
      return Fail("unterminated string");
    }
    ++i;
    return true;
  }

  bool ReadToken(std::string* w)
  {
    Skip();
    if (i >= n)
    {
      return Fail("unexpected end of file");
    }
    if (s[i] == '"')
    {
      return ReadQuoted(w);
    }
    const char c = s[i];
    if (c == '(' || c == ')' || c == '{' || c == '}' || c == ';')
    {
      w->assign(1, c);
      ++i;
      return true;
    }
    w->clear();
    while (i < n)
    {
      const char d = s[i];
      if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '(' || d == ')' || d == '{' ||
        d == '}' || d == ';' || d == '"')
      {
        break;
      }
      if (d == '/' && i + 1 < n && (s[i + 1] == '/' || s[i + 1] == '*'))
      {
        break;
      }
      w->push_back(d);
      ++i;
    }
    return !w->empty() || Fail("empty token");
  }

  bool ReadInt(int* v)
  {
    std::string t;
    if (!ReadToken(&t))
    {
      return false;
    }
    char* end = nullptr;
    const long x = std::strtol(t.c_str(), &end, 10);
    if (!end || *end != '\0')
    {
      err = "expected integer, got " + t;
      return false;
    }
    *v = static_cast<int>(x);
    return true;
  }

  bool ReadDouble(double* v)
  {
    std::string t;
    if (!ReadToken(&t))
    {
      return false;
    }
    char* end = nullptr;
    *v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0')
    {
      err = "expected number, got " + t;
      return false;
    }
    return true;
  }

  bool SkipBalanced(char openCh, char closeCh)
  {
    if (!Take(openCh))
    {
      return false;
    }
    int depth = 1;
    while (i < n && depth > 0)
    {
      if (s[i] == '"')
      {
        std::string tmp;
        if (!ReadQuoted(&tmp))
        {
          return false;
        }
        continue;
      }
      if (s[i] == '/' && i + 1 < n && (s[i + 1] == '/' || s[i + 1] == '*'))
      {
        Skip();
        continue;
      }
      if (s[i] == openCh)
      {
        ++depth;
      }
      else if (s[i] == closeCh)
      {
        --depth;
      }
      ++i;
    }
    return depth == 0 || Fail("unbalanced list/dictionary");
  }

  bool SkipValue()
  {
    const char c = Peek();
    if (c == '"')
    {
      std::string tmp;
      return ReadQuoted(&tmp);
    }
    if (c == '{')
    {
      return SkipBalanced('{', '}');
    }
    if (c == '(')
    {
      return SkipBalanced('(', ')');
    }
    std::string t;
    return ReadToken(&t);
  }

  void SkipSemicolon()
  {
    if (Peek() == ';')
    {
      ++i;
    }
  }

  bool ParseFoamFile(std::string* cls, std::string* fmt, bool* found)
  {
    *found = false;
    cls->clear();
    fmt->assign("ascii");
    Skip();
    if (i >= n)
    {
      return true;
    }
    const size_t saved = i;
    std::string tok;
    if (!ReadToken(&tok))
    {
      i = saved;
      err.clear();
      return true;
    }
    if (!EqualCI(tok, "FoamFile"))
    {
      i = saved;
      return true;
    }
    *found = true;
    if (!Take('{'))
    {
      return false;
    }
    while (!AtEnd() && Peek() != '}')
    {
      std::string key;
      if (!ReadToken(&key))
      {
        return false;
      }
      std::string val;
      if (Peek() == '{')
      {
        if (!SkipBalanced('{', '}'))
        {
          return false;
        }
      }
      else if (Peek() == '"')
      {
        if (!ReadQuoted(&val))
        {
          return false;
        }
      }
      else
      {
        if (!ReadToken(&val))
        {
          return false;
        }
      }
      SkipSemicolon();
      if (EqualCI(key, "class"))
      {
        *cls = val;
      }
      else if (EqualCI(key, "format"))
      {
        *fmt = val;
      }
    }
    return Take('}');
  }

  bool ReadPoints(std::vector<double>* xyz)
  {
    int np = 0;
    if (!ReadInt(&np) || np < 0)
    {
      return Fail("bad point count");
    }
    if (!Take('('))
    {
      return false;
    }
    xyz->resize(static_cast<size_t>(np) * 3);
    for (int k = 0; k < np; ++k)
    {
      if (!Take('(') || !ReadDouble(&(*xyz)[static_cast<size_t>(k) * 3]) ||
        !ReadDouble(&(*xyz)[static_cast<size_t>(k) * 3 + 1]) ||
        !ReadDouble(&(*xyz)[static_cast<size_t>(k) * 3 + 2]) || !Take(')'))
      {
        return false;
      }
    }
    return Take(')');
  }

  bool ReadEdges(std::vector<int>* edges)
  {
    int ne = 0;
    if (!ReadInt(&ne) || ne < 0)
    {
      return Fail("bad edge count");
    }
    if (!Take('('))
    {
      return false;
    }
    edges->resize(static_cast<size_t>(ne) * 2);
    for (int k = 0; k < ne; ++k)
    {
      if (!Take('(') || !ReadInt(&(*edges)[static_cast<size_t>(k) * 2]) ||
        !ReadInt(&(*edges)[static_cast<size_t>(k) * 2 + 1]) || !Take(')'))
      {
        return false;
      }
    }
    return Take(')');
  }

  bool SkipList()
  {
    int count = 0;
    if (!ReadInt(&count) || count < 0)
    {
      return false;
    }
    if (Peek() == '{')
    {
      return SkipValue();
    }
    if (!Take('('))
    {
      return false;
    }
    for (int k = 0; k < count; ++k)
    {
      if (!SkipValue())
      {
        return false;
      }
    }
    return Take(')');
  }

  bool ReadLabels(std::vector<int>* ids)
  {
    int count = 0;
    if (!ReadInt(&count) || count < 0)
    {
      return false;
    }
    if (!Take('('))
    {
      return false;
    }
    ids->resize(static_cast<size_t>(count));
    for (int k = 0; k < count; ++k)
    {
      if (!ReadInt(&(*ids)[static_cast<size_t>(k)]))
      {
        return false;
      }
    }
    return Take(')');
  }
};

struct Mesh
{
  std::vector<double> xyz;
  std::vector<int> edges;
  std::string foamClass;
  bool extended = false;
  bool haveStarts = false;
  int concaveStart = 0;
  int mixedStart = 0;
  int nonFeatureStart = 0;
  int internalStart = 0;
  int flatStart = 0;
  int openStart = 0;
  int multipleStart = 0;
  std::vector<int> regionEdges;
};

bool ParseMesh(const std::string& text, Mesh* mesh, std::string* err)
{
  Lexer lx;
  lx.Init(text);
  std::string cls;
  std::string fmt;
  bool hasHeader = false;
  if (!lx.ParseFoamFile(&cls, &fmt, &hasHeader))
  {
    *err = lx.err.empty() ? "invalid FoamFile header" : lx.err;
    return false;
  }
  if (hasHeader)
  {
    if (EqualCI(fmt, "binary"))
    {
      *err = "binary Foam format is not supported; convert the file to ascii";
      return false;
    }
    if (!cls.empty() && !IsEMeshClass(cls))
    {
      *err = "FoamFile class '" + cls + "' is not featureEdgeMesh/extendedFeatureEdgeMesh";
      return false;
    }
  }
  mesh->foamClass = cls.empty() ? "featureEdgeMesh" : cls;
  mesh->extended = IsExtendedClass(mesh->foamClass);
  if (!lx.ReadPoints(&mesh->xyz))
  {
    *err = lx.err.empty() ? "failed to read points" : lx.err;
    return false;
  }
  if (!lx.ReadEdges(&mesh->edges))
  {
    *err = lx.err.empty() ? "failed to read edges" : lx.err;
    return false;
  }
  const int nPts = static_cast<int>(mesh->xyz.size() / 3);
  const int nEd = static_cast<int>(mesh->edges.size() / 2);
  mesh->nonFeatureStart = nPts;
  mesh->internalStart = nEd;
  mesh->flatStart = nEd;
  mesh->openStart = nEd;
  mesh->multipleStart = nEd;
  if (lx.AtEnd())
  {
    return true;
  }
  const size_t saved = lx.i;
  if (lx.ReadInt(&mesh->concaveStart) && lx.ReadInt(&mesh->mixedStart) &&
    lx.ReadInt(&mesh->nonFeatureStart) && lx.ReadInt(&mesh->internalStart) &&
    lx.ReadInt(&mesh->flatStart) && lx.ReadInt(&mesh->openStart) &&
    lx.ReadInt(&mesh->multipleStart))
  {
    mesh->haveStarts = true;
    mesh->extended = true;
    if (EqualCI(mesh->foamClass, "featureEdgeMesh") || EqualCI(mesh->foamClass, "edgeMesh"))
    {
      mesh->foamClass = "extendedFeatureEdgeMesh";
    }
    for (int k = 0; k < 7 && !lx.AtEnd(); ++k)
    {
      if (!lx.SkipList())
      {
        lx.err.clear();
        break;
      }
    }
    if (!lx.AtEnd())
    {
      const size_t savedReg = lx.i;
      if (!lx.ReadLabels(&mesh->regionEdges))
      {
        lx.i = savedReg;
        mesh->regionEdges.clear();
      }
    }
  }
  else
  {
    lx.i = saved;
    lx.err.clear();
  }
  return true;
}

bool HeaderlessLooksLikeLists(const std::string& text)
{
  Lexer lx;
  lx.Init(text);
  int count = 0;
  return lx.ReadInt(&count) && count >= 0 && lx.Peek() == '(';
}

bool TextLooksLikeEMesh(const std::string& text, const char* path)
{
  Lexer lx;
  lx.Init(text);
  std::string cls;
  std::string fmt;
  bool found = false;
  if (!lx.ParseFoamFile(&cls, &fmt, &found))
  {
    return PathLooksLikeEMesh(path) && HeaderlessLooksLikeLists(text);
  }
  if (found)
  {
    if (EqualCI(fmt, "binary"))
    {
      return false;
    }
    return IsEMeshClass(cls);
  }
  return PathLooksLikeEMesh(path) && HeaderlessLooksLikeLists(text);
}

int ClassifyEdge(int i, const Mesh& m)
{
  if (i < m.internalStart)
  {
    return EDGE_EXTERNAL;
  }
  if (i < m.flatStart)
  {
    return EDGE_INTERNAL;
  }
  if (i < m.openStart)
  {
    return EDGE_FLAT;
  }
  if (i < m.multipleStart)
  {
    return EDGE_OPEN;
  }
  return EDGE_MULTIPLE;
}

int ClassifyPoint(int i, const Mesh& m)
{
  if (i < m.concaveStart)
  {
    return POINT_CONVEX;
  }
  if (i < m.mixedStart)
  {
    return POINT_CONCAVE;
  }
  if (i < m.nonFeatureStart)
  {
    return POINT_MIXED;
  }
  return POINT_NONFEATURE;
}

void AddFieldInt(vtkFieldData* fd, const char* name, int value)
{
  vtkNew<vtkIntArray> arr;
  arr->SetName(name);
  arr->SetNumberOfTuples(1);
  arr->SetValue(0, value);
  fd->AddArray(arr);
}
} // namespace

vtkSHYXEMeshReader::vtkSHYXEMeshReader()
{
  this->FileName = nullptr;
  this->SetNumberOfInputPorts(0);
  this->SetNumberOfOutputPorts(1);
}

vtkSHYXEMeshReader::~vtkSHYXEMeshReader()
{
  this->SetFileName(nullptr);
}

void vtkSHYXEMeshReader::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FileName: " << (this->FileName ? this->FileName : "(null)") << "\n";
}

int vtkSHYXEMeshReader::CanReadFile(const char* fname)
{
  if (!fname || !fname[0] || !vtksys::SystemTools::FileExists(fname, true))
  {
    return 0;
  }
  std::string text;
  std::string err;
  if (!LoadFileText(fname, &text, &err) || text.empty())
  {
    return 0;
  }
  return TextLooksLikeEMesh(text, fname) ? 1 : 0;
}

int vtkSHYXEMeshReader::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXEMeshReader::RequestData(
  vtkInformation*, vtkInformationVector**, vtkInformationVector* outputVector)
{
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!output)
  {
    vtkErrorMacro(<< "Missing output.");
    return 0;
  }
  output->Initialize();
  if (!this->FileName || !this->FileName[0])
  {
    vtkErrorMacro(<< "No FileName.");
    return 0;
  }

  std::string text;
  std::string err;
  if (!LoadFileText(this->FileName, &text, &err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  Mesh mesh;
  if (!ParseMesh(text, &mesh, &err))
  {
    vtkErrorMacro(<< err);
    return 0;
  }

  const int nPts = static_cast<int>(mesh.xyz.size() / 3);
  const int nEd = static_cast<int>(mesh.edges.size() / 2);
  for (int e = 0; e < nEd; ++e)
  {
    const int a = mesh.edges[static_cast<size_t>(e) * 2];
    const int b = mesh.edges[static_cast<size_t>(e) * 2 + 1];
    if (a < 0 || b < 0 || a >= nPts || b >= nPts)
    {
      vtkErrorMacro(<< "Edge " << e << " has invalid point ids (" << a << ", " << b << ").");
      return 0;
    }
  }

  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(nPts);
  for (int i = 0; i < nPts; ++i)
  {
    pts->SetPoint(i, mesh.xyz[static_cast<size_t>(i) * 3],
      mesh.xyz[static_cast<size_t>(i) * 3 + 1], mesh.xyz[static_cast<size_t>(i) * 3 + 2]);
  }
  vtkNew<vtkCellArray> lines;
  for (int e = 0; e < nEd; ++e)
  {
    const vtkIdType ids[2] = { mesh.edges[static_cast<size_t>(e) * 2],
      mesh.edges[static_cast<size_t>(e) * 2 + 1] };
    lines->InsertNextCell(2, ids);
  }
  output->SetPoints(pts);
  output->SetLines(lines);

  if (mesh.extended || mesh.haveStarts)
  {
    vtkNew<vtkIntArray> edgeStatus;
    edgeStatus->SetName("EdgeStatus");
    edgeStatus->SetNumberOfTuples(nEd);
    vtkNew<vtkStringArray> edgeStatusName;
    edgeStatusName->SetName("EdgeStatusName");
    edgeStatusName->SetNumberOfTuples(nEd);
    vtkNew<vtkIntArray> regionEdge;
    regionEdge->SetName("RegionEdge");
    regionEdge->SetNumberOfTuples(nEd);
    std::vector<char> isRegion(static_cast<size_t>(nEd), 0);
    for (int id : mesh.regionEdges)
    {
      if (id >= 0 && id < nEd)
      {
        isRegion[static_cast<size_t>(id)] = 1;
      }
    }
    for (int e = 0; e < nEd; ++e)
    {
      const int st = ClassifyEdge(e, mesh);
      edgeStatus->SetValue(e, st);
      edgeStatusName->SetValue(e, EdgeStatusName(st));
      regionEdge->SetValue(e, isRegion[static_cast<size_t>(e)] ? 1 : 0);
    }
    output->GetCellData()->AddArray(edgeStatus);
    output->GetCellData()->AddArray(edgeStatusName);
    output->GetCellData()->AddArray(regionEdge);
    output->GetCellData()->SetScalars(edgeStatus);

    vtkNew<vtkIntArray> pointStatus;
    pointStatus->SetName("PointStatus");
    pointStatus->SetNumberOfTuples(nPts);
    vtkNew<vtkStringArray> pointStatusName;
    pointStatusName->SetName("PointStatusName");
    pointStatusName->SetNumberOfTuples(nPts);
    vtkNew<vtkIntArray> featurePoint;
    featurePoint->SetName("FeaturePoint");
    featurePoint->SetNumberOfTuples(nPts);
    for (int i = 0; i < nPts; ++i)
    {
      const int st = ClassifyPoint(i, mesh);
      pointStatus->SetValue(i, st);
      pointStatusName->SetValue(i, PointStatusName(st));
      featurePoint->SetValue(i, i < mesh.nonFeatureStart ? 1 : 0);
    }
    output->GetPointData()->AddArray(pointStatus);
    output->GetPointData()->AddArray(pointStatusName);
    output->GetPointData()->AddArray(featurePoint);

    vtkFieldData* fd = output->GetFieldData();
    AddFieldInt(fd, "SHYXExtendedFeatureEdgeMesh", 1);
    AddFieldInt(fd, "concaveStart", mesh.concaveStart);
    AddFieldInt(fd, "mixedStart", mesh.mixedStart);
    AddFieldInt(fd, "nonFeatureStart", mesh.nonFeatureStart);
    AddFieldInt(fd, "internalStart", mesh.internalStart);
    AddFieldInt(fd, "flatStart", mesh.flatStart);
    AddFieldInt(fd, "openStart", mesh.openStart);
    AddFieldInt(fd, "multipleStart", mesh.multipleStart);

    vtkNew<vtkStringArray> blob;
    blob->SetName("FoamExtendedFeatureEdgeMesh");
    blob->SetNumberOfTuples(1);
    blob->SetValue(0, text.c_str());
    fd->AddArray(blob);
  }

  vtkDebugMacro(<< "Read " << nPts << " points, " << nEd << " edges from " << this->FileName);
  return 1;
}

VTK_ABI_NAMESPACE_END
