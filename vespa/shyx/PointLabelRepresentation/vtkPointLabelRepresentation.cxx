// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkPointLabelRepresentation.h"

#include "vtkActor.h"
#include "vtkAlgorithmOutput.h"
#include "vtkCallbackCommand.h"
#include "vtkCommand.h"
#include "vtkCellArray.h"
#include "vtkCellType.h"
#include "vtkDataSet.h"
#include "vtkFastLabeledDataMapper.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkMapper.h"
#include "vtkOpenGLActor.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLState.h"
#include "vtkPoints.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkProperty.h"
#include "vtkUnstructuredGrid.h"
#include "vtkMaskPoints.h"
#include "vtkMergeBlocks.h"
#include "vtkObjectFactory.h"
#include "vtkPVLODActor.h"
#include "vtkPVRenderView.h"
#include "vtkPVView.h"
#include "vtkRenderer.h"
#include "vtkTextProperty.h"
#include "vtk_glad.h"

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
 * Fast-mapper labels have to live in the main 3D renderer. Overlay (used by the
 * old vtkActor2D path) does not clear depth, so 3D glyphs vanish there.
 * OccludeLabels=off therefore disables GL_DEPTH_TEST for this actor only.
 */
class vtkPointLabelGLActor : public vtkOpenGLActor
{
public:
  static vtkPointLabelGLActor* New()
  {
    auto* self = new vtkPointLabelGLActor;
    self->InitializeObjectBase();
    return self;
  }
  vtkTypeMacro(vtkPointLabelGLActor, vtkOpenGLActor);

  vtkSetMacro(DisableDepthTest, int);
  vtkGetMacro(DisableDepthTest, int);

  void Render(vtkRenderer* ren, vtkMapper* mapper) override
  {
    vtkOpenGLRenderWindow* win =
      vtkOpenGLRenderWindow::SafeDownCast(ren ? ren->GetRenderWindow() : nullptr);
    vtkOpenGLState* state = win ? win->GetState() : nullptr;
    if (state && this->DisableDepthTest != 0)
    {
      vtkOpenGLState::ScopedglEnableDisable depthTest(state, GL_DEPTH_TEST);
      state->vtkglDisable(GL_DEPTH_TEST);
      this->Superclass::Render(ren, mapper);
      return;
    }
    this->Superclass::Render(ren, mapper);
  }

protected:
  vtkPointLabelGLActor() = default;
  ~vtkPointLabelGLActor() override = default;
  int DisableDepthTest = 1;

private:
  vtkPointLabelGLActor(const vtkPointLabelGLActor&) = delete;
  void operator=(const vtkPointLabelGLActor&) = delete;
};

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
  this->ApplyOcclusionMode();
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
  this->PointMask->GenerateVerticesOn();

  this->LabelMapper = vtkFastLabeledDataMapper::New();
  this->LabelMapper->SetInputConnection(this->PointMask->GetOutputPort());
  this->LabelMapper->SetLabelModeToLabelFieldData();
  this->LabelMapper->SetTextAnchor(vtkFastLabeledDataMapper::Center);
  this->LabelMapper->SetResolveCoincidentTopologyToPolygonOffset();

  this->LabelActor = vtkPointLabelGLActor::New();
  this->LabelActor->SetMapper(this->LabelMapper);
  this->LabelActor->SetVisibility(0);
  this->LabelActor->PickableOff();
  this->LabelActor->SetUseBounds(false);
  this->LabelActor->ForceTranslucentOn();
  if (vtkProperty* ap = this->LabelActor->GetProperty())
  {
    ap->LightingOff();
    ap->SetAmbient(1.0);
    ap->SetDiffuse(0.0);
    ap->SetSpecular(0.0);
  }

  this->LabelProperty = vtkTextProperty::New();
  this->LabelProperty->SetFontSize(14);
  this->LabelProperty->SetColor(this->LabelColor);
  this->LabelProperty->SetBold(0);
  this->LabelProperty->SetItalic(0);
  this->LabelMapper->SetLabelTextProperty(this->LabelProperty);

  this->WarningObserver = vtkCallbackCommand::New();
  this->WarningObserver->SetCallback(&vtkPointLabelRepresentation::OnWarningEvent);
  this->WarningObserver->SetClientData(this);
  this->LabelMapper->AddObserver(vtkCommand::WarningEvent, this->WarningObserver);
  this->ApplyOcclusionMode();
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
  this->MainRenderer = nullptr;
  this->OverlayRenderer = nullptr;
  this->CachedDataSet = nullptr;
  this->MergeBlocks->Delete();
  this->PointMask->Delete();
  this->LabelMapper->Delete();
  this->LabelActor->Delete();
  this->LabelProperty->Delete();
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
  this->ApplyDepthOffset();
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ApplyDepthOffset()
{
  if (!this->LabelMapper)
  {
    return;
  }
  // Always pull label quads toward the camera. Zero offset z-fights with the
  // surface and the numbers vanish even when the mapper is in the main renderer.
  this->LabelMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, this->DepthOffset);
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
  if (!source || !this->LabelMapper || !this->PointMask)
  {
    return;
  }
  const vtkIdType n = source->GetNumberOfPoints();
  this->ConfigurePointMaskSampling(n);
  // Always wire through a port: vtkFastLabeledDataMapper::RenderPiece restores
  // GetInputConnection and drops a pure SetInputData path.
  this->PointMask->SetInputData(source);
  this->LabelMapper->SetInputConnection(this->PointMask->GetOutputPort());
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
  if (!this->Actor || !this->LabelActor)
  {
    return;
  }
  this->LabelActor->SetOrientation(this->Actor->GetOrientation());
  this->LabelActor->SetOrigin(this->Actor->GetOrigin());
  this->LabelActor->SetPosition(this->Actor->GetPosition());
  this->LabelActor->SetScale(this->Actor->GetScale());
  this->LabelActor->SetUserTransform(this->Actor->GetUserTransform());
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::ApplyOcclusionMode()
{
  if (auto* a = vtkPointLabelGLActor::SafeDownCast(this->LabelActor))
  {
    // 0 = always on top (no depth test); 1 = hidden by nearer geometry.
    a->SetDisableDepthTest(this->OccludeLabels ? 0 : 1);
  }
  this->ApplyDepthOffset();
}

//------------------------------------------------------------------------------
void vtkPointLabelRepresentation::PlaceLabelActor()
{
  // vtkFastLabeledDataMapper is a 3D geometry-shader mapper. The overlay
  // renderer is for vtkActor2D and does not clear depth, so 3D labels there
  // never show. Always put the actor in the main renderer; OccludeLabels
  // toggles depth testing instead of moving renderers.
  if (this->OverlayRenderer)
  {
    this->OverlayRenderer->RemoveActor(this->LabelActor);
  }
  if (this->MainRenderer)
  {
    this->MainRenderer->AddActor(this->LabelActor);
  }
  this->ApplyOcclusionMode();
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
    this->OverlayRenderer = rview->GetNonCompositedRenderer();
    this->MainRenderer = rview->GetRenderer();
    this->PlaceLabelActor();
  }
  return this->Superclass::AddToView(view);
}

//------------------------------------------------------------------------------
bool vtkPointLabelRepresentation::RemoveFromView(vtkView* view)
{
  vtkPVRenderView* rview = vtkPVRenderView::SafeDownCast(view);
  if (rview)
  {
    if (this->OverlayRenderer)
    {
      this->OverlayRenderer->RemoveActor(this->LabelActor);
    }
    if (this->MainRenderer)
    {
      this->MainRenderer->RemoveActor(this->LabelActor);
    }
    this->OverlayRenderer = nullptr;
    this->MainRenderer = nullptr;
    this->CachedDataSet = nullptr;
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
    this->PlaceLabelActor();
    if (!this->ShouldDrawLabels())
    {
      this->LabelActor->VisibilityOff();
      this->CachedDataSet = nullptr;
      return 1;
    }

    vtkAlgorithmOutput* producerPort = vtkPVRenderView::GetPieceProducer(inInfo, this);
    if (!producerPort)
    {
      this->LabelActor->VisibilityOff();
      this->CachedDataSet = nullptr;
      return 1;
    }

    this->MergeBlocks->SetInputConnection(0, producerPort);
    this->MergeBlocks->Update();

    vtkDataSet* merged = vtkDataSet::SafeDownCast(this->MergeBlocks->GetOutputDataObject(0));
    const char* eff = this->GetEffectivePointLabelArrayName(merged);
    if (!eff)
    {
      this->LabelActor->VisibilityOff();
      this->CachedDataSet = nullptr;
      return 1;
    }

    const vtkMTimeType dataMTime = merged ? merged->GetMTime() : 0;
    const vtkMTimeType propMTime = this->GetMTime();
    if (this->CachedDataSet == merged && dataMTime == this->CachedDataMTime &&
      propMTime == this->CachedPropMTime)
    {
      this->UpdateLabelTransform();
      this->LabelActor->SetVisibility(1);
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
        this->CachedDataSet = nullptr;
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
      this->CachedDataSet = nullptr;
      return 1;
    }

    this->SetLabelSource(labelSource);
    this->LabelMapper->SetFieldDataName(eff);
    this->LabelMapper->SetLabelModeToLabelFieldData();
    this->UpdateLabelTransform();
    this->LabelActor->SetVisibility(1);
    this->CachedDataSet = merged;
    this->CachedDataMTime = dataMTime;
    this->CachedPropMTime = propMTime;
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
