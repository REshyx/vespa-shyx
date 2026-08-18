#include "vtkSHYXSelectionAppendPatches.h"

#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkCellData.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataAssembly.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkFieldData.h>
#include <vtkGeometryFilter.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPartitionedDataSetCollection.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
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
  return std::string("Part_") + std::to_string(index);
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
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
  this->SetMarkArrayName("PatchMark");
}

//------------------------------------------------------------------------------
vtkSHYXSelectionAppendPatches::~vtkSHYXSelectionAppendPatches()
{
  this->SetPatchNames(nullptr);
  this->SetPatchMarks(nullptr);
  this->SetPatchCellIds(nullptr);
  this->SetMarkArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "PatchNames: " << (this->PatchNames ? this->PatchNames : "(none)") << "\n";
  os << indent << "PatchMarks: " << (this->PatchMarks ? this->PatchMarks : "(none)") << "\n";
  os << indent << "PatchCellIds: " << (this->PatchCellIds ? this->PatchCellIds : "(none)") << "\n";
  os << indent << "MarkArrayName: " << (this->MarkArrayName ? this->MarkArrayName : "(none)") << "\n";
}

//------------------------------------------------------------------------------
void vtkSHYXSelectionAppendPatches::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
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
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXSelectionAppendPatches::RequestData(vtkInformation*, vtkInformationVector** inputVector,
  vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkPartitionedDataSetCollection* output =
    vtkPartitionedDataSetCollection::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input vtkDataSet or output vtkPartitionedDataSetCollection.");
    return 0;
  }
  output->Initialize();
  output->SetNumberOfPartitionedDataSets(0);

  auto names = SplitLines(this->PatchNames);
  auto marks = SplitLines(this->PatchMarks);
  auto idLines = SplitLines(this->PatchCellIds);
  size_t nRows = names.size();
  nRows = std::max(nRows, marks.size());
  nRows = std::max(nRows, idLines.size());

  std::vector<std::pair<std::string, std::vector<vtkIdType>>> patches;
  patches.reserve(nRows);
  std::vector<std::string> patchMarks;
  patchMarks.reserve(nRows);

  for (size_t i = 0; i < nRows; ++i)
  {
    const std::string name =
      (i < names.size() && !names[i].empty()) ? names[i] : DefaultPartName(static_cast<unsigned int>(i));
    const std::string mark = i < marks.size() ? marks[i] : std::string();
    const std::vector<vtkIdType> ids = i < idLines.size() ? ParseIdList(idLines[i]) : std::vector<vtkIdType>{};
    if (ids.empty())
    {
      continue;
    }
    patches.emplace_back(name, ids);
    patchMarks.push_back(mark);
  }

  if (patches.empty())
  {
    vtkSelection* liveSel = vtkSelection::GetData(inputVector[1], 0);
    const std::vector<vtkIdType> liveIds = CellsFromLiveSelection(input, liveSel);
    if (!liveIds.empty())
    {
      patches.emplace_back(DefaultPartName(0), liveIds);
      patchMarks.emplace_back();
    }
  }

  vtkNew<vtkDataAssembly> assembly;
  assembly->SetRootNodeName("patches");
  const char* markName = (this->MarkArrayName && this->MarkArrayName[0] != '\0')
    ? this->MarkArrayName
    : "PatchMark";

  unsigned int nOut = 0;
  for (size_t i = 0; i < patches.size(); ++i)
  {
    vtkSmartPointer<vtkDataSet> patch = ExtractCellsAsPatch(input, patches[i].second);
    if (!patch)
    {
      vtkWarningMacro(<< "Patch '" << patches[i].first << "' extracted no cells; skipped.");
      continue;
    }
    StampConstantMark(patch, markName, patchMarks[i]);

    vtkNew<vtkPartitionedDataSet> pds;
    pds->SetPartition(0, patch);
    output->SetPartitionedDataSet(nOut, pds);
    if (vtkInformation* meta = output->GetMetaData(nOut))
    {
      meta->Set(vtkCompositeDataSet::NAME(), patches[i].first.c_str());
    }
    const int node = assembly->AddNode(
      vtkDataAssembly::MakeValidNodeName(AssemblyNodeName(nOut).c_str()).c_str());
    if (node >= 0)
    {
      assembly->AddDataSetIndex(node, static_cast<unsigned int>(nOut));
      assembly->SetAttribute(node, "label", patches[i].first.c_str());
    }
    ++nOut;
  }

  if (nOut > 0)
  {
    output->SetDataAssembly(assembly);
  }
  return 1;
}

VTK_ABI_NAMESPACE_END
