// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkPointLabelRepresentation.h"

#include "vtkActor.h"
#include "vtkActor2D.h"
#include "vtkAlgorithmOutput.h"
#include "vtkBillboardTextActor3D.h"
#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkCellArray.h"
#include "vtkCellType.h"
#include "vtkDataSet.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkMapper.h"
#include "vtkPoints.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper.h"
#include "vtkPropCollection.h"
#include "vtkProperty.h"
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
 * Exposes BuildLabels so occluded (3D billboard) mode can reuse vtkLabeledDataMapper formatting
 * without going through the 2D overlay render path.
 */
class vtkPointLabelDataMapper : public vtkLabeledDataMapper
{
public:
  static vtkPointLabelDataMapper* New();
  vtkTypeMacro(vtkPointLabelDataMapper, vtkLabeledDataMapper);

  void RebuildLabels()
  {
    this->Update();
    if (!this->GetInputDataObject(0, 0))
    {
      this->NumberOfLabels = 0;
      return;
    }
    this->BuildLabels();
  }

protected:
  vtkPointLabelDataMapper() = default;
  ~vtkPointLabelDataMapper() override = default;

private:
  vtkPointLabelDataMapper(const vtkPointLabelDataMapper&) = delete;
  void operator=(const vtkPointLabelDataMapper&) = delete;
};
vtkStandardNewMacro(vtkPointLabelDataMapper);

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
void vtkPointLabelRepresentation::SetOccludeLabels(int val)
{
  val = val ? 1 : 0;
  if (this->OccludeLabels == val)
  {
    return;
  }
  this->OccludeLabels = val;
  this->Modified();
}

//------------------------------------------------------------------------------
int vtkPointLabelRepresentation::GetOccludeLabels()
{
  return this->OccludeLabels;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::OccludeLabelsOn()
{
  this->SetOccludeLabels(1);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::OccludeLabelsOff()
{
  this->SetOccludeLabels(0);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetShowEdges(int val)
{
  val = val ? 1 : 0;
  if (this->ShowEdges == val)
  {
    return;
  }
  this->ShowEdges = val;
  // Apply immediately; UpdateColoringParameters also re-applies after superclass resets it.
  if (this->Property)
  {
    this->Property->SetEdgeVisibility(this->ShowEdges);
  }
  this->Modified();
}

//------------------------------------------------------------------------------
int vtkPointLabelRepresentation::GetShowEdges()
{
  return this->ShowEdges;
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ShowEdgesOn()
{
  this->SetShowEdges(1);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ShowEdgesOff()
{
  this->SetShowEdges(0);
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
  // Keep every point/vertex for labeling. Default MergePoints=true + UG output can weld coincident
  // points (e.g. arrow tips / shared endpoints) and drop labels.
  this->MergeBlocks->SetMergePoints(false);
  this->MergeBlocks->SetOutputDataSetType(VTK_POLY_DATA);

  this->PointMask = vtkMaskPoints::New();
  this->PointMask->SetOnRatio(1);
  this->PointMask->SetMaximumNumberOfPoints(this->MaximumNumberOfLabels);
  this->PointMask->RandomModeOff();
  this->PointMask->SetRandomModeType(vtkMaskPoints::RANDOM_SAMPLING);

  this->LabelMapper = vtkPointLabelDataMapper::New();
  this->LabelMapper->SetInputConnection(this->PointMask->GetOutputPort());
  this->LabelMapper->SetLabelMode(VTK_LABEL_FIELD_DATA);
  this->LabelMapper->CoordinateSystemWorld();

  this->LabelActor = vtkActor2D::New();
  this->LabelActor->SetMapper(this->LabelMapper);
  this->LabelActor->SetVisibility(0);
  this->LabelActor->PickableOff();

  this->LabelProperty = vtkTextProperty::New();
  this->LabelProperty->SetFontSize(14);
  this->LabelProperty->SetColor(this->LabelColor);
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
  this->BillboardActors.clear();
  this->MainRenderer = nullptr;
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
    this->HideOccludedLabelActors();
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
void vtkPointLabelRepresentation::SetLabelColor(double r, double g, double b)
{
  if (this->LabelColor[0] == r && this->LabelColor[1] == g && this->LabelColor[2] == b)
  {
    return;
  }
  this->LabelColor[0] = r;
  this->LabelColor[1] = g;
  this->LabelColor[2] = b;
  if (this->LabelProperty)
  {
    this->LabelProperty->SetColor(r, g, b);
  }
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetLabelColor(const double rgb[3])
{
  this->SetLabelColor(rgb[0], rgb[1], rgb[2]);
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetDepthOffset(double units)
{
  if (this->DepthOffset == units)
  {
    return;
  }
  this->DepthOffset = units;
  this->ApplyDepthOffsetToAllBillboards();
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ApplyDepthOffsetToBillboard(vtkBillboardTextActor3D* bb)
{
  if (!bb)
  {
    return;
  }
  const bool wasVisible = bb->GetVisibility() != 0;
  bb->VisibilityOn();
  vtkNew<vtkPropCollection> props;
  bb->GetActors(props);
  vtkActor* quadActor = vtkActor::SafeDownCast(props->GetItemAsObject(0));
  if (!wasVisible)
  {
    bb->SetVisibility(0);
  }
  if (!quadActor)
  {
    return;
  }
  quadActor->PickableOff();
  if (vtkProperty* prop = quadActor->GetProperty())
  {
    prop->LightingOff();
  }
  if (vtkMapper* mapper = quadActor->GetMapper())
  {
    // Same mechanism as Surface-With-Edges line offset / selection highlight: negative units pull
    // toward the camera to reduce z-fighting with coincident surface geometry.
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, this->DepthOffset);
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ApplyDepthOffsetToAllBillboards()
{
  for (auto& bb : this->BillboardActors)
  {
    this->ApplyDepthOffsetToBillboard(bb);
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ConfigurePointMaskSampling(vtkIdType numberOfInputPoints)
{
  if (!this->PointMask)
  {
    return;
  }
  this->PointMask->SetOnRatio(1);
  this->PointMask->SetMaximumNumberOfPoints(this->MaximumNumberOfLabels);
  if (numberOfInputPoints > this->MaximumNumberOfLabels)
  {
    this->PointMask->SetRandomModeType(vtkMaskPoints::RANDOM_SAMPLING);
    this->PointMask->RandomModeOn();
  }
  else
  {
    this->PointMask->RandomModeOff();
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SetLabelSource(vtkDataSet* source)
{
  if (!source || !this->LabelMapper)
  {
    return;
  }
  const vtkIdType n = source->GetNumberOfPoints();
  this->ConfigurePointMaskSampling(n);
  // Bypass MaskPoints entirely when every point should be labeled — avoids filter undersampling.
  if (n <= this->MaximumNumberOfLabels)
  {
    this->LabelMapper->SetInputData(source);
  }
  else
  {
    this->PointMask->SetInputData(source);
    this->LabelMapper->SetInputConnection(this->PointMask->GetOutputPort());
  }
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
void vtkPointLabelRepresentation::HideOccludedLabelActors()
{
  for (auto& bb : this->BillboardActors)
  {
    if (bb)
    {
      bb->VisibilityOff();
    }
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::UpdateColoringParameters()
{
  this->Superclass::UpdateColoringParameters();
  // Superclass clears EdgeVisibility unless Representation == SURFACE_WITH_EDGES.
  // Point Label keeps Representation as Surface; re-apply our ShowEdges flag.
  if (this->Property)
  {
    this->Property->SetEdgeVisibility(this->ShowEdges != 0 ? 1 : 0);
    if (this->ShowEdges != 0)
    {
      this->Property->SetRepresentation(VTK_SURFACE);
    }
  }
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::SyncOccludedLabelActors()
{
  auto* mapper = vtkPointLabelDataMapper::SafeDownCast(this->LabelMapper);
  if (!mapper || !this->MainRenderer)
  {
    this->HideOccludedLabelActors();
    return;
  }

  // LabelMapper input is set by SetLabelSource (direct data or MaskPoints).
  mapper->RebuildLabels();

  const int nLabels = mapper->GetNumberOfLabels();
  while (static_cast<int>(this->BillboardActors.size()) < nLabels)
  {
    vtkSmartPointer<vtkBillboardTextActor3D> bb = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    bb->SetTextProperty(this->LabelProperty);
    bb->PickableOff();
    this->ApplyDepthOffsetToBillboard(bb);
    // Add directly to the main renderer. Do NOT wrap in vtkPropAssembly: assembly PokeMatrix
    // clears Prop3D Position to 0 while BillboardTextActor3D builds quads from GetPosition().
    this->MainRenderer->AddActor(bb);
    this->BillboardActors.push_back(bb);
  }

  for (int i = 0; i < nLabels; ++i)
  {
    vtkBillboardTextActor3D* bb = this->BillboardActors[static_cast<size_t>(i)];
    const char* text = mapper->GetLabelText(i);
    if (!text || !text[0])
    {
      bb->VisibilityOff();
      continue;
    }

    double pos[3];
    mapper->GetLabelPosition(i, pos);
    if (this->LabelTransform)
    {
      double world[3];
      this->LabelTransform->TransformPoint(pos, world);
      bb->SetPosition(world);
    }
    else
    {
      bb->SetPosition(pos);
    }
    bb->SetInput(text);
    bb->SetTextProperty(this->LabelProperty);
    bb->VisibilityOn();
  }

  for (size_t i = static_cast<size_t>(nLabels); i < this->BillboardActors.size(); ++i)
  {
    this->BillboardActors[i]->VisibilityOff();
  }
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
    // Overlay path (no occlusion): non-composited 2D labels.
    rview->GetNonCompositedRenderer()->AddActor(this->LabelActor);
    // Occlusion path: 3D billboards added to MainRenderer in SyncOccludedLabelActors.
    this->MainRenderer = rview->GetRenderer();
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
    if (this->MainRenderer)
    {
      for (auto& bb : this->BillboardActors)
      {
        if (bb)
        {
          this->MainRenderer->RemoveActor(bb);
        }
      }
    }
    this->BillboardActors.clear();
    this->MainRenderer = nullptr;
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
      this->HideOccludedLabelActors();
      return 1;
    }

    vtkAlgorithmOutput* producerPort = vtkPVRenderView::GetPieceProducer(inInfo, this);
    if (!producerPort)
    {
      this->LabelActor->VisibilityOff();
      this->HideOccludedLabelActors();
      return 1;
    }

    this->MergeBlocks->SetInputConnection(0, producerPort);
    this->MergeBlocks->Update();

    vtkDataSet* merged = vtkDataSet::SafeDownCast(this->MergeBlocks->GetOutputDataObject(0));
    const char* eff = this->GetEffectivePointLabelArrayName(merged);
    if (!eff)
    {
      this->LabelActor->VisibilityOff();
      this->HideOccludedLabelActors();
      return 1;
    }

    vtkIdType nLabelPoints = 0;
    vtkSmartPointer<vtkDataSet> labelSource;
    if (this->VertexOnly != 0)
    {
      vtkSmartPointer<vtkPolyData> vertexPd = ExtractVertexOnlyPoints(merged);
      if (!vertexPd || vertexPd->GetNumberOfPoints() == 0)
      {
        this->LabelActor->VisibilityOff();
        this->HideOccludedLabelActors();
        return 1;
      }
      nLabelPoints = vertexPd->GetNumberOfPoints();
      labelSource = vertexPd;
    }
    else
    {
      nLabelPoints = merged ? merged->GetNumberOfPoints() : 0;
      labelSource = merged;
    }

    if (!labelSource || nLabelPoints == 0)
    {
      this->LabelActor->VisibilityOff();
      this->HideOccludedLabelActors();
      return 1;
    }

    this->SetLabelSource(labelSource);

    this->LabelMapper->SetFieldDataName(eff);
    this->LabelMapper->SetLabelMode(VTK_LABEL_FIELD_DATA);

    this->UpdateLabelTransform();

    if (this->OccludeLabels != 0)
    {
      this->LabelActor->VisibilityOff();
      this->SyncOccludedLabelActors();
    }
    else
    {
      this->HideOccludedLabelActors();
      this->LabelActor->SetVisibility(1);
    }
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
  os << indent << "OccludeLabels: " << (this->OccludeLabels ? "on" : "off") << "\n";
  os << indent << "ShowEdges: " << (this->ShowEdges ? "on" : "off") << "\n";
  os << indent << "LabelColor: (" << this->LabelColor[0] << ", " << this->LabelColor[1] << ", "
     << this->LabelColor[2] << ")\n";
  os << indent << "DepthOffset: " << this->DepthOffset << "\n";
  os << indent << "PointLabelArray: " << (this->PointLabelArray ? this->PointLabelArray : "(null)")
     << "\n";
  os << indent << "MaximumNumberOfLabels: " << this->MaximumNumberOfLabels << "\n";
  os << indent << "VertexOnly: " << (this->VertexOnly ? "on" : "off") << "\n";
}
