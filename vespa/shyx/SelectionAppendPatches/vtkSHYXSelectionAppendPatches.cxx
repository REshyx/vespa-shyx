#include "vtkSHYXSelectionAppendPatches.h"

#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkAppendPolyData.h>
#include <vtkCellData.h>
#include <vtkCompositeDataIterator.h>
#include <vtkCompositeDataSet.h>
#include <vtkCubeSource.h>
#include <vtkDataArray.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDataObjectTypes.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
#include <vtkStringArray.h>
#include <vtkTriangleFilter.h>
#include <vtkUnstructuredGrid.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXSelectionAppendPatches);

namespace
{
std::vector<std::string> SplitLines(const char* text)
{
  std::vector<std::string> lines;
  if (!text || text[0] == '\0')
  {
    return lines;
  }
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::vector<vtkIdType> ParseIdList(const std::string& text)
{
  std::vector<vtkIdType> ids;
  std::stringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ','))
  {
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
    {
      token.erase(token.begin());
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
    {
      token.pop_back();
    }
    if (token.empty())
    {
      continue;
    }
    const auto dash = token.find('-');
    if (dash != std::string::npos && dash > 0 && dash + 1 < token.size())
    {
      try
      {
        const long long a = std::stoll(token.substr(0, dash));
        const long long b = std::stoll(token.substr(dash + 1));
        const long long lo = a < b ? a : b;
        const long long hi = a < b ? b : a;
        for (long long v = lo; v <= hi; ++v)
        {
          ids.push_back(static_cast<vtkIdType>(v));
        }
      }
      catch (...)
      {
      }
      continue;
    }
    try
    {
      ids.push_back(static_cast<vtkIdType>(std::stoll(token)));
    }
    catch (...)
    {
    }
  }
  return ids;
}

bool ParseMark(const std::string& text, double& value)
{
  if (text.empty())
  {
    value = 0.0;
    return true;
  }
  try
  {
    size_t idx = 0;
    value = std::stod(text, &idx);
    while (idx < text.size() && std::isspace(static_cast<unsigned char>(text[idx])))
    {
      ++idx;
    }
    return idx == text.size();
  }
  catch (...)
  {
    return false;
  }
}

std::string DefaultPartName(unsigned int index)
{
  return std::string("geo_") + std::to_string(index);
}

std::string AssemblyNodeName(unsigned int index)
{
  return std::string("patch_") + std::to_string(index);
}

void StampConstantMark(vtkDataSet* ds, const char* arrayName, const std::string& markText)
{
  if (!ds || !arrayName || arrayName[0] == '\0')
  {
    return;
  }
  const vtkIdType nCells = ds->GetNumberOfCells();
  double numeric = 0.0;
  const bool isNumeric = ParseMark(markText, numeric);

  vtkFieldData* fd = ds->GetFieldData();
  if (isNumeric)
  {
    vtkNew<vtkDoubleArray> cells;
    cells->SetName(arrayName);
    cells->SetNumberOfTuples(nCells);
    for (vtkIdType i = 0; i < nCells; ++i)
    {
      cells->SetValue(i, numeric);
    }
    ds->GetCellData()->RemoveArray(arrayName);
    ds->GetCellData()->AddArray(cells);

    vtkNew<vtkDoubleArray> field;
    field->SetName(arrayName);
    field->SetNumberOfTuples(1);
    field->SetValue(0, numeric);
    fd->RemoveArray(arrayName);
    fd->AddArray(field);
  }
  else
  {
    vtkNew<vtkStringArray> cells;
    cells->SetName(arrayName);
    cells->SetNumberOfValues(nCells);
    for (vtkIdType i = 0; i < nCells; ++i)
    {
      cells->SetValue(i, markText.c_str());
    }
    ds->GetCellData()->RemoveArray(arrayName);
    ds->GetCellData()->AddArray(cells);

    vtkNew<vtkStringArray> field;
    field->SetName(arrayName);
    field->SetNumberOfValues(1);
    field->SetValue(0, markText.c_str());
    fd->RemoveArray(arrayName);
    fd->AddArray(field);
  }
}

vtkSmartPointer<vtkDataSet> ExtractCellsAsPatch(vtkDataSet* input, const std::vector<vtkIdType>& ids)
{
  if (!input || ids.empty())
  {
    return nullptr;
  }
  const vtkIdType nCells = input->GetNumberOfCells();
  vtkNew<vtkIdTypeArray> list;
  list->SetNumberOfTuples(static_cast<vtkIdType>(ids.size()));
  vtkIdType nKeep = 0;
  for (vtkIdType id : ids)
  {
    if (id >= 0 && id < nCells)
    {
      list->SetValue(nKeep++, id);
    }
  }
  if (nKeep == 0)
  {
    return nullptr;
  }
  list->SetNumberOfTuples(nKeep);

  vtkNew<vtkSelectionNode> node;
  node->SetFieldType(vtkSelectionNode::CELL);
  node->SetContentType(vtkSelectionNode::INDICES);
  node->SetSelectionList(list);

  vtkNew<vtkSelection> sel;
  sel->AddNode(node);

  vtkNew<vtkExtractSelection> extract;
  extract->SetInputData(0, input);
  extract->SetInputData(1, sel);
  extract->Update();
  vtkDataSet* extracted = vtkDataSet::SafeDownCast(extract->GetOutput());
  if (!extracted || extracted->GetNumberOfCells() == 0)
  {
    return nullptr;
  }

  if (auto* pd = vtkPolyData::SafeDownCast(extracted))
  {
    vtkNew<vtkPolyData> out;
    out->DeepCopy(pd);
    return out;
  }

  vtkNew<vtkGeometryFilter> geom;
  geom->SetInputData(extracted);
  geom->Update();
  vtkPolyData* surface = geom->GetOutput();
  if (!surface || surface->GetNumberOfCells() == 0)
  {
    vtkNew<vtkUnstructuredGrid> grid;
    if (auto* ug = vtkUnstructuredGrid::SafeDownCast(extracted))
    {
      grid->DeepCopy(ug);
      return grid;
    }
    return nullptr;
  }
  vtkNew<vtkPolyData> out;
  out->DeepCopy(surface);
  return out;
}

vtkSmartPointer<vtkDataSet> ExtractInverseCells(vtkDataSet* input, const std::vector<vtkIdType>& addedIds)
{
  if (!input)
  {
    return nullptr;
  }
  const vtkIdType nCells = input->GetNumberOfCells();
  auto copyInput = [input]() -> vtkSmartPointer<vtkDataSet> {
    vtkSmartPointer<vtkDataSet> copy;
    copy.TakeReference(vtkDataSet::SafeDownCast(input->NewInstance()));
    copy->DeepCopy(input);
    return copy;
  };

  vtkNew<vtkIdTypeArray> list;
  list->SetNumberOfTuples(static_cast<vtkIdType>(addedIds.size()));
  vtkIdType nKeep = 0;
  for (vtkIdType id : addedIds)
  {
    if (id >= 0 && id < nCells)
    {
      list->SetValue(nKeep++, id);
    }
  }
  if (nKeep == 0)
  {
    return copyInput();
  }
  list->SetNumberOfTuples(nKeep);

  vtkNew<vtkSelectionNode> node;
  node->SetFieldType(vtkSelectionNode::CELL);
  node->SetContentType(vtkSelectionNode::INDICES);
  node->SetSelectionList(list);
  node->GetProperties()->Set(vtkSelectionNode::INVERSE(), 1);

  vtkNew<vtkSelection> sel;
  sel->AddNode(node);

  vtkNew<vtkExtractSelection> extract;
  extract->SetInputData(0, input);
  extract->SetInputData(1, sel);
  extract->Update();
  vtkDataSet* extracted = vtkDataSet::SafeDownCast(extract->GetOutput());
  if (!extracted)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkDataSet> out;
  out.TakeReference(vtkDataSet::SafeDownCast(extracted->NewInstance()));
  out->DeepCopy(extracted);
  return out;
}

void AssignRemainder(vtkDataSet* remainder, vtkDataSet* remainOut)
{
  if (!remainOut)
  {
    return;
  }
  if (!remainder || remainder->GetNumberOfCells() == 0)
  {
    remainOut->Initialize();
    return;
  }
  if (remainOut->GetDataObjectType() == remainder->GetDataObjectType())
  {
    remainOut->DeepCopy(remainder);
  }
  else if (vtkPolyData::SafeDownCast(remainOut))
  {
    if (auto* pd = vtkPolyData::SafeDownCast(remainder))
    {
      remainOut->DeepCopy(pd);
    }
    else
    {
      vtkNew<vtkGeometryFilter> geom;
      geom->SetInputData(remainder);
      geom->Update();
      vtkPolyData* surface = geom->GetOutput();
      if (surface && surface->GetNumberOfCells() > 0)
      {
        remainOut->DeepCopy(surface);
      }
      else
      {
        remainOut->Initialize();
        return;
      }
    }
  }
  else
  {
    remainOut->Initialize();
    return;
  }

  if (remainOut->GetCellData()->GetArray("vtkOriginalCellIds"))
  {
    return;
  }
  auto* orig =
    vtkIdTypeArray::SafeDownCast(remainder->GetCellData()->GetArray("vtkOriginalCellIds"));
  if (orig && orig->GetNumberOfTuples() == remainOut->GetNumberOfCells())
  {
    vtkNew<vtkIdTypeArray> copy;
    copy->DeepCopy(orig);
    copy->SetName("vtkOriginalCellIds");
    remainOut->GetCellData()->AddArray(copy);
  }
}

std::vector<double> ParseDoubles(const std::string& text)
{
  std::vector<double> vals;
  std::stringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ','))
  {
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
    {
      token.erase(token.begin());
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
    {
      token.pop_back();
    }
    if (token.empty())
    {
      continue;
    }
    try
    {
      vals.push_back(std::stod(token));
    }
    catch (...)
    {
    }
  }
  return vals;
}

std::string KindOf(const std::vector<std::string>& kinds, size_t i)
{
  if (i >= kinds.size())
  {
    return "selection";
  }
  std::string k = kinds[i];
  for (char& c : k)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (k == "pipeline" || k == "box" || k == "sphere")
  {
    return k;
  }
  return "selection";
}

vtkSmartPointer<vtkPolyData> CopyPolyData(vtkPolyData* pd)
{
  if (!pd || pd->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkPolyData> out;
  out->DeepCopy(pd);
  return out;
}

vtkSmartPointer<vtkPolyData> TriangulateSurface(vtkDataSet* ds)
{
  if (!ds || ds->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkPolyData> surface;
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    surface = CopyPolyData(pd);
  }
  else
  {
    vtkNew<vtkGeometryFilter> geom;
    geom->SetInputData(ds);
    geom->Update();
    surface = CopyPolyData(geom->GetOutput());
  }
  if (!surface)
  {
    return nullptr;
  }
  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputData(surface);
  tri->Update();
  return CopyPolyData(tri->GetOutput());
}

vtkSmartPointer<vtkPolyData> ConvertDataObjectToPolyData(vtkDataObject* obj)
{
  if (!obj)
  {
    return nullptr;
  }
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    return TriangulateSurface(ds);
  }
  auto* cds = vtkCompositeDataSet::SafeDownCast(obj);
  if (!cds)
  {
    return nullptr;
  }
  vtkNew<vtkAppendPolyData> append;
  int nAdded = 0;
  vtkSmartPointer<vtkCompositeDataIterator> it;
  it.TakeReference(cds->NewIterator());
  it->SkipEmptyNodesOn();
  std::vector<vtkSmartPointer<vtkPolyData>> keep;
  for (it->InitTraversal(); !it->IsDoneWithTraversal(); it->GoToNextItem())
  {
    vtkSmartPointer<vtkPolyData> piece = ConvertDataObjectToPolyData(it->GetCurrentDataObject());
    if (!piece)
    {
      continue;
    }
    keep.push_back(piece);
    append->AddInputData(piece);
    ++nAdded;
  }
  if (nAdded == 0)
  {
    return nullptr;
  }
  append->Update();
  return CopyPolyData(append->GetOutput());
}

void AabbToUnitCubeMatrix(const double b[6], vtkMatrix4x4* mat)
{
  mat->Identity();
  mat->SetElement(0, 0, b[1] - b[0]);
  mat->SetElement(1, 1, b[3] - b[2]);
  mat->SetElement(2, 2, b[5] - b[4]);
  mat->SetElement(0, 3, 0.5 * (b[0] + b[1]));
  mat->SetElement(1, 3, 0.5 * (b[2] + b[3]));
  mat->SetElement(2, 3, 0.5 * (b[4] + b[5]));
}

vtkSmartPointer<vtkPolyData> GenerateBoxPatch(const std::string& params)
{
  const std::vector<double> v = ParseDoubles(params);
  vtkNew<vtkMatrix4x4> mat;
  if (v.size() >= 16)
  {
    mat->DeepCopy(v.data());
  }
  else if (v.size() >= 6 && v[1] > v[0] && v[3] > v[2] && v[5] > v[4])
  {
    AabbToUnitCubeMatrix(v.data(), mat);
  }
  else
  {
    return nullptr;
  }

  vtkNew<vtkCubeSource> cube;
  cube->SetCenter(0.0, 0.0, 0.0);
  cube->SetXLength(1.0);
  cube->SetYLength(1.0);
  cube->SetZLength(1.0);
  cube->Update();
  vtkSmartPointer<vtkPolyData> surface = CopyPolyData(cube->GetOutput());
  if (!surface)
  {
    return nullptr;
  }

  vtkPoints* pts = surface->GetPoints();
  const vtkIdType n = pts ? pts->GetNumberOfPoints() : 0;
  for (vtkIdType i = 0; i < n; ++i)
  {
    double p[4] = { 0.0, 0.0, 0.0, 1.0 };
    pts->GetPoint(i, p);
    mat->MultiplyPoint(p, p);
    const double w = p[3] != 0.0 ? p[3] : 1.0;
    pts->SetPoint(i, p[0] / w, p[1] / w, p[2] / w);
  }
  if (pts)
  {
    pts->Modified();
  }
  return TriangulateSurface(surface);
}

vtkSmartPointer<vtkPolyData> GenerateSpherePatch(const std::string& params)
{
  const std::vector<double> v = ParseDoubles(params);
  if (v.size() < 4 || !(v[3] > 0.0))
  {
    return nullptr;
  }
  vtkNew<vtkSphereSource> sphere;
  sphere->SetCenter(v[0], v[1], v[2]);
  sphere->SetRadius(v[3]);
  sphere->SetThetaResolution(32);
  sphere->SetPhiResolution(24);
  sphere->LatLongTessellationOff();
  sphere->Update();
  return CopyPolyData(sphere->GetOutput());
}

vtkSmartPointer<vtkPolyData> AppendPieces(const std::vector<vtkSmartPointer<vtkPolyData>>& pieces)
{
  std::vector<vtkSmartPointer<vtkPolyData>> nonempty;
  nonempty.reserve(pieces.size());
  for (const auto& p : pieces)
  {
    if (p && p->GetNumberOfCells() > 0)
    {
      nonempty.push_back(p);
    }
  }
  if (nonempty.empty())
  {
    return nullptr;
  }
  if (nonempty.size() == 1)
  {
    return nonempty.front();
  }
  vtkNew<vtkAppendPolyData> append;
  for (const auto& p : nonempty)
  {
    append->AddInputData(p);
  }
  append->Update();
  return CopyPolyData(append->GetOutput());
}

std::vector<vtkIdType> CellsFromLiveSelection(vtkDataSet* input, vtkSelection* selection)
{
  std::vector<vtkIdType> ids;
  if (!input || !selection || selection->GetNumberOfNodes() == 0)
  {
    return ids;
  }
  vtkNew<vtkExtractSelection> extract;
  extract->SetInputData(0, input);
  extract->SetInputData(1, selection);
  extract->Update();
  vtkDataSet* extracted = vtkDataSet::SafeDownCast(extract->GetOutput());
  if (!extracted)
  {
    return ids;
  }
  vtkDataArray* ocid = extracted->GetCellData()->GetArray("vtkOriginalCellIds");
  if (auto* cellIds = vtkIdTypeArray::SafeDownCast(ocid))
  {
    const vtkIdType n = cellIds->GetNumberOfTuples();
    ids.reserve(static_cast<size_t>(n));
    const vtkIdType nMesh = input->GetNumberOfCells();
    for (vtkIdType i = 0; i < n; ++i)
    {
      const vtkIdType cid = cellIds->GetValue(i);
      if (cid >= 0 && cid < nMesh)
      {
        ids.push_back(cid);
      }
    }
  }
  return ids;
}
}

//------------------------------------------------------------------------------
vtkSHYXSelectionAppendPatches::vtkSHYXSelectionAppendPatches()
{
  this->SetNumberOfInputPorts(3);
  this->SetNumberOfOutputPorts(2);
  this->SetMarkArrayName("PatchMark");
}

//------------------------------------------------------------------------------
vtkSHYXSelectionAppendPatches::~vtkSHYXSelectionAppendPatches()
{
  this->SetPatchNames(nullptr);
  this->SetPatchCellIds(nullptr);
  this->SetPatchKinds(nullptr);
  this->SetPatchParams(nullptr);
  this->SetMarkArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "PatchNames: " << (this->PatchNames ? this->PatchNames : "(none)") << "\n";
  os << indent << "PatchCellIds: " << (this->PatchCellIds ? this->PatchCellIds : "(none)") << "\n";
  os << indent << "PatchKinds: " << (this->PatchKinds ? this->PatchKinds : "(none)") << "\n";
  os << indent << "PatchParams: " << (this->PatchParams ? this->PatchParams : "(none)") << "\n";
  os << indent << "MarkArrayName: " << (this->MarkArrayName ? this->MarkArrayName : "(none)") << "\n";
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::AddCustomPatchConnection(vtkAlgorithmOutput* algOutput)
{
  this->AddInputConnection(2, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::RemoveAllCustomPatchConnections()
{
  this->RemoveAllInputConnections(2);
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionAppendPatches::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkSelection");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
    return 1;
  }
  if (port == 2)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataObject");
    info->Set(vtkAlgorithm::INPUT_IS_REPEATABLE(), 1);
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionAppendPatches::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPartitionedDataSetCollection");
    return 1;
  }
  if (port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionAppendPatches::RequestDataObject(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkInformation* out0 = outputVector->GetInformationObject(0);
  vtkDataObjectAlgorithm::SetOutputDataObject(VTK_PARTITIONED_DATA_SET_COLLECTION, out0, true);

  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkInformation* out1 = outputVector->GetInformationObject(1);
  vtkDataObject* cur1 = out1->Get(vtkDataObject::DATA_OBJECT());
  if (input)
  {
    if (!cur1 || !cur1->IsA(input->GetClassName()))
    {
      vtkSmartPointer<vtkDataObject> created;
      created.TakeReference(input->NewInstance());
      out1->Set(vtkDataObject::DATA_OBJECT(), created);
      out1->Set(vtkDataObject::DATA_EXTENT_TYPE(), created->GetExtentType());
    }
  }
  else if (!vtkPolyData::SafeDownCast(cur1))
  {
    vtkNew<vtkPolyData> pd;
    out1->Set(vtkDataObject::DATA_OBJECT(), pd);
    out1->Set(vtkDataObject::DATA_EXTENT_TYPE(), pd->GetExtentType());
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionAppendPatches::RequestData(vtkInformation*, vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);
  vtkDataSet* remainOut = vtkDataSet::GetData(outputVector, 1);
  if (!input || !output || !remainOut)
  {
    vtkErrorMacro("Missing input vtkDataSet, added-patches output, or remainder output.");
    return 0;
  }
  output->Initialize();
  output->SetNumberOfPartitionedDataSets(0);

  auto names = SplitLines(this->PatchNames);
  auto idLines = SplitLines(this->PatchCellIds);
  auto kinds = SplitLines(this->PatchKinds);
  auto params = SplitLines(this->PatchParams);
  const size_t nRows = std::max(std::max(names.size(), idLines.size()), std::max(kinds.size(), params.size()));

  struct OutPatch
  {
    std::string name;
    std::vector<vtkSmartPointer<vtkPolyData>> pieces;
    std::vector<vtkIdType> selectionIds;
  };
  std::vector<OutPatch> patches;
  std::map<std::string, size_t> indexOfName;

  auto takePatch = [&](const std::string& name) -> OutPatch& {
    auto found = indexOfName.find(name);
    if (found == indexOfName.end())
    {
      indexOfName[name] = patches.size();
      OutPatch row;
      row.name = name;
      patches.push_back(std::move(row));
      return patches.back();
    }
    return patches[found->second];
  };

  int pipelineIndex = 0;
  const int nPipelineInputs =
    inputVector[2] ? inputVector[2]->GetNumberOfInformationObjects() : 0;

  for (size_t i = 0; i < nRows; ++i)
  {
    const std::string name =
      (i < names.size() && !names[i].empty()) ? names[i] : DefaultPartName(static_cast<unsigned int>(i));
    const std::string kind = KindOf(kinds, i);
    const std::string extra = i < params.size() ? params[i] : std::string();

    if (kind == "pipeline")
    {
      vtkDataObject* obj =
        (pipelineIndex < nPipelineInputs) ? vtkDataObject::GetData(inputVector[2], pipelineIndex) : nullptr;
      ++pipelineIndex;
      vtkSmartPointer<vtkPolyData> piece = ConvertDataObjectToPolyData(obj);
      if (!piece)
      {
        vtkWarningMacro(<< "Pipeline patch '" << name << "' produced no surface; skipped.");
        continue;
      }
      takePatch(name).pieces.push_back(piece);
      continue;
    }

    if (kind == "box")
    {
      vtkSmartPointer<vtkPolyData> piece = GenerateBoxPatch(extra);
      if (!piece)
      {
        vtkWarningMacro(<< "Box patch '" << name << "' has invalid bounds; skipped.");
        continue;
      }
      takePatch(name).pieces.push_back(piece);
      continue;
    }

    if (kind == "sphere")
    {
      vtkSmartPointer<vtkPolyData> piece = GenerateSpherePatch(extra);
      if (!piece)
      {
        vtkWarningMacro(<< "Sphere patch '" << name << "' has invalid center/radius; skipped.");
        continue;
      }
      takePatch(name).pieces.push_back(piece);
      continue;
    }

    const std::vector<vtkIdType> ids =
      i < idLines.size() ? ParseIdList(idLines[i]) : std::vector<vtkIdType>{};
    if (ids.empty())
    {
      continue;
    }
    vtkSmartPointer<vtkDataSet> extracted = ExtractCellsAsPatch(input, ids);
    vtkSmartPointer<vtkPolyData> piece = TriangulateSurface(extracted);
    if (!piece)
    {
      vtkWarningMacro(<< "Patch '" << name << "' extracted no cells; skipped.");
      continue;
    }
    OutPatch& dest = takePatch(name);
    dest.pieces.push_back(piece);
    dest.selectionIds.insert(dest.selectionIds.end(), ids.begin(), ids.end());
  }

  if (patches.empty())
  {
    vtkSelection* liveSel = vtkSelection::GetData(inputVector[1], 0);
    const std::vector<vtkIdType> liveIds = CellsFromLiveSelection(input, liveSel);
    if (!liveIds.empty())
    {
      vtkSmartPointer<vtkDataSet> extracted = ExtractCellsAsPatch(input, liveIds);
      if (vtkSmartPointer<vtkPolyData> piece = TriangulateSurface(extracted))
      {
        OutPatch row;
        row.name = DefaultPartName(0);
        row.pieces.push_back(piece);
        row.selectionIds = liveIds;
        patches.push_back(std::move(row));
      }
    }
  }

  for (auto& patch : patches)
  {
    std::sort(patch.selectionIds.begin(), patch.selectionIds.end());
    patch.selectionIds.erase(
      std::unique(patch.selectionIds.begin(), patch.selectionIds.end()), patch.selectionIds.end());
  }

  vtkNew<vtkDataAssembly> assembly;
  assembly->SetRootNodeName("patches");
  const char* markName = (this->MarkArrayName && this->MarkArrayName[0] != '\0')
    ? this->MarkArrayName
    : "PatchMark";

  unsigned int nOut = 0;
  for (size_t i = 0; i < patches.size(); ++i)
  {
    vtkSmartPointer<vtkPolyData> patch = AppendPieces(patches[i].pieces);
    if (!patch)
    {
      vtkWarningMacro(<< "Patch '" << patches[i].name << "' produced no geometry; skipped.");
      continue;
    }
    StampConstantMark(patch, markName, std::to_string(i));

    vtkNew<vtkPartitionedDataSet> pds;
    pds->SetPartition(0, patch);
    output->SetPartitionedDataSet(nOut, pds);
    if (vtkInformation* meta = output->GetMetaData(nOut))
    {
      meta->Set(vtkCompositeDataSet::NAME(), patches[i].name.c_str());
    }
    const int node = assembly->AddNode(
      vtkDataAssembly::MakeValidNodeName(AssemblyNodeName(nOut).c_str()).c_str());
    if (node >= 0)
    {
      assembly->AddDataSetIndex(node, static_cast<unsigned int>(nOut));
      assembly->SetAttribute(node, "label", patches[i].name.c_str());
    }
    ++nOut;
  }

  if (nOut > 0)
  {
    output->SetDataAssembly(assembly);
  }

  std::vector<vtkIdType> allAdded;
  for (const auto& patch : patches)
  {
    allAdded.insert(allAdded.end(), patch.selectionIds.begin(), patch.selectionIds.end());
  }
  std::sort(allAdded.begin(), allAdded.end());
  allAdded.erase(std::unique(allAdded.begin(), allAdded.end()), allAdded.end());
  AssignRemainder(ExtractInverseCells(input, allAdded), remainOut);
  return 1;
}

VTK_ABI_NAMESPACE_END
