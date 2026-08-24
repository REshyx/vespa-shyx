#include "vtkSHYXExtractSelectedCellsFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractCells.h>
#include <vtkExtractSelection.h>
#include <vtkFieldData.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <vtkUnstructuredGrid.h>
#include <set>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXExtractSelectedCellsFilter);

namespace
{
void CollectCellsFromExtracted(vtkDataSet* mesh, vtkDataSet* extracted, std::set<vtkIdType>& selected)
{
  if (!mesh || !extracted)
  {
    return;
  }
  const vtkIdType nMeshCells = mesh->GetNumberOfCells();

  vtkDataArray* ocid = extracted->GetCellData()->GetArray("vtkOriginalCellIds");
  if (auto* cellIds = vtkIdTypeArray::SafeDownCast(ocid))
  {
    if (cellIds->GetNumberOfTuples() == extracted->GetNumberOfCells())
    {
      for (vtkIdType i = 0; i < cellIds->GetNumberOfTuples(); ++i)
      {
        const vtkIdType cid = cellIds->GetValue(i);
        if (cid >= 0 && cid < nMeshCells)
        {
          selected.insert(cid);
        }
      }
    }
  }
  if (!selected.empty())
  {
    return;
  }

  vtkDataArray* opid = extracted->GetPointData()->GetArray("vtkOriginalPointIds");
  auto* ptIds = vtkIdTypeArray::SafeDownCast(opid);
  if (!ptIds || ptIds->GetNumberOfTuples() == 0)
  {
    return;
  }
  std::set<vtkIdType> selPt;
  for (vtkIdType i = 0; i < ptIds->GetNumberOfTuples(); ++i)
  {
    const vtkIdType pid = ptIds->GetValue(i);
    if (pid >= 0 && pid < mesh->GetNumberOfPoints())
    {
      selPt.insert(pid);
    }
  }
  if (selPt.empty())
  {
    return;
  }
  vtkNew<vtkGenericCell> cell;
  for (vtkIdType cid = 0; cid < nMeshCells; ++cid)
  {
    mesh->GetCell(cid, cell);
    vtkIdList* ids = cell->GetPointIds();
    const vtkIdType npts = ids->GetNumberOfIds();
    for (vtkIdType k = 0; k < npts; ++k)
    {
      if (selPt.count(ids->GetId(k)) != 0u)
      {
        selected.insert(cid);
        break;
      }
    }
  }
}

void AppendMaskFromCellArray(vtkDataSet* mesh, const char* arrayName, std::set<vtkIdType>& selected)
{
  if (!mesh || !arrayName || arrayName[0] == '\0')
  {
    return;
  }
  vtkDataArray* arr = mesh->GetCellData()->GetArray(arrayName);
  if (!arr)
  {
    return;
  }
  const vtkIdType nc = mesh->GetNumberOfCells();
  for (vtkIdType cid = 0; cid < nc; ++cid)
  {
    bool take = false;
    if (arr->IsIntegral())
    {
      take = (arr->GetTuple1(cid) != 0.0);
    }
    else
    {
      take = (arr->GetTuple1(cid) > 0.5);
    }
    if (take)
    {
      selected.insert(cid);
    }
  }
}

bool ArrayExistsOnCellData(vtkDataSet* mesh, const char* arrayName)
{
  return mesh && arrayName && arrayName[0] != '\0' && mesh->GetCellData()->GetArray(arrayName);
}

void GatherSelectedCells(
  vtkDataSet* input, vtkSelection* inputSel, const char* arrayName, std::set<vtkIdType>& selected)
{
  selected.clear();
  if (!input)
  {
    return;
  }
  if (inputSel && inputSel->GetNumberOfNodes() > 0)
  {
    vtkNew<vtkExtractSelection> extractSelection;
    extractSelection->SetInputData(0, input);
    extractSelection->SetInputData(1, inputSel);
    extractSelection->Update();
    vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
    if (extracted && (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
    {
      CollectCellsFromExtracted(input, extracted, selected);
    }
  }
  if (selected.empty() && arrayName && arrayName[0] != '\0')
  {
    AppendMaskFromCellArray(input, arrayName, selected);
  }
}

void InvertSelectedCells(vtkDataSet* mesh, std::set<vtkIdType>& selected)
{
  if (!mesh)
  {
    selected.clear();
    return;
  }
  std::set<vtkIdType> inverted;
  const vtkIdType nCells = mesh->GetNumberOfCells();
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (selected.count(cid) == 0u)
    {
      inverted.insert(cid);
    }
  }
  selected.swap(inverted);
}

bool IsPolyDataCellType(int ctype)
{
  switch (ctype)
  {
    case VTK_VERTEX:
    case VTK_POLY_VERTEX:
    case VTK_LINE:
    case VTK_POLY_LINE:
    case VTK_TRIANGLE:
    case VTK_QUAD:
    case VTK_POLYGON:
    case VTK_PIXEL:
    case VTK_TRIANGLE_STRIP:
      return true;
    default:
      return false;
  }
}

/** PolyData input always stays PolyData. Other datasets stay PolyData only if every selected cell is a PD type. */
bool SelectionYieldsPolyData(vtkDataSet* mesh, const std::set<vtkIdType>& selected)
{
  if (vtkPolyData::SafeDownCast(mesh))
  {
    return true;
  }
  if (!mesh || selected.empty())
  {
    return false;
  }
  const vtkIdType nCells = mesh->GetNumberOfCells();
  for (vtkIdType cid : selected)
  {
    if (cid < 0 || cid >= nCells)
    {
      continue;
    }
    if (!IsPolyDataCellType(mesh->GetCellType(cid)))
    {
      return false;
    }
  }
  return true;
}

void BuildPolyDataFromSelectedCells(
  vtkDataSet* inMesh, const std::set<vtkIdType>& selected, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh || selected.empty())
  {
    return;
  }

  const vtkIdType nPts = inMesh->GetNumberOfPoints();
  const vtkIdType nCells = inMesh->GetNumberOfCells();

  std::vector<char> usedPt(static_cast<size_t>(nPts), 0);
  vtkNew<vtkIdList> cellPts;
  for (vtkIdType cid : selected)
  {
    if (cid < 0 || cid >= nCells)
    {
      continue;
    }
    inMesh->GetCellPoints(cid, cellPts);
    const vtkIdType npts = cellPts->GetNumberOfIds();
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType pid = cellPts->GetId(k);
      if (pid >= 0 && pid < nPts)
      {
        usedPt[static_cast<size_t>(pid)] = 1;
      }
    }
  }

  vtkNew<vtkPoints> newPts;
  std::vector<vtkIdType> old2new(static_cast<size_t>(nPts), -1);
  double x[3];
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    if (!usedPt[static_cast<size_t>(i)])
    {
      continue;
    }
    inMesh->GetPoint(i, x);
    old2new[static_cast<size_t>(i)] = newPts->InsertNextPoint(x);
  }

  vtkNew<vtkCellArray> newVerts;
  vtkNew<vtkCellArray> newLines;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;
  vtkNew<vtkIdList> remapped;
  std::vector<vtkIdType> keptOrig;

  for (vtkIdType cid : selected)
  {
    if (cid < 0 || cid >= nCells)
    {
      continue;
    }
    const int ctype = inMesh->GetCellType(cid);
    inMesh->GetCellPoints(cid, cellPts);
    const vtkIdType npts = cellPts->GetNumberOfIds();
    remapped->SetNumberOfIds(npts);
    bool ok = true;
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType m = old2new[static_cast<size_t>(cellPts->GetId(k))];
      if (m < 0)
      {
        ok = false;
        break;
      }
      remapped->SetId(k, m);
    }
    if (!ok)
    {
      continue;
    }

    vtkCellArray* target = nullptr;
    switch (ctype)
    {
      case VTK_VERTEX:
      case VTK_POLY_VERTEX:
        target = newVerts;
        break;
      case VTK_LINE:
      case VTK_POLY_LINE:
        target = newLines;
        break;
      case VTK_TRIANGLE:
      case VTK_QUAD:
      case VTK_POLYGON:
      case VTK_PIXEL:
        target = newPolys;
        break;
      case VTK_TRIANGLE_STRIP:
        target = newStrips;
        break;
      default:
        target = newPolys;
        break;
    }
    if (target)
    {
      target->InsertNextCell(remapped);
      keptOrig.push_back(cid);
    }
  }

  out->SetPoints(newPts);
  out->SetVerts(newVerts);
  out->SetLines(newLines);
  out->SetPolys(newPolys);
  out->SetStrips(newStrips);

  const vtkIdType nOutCells = static_cast<vtkIdType>(keptOrig.size());
  out->GetCellData()->CopyAllocate(inMesh->GetCellData(), nOutCells);
  for (vtkIdType j = 0; j < nOutCells; ++j)
  {
    out->GetCellData()->CopyData(inMesh->GetCellData(), keptOrig[static_cast<size_t>(j)], j);
  }

  const vtkIdType nOutPts = newPts->GetNumberOfPoints();
  out->GetPointData()->CopyAllocate(inMesh->GetPointData(), nOutPts);
  for (vtkIdType oldIdx = 0; oldIdx < nPts; ++oldIdx)
  {
    const vtkIdType newIdx = old2new[static_cast<size_t>(oldIdx)];
    if (newIdx >= 0)
    {
      out->GetPointData()->CopyData(inMesh->GetPointData(), oldIdx, newIdx);
    }
  }

  out->GetFieldData()->PassData(inMesh->GetFieldData());
  out->Squeeze();
}

void BuildUnstructuredGridFromSelectedCells(
  vtkDataSet* inMesh, const std::set<vtkIdType>& selected, vtkUnstructuredGrid* out)
{
  out->Initialize();
  if (!inMesh || selected.empty())
  {
    return;
  }

  vtkNew<vtkIdList> keep;
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  for (vtkIdType cid : selected)
  {
    if (cid >= 0 && cid < nCells)
    {
      keep->InsertNextId(cid);
    }
  }
  if (keep->GetNumberOfIds() == 0)
  {
    return;
  }

  vtkNew<vtkExtractCells> ex;
  ex->SetInputData(inMesh);
  ex->SetCellList(keep);
  ex->Update();
  out->DeepCopy(ex->GetOutput());
}

int DecideOutputType(vtkDataSet* input, const std::set<vtkIdType>& selected)
{
  return SelectionYieldsPolyData(input, selected) ? VTK_POLY_DATA : VTK_UNSTRUCTURED_GRID;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXExtractSelectedCellsFilter::vtkSHYXExtractSelectedCellsFilter()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXExtractSelectedCellsFilter::~vtkSHYXExtractSelectedCellsFilter()
{
  this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXExtractSelectedCellsFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXExtractSelectedCellsFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
  os << indent << "InvertSelection: " << this->InvertSelection << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXExtractSelectedCellsFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXExtractSelectedCellsFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXExtractSelectedCellsFilter::RequestDataObject(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkSelection* inputSel = vtkSelection::GetData(inputVector[1], 0);

  std::set<vtkIdType> selected;
  if (input)
  {
    GatherSelectedCells(input, inputSel, this->SelectionCellArrayName, selected);
    if (this->InvertSelection)
    {
      InvertSelectedCells(input, selected);
    }
  }

  const int outputType = DecideOutputType(input, selected);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  return vtkDataObjectAlgorithm::SetOutputDataObject(outputType, outInfo, /*exact=*/true) ? 1 : 0;
}

//------------------------------------------------------------------------------
int vtkSHYXExtractSelectedCellsFilter::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  if (!input)
  {
    vtkErrorMacro("Input port 0 (vtkDataSet) is required.");
    return 0;
  }

  vtkSelection* inputSel = vtkSelection::GetData(inputVector[1], 0);
  std::set<vtkIdType> selected;
  GatherSelectedCells(input, inputSel, this->SelectionCellArrayName, selected);

  if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0' &&
    !ArrayExistsOnCellData(input, this->SelectionCellArrayName))
  {
    vtkWarningMacro("SelectionCellArrayName \"" << this->SelectionCellArrayName
                                                << "\" not found on input cell data.");
  }

  if (this->InvertSelection)
  {
    InvertSelectedCells(input, selected);
  }

  const int outputType = DecideOutputType(input, selected);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  if (!vtkDataObjectAlgorithm::SetOutputDataObject(outputType, outInfo, /*exact=*/true))
  {
    vtkErrorMacro("Could not create output data object.");
    return 0;
  }

  if (selected.empty())
  {
    vtkWarningMacro("No selection: output is empty.");
    return 1;
  }

  const vtkIdType nCells = input->GetNumberOfCells();
  if (nCells == 0)
  {
    return 1;
  }

  if (selected.size() == static_cast<size_t>(nCells))
  {
    if (outputType == VTK_POLY_DATA)
    {
      if (auto* inPd = vtkPolyData::SafeDownCast(input))
      {
        if (auto* outPd = vtkPolyData::GetData(outInfo))
        {
          outPd->ShallowCopy(inPd);
          return 1;
        }
      }
    }
    else if (auto* inUg = vtkUnstructuredGrid::SafeDownCast(input))
    {
      if (auto* outUg = vtkUnstructuredGrid::GetData(outInfo))
      {
        outUg->ShallowCopy(inUg);
        return 1;
      }
    }
  }

  if (outputType == VTK_POLY_DATA)
  {
    auto* outPd = vtkPolyData::GetData(outInfo);
    if (!outPd)
    {
      vtkErrorMacro("Internal error: expected vtkPolyData output.");
      return 0;
    }
    BuildPolyDataFromSelectedCells(input, selected, outPd);
    return 1;
  }

  auto* outUg = vtkUnstructuredGrid::GetData(outInfo);
  if (!outUg)
  {
    vtkErrorMacro("Internal error: expected vtkUnstructuredGrid output.");
    return 0;
  }
  BuildUnstructuredGridFromSelectedCells(input, selected, outUg);
  return 1;
}

VTK_ABI_NAMESPACE_END
