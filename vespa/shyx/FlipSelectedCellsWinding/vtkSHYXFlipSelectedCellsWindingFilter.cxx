#include "vtkSHYXFlipSelectedCellsWindingFilter.h"

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSelection.h>
#include <set>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXFlipSelectedCellsWindingFilter);

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
} // namespace

//------------------------------------------------------------------------------
vtkSHYXFlipSelectedCellsWindingFilter::vtkSHYXFlipSelectedCellsWindingFilter()
{
  this->SetNumberOfInputPorts(2);
}

//------------------------------------------------------------------------------
vtkSHYXFlipSelectedCellsWindingFilter::~vtkSHYXFlipSelectedCellsWindingFilter()
{
  this->SetSelectionCellArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXFlipSelectedCellsWindingFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXFlipSelectedCellsWindingFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXFlipSelectedCellsWindingFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXFlipSelectedCellsWindingFilter::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
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
    const vtkIdType nc = mesh->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nc; ++cid)
    {
      selected.insert(cid);
    }
  }

  vtkNew<vtkPolyData> work;
  work->DeepCopy(mesh);
  for (const vtkIdType cid : selected)
  {
    if (cid < 0 || cid >= work->GetNumberOfCells())
    {
      continue;
    }
    work->ReverseCell(cid);
  }

  output->ShallowCopy(work);
  return 1;
}

VTK_ABI_NAMESPACE_END
