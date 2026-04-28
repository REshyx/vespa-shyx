#include "vtkSHYXOppositeFaceThickness.h"

#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSMPThreadLocalObject.h>
#include <vtkSMPTools.h>
#include <vtkStaticCellLocator.h>
#include <vtkTriangleFilter.h>

#include <cmath>
#include <limits>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXOppositeFaceThickness);

namespace
{
constexpr double kMinNormalLength = 1e-30;

double BoundsDiagonal(const double b[6])
{
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool CellUsesPoint(vtkPolyData* mesh, vtkIdType cellId, vtkIdType ptId, vtkIdList* ids)
{
  mesh->GetCellPoints(cellId, ids);
  const vtkIdType n = ids->GetNumberOfIds();
  for (vtkIdType k = 0; k < n; ++k)
  {
    if (ids->GetId(k) == ptId)
    {
      return true;
    }
  }
  return false;
}

/** Thickness = distance from \p pVertex to first valid hit along \c -n (unit); ray segment starts at
 *  \p pVertex + surfaceOff * n. */
double OppositeFaceThicknessRay(vtkStaticCellLocator* locator, vtkPolyData* mesh, vtkIdType ptId,
  const double pVertex[3], const double n[3], double surfaceOff, double maxLen, double rayTol,
  double epsAdv, int maxRayIters, vtkGenericCell* genCell, vtkIdList* cellPtIds)
{
  double pSeg0[3] = { pVertex[0] + n[0] * surfaceOff, pVertex[1] + n[1] * surfaceOff,
    pVertex[2] + n[2] * surfaceOff };
  double pSeg1[3] = { pSeg0[0] - n[0] * maxLen, pSeg0[1] - n[1] * maxLen, pSeg0[2] - n[2] * maxLen };
  const double nanv = std::numeric_limits<double>::quiet_NaN();
  double dirSeg[3];
  double xHit[3], pcoords[3];

  for (int iter = 0; iter < maxRayIters; ++iter)
  {
    double tHit = 0.0;
    int subId = 0;
    vtkIdType cellId = -1;
    const int hit = locator->IntersectWithLine(pSeg0, pSeg1, rayTol, tHit, xHit, pcoords, subId,
      cellId, genCell);
    if (!hit || cellId < 0)
    {
      return nanv;
    }
    if (CellUsesPoint(mesh, cellId, ptId, cellPtIds))
    {
      vtkMath::Subtract(pSeg1, pSeg0, dirSeg);
      const double segLen = vtkMath::Norm(dirSeg);
      if (segLen < 1e-30)
      {
        return nanv;
      }
      dirSeg[0] /= segLen;
      dirSeg[1] /= segLen;
      dirSeg[2] /= segLen;
      pSeg0[0] = xHit[0] + dirSeg[0] * epsAdv;
      pSeg0[1] = xHit[1] + dirSeg[1] * epsAdv;
      pSeg0[2] = xHit[2] + dirSeg[2] * epsAdv;
      continue;
    }
    return std::sqrt(vtkMath::Distance2BetweenPoints(pVertex, xHit));
  }
  return nanv;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXOppositeFaceThickness::vtkSHYXOppositeFaceThickness()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->NormalArrayName = nullptr;
  this->ThicknessArrayName = nullptr;
  this->SetNormalArrayName("Normals");
  this->SetThicknessArrayName("OppositeFaceThickness");
}

//------------------------------------------------------------------------------
vtkSHYXOppositeFaceThickness::~vtkSHYXOppositeFaceThickness()
{
  this->SetNormalArrayName(nullptr);
  this->SetThicknessArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXOppositeFaceThickness::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NormalArrayName: " << (this->NormalArrayName ? this->NormalArrayName : "(null)")
     << "\n";
  os << indent << "AutoComputeNormals: " << this->AutoComputeNormals << "\n";
  os << indent << "MaxRayLength: " << this->MaxRayLength << "\n";
  os << indent << "SurfaceOffset: " << this->SurfaceOffset << "\n";
  os << indent << "ThicknessArrayName: "
     << (this->ThicknessArrayName ? this->ThicknessArrayName : "(null)") << "\n";
  os << indent << "RayTolerance: " << this->RayTolerance << "\n";
  os << indent << "RayAdvanceEpsilon: " << this->RayAdvanceEpsilon << "\n";
  os << indent << "MaxRayIterations: " << this->MaxRayIterations << "\n";
  os << indent << "ThinThicknessRecalculate: " << this->ThinThicknessRecalculate << "\n";
  os << indent << "ThinThicknessRecalculateThreshold: " << this->ThinThicknessRecalculateThreshold
     << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThickness::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThickness::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThickness::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts == 0)
  {
    vtkWarningMacro(<< "Input has no points.");
    output->ShallowCopy(input);
    return 1;
  }

  const char* normalName =
    (this->NormalArrayName && this->NormalArrayName[0]) ? this->NormalArrayName : "Normals";
  const char* outArrayName =
    (this->ThicknessArrayName && this->ThicknessArrayName[0]) ? this->ThicknessArrayName
                                                             : "OppositeFaceThickness";

  vtkPolyData* sourceForNormals = input;
  vtkNew<vtkPolyDataNormals> normalsFilter;
  vtkDataArray* inNormals = input->GetPointData()->GetArray(normalName);
  const bool haveInputNormals = inNormals && inNormals->GetNumberOfComponents() >= 3 &&
    inNormals->GetNumberOfTuples() == nPts;

  vtkDataArray* normals = nullptr;
  if (haveInputNormals)
  {
    normals = inNormals;
  }
  else
  {
    if (!this->AutoComputeNormals)
    {
      vtkErrorMacro(<< "Point normals \"" << normalName
                    << "\" not found or wrong size; enable AutoComputeNormals or provide a 3-vector "
                       "array.");
      return 0;
    }
    normalsFilter->SetInputData(input);
    normalsFilter->SetComputePointNormals(1);
    normalsFilter->SetComputeCellNormals(0);
    normalsFilter->SplittingOff();
    normalsFilter->ConsistencyOn();
    normalsFilter->AutoOrientNormalsOn();
    normalsFilter->Update();
    sourceForNormals = normalsFilter->GetOutput();
    if (!sourceForNormals)
    {
      vtkErrorMacro(<< "vtkPolyDataNormals produced null output.");
      return 0;
    }
    vtkPointData* spd = sourceForNormals->GetPointData();
    normals = spd->GetNormals();
    if (!normals)
    {
      normals = spd->GetArray("Normals");
    }
  }

  if (!normals || normals->GetNumberOfComponents() < 3 || normals->GetNumberOfTuples() != nPts)
  {
    vtkErrorMacro(<< "Could not resolve point normals.");
    return 0;
  }

  vtkNew<vtkTriangleFilter> triFilter;
  triFilter->SetInputData(sourceForNormals);
  triFilter->Update();
  vtkPolyData* mesh = triFilter->GetOutput();
  if (!mesh || mesh->GetNumberOfCells() == 0)
  {
    vtkErrorMacro(<< "Triangle extraction produced empty mesh.");
    return 0;
  }
  mesh->BuildLinks();

  vtkNew<vtkStaticCellLocator> locator;
  locator->SetDataSet(mesh);
  locator->BuildLocator();

  double b[6];
  input->GetBounds(b);
  const double diag = BoundsDiagonal(b);
  const double maxLen = (this->MaxRayLength > 0.0) ? this->MaxRayLength : (10.0 * diag);
  const double surfaceOff =
    (this->SurfaceOffset > 0.0) ? this->SurfaceOffset : (1e-4 * (diag > 0.0 ? diag : 1.0));
  const double epsAdv = (this->RayAdvanceEpsilon > 0.0)
    ? this->RayAdvanceEpsilon
    : (1e-6 * (diag > 0.0 ? diag : 1.0));

  if (!(maxLen > 0.0) || !vtkMath::IsFinite(maxLen))
  {
    vtkErrorMacro(<< "Invalid MaxRayLength (auto or explicit).");
    return 0;
  }

  if (this->ThinThicknessRecalculate &&
    (!(this->ThinThicknessRecalculateThreshold > 0.0) ||
      !vtkMath::IsFinite(this->ThinThicknessRecalculateThreshold)))
  {
    vtkWarningMacro(<< "ThinThicknessRecalculate is on but ThinThicknessRecalculateThreshold is not "
                       "a finite positive value; thin correction is skipped.");
  }

  vtkNew<vtkDoubleArray> thickness;
  thickness->SetName(outArrayName);
  thickness->SetNumberOfComponents(1);
  thickness->SetNumberOfTuples(nPts);
  const double nanv = std::numeric_limits<double>::quiet_NaN();
  thickness->Fill(nanv);

  output->ShallowCopy(input);
  vtkNew<vtkPoints> outPts;
  vtkPoints* queryPts = input->GetPoints();
  const int thinOn = this->ThinThicknessRecalculate;
  if (thinOn)
  {
    outPts->DeepCopy(input->GetPoints());
    output->SetPoints(outPts);
    queryPts = outPts;
  }

  vtkSMPThreadLocalObject<vtkGenericCell> cellTLS;
  vtkSMPThreadLocalObject<vtkIdList> idListTLS;

  vtkStaticCellLocator* locPtr = locator;
  const double thinT = this->ThinThicknessRecalculateThreshold;
  const int maxIters = this->MaxRayIterations;
  const double rayTol = this->RayTolerance;

  vtkSMPTools::For(0, nPts, [&](vtkIdType begin, vtkIdType end) {
    vtkGenericCell* genCell = cellTLS.Local();
    vtkIdList* cellPtIds = idListTLS.Local();
    double p[3], n[3];
    double pPushed[3];
    for (vtkIdType i = begin; i < end; ++i)
    {
      queryPts->GetPoint(i, p);
      normals->GetTuple(i, n);
      const double nlen = vtkMath::Norm(n);
      if (nlen < kMinNormalLength || !vtkMath::IsFinite(nlen))
      {
        thickness->SetValue(i, nanv);
        continue;
      }
      n[0] /= nlen;
      n[1] /= nlen;
      n[2] /= nlen;

      double d0 = OppositeFaceThicknessRay(locPtr, mesh, i, p, n, surfaceOff, maxLen, rayTol, epsAdv,
        maxIters, genCell, cellPtIds);

      double out = d0;
      if (thinOn && thinT > 0.0 && vtkMath::IsFinite(thinT) && vtkMath::IsFinite(d0) && d0 >= 0.0 &&
        d0 < thinT)
      {
        pPushed[0] = p[0] + thinT * n[0];
        pPushed[1] = p[1] + thinT * n[1];
        pPushed[2] = p[2] + thinT * n[2];
        queryPts->SetPoint(i, pPushed);
        const double d1 = OppositeFaceThicknessRay(locPtr, mesh, i, pPushed, n, surfaceOff, maxLen,
          rayTol, epsAdv, maxIters, genCell, cellPtIds);
        if (vtkMath::IsFinite(d1))
        {
          out = d1;
        }
      }
      thickness->SetValue(i, out);
    }
  });

  vtkPointData* outPD = output->GetPointData();
  outPD->RemoveArray(outArrayName);
  outPD->AddArray(thickness);
  return 1;
}

VTK_ABI_NAMESPACE_END
