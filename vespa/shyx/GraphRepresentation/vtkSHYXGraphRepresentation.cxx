// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkSHYXGraphRepresentation.h"

#include "vtkActor.h"
#include "vtkActor2D.h"
#include "vtkAlgorithmOutput.h"
#include "vtkCallbackCommand.h"
#include "vtkCellArray.h"
#include "vtkCellCenters.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkCommand.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkExtractCells.h"
#include "vtkExtractEdges.h"
#include "vtkGenerateIds.h"
#include "vtkGeometryFilter.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkLabeledDataMapper.h"
#include "vtkMaskPoints.h"
#include "vtkMath.h"
#include "vtkMergeBlocks.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPVLODActor.h"
#include "vtkPVRenderView.h"
#include "vtkPVView.h"
#include "vtkPointData.h"
#include "vtkPointGaussianMapper.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkTextProperty.h"
#include "vtkTransform.h"
#include "vtkType.h"
#include "vtkUnstructuredGrid.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr const char* kCircleSplat =
  "//VTK::Color::Impl\n"
  "  float dist2 = dot(offsetVCVSOutput.xy,offsetVCVSOutput.xy);\n"
  "  if (dist2 > 1.0) { discard; }\n";

bool IsNoneName(const char* s)
{
  return s && (strcmp(s, "None") == 0 || strcmp(s, "(null)") == 0);
}

bool IsIdName(const char* s)
{
  return !s || !s[0] || strcmp(s, "IDs") == 0 || strcmp(s, "(Scalars)") == 0;
}

int CellDimensionOfType(int cellType)
{
  switch (cellType)
  {
    case VTK_EMPTY_CELL:
      return -1;
    case VTK_VERTEX:
    case VTK_POLY_VERTEX:
      return 0;
    case VTK_LINE:
    case VTK_POLY_LINE:
    case VTK_QUADRATIC_EDGE:
    case VTK_CUBIC_LINE:
    case VTK_LAGRANGE_CURVE:
    case VTK_BEZIER_CURVE:
      return 1;
    case VTK_TRIANGLE:
    case VTK_QUAD:
    case VTK_POLYGON:
    case VTK_PIXEL:
    case VTK_TRIANGLE_STRIP:
    case VTK_QUADRATIC_TRIANGLE:
    case VTK_QUADRATIC_QUAD:
    case VTK_BIQUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
    case VTK_BIQUADRATIC_TRIANGLE:
    case VTK_LAGRANGE_TRIANGLE:
    case VTK_LAGRANGE_QUADRILATERAL:
    case VTK_BEZIER_TRIANGLE:
    case VTK_BEZIER_QUADRILATERAL:
      return 2;
    default:
      return 3;
  }
}

void ContrastColor(const double rgb[3], double out[3])
{
  const double y = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
  const double v = (y > 0.45) ? 0.08 : 1.0;
  out[0] = out[1] = out[2] = v;
}

void CopyStringProp(char*& dst, const char* src)
{
  if (dst == nullptr && src == nullptr)
  {
    return;
  }
  if (dst && src && strcmp(dst, src) == 0)
  {
    return;
  }
  delete[] dst;
  dst = nullptr;
  if (src)
  {
    const size_t n = strlen(src) + 1;
    dst = new char[n];
    memcpy(dst, src, n);
  }
}

vtkSmartPointer<vtkPolyData> PointsAsVerts(vtkDataSet* ds, const std::set<vtkIdType>* only)
{
  if (!ds)
  {
    return nullptr;
  }
  const vtkIdType nIn = ds->GetNumberOfPoints();
  if (nIn < 1)
  {
    return nullptr;
  }

  vtkNew<vtkPoints> pts;
  vtkNew<vtkCellArray> verts;
  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  vtkPointData* inPD = ds->GetPointData();
  vtkPointData* outPD = out->GetPointData();

  if (!only)
  {
    pts->SetNumberOfPoints(nIn);
    for (vtkIdType i = 0; i < nIn; ++i)
    {
      pts->SetPoint(i, ds->GetPoint(i));
    }
    verts->AllocateEstimate(nIn, 1);
    for (vtkIdType i = 0; i < nIn; ++i)
    {
      verts->InsertNextCell(1);
      verts->InsertCellPoint(i);
    }
    out->SetPoints(pts);
    out->SetVerts(verts);
    outPD->PassData(inPD);
    return out;
  }

  if (only->empty())
  {
    return nullptr;
  }
  const vtkIdType nOut = static_cast<vtkIdType>(only->size());
  outPD->CopyAllocate(inPD, nOut);
  vtkIdType newIndex = 0;
  for (vtkIdType oldPid : *only)
  {
    const vtkIdType nid = pts->InsertNextPoint(ds->GetPoint(oldPid));
    verts->InsertNextCell(1);
    verts->InsertCellPoint(nid);
    outPD->CopyData(inPD, oldPid, newIndex);
    ++newIndex;
  }
  out->SetPoints(pts);
  out->SetVerts(verts);
  return out;
}

vtkSmartPointer<vtkPolyData> ExtractVertexPoints(vtkDataSet* ds, bool allPoint)
{
  if (!ds)
  {
    return nullptr;
  }
  if (allPoint)
  {
    return PointsAsVerts(ds, nullptr);
  }

  std::set<vtkIdType> pointIds;
  if (auto* pd = vtkPolyData::SafeDownCast(ds))
  {
    vtkCellArray* va = pd->GetVerts();
    if (!va || va->GetNumberOfCells() == 0)
    {
      return nullptr;
    }
    vtkIdType npts = 0;
    const vtkIdType* pts = nullptr;
    va->InitTraversal();
    while (va->GetNextCell(npts, pts))
    {
      for (vtkIdType i = 0; i < npts; ++i)
      {
        pointIds.insert(pts[i]);
      }
    }
  }
  else
  {
    const vtkIdType nCells = ds->GetNumberOfCells();
    vtkNew<vtkIdList> ids;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (CellDimensionOfType(ds->GetCellType(cid)) != 0)
      {
        continue;
      }
      ds->GetCellPoints(cid, ids);
      for (vtkIdType j = 0; j < ids->GetNumberOfIds(); ++j)
      {
        pointIds.insert(ids->GetId(j));
      }
    }
  }
  return PointsAsVerts(ds, &pointIds);
}

vtkSmartPointer<vtkUnstructuredGrid> ExtractDim(vtkDataSet* ds, int dim)
{
  if (!ds)
  {
    return nullptr;
  }
  vtkNew<vtkIdList> ids;
  const vtkIdType n = ds->GetNumberOfCells();
  ids->Allocate(n);
  for (vtkIdType i = 0; i < n; ++i)
  {
    if (CellDimensionOfType(ds->GetCellType(i)) == dim)
    {
      ids->InsertNextId(i);
    }
  }
  if (ids->GetNumberOfIds() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkExtractCells> ex;
  ex->SetInputData(ds);
  ex->SetCellList(ids);
  ex->Update();
  vtkUnstructuredGrid* ug = vtkUnstructuredGrid::SafeDownCast(ex->GetOutput());
  if (!ug || ug->GetNumberOfCells() == 0)
  {
    return nullptr;
  }
  vtkSmartPointer<vtkUnstructuredGrid> out = vtkSmartPointer<vtkUnstructuredGrid>::New();
  out->ShallowCopy(ug);
  return out;
}

vtkDataArray* FindArray(vtkDataSet* ds, const char* name, bool preferPoint)
{
  if (!ds || !name || !name[0])
  {
    return nullptr;
  }
  vtkDataArray* pd = ds->GetPointData() ? ds->GetPointData()->GetArray(name) : nullptr;
  vtkDataArray* cd = ds->GetCellData() ? ds->GetCellData()->GetArray(name) : nullptr;
  if (preferPoint)
  {
    return pd ? pd : cd;
  }
  return cd ? cd : pd;
}

void AveragePointArrayOntoCells(vtkDataSet* cells, vtkDataArray* pointArr)
{
  if (!cells || !pointArr)
  {
    return;
  }
  const vtkIdType nCells = cells->GetNumberOfCells();
  vtkSmartPointer<vtkDataArray> cellArr;
  cellArr.TakeReference(pointArr->NewInstance());
  cellArr->SetName(pointArr->GetName());
  cellArr->SetNumberOfComponents(pointArr->GetNumberOfComponents());
  cellArr->SetNumberOfTuples(nCells);
  vtkNew<vtkIdList> ids;
  const int nComp = pointArr->GetNumberOfComponents();
  std::vector<double> acc(static_cast<size_t>(nComp), 0.0);
  std::vector<double> tmp(static_cast<size_t>(nComp), 0.0);
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    cells->GetCellPoints(c, ids);
    const vtkIdType npts = ids->GetNumberOfIds();
    for (int k = 0; k < nComp; ++k)
    {
      acc[static_cast<size_t>(k)] = 0.0;
    }
    if (npts < 1)
    {
      cellArr->SetTuple(c, acc.data());
      continue;
    }
    for (vtkIdType j = 0; j < npts; ++j)
    {
      pointArr->GetTuple(ids->GetId(j), tmp.data());
      for (int k = 0; k < nComp; ++k)
      {
        acc[static_cast<size_t>(k)] += tmp[static_cast<size_t>(k)];
      }
    }
    for (int k = 0; k < nComp; ++k)
    {
      acc[static_cast<size_t>(k)] /= static_cast<double>(npts);
    }
    cellArr->SetTuple(c, acc.data());
  }
  cells->GetCellData()->AddArray(cellArr);
}

void ConfigureMask(vtkMaskPoints* mask, vtkIdType n, int maxLabels)
{
  if (!mask)
  {
    return;
  }
  mask->SetOnRatio(1);
  mask->SetMaximumNumberOfPoints(maxLabels);
  if (n > maxLabels)
  {
    mask->SetRandomModeType(vtkMaskPoints::RANDOM_SAMPLING);
    mask->RandomModeOn();
  }
  else
  {
    mask->RandomModeOff();
  }
}

void SetLabelInput(vtkLabeledDataMapper* mapper, vtkMaskPoints* mask, vtkDataSet* source,
  int maxLabels, const char* arrayName, const char* format)
{
  if (!mapper || !source)
  {
    return;
  }
  const vtkIdType n = source->GetNumberOfPoints();
  ConfigureMask(mask, n, maxLabels);
  if (n <= maxLabels)
  {
    mapper->SetInputData(source);
  }
  else if (mask)
  {
    mask->SetInputData(source);
    mapper->SetInputConnection(mask->GetOutputPort());
  }
  mapper->SetLabelMode(VTK_LABEL_FIELD_DATA);
  mapper->SetFieldDataName(arrayName);
  if (format && format[0])
  {
    mapper->SetLabelFormat(format);
  }
  else
  {
    mapper->SetLabelFormat(nullptr);
  }
}

const char* ResolveLabelArray(
  vtkDataSet* ds, const char* requested, bool preferPoint, std::string& owned)
{
  if (IsNoneName(requested))
  {
    return nullptr;
  }
  if (IsIdName(requested))
  {
    const char* idName = preferPoint ? "vtkPointIds" : "vtkCellIds";
    if (FindArray(ds, idName, preferPoint))
    {
      return idName;
    }
    return nullptr;
  }
  if (FindArray(ds, requested, preferPoint))
  {
    return requested;
  }
  owned = requested;
  return owned.c_str();
}

void ApplySolidColor(vtkActor* actor, const double rgb[3], double opacity, bool lighting)
{
  if (!actor)
  {
    return;
  }
  vtkProperty* p = actor->GetProperty();
  p->SetColor(rgb[0], rgb[1], rgb[2]);
  p->SetOpacity(opacity);
  p->SetAmbient(lighting ? 0.35 : 1.0);
  p->SetDiffuse(lighting ? 0.65 : 0.0);
  p->SetSpecular(0.0);
  p->SetLighting(lighting ? 1 : 0);
}

void CopyXform(vtkActor* dst, vtkPVLODActor* src)
{
  if (!dst || !src)
  {
    return;
  }
  dst->SetOrientation(src->GetOrientation());
  dst->SetOrigin(src->GetOrigin());
  dst->SetPosition(src->GetPosition());
  dst->SetScale(src->GetScale());
  dst->SetUserTransform(src->GetUserTransform());
}
} // namespace

vtkStandardNewMacro(vtkSHYXGraphRepresentation);

//------------------------------------------------------------------------------
vtkSHYXGraphRepresentation::vtkSHYXGraphRepresentation()
{
  this->MergeBlocks = vtkMergeBlocks::New();
  this->MergeBlocks->SetMergePoints(false);
  this->MergeBlocks->SetOutputDataSetType(VTK_UNSTRUCTURED_GRID);

  this->LayerTransform = vtkTransform::New();
  this->LayerTransform->Identity();
  this->TransformHelperProp = vtkActor::New();

  this->WarningObserver = vtkCallbackCommand::New();
  this->WarningObserver->SetCallback(&vtkSHYXGraphRepresentation::OnWarningEvent);
  this->WarningObserver->SetClientData(this);

  auto makeLabel = [this](vtkLabeledDataMapper*& mapper, vtkActor2D*& actor, vtkTextProperty*& prop,
                      vtkMaskPoints*& mask) {
    mask = vtkMaskPoints::New();
    mask->SetOnRatio(1);
    mask->SetMaximumNumberOfPoints(this->MaximumNumberOfLabels);
    mask->RandomModeOff();
    mapper = vtkLabeledDataMapper::New();
    mapper->SetInputConnection(mask->GetOutputPort());
    mapper->SetLabelMode(VTK_LABEL_FIELD_DATA);
    mapper->CoordinateSystemWorld();
    mapper->SetTransform(this->LayerTransform);
    mapper->AddObserver(vtkCommand::WarningEvent, this->WarningObserver);
    prop = vtkTextProperty::New();
    prop->SetFontSize(this->LabelFontSize);
    prop->SetBold(0);
    prop->SetJustificationToCentered();
    prop->SetVerticalJustificationToCentered();
    mapper->SetLabelTextProperty(prop);
    actor = vtkActor2D::New();
    actor->SetMapper(mapper);
    actor->SetVisibility(0);
    actor->PickableOff();
  };

  makeLabel(this->VertexLabelMapper, this->VertexLabelActor, this->VertexLabelProperty,
    this->VertexMask);
  makeLabel(this->LineLabelMapper, this->LineLabelActor, this->LineLabelProperty, this->LineMask);
  makeLabel(this->FaceLabelMapper, this->FaceLabelActor, this->FaceLabelProperty, this->FaceMask);
  makeLabel(this->VolumeLabelMapper, this->VolumeLabelActor, this->VolumeLabelProperty,
    this->VolumeMask);

  this->LineCenters = vtkCellCenters::New();
  this->LineCenters->VertexCellsOn();
  this->FaceCenters = vtkCellCenters::New();
  this->FaceCenters->VertexCellsOn();
  this->VolumeCenters = vtkCellCenters::New();
  this->VolumeCenters->VertexCellsOn();

  this->VertexMapper = vtkPointGaussianMapper::New();
  this->VertexMapper->ScalarVisibilityOff();
  this->VertexMapper->EmissiveOn();
  this->VertexMapper->SetSplatShaderCode(kCircleSplat);
  this->VertexMapper->SetBoundScale(1.1);
  this->VertexMapper->SetScaleFactor(1.0);
  this->VertexActor = vtkActor::New();
  this->VertexActor->SetMapper(this->VertexMapper);
  this->VertexActor->SetVisibility(0);
  this->VertexActor->PickableOff();

  auto makeGeom = [](vtkPolyDataMapper*& mapper, vtkActor*& actor) {
    mapper = vtkPolyDataMapper::New();
    mapper->ScalarVisibilityOff();
    actor = vtkActor::New();
    actor->SetMapper(mapper);
    actor->SetVisibility(0);
    actor->PickableOff();
  };
  makeGeom(this->LineMapper, this->LineActor);
  makeGeom(this->FaceMapper, this->FaceActor);
  makeGeom(this->VolumeFaceMapper, this->VolumeFaceActor);
  makeGeom(this->VolumeEdgeMapper, this->VolumeEdgeActor);

  this->LineGeometry = vtkGeometryFilter::New();
  this->FaceGeometry = vtkGeometryFilter::New();
  this->VolumeGeometry = vtkGeometryFilter::New();
  this->VolumeEdges = vtkExtractEdges::New();

  this->VolumeEdgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
  this->VolumeEdgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, -2.0);

  this->SetVertexArray("IDs");
  this->SetLineArray("IDs");
  this->SetFaceArray("IDs");
  this->SetVolumeArray("IDs");
}

//------------------------------------------------------------------------------
vtkSHYXGraphRepresentation::~vtkSHYXGraphRepresentation()
{
  this->SetVertexArray(nullptr);
  this->SetLineArray(nullptr);
  this->SetFaceArray(nullptr);
  this->SetVolumeArray(nullptr);
  this->SetLabelFormat(nullptr);

  auto dropObs = [this](vtkLabeledDataMapper* m) {
    if (m && this->WarningObserver)
    {
      m->RemoveObservers(vtkCommand::WarningEvent, this->WarningObserver);
    }
  };
  dropObs(this->VertexLabelMapper);
  dropObs(this->LineLabelMapper);
  dropObs(this->FaceLabelMapper);
  dropObs(this->VolumeLabelMapper);

  if (this->WarningObserver)
  {
    this->WarningObserver->Delete();
    this->WarningObserver = nullptr;
  }

  this->MergeBlocks->Delete();
  this->LayerTransform->Delete();
  this->TransformHelperProp->Delete();

  this->VertexMapper->Delete();
  this->VertexActor->Delete();
  this->LineMapper->Delete();
  this->LineActor->Delete();
  this->FaceMapper->Delete();
  this->FaceActor->Delete();
  this->VolumeFaceMapper->Delete();
  this->VolumeFaceActor->Delete();
  this->VolumeEdgeMapper->Delete();
  this->VolumeEdgeActor->Delete();

  this->VertexLabelMapper->Delete();
  this->VertexLabelActor->Delete();
  this->VertexLabelProperty->Delete();
  this->VertexMask->Delete();
  this->LineLabelMapper->Delete();
  this->LineLabelActor->Delete();
  this->LineLabelProperty->Delete();
  this->LineMask->Delete();
  this->LineCenters->Delete();
  this->FaceLabelMapper->Delete();
  this->FaceLabelActor->Delete();
  this->FaceLabelProperty->Delete();
  this->FaceMask->Delete();
  this->FaceCenters->Delete();
  this->VolumeLabelMapper->Delete();
  this->VolumeLabelActor->Delete();
  this->VolumeLabelProperty->Delete();
  this->VolumeMask->Delete();
  this->VolumeCenters->Delete();
  this->LineGeometry->Delete();
  this->FaceGeometry->Delete();
  this->VolumeGeometry->Delete();
  this->VolumeEdges->Delete();
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetVertexArray(const char* name)
{
  CopyStringProp(this->VertexArray, name);
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetLineArray(const char* name)
{
  CopyStringProp(this->LineArray, name);
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetFaceArray(const char* name)
{
  CopyStringProp(this->FaceArray, name);
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetVolumeArray(const char* name)
{
  CopyStringProp(this->VolumeArray, name);
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetVertexColor(double r, double g, double b)
{
  if (this->VertexColor[0] == r && this->VertexColor[1] == g && this->VertexColor[2] == b)
  {
    return;
  }
  this->VertexColor[0] = r;
  this->VertexColor[1] = g;
  this->VertexColor[2] = b;
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetVertexColor(const double rgb[3])
{
  this->SetVertexColor(rgb[0], rgb[1], rgb[2]);
}
void vtkSHYXGraphRepresentation::SetLineColor(double r, double g, double b)
{
  if (this->LineColor[0] == r && this->LineColor[1] == g && this->LineColor[2] == b)
  {
    return;
  }
  this->LineColor[0] = r;
  this->LineColor[1] = g;
  this->LineColor[2] = b;
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetLineColor(const double rgb[3])
{
  this->SetLineColor(rgb[0], rgb[1], rgb[2]);
}
void vtkSHYXGraphRepresentation::SetFaceColor(double r, double g, double b)
{
  if (this->FaceColor[0] == r && this->FaceColor[1] == g && this->FaceColor[2] == b)
  {
    return;
  }
  this->FaceColor[0] = r;
  this->FaceColor[1] = g;
  this->FaceColor[2] = b;
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetFaceColor(const double rgb[3])
{
  this->SetFaceColor(rgb[0], rgb[1], rgb[2]);
}
void vtkSHYXGraphRepresentation::SetVolumeColor(double r, double g, double b)
{
  if (this->VolumeColor[0] == r && this->VolumeColor[1] == g && this->VolumeColor[2] == b)
  {
    return;
  }
  this->VolumeColor[0] = r;
  this->VolumeColor[1] = g;
  this->VolumeColor[2] = b;
  this->Modified();
}
void vtkSHYXGraphRepresentation::SetVolumeColor(const double rgb[3])
{
  this->SetVolumeColor(rgb[0], rgb[1], rgb[2]);
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetMaximumNumberOfLabels(int n)
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
  this->VertexMask->SetMaximumNumberOfPoints(n);
  this->LineMask->SetMaximumNumberOfPoints(n);
  this->FaceMask->SetMaximumNumberOfPoints(n);
  this->VolumeMask->SetMaximumNumberOfPoints(n);
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetLabelFormat(const char* format)
{
  if (this->LabelFormat == nullptr && (format == nullptr || !format[0]))
  {
    return;
  }
  if (this->LabelFormat && format && strcmp(this->LabelFormat, format) == 0)
  {
    return;
  }
  delete[] this->LabelFormat;
  this->LabelFormat = nullptr;
  if (format && format[0])
  {
    const size_t n = strlen(format) + 1;
    this->LabelFormat = new char[n];
    memcpy(this->LabelFormat, format, n);
  }
  this->Modified();
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetVisibility(bool val)
{
  this->Superclass::SetVisibility(val);
  if (!val)
  {
    this->HideAllLayers();
  }
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::HideDefaultSurfaceActors()
{
  if (this->Actor)
  {
    this->Actor->SetVisibility(0);
  }
  if (this->BackfaceActor)
  {
    this->BackfaceActor->SetVisibility(0);
  }
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::UpdateColoringParameters()
{
  this->Superclass::UpdateColoringParameters();
  this->HideDefaultSurfaceActors();
  this->ApplyAppearance();
}

//------------------------------------------------------------------------------
bool vtkSHYXGraphRepresentation::NeedsOrderedCompositing()
{
  if ((this->ShowVertex && this->VertexOpacity < 1.0) ||
    (this->ShowLine && this->LineOpacity < 1.0) || (this->ShowFace && this->FaceOpacity < 1.0) ||
    (this->ShowVolume && this->VolumeOpacity < 1.0))
  {
    return true;
  }
  return this->Superclass::NeedsOrderedCompositing();
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::ApplyAppearance()
{
  ApplySolidColor(this->VertexActor, this->VertexColor, this->VertexOpacity, false);
  ApplySolidColor(this->LineActor, this->LineColor, this->LineOpacity, false);
  ApplySolidColor(this->FaceActor, this->FaceColor, this->FaceOpacity, true);
  this->FaceActor->GetProperty()->SetRepresentationToSurface();
  ApplySolidColor(this->VolumeFaceActor, this->VolumeColor, this->VolumeOpacity, true);
  this->VolumeFaceActor->GetProperty()->SetRepresentationToSurface();
  const double edgeOp = std::min(1.0, this->VolumeOpacity + 0.45);
  ApplySolidColor(this->VolumeEdgeActor, this->VolumeColor, edgeOp, false);
  this->LineActor->GetProperty()->SetLineWidth(2.0);
  this->VolumeEdgeActor->GetProperty()->SetLineWidth(1.5);
  this->VolumeEdgeActor->GetProperty()->SetRepresentationToWireframe();

  auto style = [this](vtkTextProperty* p, const double rgb[3]) {
    if (!p)
    {
      return;
    }
    double c[3];
    ContrastColor(rgb, c);
    p->SetColor(c);
    p->SetFontSize(this->LabelFontSize);
    p->SetJustificationToCentered();
    p->SetVerticalJustificationToCentered();
  };
  style(this->VertexLabelProperty, this->VertexColor);
  style(this->LineLabelProperty, this->LineColor);
  style(this->FaceLabelProperty, this->FaceColor);
  style(this->VolumeLabelProperty, this->VolumeColor);
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::HideAllLayers()
{
  this->VertexActor->SetVisibility(0);
  this->LineActor->SetVisibility(0);
  this->FaceActor->SetVisibility(0);
  this->VolumeFaceActor->SetVisibility(0);
  this->VolumeEdgeActor->SetVisibility(0);
  this->VertexLabelActor->SetVisibility(0);
  this->LineLabelActor->SetVisibility(0);
  this->FaceLabelActor->SetVisibility(0);
  this->VolumeLabelActor->SetVisibility(0);
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::UpdateLayerTransforms()
{
  if (!this->Actor || !this->TransformHelperProp || !this->LayerTransform)
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
  this->LayerTransform->SetMatrix(elements);
  CopyXform(this->VertexActor, this->Actor);
  CopyXform(this->LineActor, this->Actor);
  CopyXform(this->FaceActor, this->Actor);
  CopyXform(this->VolumeFaceActor, this->Actor);
  CopyXform(this->VolumeEdgeActor, this->Actor);
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::SetOrientation(double x, double y, double z)
{
  this->Superclass::SetOrientation(x, y, z);
  this->UpdateLayerTransforms();
}
void vtkSHYXGraphRepresentation::SetOrigin(double x, double y, double z)
{
  this->Superclass::SetOrigin(x, y, z);
  this->UpdateLayerTransforms();
}
void vtkSHYXGraphRepresentation::SetPosition(double x, double y, double z)
{
  this->Superclass::SetPosition(x, y, z);
  this->UpdateLayerTransforms();
}
void vtkSHYXGraphRepresentation::SetScale(double x, double y, double z)
{
  this->Superclass::SetScale(x, y, z);
  this->UpdateLayerTransforms();
}
void vtkSHYXGraphRepresentation::SetUserTransform(const double matrix[16])
{
  this->Superclass::SetUserTransform(matrix);
  this->UpdateLayerTransforms();
}

//------------------------------------------------------------------------------
int vtkSHYXGraphRepresentation::RequestData(
  vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  const int ret = this->Superclass::RequestData(request, inputVector, outputVector);
  vtkAlgorithmOutput* port = this->GetInternalOutputPort();
  this->OriginalInput = nullptr;
  if (port && port->GetProducer())
  {
    port->GetProducer()->Update();
    this->OriginalInput = port->GetProducer()->GetOutputDataObject(port->GetIndex());
  }
  return ret;
}

//------------------------------------------------------------------------------
bool vtkSHYXGraphRepresentation::AddToView(vtkView* view)
{
  vtkPVRenderView* rview = vtkPVRenderView::SafeDownCast(view);
  if (rview)
  {
    vtkRenderer* main = rview->GetRenderer();
    vtkRenderer* overlay = rview->GetNonCompositedRenderer();
    this->MainRenderer = main;
    main->AddActor(this->VertexActor);
    main->AddActor(this->LineActor);
    main->AddActor(this->FaceActor);
    main->AddActor(this->VolumeFaceActor);
    main->AddActor(this->VolumeEdgeActor);
    overlay->AddActor(this->VertexLabelActor);
    overlay->AddActor(this->LineLabelActor);
    overlay->AddActor(this->FaceLabelActor);
    overlay->AddActor(this->VolumeLabelActor);
  }
  const bool ok = this->Superclass::AddToView(view);
  this->HideDefaultSurfaceActors();
  return ok;
}

//------------------------------------------------------------------------------
bool vtkSHYXGraphRepresentation::RemoveFromView(vtkView* view)
{
  vtkPVRenderView* rview = vtkPVRenderView::SafeDownCast(view);
  if (rview)
  {
    vtkRenderer* main = rview->GetRenderer();
    vtkRenderer* overlay = rview->GetNonCompositedRenderer();
    main->RemoveActor(this->VertexActor);
    main->RemoveActor(this->LineActor);
    main->RemoveActor(this->FaceActor);
    main->RemoveActor(this->VolumeFaceActor);
    main->RemoveActor(this->VolumeEdgeActor);
    overlay->RemoveActor(this->VertexLabelActor);
    overlay->RemoveActor(this->LineLabelActor);
    overlay->RemoveActor(this->FaceLabelActor);
    overlay->RemoveActor(this->VolumeLabelActor);
    this->MainRenderer = nullptr;
  }
  return this->Superclass::RemoveFromView(view);
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::RebuildFromData(vtkDataSet* ds)
{
  this->HideAllLayers();
  if (!ds || !this->GetVisibility())
  {
    return;
  }
  vtkNew<vtkGenerateIds> genIds;
  genIds->SetInputData(ds);
  genIds->PointIdsOn();
  genIds->CellIdsOn();
  genIds->FieldDataOff();
  genIds->Update();
  if (vtkDataSet* withIds = vtkDataSet::SafeDownCast(genIds->GetOutputDataObject(0)))
  {
    ds = withIds;
  }
  this->ApplyAppearance();
  this->UpdateLayerTransforms();

  double b[6];
  ds->GetBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double radius = (diag > 0.0 ? 0.012 * diag : 1.0) * (this->VertexScale > 0.0 ? this->VertexScale : 1.0);

  auto wireLabels = [this](vtkLabeledDataMapper* mapper, vtkMaskPoints* mask, vtkActor2D* actor,
                      vtkDataSet* labelPts, const char* arrayName, bool preferPoint) {
    if (!labelPts || labelPts->GetNumberOfPoints() < 1)
    {
      actor->SetVisibility(0);
      return;
    }
    std::string owned;
    const char* arr = ResolveLabelArray(labelPts, arrayName, preferPoint, owned);
    if (!arr)
    {
      actor->SetVisibility(0);
      return;
    }
    if (!FindArray(labelPts, arr, preferPoint) && !labelPts->GetPointData()->GetArray(arr))
    {
      actor->SetVisibility(0);
      return;
    }
    SetLabelInput(mapper, mask, labelPts, this->MaximumNumberOfLabels, arr, this->LabelFormat);
    actor->SetVisibility(this->GetVisibility() ? 1 : 0);
  };

  if (this->ShowVertex)
  {
    vtkSmartPointer<vtkPolyData> verts = ExtractVertexPoints(ds, this->AllPoint != 0);
    if (verts && verts->GetNumberOfPoints() > 0)
    {
      this->VertexMapper->SetInputData(verts);
      this->VertexMapper->SetScaleFactor(radius);
      this->VertexActor->SetVisibility(1);
      wireLabels(this->VertexLabelMapper, this->VertexMask, this->VertexLabelActor, verts,
        this->VertexArray, true);
    }
  }

  auto prepCellArray = [](vtkDataSet* cells, vtkDataSet* full, const char* requested) {
    if (!cells || IsNoneName(requested) || IsIdName(requested))
    {
      return;
    }
    if (cells->GetCellData()->GetArray(requested))
    {
      return;
    }
    vtkDataArray* pd = full && full->GetPointData() ? full->GetPointData()->GetArray(requested) : nullptr;
    if (pd)
    {
      AveragePointArrayOntoCells(cells, pd);
    }
  };

  if (this->ShowLine)
  {
    vtkSmartPointer<vtkUnstructuredGrid> lines = ExtractDim(ds, 1);
    if (lines && lines->GetNumberOfCells() > 0)
    {
      prepCellArray(lines, ds, this->LineArray);
      this->LineGeometry->SetInputData(lines);
      this->LineGeometry->Update();
      this->LineMapper->SetInputData(this->LineGeometry->GetOutput());
      this->LineActor->SetVisibility(1);
      this->LineCenters->SetInputData(lines);
      this->LineCenters->Update();
      wireLabels(this->LineLabelMapper, this->LineMask, this->LineLabelActor,
        this->LineCenters->GetOutput(), this->LineArray, false);
    }
  }

  if (this->ShowFace)
  {
    vtkSmartPointer<vtkUnstructuredGrid> faces = ExtractDim(ds, 2);
    if (faces && faces->GetNumberOfCells() > 0)
    {
      prepCellArray(faces, ds, this->FaceArray);
      this->FaceGeometry->SetInputData(faces);
      this->FaceGeometry->Update();
      this->FaceMapper->SetInputData(this->FaceGeometry->GetOutput());
      this->FaceActor->SetVisibility(1);
      this->FaceCenters->SetInputData(faces);
      this->FaceCenters->Update();
      wireLabels(this->FaceLabelMapper, this->FaceMask, this->FaceLabelActor,
        this->FaceCenters->GetOutput(), this->FaceArray, false);
    }
  }

  if (this->ShowVolume)
  {
    vtkSmartPointer<vtkUnstructuredGrid> vols = ExtractDim(ds, 3);
    if (vols && vols->GetNumberOfCells() > 0)
    {
      prepCellArray(vols, ds, this->VolumeArray);
      this->VolumeGeometry->SetInputData(vols);
      this->VolumeGeometry->Update();
      this->VolumeFaceMapper->SetInputData(this->VolumeGeometry->GetOutput());
      this->VolumeFaceActor->SetVisibility(1);
      this->VolumeEdges->SetInputData(vols);
      this->VolumeEdges->Update();
      this->VolumeEdgeMapper->SetInputData(this->VolumeEdges->GetOutput());
      this->VolumeEdgeActor->SetVisibility(1);
      this->VolumeCenters->SetInputData(vols);
      this->VolumeCenters->Update();
      wireLabels(this->VolumeLabelMapper, this->VolumeMask, this->VolumeLabelActor,
        this->VolumeCenters->GetOutput(), this->VolumeArray, false);
    }
  }
}

//------------------------------------------------------------------------------
int vtkSHYXGraphRepresentation::ProcessViewRequest(
  vtkInformationRequestKey* request_type, vtkInformation* inInfo, vtkInformation* outInfo)
{
  if (!this->Superclass::ProcessViewRequest(request_type, inInfo, outInfo))
  {
    this->HideAllLayers();
    return 0;
  }

  if (request_type == vtkPVView::REQUEST_UPDATE())
  {
    if (this->OriginalInput)
    {
      vtkPVView::SetPiece(inInfo, this, this->OriginalInput, 0, 1);
    }
  }
  else if (request_type == vtkPVView::REQUEST_RENDER())
  {
    this->HideDefaultSurfaceActors();
    this->ApplyAppearance();

    vtkAlgorithmOutput* graphPort = vtkPVRenderView::GetPieceProducer(inInfo, this, 1);
    if (!graphPort)
    {
      graphPort = vtkPVRenderView::GetPieceProducer(inInfo, this, 0);
    }
    if (!graphPort)
    {
      this->HideAllLayers();
      return 1;
    }

    graphPort->GetProducer()->Update();
    vtkDataObject* piece = graphPort->GetProducer()->GetOutputDataObject(graphPort->GetIndex());
    vtkDataSet* ds = vtkDataSet::SafeDownCast(piece);
    if (!ds)
    {
      this->MergeBlocks->SetInputData(piece);
      this->MergeBlocks->Update();
      ds = vtkDataSet::SafeDownCast(this->MergeBlocks->GetOutputDataObject(0));
    }
    if (!ds)
    {
      this->HideAllLayers();
      return 1;
    }
    this->RebuildFromData(ds);
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::OnWarningEvent(vtkObject*, unsigned long, void*, void*) {}

//------------------------------------------------------------------------------
void vtkSHYXGraphRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ShowVertex: " << this->ShowVertex << "\n";
  os << indent << "AllPoint: " << this->AllPoint << "\n";
  os << indent << "ShowLine: " << this->ShowLine << "\n";
  os << indent << "ShowFace: " << this->ShowFace << "\n";
  os << indent << "ShowVolume: " << this->ShowVolume << "\n";
}
