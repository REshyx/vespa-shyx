#include "vtkSHYXExtrudeFilter.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkExtractSelection.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObject.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSMPTools.h>
#include <vtkSelection.h>

#include <cmath>
#include <map>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXExtrudeFilter);

namespace
{
using EdgeKey = std::pair<vtkIdType, vtkIdType>;

EdgeKey MakeEdge(vtkIdType a, vtkIdType b)
{
  return a < b ? EdgeKey(a, b) : EdgeKey(b, a);
}

bool IsSurfacePoly(int cellType)
{
  return cellType == VTK_TRIANGLE || cellType == VTK_QUAD || cellType == VTK_POLYGON;
}

void NormalizeOrFallback(double v[3])
{
  const double len = vtkMath::Norm(v);
  if (len > 1e-30)
  {
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }
  else
  {
    v[0] = 0.0;
    v[1] = 0.0;
    v[2] = 1.0;
  }
}

double TupleMagnitude(vtkDataArray* arr, vtkIdType tupleIdx)
{
  if (!arr || tupleIdx < 0 || tupleIdx >= arr->GetNumberOfTuples())
  {
    return 0.0;
  }
  const int nc = arr->GetNumberOfComponents();
  if (nc <= 0)
  {
    return 0.0;
  }
  if (nc == 1)
  {
    return arr->GetTuple1(tupleIdx);
  }
  double sum = 0.0;
  for (int c = 0; c < nc; ++c)
  {
    const double v = arr->GetComponent(tupleIdx, c);
    sum += v * v;
  }
  return std::sqrt(sum);
}

double Tuple1(vtkDataArray* a, vtkIdType i)
{
  if (!a || i < 0 || i >= a->GetNumberOfTuples())
  {
    return 1.0;
  }
  return a->GetComponent(i, 0);
}

bool PassesThreshold(double x, int method, double lo, double hi)
{
  switch (method)
  {
    case vtkSHYXExtrudeFilter::THRESHOLD_BELOW_LOWER:
      return x <= lo;
    case vtkSHYXExtrudeFilter::THRESHOLD_ABOVE_UPPER:
      return x >= hi;
    default:
      return x >= lo && x <= hi;
  }
}

void PolygonNormal(vtkPoints* pts, vtkIdType npts, const vtkIdType* pids, double n[3])
{
  n[0] = n[1] = n[2] = 0.0;
  if (npts < 3 || !pts || !pids)
  {
    n[2] = 1.0;
    return;
  }
  for (vtkIdType i = 0; i < npts; ++i)
  {
    double p0[3], p1[3];
    pts->GetPoint(pids[i], p0);
    pts->GetPoint(pids[(i + 1) % npts], p1);
    n[0] += (p0[1] - p1[1]) * (p0[2] + p1[2]);
    n[1] += (p0[2] - p1[2]) * (p0[0] + p1[0]);
    n[2] += (p0[0] - p1[0]) * (p0[1] + p1[1]);
  }
  NormalizeOrFallback(n);
}

void InsertIds(vtkCellArray* dst, vtkIdType npts, const vtkIdType* pids, bool reverse)
{
  vtkNew<vtkIdList> ids;
  ids->SetNumberOfIds(npts);
  if (!reverse)
  {
    for (vtkIdType i = 0; i < npts; ++i)
    {
      ids->SetId(i, pids[i]);
    }
  }
  else
  {
    for (vtkIdType i = 0; i < npts; ++i)
    {
      ids->SetId(i, pids[npts - 1 - i]);
    }
  }
  dst->InsertNextCell(ids);
}

void InsertQuad(vtkCellArray* dst, vtkIdType a, vtkIdType b, vtkIdType c, vtkIdType d)
{
  vtkNew<vtkIdList> ids;
  ids->SetNumberOfIds(4);
  ids->SetId(0, a);
  ids->SetId(1, b);
  ids->SetId(2, c);
  ids->SetId(3, d);
  dst->InsertNextCell(ids);
}

void ExpandCellsToPoints(vtkPolyData* mesh, const std::vector<char>& selectedCell,
  std::vector<char>& selectedPoint, bool allScalars)
{
  const vtkIdType nPts = mesh->GetNumberOfPoints();
  const vtkIdType nCells = mesh->GetNumberOfCells();
  selectedPoint.assign(static_cast<size_t>(nPts), 0);
  if (!allScalars)
  {
    vtkIdType npts = 0;
    const vtkIdType* pids = nullptr;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!selectedCell[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, npts, pids);
      for (vtkIdType k = 0; k < npts; ++k)
      {
        if (pids[k] >= 0 && pids[k] < nPts)
        {
          selectedPoint[static_cast<size_t>(pids[k])] = 1;
        }
      }
    }
    return;
  }

  mesh->BuildLinks();
  vtkNew<vtkIdList> cellIds;
  for (vtkIdType pid = 0; pid < nPts; ++pid)
  {
    mesh->GetPointCells(pid, cellIds);
    const vtkIdType nInc = cellIds->GetNumberOfIds();
    if (nInc <= 0)
    {
      continue;
    }
    bool allPass = true;
    for (vtkIdType i = 0; i < nInc; ++i)
    {
      const vtkIdType cid = cellIds->GetId(i);
      if (cid < 0 || cid >= nCells || !selectedCell[static_cast<size_t>(cid)])
      {
        allPass = false;
        break;
      }
    }
    selectedPoint[static_cast<size_t>(pid)] = allPass ? 1 : 0;
  }
}

void ExpandPointsToCells(vtkPolyData* mesh, const std::vector<char>& selectedPoint,
  std::vector<char>& selectedCell, bool allScalars)
{
  const vtkIdType nCells = mesh->GetNumberOfCells();
  const vtkIdType nPts = mesh->GetNumberOfPoints();
  selectedCell.assign(static_cast<size_t>(nCells), 0);
  vtkIdType npts = 0;
  const vtkIdType* pids = nullptr;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!IsSurfacePoly(mesh->GetCellType(cid)))
    {
      continue;
    }
    mesh->GetCellPoints(cid, npts, pids);
    if (npts <= 0)
    {
      continue;
    }
    bool anyPass = false;
    bool allPass = true;
    for (vtkIdType k = 0; k < npts; ++k)
    {
      const vtkIdType pid = pids[k];
      const bool pass = (pid >= 0 && pid < nPts && selectedPoint[static_cast<size_t>(pid)]);
      anyPass = anyPass || pass;
      allPass = allPass && pass;
    }
    selectedCell[static_cast<size_t>(cid)] = (allScalars ? allPass : anyPass) ? 1 : 0;
  }
}

void CollectFromExtracted(vtkPolyData* mesh, vtkDataSet* extracted,
  std::vector<char>& selectedCell, std::vector<char>& selectedPoint, bool& primaryIsCell)
{
  const vtkIdType nMeshCells = mesh->GetNumberOfCells();
  const vtkIdType nMeshPts = mesh->GetNumberOfPoints();
  bool markedCells = false;

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
          selectedCell[static_cast<size_t>(cid)] = 1;
          markedCells = true;
        }
      }
    }
  }

  vtkDataArray* opid = extracted->GetPointData()->GetArray("vtkOriginalPointIds");
  if (auto* ptIds = vtkIdTypeArray::SafeDownCast(opid))
  {
    for (vtkIdType i = 0; i < ptIds->GetNumberOfTuples(); ++i)
    {
      const vtkIdType pid = ptIds->GetValue(i);
      if (pid >= 0 && pid < nMeshPts)
      {
        selectedPoint[static_cast<size_t>(pid)] = 1;
      }
    }
  }

  if (markedCells)
  {
    primaryIsCell = true;
    ExpandCellsToPoints(mesh, selectedCell, selectedPoint, false);
    return;
  }

  bool anyPt = false;
  for (char b : selectedPoint)
  {
    if (b)
    {
      anyPt = true;
      break;
    }
  }
  if (anyPt)
  {
    primaryIsCell = false;
    ExpandPointsToCells(mesh, selectedPoint, selectedCell, false);
  }
}

bool AnyTrue(const std::vector<char>& m)
{
  for (char b : m)
  {
    if (b)
    {
      return true;
    }
  }
  return false;
}

void AndMasks(std::vector<char>& a, const std::vector<char>& b)
{
  const size_t n = a.size();
  for (size_t i = 0; i < n; ++i)
  {
    a[i] = (a[i] && b[i]) ? 1 : 0;
  }
}

bool ApplyThresholdMask(vtkPolyData* mesh, const char* name, int method, double lo, double hi,
  bool allScalars, std::vector<char>& selectedCell, std::vector<char>& selectedPoint,
  bool& primaryIsCell, vtkObject* self)
{
  const vtkIdType nCells = mesh->GetNumberOfCells();
  const vtkIdType nPts = mesh->GetNumberOfPoints();
  vtkDataArray* const cellArr = mesh->GetCellData()->GetArray(name);
  vtkDataArray* const ptArr = mesh->GetPointData()->GetArray(name);
  const bool cellOk = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
  const bool pointOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);
  vtkDataArray* arr = nullptr;
  bool usePointCorners = false;
  if (cellOk)
  {
    arr = cellArr;
    usePointCorners = false;
    primaryIsCell = true;
  }
  else if (pointOk)
  {
    arr = ptArr;
    usePointCorners = true;
    primaryIsCell = false;
  }
  if (!arr)
  {
    vtkWarningWithObjectMacro(self, "Mask array \"" << name << "\" not found on point or cell data.");
    return false;
  }
  selectedCell.assign(static_cast<size_t>(nCells), 0);
  selectedPoint.assign(static_cast<size_t>(nPts), 0);
  if (!usePointCorners)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!IsSurfacePoly(mesh->GetCellType(cid)))
      {
        continue;
      }
      selectedCell[static_cast<size_t>(cid)] =
        PassesThreshold(TupleMagnitude(arr, cid), method, lo, hi) ? 1 : 0;
    }
    ExpandCellsToPoints(mesh, selectedCell, selectedPoint, allScalars);
  }
  else
  {
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
      selectedPoint[static_cast<size_t>(pid)] =
        PassesThreshold(TupleMagnitude(arr, pid), method, lo, hi) ? 1 : 0;
    }
    ExpandPointsToCells(mesh, selectedPoint, selectedCell, allScalars);
  }
  return true;
}

void AppendUnselected(vtkPolyData* mesh, const std::vector<char>& selectedCell,
  const std::vector<char>& selectedPoint, bool dropSelectedPolys, vtkCellArray* verts,
  vtkCellArray* lines, vtkCellArray* polys, vtkCellArray* strips, vtkIdType& skippedNonPoly)
{
  const vtkIdType nCells = mesh->GetNumberOfCells();
  vtkIdType npts = 0;
  const vtkIdType* pids = nullptr;
  skippedNonPoly = 0;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    const int type = mesh->GetCellType(cid);
    mesh->GetCellPoints(cid, npts, pids);
    if (IsSurfacePoly(type))
    {
      if (dropSelectedPolys && selectedCell[static_cast<size_t>(cid)])
      {
        continue;
      }
      InsertIds(polys, npts, pids, false);
      continue;
    }
    if (selectedCell[static_cast<size_t>(cid)])
    {
      ++skippedNonPoly;
      continue;
    }
    if (type == VTK_VERTEX || type == VTK_POLY_VERTEX)
    {
      bool keep = true;
      for (vtkIdType k = 0; k < npts; ++k)
      {
        if (selectedPoint[static_cast<size_t>(pids[k])])
        {
          keep = false;
          break;
        }
      }
      if (keep)
      {
        InsertIds(verts, npts, pids, false);
      }
      continue;
    }
    if (type == VTK_LINE || type == VTK_POLY_LINE)
    {
      bool keep = true;
      for (vtkIdType k = 0; k < npts; ++k)
      {
        if (selectedPoint[static_cast<size_t>(pids[k])])
        {
          keep = false;
          break;
        }
      }
      if (keep)
      {
        InsertIds(lines, npts, pids, false);
      }
      continue;
    }
    if (type == VTK_TRIANGLE_STRIP)
    {
      InsertIds(strips, npts, pids, false);
      continue;
    }
    ++skippedNonPoly;
  }
}

void DirectionFromArray(vtkDataArray* vecs, vtkIdType i, double dir[3])
{
  if (!vecs || vecs->GetNumberOfComponents() < 3 || i >= vecs->GetNumberOfTuples())
  {
    dir[0] = 0.0;
    dir[1] = 0.0;
    dir[2] = 1.0;
    return;
  }
  vecs->GetTuple(i, dir);
  NormalizeOrFallback(dir);
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXExtrudeFilter::vtkSHYXExtrudeFilter()
{
  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXExtrudeFilter::~vtkSHYXExtrudeFilter()
{
  this->SetDirectionArrayName(nullptr);
  this->SetDistanceMultiplierArrayName(nullptr);
  this->SetMaskArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXExtrudeFilter::SetSourceConnection(vtkAlgorithmOutput* algOutput)
{
  this->SetInputConnection(1, algOutput);
}

//------------------------------------------------------------------------------
void vtkSHYXExtrudeFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ExtrudeType: " << this->ExtrudeType << "\n";
  os << indent << "OutputFront: " << this->OutputFront << "\n";
  os << indent << "OutputSide: " << this->OutputSide << "\n";
  os << indent << "OutputBack: " << this->OutputBack << "\n";
  os << indent << "ExtrusionDistance: " << this->ExtrusionDistance << "\n";
  os << indent << "FlipDirection: " << this->FlipDirection << "\n";
  os << indent << "UseNormalsForDirection: " << this->UseNormalsForDirection << "\n";
  os << indent << "DirectionArrayName: "
     << (this->DirectionArrayName ? this->DirectionArrayName : "(null)") << "\n";
  os << indent << "UseDistanceMultiplierArray: " << this->UseDistanceMultiplierArray << "\n";
  os << indent << "DistanceMultiplierArrayName: "
     << (this->DistanceMultiplierArrayName ? this->DistanceMultiplierArrayName : "(null)") << "\n";
  os << indent << "MaskArrayName: " << (this->MaskArrayName ? this->MaskArrayName : "(null)")
     << "\n";
  os << indent << "ThresholdMethod: " << this->ThresholdMethod << "\n";
  os << indent << "LowerThreshold: " << this->LowerThreshold << "\n";
  os << indent << "UpperThreshold: " << this->UpperThreshold << "\n";
  os << indent << "AllScalars: " << this->AllScalars << "\n";
  os << indent << "UseSelection: " << this->UseSelection << "\n";
  os << indent << "Invert: " << this->Invert << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXExtrudeFilter::FillInputPortInformation(int port, vtkInformation* info)
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
int vtkSHYXExtrudeFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXExtrudeFilter::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* mesh = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!mesh || !output)
  {
    vtkErrorMacro("Null input or output.");
    return 0;
  }

  const vtkIdType nPts = mesh->GetNumberOfPoints();
  const vtkIdType nCells = mesh->GetNumberOfCells();
  if (nPts == 0)
  {
    output->Initialize();
    return 1;
  }

  mesh->BuildCells();

  std::vector<char> selCell(static_cast<size_t>(nCells), 0);
  std::vector<char> selPoint(static_cast<size_t>(nPts), 0);
  bool selPrimaryIsCell = true;
  bool haveSel = false;

  if (this->UseSelection && this->GetNumberOfInputConnections(1) > 0)
  {
    vtkInformation* selInfo = inputVector[1]->GetInformationObject(0);
    if (selInfo && selInfo->Has(vtkDataObject::DATA_OBJECT()))
    {
      vtkSelection* inputSel =
        vtkSelection::SafeDownCast(selInfo->Get(vtkDataObject::DATA_OBJECT()));
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
          CollectFromExtracted(mesh, extracted, selCell, selPoint, selPrimaryIsCell);
          haveSel = AnyTrue(selCell) || AnyTrue(selPoint);
        }
      }
    }
  }
  if (this->UseSelection && !haveSel)
  {
    vtkWarningMacro("Use Selection is on but the Selection is empty; it does not restrict the region.");
  }

  std::vector<char> maskCell(static_cast<size_t>(nCells), 0);
  std::vector<char> maskPoint(static_cast<size_t>(nPts), 0);
  bool maskPrimaryIsCell = true;
  bool haveMask = false;
  if (this->MaskArrayName && this->MaskArrayName[0] != '\0')
  {
    haveMask = ApplyThresholdMask(mesh, this->MaskArrayName, this->ThresholdMethod,
      this->LowerThreshold, this->UpperThreshold, this->AllScalars != 0, maskCell, maskPoint,
      maskPrimaryIsCell, this);
  }

  std::vector<char> selectedCell(static_cast<size_t>(nCells), 0);
  std::vector<char> selectedPoint(static_cast<size_t>(nPts), 0);
  bool primaryIsCell = true;

  if (haveSel && haveMask)
  {
    if (this->ExtrudeType == EXTRUDE_POLY)
    {
      selectedCell = maskCell;
      AndMasks(selectedCell, selCell);
      ExpandCellsToPoints(mesh, selectedCell, selectedPoint, false);
      primaryIsCell = true;
    }
    else
    {
      selectedPoint = maskPoint;
      AndMasks(selectedPoint, selPoint);
      ExpandPointsToCells(mesh, selectedPoint, selectedCell, this->AllScalars != 0);
      primaryIsCell = false;
    }
  }
  else if (haveSel)
  {
    selectedCell = std::move(selCell);
    selectedPoint = std::move(selPoint);
    primaryIsCell = selPrimaryIsCell;
  }
  else if (haveMask)
  {
    selectedCell = std::move(maskCell);
    selectedPoint = std::move(maskPoint);
    primaryIsCell = maskPrimaryIsCell;
  }
  else
  {
    primaryIsCell = true;
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
      selectedPoint[static_cast<size_t>(pid)] = 1;
    }
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      selectedCell[static_cast<size_t>(cid)] = IsSurfacePoly(mesh->GetCellType(cid)) ? 1 : 0;
    }
  }

  if (this->Invert)
  {
    if (primaryIsCell)
    {
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (IsSurfacePoly(mesh->GetCellType(cid)))
        {
          selectedCell[static_cast<size_t>(cid)] =
            selectedCell[static_cast<size_t>(cid)] ? 0 : 1;
        }
        else
        {
          selectedCell[static_cast<size_t>(cid)] = 0;
        }
      }
      ExpandCellsToPoints(mesh, selectedCell, selectedPoint, false);
    }
    else
    {
      for (vtkIdType pid = 0; pid < nPts; ++pid)
      {
        selectedPoint[static_cast<size_t>(pid)] = selectedPoint[static_cast<size_t>(pid)] ? 0 : 1;
      }
      ExpandPointsToCells(mesh, selectedPoint, selectedCell, this->AllScalars != 0);
    }
  }

  vtkIdType nSelCell = 0;
  vtkIdType nSelPt = 0;
  vtkIdType skippedSelNonPoly = 0;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    if (!selectedCell[static_cast<size_t>(cid)])
    {
      continue;
    }
    if (IsSurfacePoly(mesh->GetCellType(cid)))
    {
      ++nSelCell;
    }
    else
    {
      selectedCell[static_cast<size_t>(cid)] = 0;
      ++skippedSelNonPoly;
    }
  }
  for (vtkIdType pid = 0; pid < nPts; ++pid)
  {
    if (selectedPoint[static_cast<size_t>(pid)])
    {
      ++nSelPt;
    }
  }
  if (skippedSelNonPoly > 0)
  {
    vtkWarningMacro(<< skippedSelNonPoly
                    << " selected non-polygon cells were ignored (verts/lines/strips are not extruded).");
  }

  const bool pointInPlace =
    (this->ExtrudeType == EXTRUDE_POINT && !this->OutputSide && !this->OutputBack);
  const bool emitAnything = this->OutputFront || this->OutputSide || this->OutputBack;

  if (!emitAnything)
  {
    vtkWarningMacro("Output Front, Side, and Back are all off; keeping unselected geometry only.");
  }

  auto finishEmptyish = [&]() {
    vtkNew<vtkCellArray> verts;
    vtkNew<vtkCellArray> lines;
    vtkNew<vtkCellArray> polys;
    vtkNew<vtkCellArray> strips;
    vtkIdType skipped = 0;
    vtkNew<vtkPoints> pts;
    pts->DeepCopy(mesh->GetPoints());
    AppendUnselected(mesh, selectedCell, selectedPoint, /*dropSelectedPolys=*/true, verts, lines,
      polys, strips, skipped);
    output->Initialize();
    output->SetPoints(pts);
    output->SetVerts(verts);
    output->SetLines(lines);
    output->SetPolys(polys);
    output->SetStrips(strips);
    return 1;
  };

  if (!emitAnything)
  {
    return finishEmptyish();
  }

  if (pointInPlace)
  {
    if (!this->OutputFront || nSelPt == 0)
    {
      if (nSelPt == 0)
      {
        vtkWarningMacro("No selected points; passing input through unchanged.");
        output->ShallowCopy(mesh);
        return 1;
      }
      return finishEmptyish();
    }

    vtkDataArray* normals = nullptr;
    vtkNew<vtkPolyDataNormals> computeNormals;
    vtkDataArray* dirArray = nullptr;

    if (this->UseNormalsForDirection)
    {
      computeNormals->SetInputData(mesh);
      computeNormals->ComputePointNormalsOn();
      computeNormals->ComputeCellNormalsOff();
      computeNormals->ConsistencyOn();
      computeNormals->AutoOrientNormalsOn();
      computeNormals->SplittingOff();
      computeNormals->Update();
      vtkPolyData* nOut = computeNormals->GetOutput();
      if (nOut && nOut->GetNumberOfPoints() == nPts)
      {
        normals = nOut->GetPointData()->GetNormals();
      }
      if (!normals || normals->GetNumberOfTuples() != nPts)
      {
        vtkWarningMacro("Could not obtain point normals; provide a Direction Array or ensure "
                        "the mesh has polygon connectivity.");
        normals = nullptr;
      }
    }
    else
    {
      if (!this->DirectionArrayName || this->DirectionArrayName[0] == '\0')
      {
        vtkErrorMacro("Direction Array Name is required when UseNormalsForDirection is off.");
        return 0;
      }
      dirArray = mesh->GetPointData()->GetArray(this->DirectionArrayName);
      if (!dirArray || dirArray->GetNumberOfComponents() < 3 ||
        dirArray->GetNumberOfTuples() != nPts)
      {
        vtkErrorMacro("Direction array \""
          << this->DirectionArrayName << "\" missing or not a 3-component point array.");
        return 0;
      }
    }

    vtkDataArray* multArray = nullptr;
    if (this->UseDistanceMultiplierArray)
    {
      if (!this->DistanceMultiplierArrayName || this->DistanceMultiplierArrayName[0] == '\0')
      {
        vtkErrorMacro("Distance Multiplier Array Name is required when enabled.");
        return 0;
      }
      multArray = mesh->GetPointData()->GetArray(this->DistanceMultiplierArrayName);
      if (!multArray || multArray->GetNumberOfTuples() != nPts)
      {
        vtkErrorMacro("Distance multiplier array \"" << this->DistanceMultiplierArrayName
                                                     << "\" not found on point data.");
        return 0;
      }
    }

    output->DeepCopy(mesh);
    vtkPoints* outPts = output->GetPoints();
    const double base = this->ExtrusionDistance;
    const int flip = this->FlipDirection ? -1 : 1;

    vtkSMPTools::For(0, nPts, [&](vtkIdType begin, vtkIdType end) {
      double dir[3];
      double p[3];
      for (vtkIdType i = begin; i < end; ++i)
      {
        if (!selectedPoint[static_cast<size_t>(i)])
        {
          continue;
        }
        if (this->UseNormalsForDirection && normals)
        {
          normals->GetTuple(i, dir);
          NormalizeOrFallback(dir);
        }
        else if (dirArray)
        {
          DirectionFromArray(dirArray, i, dir);
        }
        else
        {
          dir[0] = 0.0;
          dir[1] = 0.0;
          dir[2] = 1.0;
        }
        if (flip < 0)
        {
          dir[0] = -dir[0];
          dir[1] = -dir[1];
          dir[2] = -dir[2];
        }
        double dist = base;
        if (multArray)
        {
          dist *= Tuple1(multArray, i);
        }
        outPts->GetPoint(i, p);
        outPts->SetPoint(i, p[0] + dist * dir[0], p[1] + dist * dir[1], p[2] + dist * dir[2]);
      }
    });
    return 1;
  }

  if (nSelCell == 0)
  {
    vtkWarningMacro("No selected polygons; passing input through unchanged.");
    output->ShallowCopy(mesh);
    return 1;
  }

  vtkPoints* inPts = mesh->GetPoints();
  vtkNew<vtkPoints> outPts;
  outPts->DeepCopy(inPts);

  vtkDataArray* pointNormals = nullptr;
  vtkNew<vtkPolyDataNormals> computeNormals;
  vtkDataArray* dirArray = nullptr;
  if (this->ExtrudeType == EXTRUDE_POINT)
  {
    if (this->UseNormalsForDirection)
    {
      computeNormals->SetInputData(mesh);
      computeNormals->ComputePointNormalsOn();
      computeNormals->ComputeCellNormalsOff();
      computeNormals->ConsistencyOn();
      computeNormals->AutoOrientNormalsOn();
      computeNormals->SplittingOff();
      computeNormals->Update();
      vtkPolyData* nOut = computeNormals->GetOutput();
      if (nOut && nOut->GetNumberOfPoints() == nPts)
      {
        pointNormals = nOut->GetPointData()->GetNormals();
      }
    }
    else
    {
      if (!this->DirectionArrayName || this->DirectionArrayName[0] == '\0')
      {
        vtkErrorMacro("Direction Array Name is required when UseNormalsForDirection is off.");
        return 0;
      }
      dirArray = mesh->GetPointData()->GetArray(this->DirectionArrayName);
      if (!dirArray || dirArray->GetNumberOfComponents() < 3 ||
        dirArray->GetNumberOfTuples() != nPts)
      {
        vtkErrorMacro("Direction array \""
          << this->DirectionArrayName << "\" missing or not a 3-component point array.");
        return 0;
      }
    }
  }

  vtkDataArray* multPt = nullptr;
  vtkDataArray* multCell = nullptr;
  if (this->UseDistanceMultiplierArray)
  {
    if (!this->DistanceMultiplierArrayName || this->DistanceMultiplierArrayName[0] == '\0')
    {
      vtkErrorMacro("Distance Multiplier Array Name is required when enabled.");
      return 0;
    }
    vtkDataArray* namedCell = mesh->GetCellData()->GetArray(this->DistanceMultiplierArrayName);
    vtkDataArray* namedPt = mesh->GetPointData()->GetArray(this->DistanceMultiplierArrayName);
    if (namedCell && namedCell->GetNumberOfTuples() == nCells)
    {
      multCell = namedCell;
    }
    else if (namedPt && namedPt->GetNumberOfTuples() == nPts)
    {
      multPt = namedPt;
    }
    else
    {
      vtkErrorMacro("Distance multiplier array \"" << this->DistanceMultiplierArrayName
                                                   << "\" not found.");
      return 0;
    }
  }

  auto pointDir = [&](vtkIdType pid, double dir[3]) {
    if (this->UseNormalsForDirection && pointNormals && pid < pointNormals->GetNumberOfTuples())
    {
      pointNormals->GetTuple(pid, dir);
      NormalizeOrFallback(dir);
    }
    else if (dirArray)
    {
      DirectionFromArray(dirArray, pid, dir);
    }
    else
    {
      dir[0] = 0.0;
      dir[1] = 0.0;
      dir[2] = 1.0;
    }
    if (this->FlipDirection)
    {
      dir[0] = -dir[0];
      dir[1] = -dir[1];
      dir[2] = -dir[2];
    }
  };

  auto pointDist = [&](vtkIdType pid) {
    double dist = this->ExtrusionDistance;
    if (multPt)
    {
      dist *= Tuple1(multPt, pid);
    }
    return dist;
  };

  auto faceDist = [&](vtkIdType cid, vtkIdType npts, const vtkIdType* pids) {
    double dist = this->ExtrusionDistance;
    if (multCell)
    {
      dist *= Tuple1(multCell, cid);
    }
    else if (multPt && npts > 0)
    {
      double acc = 0.0;
      for (vtkIdType k = 0; k < npts; ++k)
      {
        acc += Tuple1(multPt, pids[k]);
      }
      dist *= (acc / static_cast<double>(npts));
    }
    return dist;
  };

  vtkNew<vtkCellArray> verts;
  vtkNew<vtkCellArray> lines;
  vtkNew<vtkCellArray> polys;
  vtkNew<vtkCellArray> strips;
  vtkIdType skippedUnsel = 0;
  AppendUnselected(
    mesh, selectedCell, selectedPoint, /*dropSelectedPolys=*/true, verts, lines, polys, strips,
    skippedUnsel);
  if (skippedUnsel > 0)
  {
    vtkWarningMacro(<< skippedUnsel << " unselected non-polygon cells were omitted from rebuild.");
  }

  const bool reverseBack = (this->OutputBack && this->OutputFront);
  vtkIdType npts = 0;
  const vtkIdType* pids = nullptr;

  if (this->OutputBack)
  {
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!selectedCell[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, npts, pids);
      InsertIds(polys, npts, pids, reverseBack);
    }
  }

  if (this->ExtrudeType == EXTRUDE_POINT)
  {
    std::map<vtkIdType, vtkIdType> topMap;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!selectedCell[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, npts, pids);
      for (vtkIdType k = 0; k < npts; ++k)
      {
        const vtkIdType vid = pids[k];
        if (topMap.count(vid) != 0)
        {
          continue;
        }
        double p[3];
        double dir[3];
        inPts->GetPoint(vid, p);
        pointDir(vid, dir);
        const double d = pointDist(vid);
        const vtkIdType nid =
          outPts->InsertNextPoint(p[0] + d * dir[0], p[1] + d * dir[1], p[2] + d * dir[2]);
        topMap[vid] = nid;
      }
    }

    if (this->OutputFront)
    {
      vtkNew<vtkIdList> capIds;
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (!selectedCell[static_cast<size_t>(cid)])
        {
          continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        capIds->SetNumberOfIds(npts);
        for (vtkIdType k = 0; k < npts; ++k)
        {
          capIds->SetId(k, topMap[pids[k]]);
        }
        polys->InsertNextCell(capIds);
      }
    }

    if (this->OutputSide)
    {
      std::map<EdgeKey, int> edgeCount;
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (!selectedCell[static_cast<size_t>(cid)])
        {
          continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        for (vtkIdType k = 0; k < npts; ++k)
        {
          ++edgeCount[MakeEdge(pids[k], pids[(k + 1) % npts])];
        }
      }
      for (vtkIdType cid = 0; cid < nCells; ++cid)
      {
        if (!selectedCell[static_cast<size_t>(cid)])
        {
          continue;
        }
        mesh->GetCellPoints(cid, npts, pids);
        for (vtkIdType k = 0; k < npts; ++k)
        {
          const vtkIdType v0 = pids[k];
          const vtkIdType v1 = pids[(k + 1) % npts];
          if (edgeCount[MakeEdge(v0, v1)] != 1)
          {
            continue;
          }
          const auto it0 = topMap.find(v0);
          const auto it1 = topMap.find(v1);
          if (it0 == topMap.end() || it1 == topMap.end())
          {
            continue;
          }
          InsertQuad(polys, v0, v1, it1->second, it0->second);
        }
      }
    }
  }
  else
  {
    vtkIdType skippedDegenerate = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
      if (!selectedCell[static_cast<size_t>(cid)])
      {
        continue;
      }
      mesh->GetCellPoints(cid, npts, pids);
      if (npts < 3)
      {
        ++skippedDegenerate;
        continue;
      }
      double n[3];
      PolygonNormal(inPts, npts, pids, n);
      if (this->FlipDirection)
      {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
      }
      const double d = faceDist(cid, npts, pids);
      std::vector<vtkIdType> cap(static_cast<size_t>(npts));
      for (vtkIdType k = 0; k < npts; ++k)
      {
        double p[3];
        inPts->GetPoint(pids[k], p);
        cap[static_cast<size_t>(k)] =
          outPts->InsertNextPoint(p[0] + d * n[0], p[1] + d * n[1], p[2] + d * n[2]);
      }
      if (this->OutputFront)
      {
        InsertIds(polys, npts, cap.data(), false);
      }
      if (this->OutputSide)
      {
        for (vtkIdType k = 0; k < npts; ++k)
        {
          const vtkIdType v0 = pids[k];
          const vtkIdType v1 = pids[(k + 1) % npts];
          InsertQuad(polys, v0, v1, cap[static_cast<size_t>((k + 1) % npts)],
            cap[static_cast<size_t>(k)]);
        }
      }
    }
    if (skippedDegenerate > 0)
    {
      vtkWarningMacro(<< skippedDegenerate << " selected cells with fewer than 3 points skipped.");
    }
  }

  output->Initialize();
  output->SetPoints(outPts);
  output->SetVerts(verts);
  output->SetLines(lines);
  output->SetPolys(polys);
  output->SetStrips(strips);
  return 1;
}

VTK_ABI_NAMESPACE_END
