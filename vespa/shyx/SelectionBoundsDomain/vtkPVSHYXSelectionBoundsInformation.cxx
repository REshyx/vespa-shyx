#include "vtkPVSHYXSelectionBoundsInformation.h"

#include "vtkAlgorithm.h"
#include "vtkAlgorithmOutput.h"
#include "vtkBoundingBox.h"
#include "vtkCellData.h"
#include "vtkClientServerStream.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkExtractSelection.h"
#include "vtkMath.h"
#include "vtkMultiProcessStream.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkSelection.h"

#include <string>

vtkStandardNewMacro(vtkPVSHYXSelectionBoundsInformation);

namespace
{

void Uninit(double bounds[6])
{
  vtkMath::UninitializeBounds(bounds);
}

bool AddDataObjectBounds(vtkDataObject* obj, vtkBoundingBox& box)
{
  if (!obj)
  {
    return false;
  }
  double b[6];
  Uninit(b);
  if (auto* ds = vtkDataSet::SafeDownCast(obj))
  {
    ds->GetBounds(b);
  }
  else if (auto* cd = vtkCompositeDataSet::SafeDownCast(obj))
  {
    cd->GetBounds(b);
  }
  else
  {
    return false;
  }
  if (!vtkMath::AreBoundsInitialized(b))
  {
    return false;
  }
  box.AddBounds(b);
  return true;
}

void AddCellArrayMaskBounds(vtkDataSet* ds, const char* arrayName, vtkBoundingBox& box)
{
  if (!ds || !arrayName || arrayName[0] == '\0')
  {
    return;
  }
  vtkDataArray* arr = ds->GetCellData()->GetArray(arrayName);
  if (!arr)
  {
    return;
  }
  const vtkIdType nc = ds->GetNumberOfCells();
  for (vtkIdType cid = 0; cid < nc; ++cid)
  {
    const bool take =
      arr->IsIntegral() ? (arr->GetTuple1(cid) != 0.0) : (arr->GetTuple1(cid) > 0.5);
    if (!take)
    {
      continue;
    }
    double b[6];
    ds->GetCellBounds(cid, b);
    if (vtkMath::AreBoundsInitialized(b))
    {
      box.AddBounds(b);
    }
  }
}

void UpdateProducer(vtkAlgorithm* alg, int port, int connection)
{
  if (!alg)
  {
    return;
  }
  vtkAlgorithmOutput* ao = alg->GetInputConnection(port, connection);
  if (ao && ao->GetProducer())
  {
    ao->GetProducer()->Update();
  }
}

} // namespace

vtkPVSHYXSelectionBoundsInformation::vtkPVSHYXSelectionBoundsInformation()
{
  this->RootOnly = 0;
  Uninit(this->Bounds);
}

vtkPVSHYXSelectionBoundsInformation::~vtkPVSHYXSelectionBoundsInformation()
{
  this->SetSelectionCellArrayName(nullptr);
}

void vtkPVSHYXSelectionBoundsInformation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Valid: " << (this->Valid ? "on" : "off") << "\n";
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
  os << indent << "Bounds: " << this->Bounds[0] << ", " << this->Bounds[1] << ", " << this->Bounds[2]
     << ", " << this->Bounds[3] << ", " << this->Bounds[4] << ", " << this->Bounds[5] << "\n";
}

void vtkPVSHYXSelectionBoundsInformation::GetBounds(double bounds[6]) const
{
  for (int i = 0; i < 6; ++i)
  {
    bounds[i] = this->Bounds[i];
  }
}

void vtkPVSHYXSelectionBoundsInformation::CopyFromObject(vtkObject* obj)
{
  this->Valid = false;
  Uninit(this->Bounds);

  vtkAlgorithm* alg = vtkAlgorithm::SafeDownCast(obj);
  if (!alg)
  {
    return;
  }

  vtkBoundingBox box;
  const int nIn = alg->GetNumberOfInputConnections(0);
  const int nSel = alg->GetNumberOfInputConnections(1);

  for (int i = 0; i < nIn; ++i)
  {
    UpdateProducer(alg, 0, i);
    vtkDataObject* inObj = alg->GetInputDataObject(0, i);
    vtkSelection* sel = nullptr;
    if (i < nSel)
    {
      UpdateProducer(alg, 1, i);
      sel = vtkSelection::SafeDownCast(alg->GetInputDataObject(1, i));
    }

    if (sel && sel->GetNumberOfNodes() > 0)
    {
      vtkNew<vtkExtractSelection> extract;
      extract->SetInputData(0, inObj);
      extract->SetInputData(1, sel);
      extract->Update();
      AddDataObjectBounds(extract->GetOutputDataObject(0), box);
    }
  }

  if (!box.IsValid() && this->SelectionCellArrayName && this->SelectionCellArrayName[0] != '\0')
  {
    for (int i = 0; i < nIn; ++i)
    {
      if (auto* ds = vtkDataSet::SafeDownCast(alg->GetInputDataObject(0, i)))
      {
        AddCellArrayMaskBounds(ds, this->SelectionCellArrayName, box);
      }
    }
  }

  if (!box.IsValid())
  {
    return;
  }
  box.GetBounds(this->Bounds);
  this->Valid = true;
}

void vtkPVSHYXSelectionBoundsInformation::AddInformation(vtkPVInformation* info)
{
  auto* other = vtkPVSHYXSelectionBoundsInformation::SafeDownCast(info);
  if (!other || !other->Valid)
  {
    return;
  }
  if (!this->Valid)
  {
    other->GetBounds(this->Bounds);
    this->Valid = true;
    return;
  }
  vtkBoundingBox box(this->Bounds);
  box.AddBounds(other->Bounds);
  box.GetBounds(this->Bounds);
}

void vtkPVSHYXSelectionBoundsInformation::CopyToStream(vtkClientServerStream* css)
{
  css->Reset();
  *css << vtkClientServerStream::Reply << (this->Valid ? 1 : 0) << this->Bounds[0]
       << this->Bounds[1] << this->Bounds[2] << this->Bounds[3] << this->Bounds[4]
       << this->Bounds[5] << vtkClientServerStream::End;
}

void vtkPVSHYXSelectionBoundsInformation::CopyFromStream(const vtkClientServerStream* css)
{
  int valid = 0;
  css->GetArgument(0, 0, &valid);
  this->Valid = valid != 0;
  for (int i = 0; i < 6; ++i)
  {
    css->GetArgument(0, i + 1, &this->Bounds[i]);
  }
}

void vtkPVSHYXSelectionBoundsInformation::CopyParametersToStream(vtkMultiProcessStream& str)
{
  str << 912407 << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "");
}

void vtkPVSHYXSelectionBoundsInformation::CopyParametersFromStream(vtkMultiProcessStream& str)
{
  int magic = 0;
  std::string name;
  str >> magic >> name;
  if (magic != 912407)
  {
    vtkErrorMacro("Magic number mismatch.");
    return;
  }
  this->SetSelectionCellArrayName(name.empty() ? nullptr : name.c_str());
}
