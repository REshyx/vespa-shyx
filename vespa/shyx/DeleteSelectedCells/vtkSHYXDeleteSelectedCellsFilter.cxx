#include "vtkSHYXDeleteSelectedCellsFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetAttributes.h>
#include <vtkExplicitStructuredGrid.h>
#include <vtkExtractCells.h>
#include <vtkExtractSelection.h>
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
#include <vtkStructuredGrid.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <set>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDeleteSelectedCellsFilter);

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

/** Output mesh = input minus selected cells; unused points dropped (poly data only). */
void BuildPolyDataWithoutCells(vtkPolyData* inMesh, const std::set<vtkIdType>& selected, vtkPolyData* out)
{
  out->Initialize();
  if (!inMesh)
  {
    return;
  }

  const vtkIdType nPts = inMesh->GetNumberOfPoints();
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  if (nCells == 0)
  {
    out->ShallowCopy(inMesh);
    return;
  }

  std::vector<char> usedPt(static_cast<size_t>(nPts), 0);
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (selected.count(cid) != 0u)
    {
      continue;
    }
    inMesh->GetCellPoints(cid, npts, pids);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      if (pids[k] >= 0 && pids[k] < nPts)
      {
        usedPt[static_cast<size_t>(pids[k])] = 1;
      }
    }
  }

  vtkPoints* inPts = inMesh->GetPoints();
  vtkNew<vtkPoints> newPts;
  std::vector<vtkIdType> old2new(static_cast<size_t>(nPts), -1);
  if (inPts)
  {
    double x[3];
    for (vtkIdType i = 0; i < nPts; ++i)
    {
      if (!usedPt[static_cast<size_t>(i)])
      {
        continue;
      }
      inPts->GetPoint(i, x);
      old2new[static_cast<size_t>(i)] = newPts->InsertNextPoint(x);
    }
  }

  vtkNew<vtkCellArray> newVerts;
  vtkNew<vtkCellArray> newLines;
  vtkNew<vtkCellArray> newPolys;
  vtkNew<vtkCellArray> newStrips;
  vtkNew<vtkIdList> remapped;
  std::vector<vtkIdType> keptOrig;

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (selected.count(cid) != 0u)
    {
      continue;
    }
    const int ctype = inMesh->GetCellType(cid);
    inMesh->GetCellPoints(cid, npts, pids);
    remapped->SetNumberOfIds(npts);
    bool ok = true;
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType m = old2new[static_cast<size_t>(pids[k])];
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

void BuildUnstructuredGridWithoutCells(
  vtkUnstructuredGrid* inMesh, const std::set<vtkIdType>& selected, vtkUnstructuredGrid* out)
{
  out->Initialize();
  if (!inMesh)
  {
    return;
  }
  const vtkIdType nCells = inMesh->GetNumberOfCells();
  if (nCells == 0)
  {
    out->ShallowCopy(inMesh);
    return;
  }

  vtkNew<vtkIdList> keep;
  for (vtkIdType i = 0; i < nCells; ++i)
  {
    if (selected.count(i) == 0u)
    {
      keep->InsertNextId(i);
    }
  }
  if (keep->GetNumberOfIds() == 0)
  {
    return;
  }
  if (keep->GetNumberOfIds() == nCells)
  {
    out->ShallowCopy(inMesh);
    return;
  }

  vtkNew<vtkExtractCells> ex;
  ex->SetInputData(inMesh);
  ex->SetCellList(keep);
  ex->Update();
  out->DeepCopy(ex->GetOutput());
}

/** Structured grid: same concrete type; blank selected cells. */
void BuildStructuredGridBlanked(
  vtkStructuredGrid* inMesh, const std::set<vtkIdType>& selected, vtkStructuredGrid* out)
{
  if (!inMesh || !out)
  {
    return;
  }
  out->DeepCopy(inMesh);
  if (selected.empty())
  {
    return;
  }
  const vtkIdType nCells = out->GetNumberOfCells();
  for (vtkIdType cid : selected)
  {
    if (cid >= 0 && cid < nCells)
    {
      out->BlankCell(cid);
    }
  }
}

/** Explicit structured grid: same concrete type; blank selected cells. */
void BuildExplicitStructuredGridBlanked(vtkExplicitStructuredGrid* inMesh,
  const std::set<vtkIdType>& selected, vtkExplicitStructuredGrid* out)
{
  if (!inMesh || !out)
  {
    return;
  }
  out->DeepCopy(inMesh);
  if (selected.empty())
  {
    return;
  }
  const vtkIdType nCells = out->GetNumberOfCells();
  for (vtkIdType cid : selected)
  {
    if (cid >= 0 && cid < nCells)
    {
      out->BlankCell(cid);
    }
  }
}

/**
 * Image / rectilinear / generic dataset: DeepCopy input and hide selected cells via the
 * vtkDataSet cell ghost bit HIDDENCELL (topology and type unchanged).
 */
bool BuildDataSetWithHiddenCells(
  vtkDataSet* inMesh, const std::set<vtkIdType>& selected, vtkDataSet* out)
{
  if (!inMesh || !out)
  {
    return false;
  }
  out->DeepCopy(inMesh);
  if (selected.empty())
  {
    return true;
  }
  vtkUnsignedCharArray* ghosts = out->GetCellGhostArray();
  if (!ghosts)
  {
    ghosts = out->AllocateCellGhostArray();
  }
  if (!ghosts)
  {
    return false;
  }
  const vtkIdType nCells = out->GetNumberOfCells();
  for (vtkIdType cid : selected)
  {
    if (cid < 0 || cid >= nCells)
    {
      continue;
    }
    unsigned char v = ghosts->GetValue(cid);
    v = static_cast<unsigned char>(v | vtkDataSetAttributes::HIDDENCELL);
    ghosts->SetValue(cid, v);
  }
  return true;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXDeleteSelectedCellsFilter::vtkSHYXDeleteSelectedCellsFilter()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXDeleteSelectedCellsFilter::~vtkSHYXDeleteSelectedCellsFilter()
{
  this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXDeleteSelectedCellsFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXDeleteSelectedCellsFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXDeleteSelectedCellsFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXDeleteSelectedCellsFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
  vtkDataSet* output = vtkDataSet::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro("Input port 0 (vtkDataSet) is required.");
    return 0;
  }
  if (!output)
  {
    vtkErrorMacro("Output data set is null.");
    return 0;
  }

  if (input->GetNumberOfCells() == 0)
  {
    output->ShallowCopy(input);
    return 1;
  }

  std::set<vtkIdType> selected;

  if (this->GetNumberOfInputConnections(1) > 0)
  {
    vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
    if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
    {
      vtkSelection* inputSel = vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
      if (inputSel && inputSel->GetNumberOfNodes() > 0)
      {
        vtkNew<vtkExtractSelection> extractSelection;
        extractSelection->SetInputData(0, input);
        extractSelection->SetInputData(1, inputSel);
        extractSelection->Update();
        vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
        if (extracted &&
          (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
        {
          CollectCellsFromExtracted(input, extracted, selected);
        }
      }
    }
  }

  if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
  {
    vtkDataArray* arr = input->GetCellData()->GetArray(this->SelectionCellArrayName);
    if (!arr)
    {
      vtkWarningMacro("SelectionCellArrayName \""
        << this->SelectionCellArrayName << "\" not found on input cell data.");
    }
    else
    {
      const vtkIdType nc = input->GetNumberOfCells();
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
  }

  if (selected.empty())
  {
    vtkWarningMacro("No selection: output is a shallow copy of the input.");
    output->ShallowCopy(input);
    return 1;
  }

  if (auto* inPd = vtkPolyData::SafeDownCast(input))
  {
    auto* outPd = vtkPolyData::SafeDownCast(output);
    if (!outPd)
    {
      vtkErrorMacro("Internal error: expected vtkPolyData output.");
      return 0;
    }
    BuildPolyDataWithoutCells(inPd, selected, outPd);
    return 1;
  }
  if (auto* inUg = vtkUnstructuredGrid::SafeDownCast(input))
  {
    auto* outUg = vtkUnstructuredGrid::SafeDownCast(output);
    if (!outUg)
    {
      vtkErrorMacro("Internal error: expected vtkUnstructuredGrid output.");
      return 0;
    }
    BuildUnstructuredGridWithoutCells(inUg, selected, outUg);
    return 1;
  }

  if (auto* inSg = vtkStructuredGrid::SafeDownCast(input))
  {
    auto* outSg = vtkStructuredGrid::SafeDownCast(output);
    if (!outSg)
    {
      vtkErrorMacro("Internal error: expected vtkStructuredGrid output.");
      return 0;
    }
    BuildStructuredGridBlanked(inSg, selected, outSg);
    return 1;
  }

  if (auto* inEsg = vtkExplicitStructuredGrid::SafeDownCast(input))
  {
    auto* outEsg = vtkExplicitStructuredGrid::SafeDownCast(output);
    if (!outEsg)
    {
      vtkErrorMacro("Internal error: expected vtkExplicitStructuredGrid output.");
      return 0;
    }
    BuildExplicitStructuredGridBlanked(inEsg, selected, outEsg);
    return 1;
  }

  if (!BuildDataSetWithHiddenCells(input, selected, output))
  {
    vtkErrorMacro("Could not mark selected cells as hidden (cell ghost allocation failed).");
    return 0;
  }
  return 1;
}

VTK_ABI_NAMESPACE_END
