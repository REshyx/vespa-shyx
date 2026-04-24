#include "vtkSHYXDeleteSelectedCellsFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
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
#include <set>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXDeleteSelectedCellsFilter);

namespace
{
void CollectCellsFromExtracted(vtkPolyData* mesh, vtkDataSet* extracted, std::set<vtkIdType>& selected)
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
  vtkIdType npts;
  const vtkIdType* pids;
  for (vtkIdType cid = 0; cid < nMeshCells; ++cid)
  {
    mesh->GetCellPoints(cid, npts, pids);
    for (vtkIdType k = 0; k < npts; ++k)
    {
      if (selPt.count(pids[k]) != 0u)
      {
        selected.insert(cid);
        break;
      }
    }
  }
}

/** Output mesh = input minus selected cells; unused points dropped. */
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

} // namespace

//------------------------------------------------------------------------------
vtkSHYXDeleteSelectedCellsFilter::vtkSHYXDeleteSelectedCellsFilter()
{
  this->SetNumberOfInputPorts(2);
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
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
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
  vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!mesh)
  {
    vtkErrorMacro("Input port 0 (vtkPolyData) is required.");
    return 0;
  }
  if (!output)
  {
    vtkErrorMacro("Output poly data is null.");
    return 0;
  }

  if (mesh->GetNumberOfCells() == 0)
  {
    output->ShallowCopy(mesh);
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
        extractSelection->SetInputData(0, mesh);
        extractSelection->SetInputData(1, inputSel);
        extractSelection->Update();
        vtkDataSet* extracted = vtkDataSet::SafeDownCast(extractSelection->GetOutputDataObject(0));
        if (extracted &&
          (extracted->GetNumberOfCells() > 0 || extracted->GetNumberOfPoints() > 0))
        {
          CollectCellsFromExtracted(mesh, extracted, selected);
        }
      }
    }
  }

  if (selected.empty() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
  {
    vtkDataArray* arr = mesh->GetCellData()->GetArray(this->SelectionCellArrayName);
    if (!arr)
    {
      vtkWarningMacro("SelectionCellArrayName \""
        << this->SelectionCellArrayName << "\" not found on input cell data.");
    }
    else
    {
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
  }

  if (selected.empty())
  {
    vtkWarningMacro("No selection: output is a shallow copy of the input (no cells removed).");
    output->ShallowCopy(mesh);
    return 1;
  }

  BuildPolyDataWithoutCells(mesh, selected, output);
  return 1;
}

VTK_ABI_NAMESPACE_END
