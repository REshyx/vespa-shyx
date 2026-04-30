#include "vtkSHYXSelectionPlaneClipper.h"

#include <vtkAppendPolyData.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCleanPolyData.h>
#include <vtkClipPolyData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkFillHolesFilter.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkSelection.h>
#include <vtkTriangleFilter.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXSelectionPlaneClipper);

namespace
{
constexpr char kPlanePackedFieldName[] = "SHYX_SelectionPlaneClipper_PlanePacked";

void RemovePlanePackedField(vtkPolyData* pd)
{
  if (pd && pd->GetFieldData())
  {
    pd->GetFieldData()->RemoveArray(kPlanePackedFieldName);
  }
}

void AttachPlanePackedField(vtkPolyData* output, const double meshBounds[6], const double origin[3],
  const double planeNormal[3])
{
  if (!output)
  {
    return;
  }
  const double dx = meshBounds[1] - meshBounds[0];
  const double dy = meshBounds[3] - meshBounds[2];
  const double dz = meshBounds[5] - meshBounds[4];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double arrowLen = diag * 0.04;
  const double dh[3] = { origin[0] + arrowLen * planeNormal[0], origin[1] + arrowLen * planeNormal[1],
    origin[2] + arrowLen * planeNormal[2] };

  vtkNew<vtkDoubleArray> arr;
  arr->SetName(kPlanePackedFieldName);
  arr->SetNumberOfComponents(6);
  arr->SetNumberOfTuples(1);
  const double tuple[6] = { origin[0], origin[1], origin[2], dh[0], dh[1], dh[2] };
  arr->SetTuple(0, tuple);
  output->GetFieldData()->RemoveArray(kPlanePackedFieldName);
  output->GetFieldData()->AddArray(arr);
}

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
    if (mesh->GetCellType(cid) != VTK_TRIANGLE)
    {
      continue;
    }
    mesh->GetCellPoints(cid, npts, pids);
    for (int k = 0; k < 3; ++k)
    {
      if (selPt.count(pids[k]) != 0u)
      {
        selected.insert(cid);
        break;
      }
    }
  }
}

vtkSmartPointer<vtkPolyData> BuildTrianglePatch(vtkPolyData* mesh, const std::set<vtkIdType>& selected)
{
  vtkNew<vtkPoints> pts;
  std::vector<vtkIdType> meshPidToNew;
  meshPidToNew.assign(static_cast<size_t>(mesh->GetNumberOfPoints()), -1);

  auto mapPoint = [&](vtkIdType pid) -> vtkIdType {
    if (pid < 0 || pid >= mesh->GetNumberOfPoints())
    {
      return -1;
    }
    const size_t ui = static_cast<size_t>(pid);
    if (meshPidToNew[ui] >= 0)
    {
      return meshPidToNew[ui];
    }
    double x[3];
    mesh->GetPoint(pid, x);
    const vtkIdType nid = pts->InsertNextPoint(x);
    meshPidToNew[ui] = nid;
    return nid;
  };

  vtkNew<vtkCellArray> polys;
  for (vtkIdType cid : selected)
  {
    if (mesh->GetCellType(cid) != VTK_TRIANGLE)
    {
      continue;
    }
    vtkIdType npts = 0;
    const vtkIdType* pids = nullptr;
    mesh->GetCellPoints(cid, npts, pids);
    if (npts != 3)
    {
      continue;
    }
    const vtkIdType a = mapPoint(pids[0]);
    const vtkIdType b = mapPoint(pids[1]);
    const vtkIdType c = mapPoint(pids[2]);
    if (a < 0 || b < 0 || c < 0)
    {
      continue;
    }
    polys->InsertNextCell(3);
    polys->InsertCellPoint(a);
    polys->InsertCellPoint(b);
    polys->InsertCellPoint(c);
  }

  vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
  out->SetPoints(pts);
  out->SetPolys(polys);
  return out;
}

bool ParseInteractivePacked(const char* s, std::vector<double>& out)
{
  out.clear();
  if (!s || !*s)
  {
    return false;
  }
  std::istringstream iss(s);
  double v = 0.0;
  while (iss >> v)
  {
    out.push_back(v);
  }
  return out.size() == 6u;
}

vtkSmartPointer<vtkPolyData> clipOneSide(vtkPolyData* input, vtkPlane* plane, bool keepPositive)
{
  vtkNew<vtkClipPolyData> clipper;
  clipper->SetInputData(input);
  clipper->SetClipFunction(plane);
  clipper->SetValue(0.0);
  if (!keepPositive)
  {
    clipper->InsideOutOn();
  }
  clipper->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(clipper->GetOutput());
  return o;
}

vtkSmartPointer<vtkPolyData> extractClosestComponent(vtkPolyData* input, const double pt[3])
{
  if (!input || input->GetNumberOfPoints() == 0)
  {
    return nullptr;
  }
  vtkNew<vtkPolyDataConnectivityFilter> conn;
  conn->SetInputData(input);
  conn->SetExtractionModeToClosestPointRegion();
  conn->SetClosestPoint(pt[0], pt[1], pt[2]);
  conn->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(conn->GetOutput());
  return o;
}

vtkSmartPointer<vtkPolyData> removeClosestComponent(vtkPolyData* side, const double refPt[3])
{
  if (!side || side->GetNumberOfPoints() == 0)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }
  vtkNew<vtkPolyDataConnectivityFilter> allRegions;
  allRegions->SetInputData(side);
  allRegions->SetExtractionModeToAllRegions();
  allRegions->ColorRegionsOn();
  allRegions->Update();

  const int nRegions = allRegions->GetNumberOfExtractedRegions();
  if (nRegions <= 1)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  vtkSmartPointer<vtkPolyData> closestComp = extractClosestComponent(side, refPt);
  if (!closestComp || closestComp->GetNumberOfPoints() == 0)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  double probePt[3];
  closestComp->GetPoint(0, probePt);

  vtkNew<vtkPointLocator> locator;
  locator->SetDataSet(allRegions->GetOutput());
  locator->BuildLocator();
  const vtkIdType ptId = locator->FindClosestPoint(probePt);

  vtkDataArray* regionIds = allRegions->GetOutput()->GetPointData()->GetArray("RegionId");
  if (!regionIds)
  {
    return vtkSmartPointer<vtkPolyData>::New();
  }

  const int tipRegionId = static_cast<int>(regionIds->GetTuple1(ptId));

  vtkNew<vtkPolyDataConnectivityFilter> keep;
  keep->SetInputData(side);
  keep->SetExtractionModeToSpecifiedRegions();
  for (int r = 0; r < nRegions; ++r)
  {
    if (r != tipRegionId)
    {
      keep->AddSpecifiedRegion(r);
    }
  }
  keep->Update();
  vtkSmartPointer<vtkPolyData> o = vtkSmartPointer<vtkPolyData>::New();
  o->DeepCopy(keep->GetOutput());
  return o;
}

/** Area-weighted triangle centroid and summed normal*area. Returns false if no usable triangles. */
bool ComputePatchCentroidAndNormal(vtkPolyData* patch, double centroid[3], double avgNormal[3])
{
  centroid[0] = centroid[1] = centroid[2] = 0.0;
  avgNormal[0] = avgNormal[1] = avgNormal[2] = 0.0;

  vtkNew<vtkTriangleFilter> triF;
  triF->SetInputData(patch);
  triF->Update();
  vtkPolyData* triMesh = triF->GetOutput();
  if (!triMesh || triMesh->GetNumberOfCells() == 0)
  {
    return false;
  }

  vtkNew<vtkPolyDataNormals> nrm;
  nrm->SetInputData(triMesh);
  nrm->ComputeCellNormalsOn();
  nrm->ComputePointNormalsOff();
  nrm->ConsistencyOff();
  nrm->SplittingOff();
  nrm->AutoOrientNormalsOff();
  nrm->Update();
  vtkPolyData* withN = nrm->GetOutput();
  vtkDataArray* cellNormals = withN->GetCellData()->GetNormals();
  if (!cellNormals || cellNormals->GetNumberOfTuples() != withN->GetNumberOfCells())
  {
    return false;
  }

  double totalArea = 0.0;
  const vtkIdType nCells = withN->GetNumberOfCells();
  for (vtkIdType cellId = 0; cellId < nCells; ++cellId)
  {
    vtkCell* cell = withN->GetCell(cellId);
    if (!cell || cell->GetCellType() != VTK_TRIANGLE)
    {
      continue;
    }
    double p0[3], p1[3], p2[3];
    withN->GetPoint(cell->GetPointId(0), p0);
    withN->GetPoint(cell->GetPointId(1), p1);
    withN->GetPoint(cell->GetPointId(2), p2);
    double e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    double c[3];
    vtkMath::Cross(e1, e2, c);
    const double area = 0.5 * vtkMath::Norm(c);
    if (area < 1e-30)
    {
      continue;
    }
    const double gc[3] = { (p0[0] + p1[0] + p2[0]) / 3.0, (p0[1] + p1[1] + p2[1]) / 3.0,
      (p0[2] + p1[2] + p2[2]) / 3.0 };
    double n[3];
    cellNormals->GetTuple(cellId, n);
    if (vtkMath::Normalize(n) < 1e-15)
    {
      continue;
    }
    totalArea += area;
    centroid[0] += gc[0] * area;
    centroid[1] += gc[1] * area;
    centroid[2] += gc[2] * area;
    avgNormal[0] += n[0] * area;
    avgNormal[1] += n[1] * area;
    avgNormal[2] += n[2] * area;
  }

  if (totalArea < 1e-30)
  {
    // Point-only patch: use average position and default Z normal
    const vtkIdType np = patch->GetNumberOfPoints();
    if (np < 1)
    {
      return false;
    }
    for (vtkIdType i = 0; i < np; ++i)
    {
      double x[3];
      patch->GetPoint(i, x);
      centroid[0] += x[0];
      centroid[1] += x[1];
      centroid[2] += x[2];
    }
    const double inv = 1.0 / static_cast<double>(np);
    centroid[0] *= inv;
    centroid[1] *= inv;
    centroid[2] *= inv;
    avgNormal[0] = 0.0;
    avgNormal[1] = 0.0;
    avgNormal[2] = 1.0;
    return true;
  }

  const double invA = 1.0 / totalArea;
  centroid[0] *= invA;
  centroid[1] *= invA;
  centroid[2] *= invA;
  if (vtkMath::Normalize(avgNormal) < 1e-15)
  {
    avgNormal[0] = 0.0;
    avgNormal[1] = 0.0;
    avgNormal[2] = 1.0;
  }
  return true;
}

} // namespace

vtkSHYXSelectionPlaneClipper::vtkSHYXSelectionPlaneClipper()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
}

vtkSHYXSelectionPlaneClipper::~vtkSHYXSelectionPlaneClipper()
{
  this->SetInteractiveCutPackedString(nullptr);
  this->SetSelectionCellArrayName(nullptr);
}

void vtkSHYXSelectionPlaneClipper::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

void vtkSHYXSelectionPlaneClipper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ClipOffset: " << this->ClipOffset << "\n";
  os << indent << "InvertPlane: " << this->InvertPlane << "\n";
  os << indent << "UseTipConnectivity: " << this->UseTipConnectivity << "\n";
  os << indent << "RemovePositiveHalfSpace: " << this->RemovePositiveHalfSpace << "\n";
  os << indent << "UseInteractiveCutPlanes: " << this->UseInteractiveCutPlanes << "\n";
  os << indent << "FillHoles: " << this->FillHoles << "\n";
  os << indent << "FillHolesMaximumSize: " << this->FillHolesMaximumSize << "\n";
  os << indent << "InteractiveCutPackedString: "
     << (this->InteractiveCutPackedString ? this->InteractiveCutPackedString : "") << "\n";
  os << indent << "SelectionCellArrayName: "
     << (this->SelectionCellArrayName ? this->SelectionCellArrayName : "(null)") << "\n";
}

int vtkSHYXSelectionPlaneClipper::FillInputPortInformation(int port, vtkInformation* info)
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

int vtkSHYXSelectionPlaneClipper::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXSelectionPlaneClipper::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!mesh || !output)
  {
    return 0;
  }

  if (!mesh->GetNumberOfCells())
  {
    vtkWarningMacro("Input mesh is empty.");
    output->Initialize();
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
      vtkWarningMacro("SelectionCellArrayName \"" << this->SelectionCellArrayName
                                                  << "\" not found on input cell data.");
    }
    else
    {
      const vtkIdType nc = mesh->GetNumberOfCells();
      for (vtkIdType cid = 0; cid < nc; ++cid)
      {
        if (mesh->GetCellType(cid) != VTK_TRIANGLE)
        {
          continue;
        }
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

  vtkSmartPointer<vtkPolyData> patch = BuildTrianglePatch(mesh, selected);
  if (!patch || patch->GetNumberOfCells() == 0)
  {
    vtkWarningMacro("No selected triangles (use Copy Active Selection on the Selection port, "
                    "or set Selection Cell Array Name). Pass-through input mesh.");
    output->ShallowCopy(mesh);
    RemovePlanePackedField(output);
    return 1;
  }

  double centroid[3];
  double planeNormal[3];
  if (!ComputePatchCentroidAndNormal(patch, centroid, planeNormal))
  {
    vtkWarningMacro("Could not compute centroid/normal from selection patch.");
    output->ShallowCopy(mesh);
    RemovePlanePackedField(output);
    return 1;
  }

  if (this->InvertPlane)
  {
    planeNormal[0] = -planeNormal[0];
    planeNormal[1] = -planeNormal[1];
    planeNormal[2] = -planeNormal[2];
  }

  double origin[3] = { centroid[0] + this->ClipOffset * planeNormal[0],
    centroid[1] + this->ClipOffset * planeNormal[1], centroid[2] + this->ClipOffset * planeNormal[2] };

  if (this->UseInteractiveCutPlanes)
  {
    std::vector<double> packed;
    if (ParseInteractivePacked(this->InteractiveCutPackedString, packed))
    {
      const double* o = packed.data();
      const double* d = packed.data() + 3;
      double nx = d[0] - o[0];
      double ny = d[1] - o[1];
      double nz = d[2] - o[2];
      const double nn = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (nn > 1e-15)
      {
        origin[0] = o[0];
        origin[1] = o[1];
        origin[2] = o[2];
        planeNormal[0] = nx / nn;
        planeNormal[1] = ny / nn;
        planeNormal[2] = nz / nn;
      }
    }
  }

  double meshBounds[6];
  mesh->GetBounds(meshBounds);

  vtkNew<vtkPlane> plane;
  plane->SetOrigin(origin);
  plane->SetNormal(planeNormal);

  vtkSmartPointer<vtkPolyData> result;

  if (this->UseTipConnectivity)
  {
    vtkSmartPointer<vtkPolyData> sidePos = clipOneSide(mesh, plane, true);
    vtkSmartPointer<vtkPolyData> sideNeg = clipOneSide(mesh, plane, false);

    if ((!sidePos || sidePos->GetNumberOfPoints() == 0) && (!sideNeg || sideNeg->GetNumberOfPoints() == 0))
    {
      vtkWarningMacro("Clip produced empty geometry; pass-through.");
      output->ShallowCopy(mesh);
      RemovePlanePackedField(output);
      return 1;
    }

    vtkSmartPointer<vtkPolyData> closestPos = extractClosestComponent(sidePos, centroid);
    vtkSmartPointer<vtkPolyData> closestNeg = extractClosestComponent(sideNeg, centroid);

    const vtkIdType nClosestPos = closestPos ? closestPos->GetNumberOfPoints() : 0;
    const vtkIdType nClosestNeg = closestNeg ? closestNeg->GetNumberOfPoints() : 0;

    bool tipOnPositive = false;
    if (nClosestPos == 0)
    {
      tipOnPositive = false;
    }
    else if (nClosestNeg == 0)
    {
      tipOnPositive = true;
    }
    else
    {
      tipOnPositive = (nClosestPos <= nClosestNeg);
    }

    vtkPolyData* tipSide = tipOnPositive ? sidePos.GetPointer() : sideNeg.GetPointer();
    vtkPolyData* bodySide = tipOnPositive ? sideNeg.GetPointer() : sidePos.GetPointer();

    vtkSmartPointer<vtkPolyData> keptFromTipSide = removeClosestComponent(tipSide, centroid);
    const bool hasKept = (keptFromTipSide && keptFromTipSide->GetNumberOfCells() > 0);

    if (!hasKept)
    {
      result = bodySide;
    }
    else
    {
      vtkNew<vtkAppendPolyData> merger;
      merger->AddInputData(bodySide);
      merger->AddInputData(keptFromTipSide);
      merger->Update();
      vtkNew<vtkCleanPolyData> cleaner;
      cleaner->SetInputConnection(merger->GetOutputPort());
      cleaner->Update();
      result = vtkSmartPointer<vtkPolyData>::New();
      result->DeepCopy(cleaner->GetOutput());
    }
  }
  else
  {
    vtkNew<vtkClipPolyData> clipper;
    clipper->SetInputData(mesh);
    clipper->SetClipFunction(plane);
    clipper->SetValue(0.0);
    if (this->RemovePositiveHalfSpace)
    {
      clipper->InsideOutOff();
    }
    else
    {
      clipper->InsideOutOn();
    }
    clipper->Update();
    result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(clipper->GetOutput());
  }

  if (!result || result->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro("Clip result empty; pass-through input.");
    output->ShallowCopy(mesh);
    RemovePlanePackedField(output);
    return 1;
  }

  vtkNew<vtkCleanPolyData> finalClean;
  finalClean->SetInputData(result);
  finalClean->Update();
  vtkPolyData* cleaned = finalClean->GetOutput();

  if (this->FillHoles && cleaned && cleaned->GetNumberOfPoints() > 0)
  {
    double bb[6];
    cleaned->GetBounds(bb);
    const double dx = bb[1] - bb[0];
    const double dy = bb[3] - bb[2];
    const double dz = bb[5] - bb[4];
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double holeSize = (this->FillHolesMaximumSize > 0.0)
      ? this->FillHolesMaximumSize
      : std::max(diag * 0.35, 1e-6);

    vtkNew<vtkFillHolesFilter> filler;
    filler->SetInputData(cleaned);
    filler->SetHoleSize(holeSize);
    filler->Update();
    vtkNew<vtkCleanPolyData> postClean;
    postClean->SetInputConnection(filler->GetOutputPort());
    postClean->Update();
    output->ShallowCopy(postClean->GetOutput());
  }
  else
  {
    output->ShallowCopy(cleaned);
  }
  AttachPlanePackedField(output, meshBounds, origin, planeNormal);
  return 1;
}

VTK_ABI_NAMESPACE_END
