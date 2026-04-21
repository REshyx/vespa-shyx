// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkPointLabelRepresentation.h"

#include "vtkActor.h"
#include "vtkActor2D.h"
#include "vtkAlgorithmOutput.h"
#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkCellArray.h"
#include "vtkCellType.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkPoints.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkUnstructuredGrid.h"
#include "vtkLabeledDataMapper.h"
#include "vtkMaskPoints.h"
#include "vtkMergeBlocks.h"
#include "vtkObjectFactory.h"
#include "vtkPVLODActor.h"
#include "vtkPVRenderView.h"
#include "vtkPVView.h"
#include "vtkRenderer.h"
#include "vtkTextProperty.h"
#include "vtkTransform.h"

#include <cstring>
#include <set>

#include "vtkNew.h"
#include "vtkSmartPointer.h"

namespace
{
bool IsExplicitNoneArray(const char* s)
{
  return s && (strcmp(s, "None") == 0 || strcmp(s, "(null)") == 0);
}

/**
 * Build a polydata whose points are exactly those referenced by vertex cells: vtkPolyData::verts
 * or unstructured VTK_VERTEX cells. Used when VertexOnly is enabled so line/poly endpoints are not
 * labeled separately from vertex markers.
 */
vtkSmartPointer<vtkPolyData> ExtractVertexOnlyPoints(vtkDataSet* ds)
{
  if (!ds)
  {
    return nullptr;
  }

  std::set<vtkIdType> pointIds;

  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    vtkCellArray* va = pd->GetVerts();
    if (!va || va->GetNumberOfCells() == 0)
    {
      return nullptr;
    }
    vtkIdType npts;
    const vtkIdType* pts;
    va->InitTraversal();
    while (va->GetNextCell(npts, pts))
    {
      for (vtkIdType i = 0; i < npts; ++i)
      {
        pointIds.insert(pts[i]);
      }
    }
  }
  else if (auto* ug = vtkUnstructuredGrid::SafeDownCast(ds))
  {
    for (vtkIdType cid = 0; cid < ug->GetNumberOfCells(); ++cid)
    {
      if (ug->GetCellType(cid) != VTK_VERTEX)
      {
        continue;
      }
      vtkNew<vtkIdList> ids;
      ug->GetCellPoints(cid, ids.GetPointer());
      for (vtkIdType j = 0; j < ids->GetNumberOfIds(); ++j)
      {
        pointIds.insert(ids->GetId(j));
      }
    }
  }
  else
  {
    return nullptr;
  }

  if (pointIds.empty())
  {
    return nullptr;
  }

  vtkPointData* inPD = ds->GetPointData();
  const vtkIdType nOut = static_cast<vtkIdType>(pointIds.size());

  vtkNew<vtkPoints> outPts;
  vtkNew<vtkCellArray> outVerts;
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->SetPoints(outPts);
  out->SetVerts(outVerts);

  vtkPointData* outPD = out->GetPointData();
  outPD->CopyAllocate(inPD, nOut);

  vtkIdType newIndex = 0;
  for (vtkIdType oldPid : pointIds)
  {
    double x[3];
    ds->GetPoint(oldPid, x);
    const vtkIdType nid = outPts->InsertNextPoint(x);
    outVerts->InsertNextCell(1);
    outVerts->InsertCellPoint(nid);
    outPD->CopyData(inPD, oldPid, newIndex);
    ++newIndex;
  }

  return out;
}
} // namespace

vtkStandardNewMacro(vtkPointLabelRepresentation);

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetShowPointLabels(int val)
{
  if (this->ShowPointLabels == val)
  {
    return;
  }
  this->ShowPointLabels = val;
  this->Modified();
}

//------------------------------------------------------------------------------
int vtkPointLabelRepresentation::GetShowPointLabels()
{
  return this->ShowPointLabels;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ShowPointLabelsOn()
{
  this->SetShowPointLabels(1);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ShowPointLabelsOff()
{
  this->SetShowPointLabels(0);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetVertexOnly(int val)
{
  val = val ? 1 : 0;
  if (this->VertexOnly == val)
  {
    return;
  }
  this->VertexOnly = val;
  this->Modified();
}

//------------------------------------------------------------------------------
int vtkPointLabelRepresentation::GetVertexOnly()
{
  return this->VertexOnly;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::VertexOnlyOn()
{
  this->SetVertexOnly(1);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::VertexOnlyOff()
{
  this->SetVertexOnly(0);
}

//------------------------------------------------------------------------------
vtkPointLabelRepresentation::vtkPointLabelRepresentation()
{
  this->MergeBlocks = vtkMergeBlocks::New();
  this->PointMask = vtkMaskPoints::New();
  this->PointMask->SetOnRatio(1);
  this->PointMask->SetMaximumNumberOfPoints(this->MaximumNumberOfLabels);
  this->PointMask->RandomModeOn();

  this->LabelMapper = vtkLabeledDataMapper::New();
  this->LabelMapper->SetInputConnection(this->PointMask->GetOutputPort());
  this->LabelMapper->SetLabelMode(VTK_LABEL_FIELD_DATA);
  this->LabelMapper->CoordinateSystemWorld();

  this->LabelActor = vtkActor2D::New();
  this->LabelActor->SetMapper(this->LabelMapper);
  this->LabelActor->SetVisibility(0);
  this->LabelActor->PickableOff();

  this->LabelProperty = vtkTextProperty::New();
  this->LabelProperty->SetFontSize(14);
  this->LabelProperty->SetColor(0.9, 0.9, 0.95);
  this->LabelProperty->SetBold(0);
  this->LabelProperty->SetItalic(0);
  this->LabelMapper->SetLabelTextProperty(this->LabelProperty);

  this->LabelTransform = vtkTransform::New();
  this->LabelTransform->Identity();
  this->LabelMapper->SetTransform(this->LabelTransform);

  this->TransformHelperProp = vtkActor::New();

  this->WarningObserver = vtkCallbackCommand::New();
  this->WarningObserver->SetCallback(&vtkPointLabelRepresentation::OnWarningEvent);
  this->WarningObserver->SetClientData(this);
  this->LabelMapper->AddObserver(vtkCommand::WarningEvent, this->WarningObserver);
}

//------------------------------------------------------------------------------
vtkPointLabelRepresentation::~vtkPointLabelRepresentation()
{
  this->SetPointLabelArray(nullptr);
  this->SetLabelFormat(nullptr);
  if (this->WarningObserver && this->LabelMapper)
  {
    this->LabelMapper->RemoveObservers(vtkCommand::WarningEvent, this->WarningObserver);
  }
  if (this->WarningObserver)
  {
    this->WarningObserver->Delete();
    this->WarningObserver = nullptr;
  }
  this->MergeBlocks->Delete();
  this->PointMask->Delete();
  this->LabelMapper->Delete();
  this->LabelActor->Delete();
  this->LabelProperty->Delete();
  this->LabelTransform->Delete();
  this->TransformHelperProp->Delete();
}

//------------------------------------------------------------------------------
bool vtkPointLabelRepresentation::ShouldDrawLabels()
{
  if (!(this->ShowPointLabels != 0 && this->GetVisibility()))
  {
    return false;
  }
  return !IsExplicitNoneArray(this->PointLabelArray);
}

//------------------------------------------------------------------------------
const char* vtkPointLabelRepresentation::GetEffectivePointLabelArrayName(vtkDataSet* mergedPiece) const
{
  const char* req = this->PointLabelArray;
  if (IsExplicitNoneArray(req))
  {
    return nullptr;
  }
  const bool useActive = !req || !req[0] || strcmp(req, "(Scalars)") == 0;
  if (useActive)
  {
    if (mergedPiece && mergedPiece->GetPointData() && mergedPiece->GetPointData()->GetScalars())
    {
      return mergedPiece->GetPointData()->GetScalars()->GetName();
    }
    return nullptr;
  }
  return req;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetVisibility(bool val)
{
  this->Superclass::SetVisibility(val);
  if (!this->ShouldDrawLabels())
  {
    this->LabelActor->VisibilityOff();
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetPointLabelArray(const char* name)
{
  if (this->PointLabelArray == nullptr && name == nullptr)
  {
    return;
  }
  if (this->PointLabelArray && name && (!strcmp(this->PointLabelArray, name)))
  {
    return;
  }
  delete[] this->PointLabelArray;
  this->PointLabelArray = nullptr;
  if (name)
  {
    size_t n = strlen(name) + 1;
    this->PointLabelArray = new char[n];
    memcpy(this->PointLabelArray, name, n);
  }
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetLabelFormat(const char* format)
{
  if (this->LabelFormat == nullptr && format == nullptr)
  {
    return;
  }
  if (this->LabelFormat && format && (!strcmp(this->LabelFormat, format)))
  {
    return;
  }
  delete[] this->LabelFormat;
  this->LabelFormat = nullptr;
  if (format && format[0])
  {
    size_t n = strlen(format) + 1;
    this->LabelFormat = new char[n];
    memcpy(this->LabelFormat, format, n);
  }
  if (this->LabelMapper)
  {
    if (this->LabelFormat && this->LabelFormat[0])
    {
      this->LabelMapper->SetLabelFormat(this->LabelFormat);
    }
    else
    {
      this->LabelMapper->SetLabelFormat(nullptr);
    }
  }
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetMaximumNumberOfLabels(int n)
{
  if (n < 1)
  {
    n = 1;
  }
  if (n == this->MaximumNumberOfLabels)
  {
    return;
  }
  this->MaximumNumberOfLabels = n;
  if (this->PointMask)
  {
    this->PointMask->SetMaximumNumberOfPoints(n);
  }
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::UpdateLabelTransform()
{
  if (!this->Actor || !this->TransformHelperProp || !this->LabelTransform)
  {
    return;
  }
  this->TransformHelperProp->SetOrientation(this->Actor->GetOrientation());
  this->TransformHelperProp->SetOrigin(this->Actor->GetOrigin());
  this->TransformHelperProp->SetPosition(this->Actor->GetPosition());
  this->TransformHelperProp->SetScale(this->Actor->GetScale());
  this->TransformHelperProp->SetUserTransform(this->Actor->GetUserTransform());
  double elements[16];
  this->TransformHelperProp->GetMatrix(elements);
  this->LabelTransform->SetMatrix(elements);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetOrientation(double x, double y, double z)
{
  this->Superclass::SetOrientation(x, y, z);
  this->UpdateLabelTransform();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetOrigin(double x, double y, double z)
{
  this->Superclass::SetOrigin(x, y, z);
  this->UpdateLabelTransform();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetPosition(double x, double y, double z)
{
  this->Superclass::SetPosition(x, y, z);
  this->UpdateLabelTransform();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetScale(double x, double y, double z)
{
  this->Superclass::SetScale(x, y, z);
  this->UpdateLabelTransform();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetUserTransform(const double matrix[16])
{
  this->Superclass::SetUserTransform(matrix);
  this->UpdateLabelTransform();
}

//------------------------------------------------------------------------------
bool vtkPointLabelRepresentation::AddToView(vtkView* view)
{
  vtkPVRenderView* rview = vtkPVRenderView::SafeDownCast(view);
  if (rview)
  {
    rview->GetNonCompositedRenderer()->AddActor(this->LabelActor);
  }
  return this->Superclass::AddToView(view);
}

//------------------------------------------------------------------------------
bool vtkPointLabelRepresentation::RemoveFromView(vtkView* view)
{
  vtkPVRenderView* rview = vtkPVRenderView::SafeDownCast(view);
  if (rview)
  {
    rview->GetNonCompositedRenderer()->RemoveActor(this->LabelActor);
  }
  return this->Superclass::RemoveFromView(view);
}

//------------------------------------------------------------------------------
int vtkPointLabelRepresentation::ProcessViewRequest(
  vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo)
{
  if (!this->Superclass::ProcessViewRequest(request_type, inInfo, outInfo))
  {
    return 0;
  }

  if (request_type == vtkPVView::REQUEST_RENDER())
  {
    if (!this->ShouldDrawLabels())
    {
      this->LabelActor->VisibilityOff();
      return 1;
    }

    vtkAlgorithmOutput* producerPort = vtkPVRenderView::GetPieceProducer(inInfo, this);
    if (!producerPort)
    {
      this->LabelActor->VisibilityOff();
      return 1;
    }

    this->MergeBlocks->SetInputConnection(0, producerPort);
    this->MergeBlocks->Update();

    vtkDataSet* merged = vtkDataSet::SafeDownCast(this->MergeBlocks->GetOutputDataObject(0));
    const char* eff = this->GetEffectivePointLabelArrayName(merged);
    if (!eff)
    {
      this->LabelActor->VisibilityOff();
      return 1;
    }

    if (this->VertexOnly != 0)
    {
      vtkSmartPointer<vtkPolyData> vertexPd = ExtractVertexOnlyPoints(merged);
      if (!vertexPd || vertexPd->GetNumberOfPoints() == 0)
      {
        this->LabelActor->VisibilityOff();
        return 1;
      }
      this->PointMask->SetInputData(vertexPd);
    }
    else
    {
      this->PointMask->SetInputConnection(0, this->MergeBlocks->GetOutputPort());
    }

    this->LabelMapper->SetFieldDataName(eff);
    this->LabelMapper->SetLabelMode(VTK_LABEL_FIELD_DATA);

    this->UpdateLabelTransform();
    this->LabelActor->SetVisibility(1);
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::OnWarningEvent(
  vtkObject*, unsigned long, void* clientdata, void*)
{
  (void)clientdata;
  // Mute vtkLabeledDataMapper missing-array warnings; visibility is handled explicitly.
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ShowPointLabels: " << (this->ShowPointLabels ? "on" : "off") << "\n";
  os << indent << "PointLabelArray: " << (this->PointLabelArray ? this->PointLabelArray : "(null)")
     << "\n";
  os << indent << "MaximumNumberOfLabels: " << this->MaximumNumberOfLabels << "\n";
  os << indent << "VertexOnly: " << (this->VertexOnly ? "on" : "off") << "\n";
}
