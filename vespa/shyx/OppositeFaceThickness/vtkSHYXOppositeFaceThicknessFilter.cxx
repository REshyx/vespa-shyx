#include "vtkSHYXOppositeFaceThicknessFilter.h"

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkGenericCell.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkSmartPointer.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolygon.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkSMPThreadLocal.h>
#include <vtkSMPTools.h>
#include <vtkTriangle.h>

#include <algorithm>
#include <cmath>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXOppositeFaceThicknessFilter);

namespace
{
void UnitOrZ(double v[3])
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

bool PointUsesCellIds(const std::vector<vtkIdType>& ids, vtkIdType pid)
{
  for (vtkIdType id : ids)
  {
    if (id == pid)
    {
      return true;
    }
  }
  return false;
}

/** Squared distance from \a p to triangle (v0,v1,v2); VTK 9+ has no vtkTriangle::DistanceToTriangle. */
void PointToTriangleDist2(const double p[3], const double v0[3], const double v1[3], const double v2[3],
  double closest[3], double& dist2, vtkTriangle* tri, vtkPoints* triPts, std::vector<double>& weights)
{
  if (weights.size() < 3)
  {
    weights.resize(3);
  }
  triPts->SetNumberOfPoints(3);
  triPts->SetPoint(0, v0);
  triPts->SetPoint(1, v1);
  triPts->SetPoint(2, v2);
  tri->Initialize(3, triPts);
  int subId = 0;
  double pcoords[3];
  dist2 = VTK_DOUBLE_MAX;
  const int ok = tri->EvaluatePosition(p, closest, subId, pcoords, dist2, weights.data());
  if (ok < 0 || !vtkMath::IsFinite(dist2))
  {
    dist2 = VTK_DOUBLE_MAX;
  }
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXOppositeFaceThicknessFilter::vtkSHYXOppositeFaceThicknessFilter()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetThicknessArrayName("SHYX_MinOppositeFaceThickness");
  this->SetPointNormalArrayName("Normals");
}

//------------------------------------------------------------------------------
vtkSHYXOppositeFaceThicknessFilter::~vtkSHYXOppositeFaceThicknessFilter()
{
  this->SetThicknessArrayName(nullptr);
  this->SetPointNormalArrayName(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXOppositeFaceThicknessFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "OppositeNormalDotMax: " << this->OppositeNormalDotMax << "\n";
  os << indent << "ThicknessArrayName: "
     << (this->ThicknessArrayName ? this->ThicknessArrayName : "(null)") << "\n";
  os << indent << "UseInputNormalArray: " << this->UseInputNormalArray << "\n";
  os << indent << "PointNormalArrayName: "
     << (this->PointNormalArrayName ? this->PointNormalArrayName : "(null)") << "\n";
  os << indent << "EnableThicknessRepair: " << this->EnableThicknessRepair << "\n";
  os << indent << "ThicknessThreshold: " << this->ThicknessThreshold << "\n";
  os << indent << "MaxDisplacement: " << this->MaxDisplacement << "\n";
  os << indent << "InvalidThicknessValue: " << this->InvalidThicknessValue << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThicknessFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThicknessFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXOppositeFaceThicknessFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro("Null input or output.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts == 0)
  {
    output->Initialize();
    return 1;
  }

  vtkPoints* inPts = input->GetPoints();
  if (!inPts)
  {
    vtkErrorMacro("Input has no vtkPoints.");
    return 0;
  }

  vtkNew<vtkPolyDataNormals> norGen;
  norGen->SetInputData(input);
  norGen->ComputeCellNormalsOn();
  norGen->SetComputePointNormals(this->UseInputNormalArray ? 0 : 1);
  norGen->ConsistencyOn();
  norGen->AutoOrientNormalsOn();
  norGen->SplittingOff();
  norGen->Update();

  vtkPolyData* normed = norGen->GetOutput();
  vtkDataArray* cellNormals = normed->GetCellData()->GetNormals();
  if (!cellNormals || cellNormals->GetNumberOfTuples() != input->GetNumberOfCells())
  {
    vtkErrorMacro("Could not obtain per-cell normals.");
    return 0;
  }

  vtkDataArray* pointNormals = nullptr;
  if (this->UseInputNormalArray)
  {
    const char* aname = this->PointNormalArrayName;
    if (!aname || aname[0] == '\0')
    {
      vtkErrorMacro("PointNormalArrayName is required when UseInputNormalArray is on.");
      return 0;
    }
    pointNormals = input->GetPointData()->GetArray(aname);
    if (!pointNormals)
    {
      vtkErrorMacro("Point normal array \"" << aname << "\" not found on input point data.");
      return 0;
    }
    if (pointNormals->GetNumberOfComponents() < 3 || pointNormals->GetNumberOfTuples() != nPts)
    {
      vtkErrorMacro("Point normal array must have 3 components and one tuple per point.");
      return 0;
    }
  }
  else
  {
    pointNormals = normed->GetPointData()->GetNormals();
  }

  if (!pointNormals || pointNormals->GetNumberOfTuples() != nPts)
  {
    vtkErrorMacro("Could not obtain per-point normals.");
    return 0;
  }

  output->DeepCopy(input);

  const vtkIdType nCells = input->GetNumberOfCells();
  vtkNew<vtkDoubleArray> thickness;
  thickness->SetName(this->ThicknessArrayName ? this->ThicknessArrayName : "SHYX_MinOppositeFaceThickness");
  thickness->SetNumberOfComponents(1);
  thickness->SetNumberOfTuples(nPts);
  thickness->FillComponent(0, this->InvalidThicknessValue);

  const double dotMax = this->OppositeNormalDotMax;
  const double invalidV = this->InvalidThicknessValue;

  // vtkPolyData::GetCell is not thread-safe; precompute 2D cell point ids once, then parallelize using
  // vtkPoints::GetPoint (read-only) plus vtkTriangle / per-thread vtkPolygon.
  constexpr int kMaxPolyVerts = 256;
  std::vector<std::vector<vtkIdType>> cellPointIds(static_cast<size_t>(nCells));
  vtkNew<vtkGenericCell> gcell;
  vtkIdType skippedLargeCells = 0;
  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    input->GetCell(cid, gcell);
    if (gcell->GetCellDimension() != 2)
    {
      continue;
    }
    const int nv = gcell->GetNumberOfPoints();
    if (nv < 1 || nv > kMaxPolyVerts)
    {
      if (nv > kMaxPolyVerts)
      {
        ++skippedLargeCells;
      }
      continue;
    }
    std::vector<vtkIdType>& ids = cellPointIds[static_cast<size_t>(cid)];
    ids.resize(static_cast<size_t>(nv));
    for (int i = 0; i < nv; ++i)
    {
      ids[static_cast<size_t>(i)] = gcell->GetPointId(i);
    }
  }
  if (skippedLargeCells > 0)
  {
    vtkWarningMacro(<< skippedLargeCells << " 2D cells skipped (>" << kMaxPolyVerts << " vertices).");
  }

  // vtkSMPThreadLocal<T> requires T to be copy-constructible on some backends (e.g. MSVC + STDThread);
  // vtkNew is non-copyable, so use vtkSmartPointer here.
  struct PolyScratch
  {
    vtkSmartPointer<vtkPoints> polyPts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkTriangle> triangle = vtkSmartPointer<vtkTriangle>::New();
    vtkSmartPointer<vtkPolygon> polygon = vtkSmartPointer<vtkPolygon>::New();
    std::vector<double> weights;
  };
  vtkSMPThreadLocal<PolyScratch> polyScratchTLS;

  vtkSMPTools::For(
    0,
    nPts,
    [&](vtkIdType begin, vtkIdType end)
    {
      double p[3], np[3];
      double a[3], b[3], c[3], closest[3];
      for (vtkIdType pid = begin; pid < end; ++pid)
      {
        inPts->GetPoint(pid, p);
        pointNormals->GetTuple(pid, np);
        UnitOrZ(np);

        double minDist2 = VTK_DOUBLE_MAX;

        for (vtkIdType cid = 0; cid < nCells; ++cid)
        {
          const std::vector<vtkIdType>& ids = cellPointIds[static_cast<size_t>(cid)];
          if (ids.empty())
          {
            continue;
          }

          double nc[3];
          cellNormals->GetTuple(cid, nc);
          UnitOrZ(nc);
          if (vtkMath::Dot(np, nc) > dotMax)
          {
            continue;
          }
          if (PointUsesCellIds(ids, pid))
          {
            continue;
          }

          const int nv = static_cast<int>(ids.size());
          if (nv < 3)
          {
            continue;
          }
          double dist2 = VTK_DOUBLE_MAX;
          PolyScratch& sc = polyScratchTLS.Local();

          if (nv == 3)
          {
            inPts->GetPoint(ids[0], a);
            inPts->GetPoint(ids[1], b);
            inPts->GetPoint(ids[2], c);
            PointToTriangleDist2(
              p, a, b, c, closest, dist2, sc.triangle.Get(), sc.polyPts.Get(), sc.weights);
          }
          else if (nv == 4)
          {
            double v0[3], v1[3], v2[3], v3[3];
            inPts->GetPoint(ids[0], v0);
            inPts->GetPoint(ids[1], v1);
            inPts->GetPoint(ids[2], v2);
            inPts->GetPoint(ids[3], v3);
            double d0, d1;
            PointToTriangleDist2(
              p, v0, v1, v2, closest, d0, sc.triangle.Get(), sc.polyPts.Get(), sc.weights);
            PointToTriangleDist2(
              p, v0, v2, v3, closest, d1, sc.triangle.Get(), sc.polyPts.Get(), sc.weights);
            dist2 = (std::min)(d0, d1);
          }
          else
          {
            vtkPoints* pts = sc.polyPts.Get();
            vtkPolygon* poly = sc.polygon.Get();
            pts->SetNumberOfPoints(nv);
            for (int i = 0; i < nv; ++i)
            {
              inPts->GetPoint(ids[static_cast<size_t>(i)], a);
              pts->SetPoint(i, a);
            }
            poly->Initialize(nv, pts);
            int subId = 0;
            double pcoords[3];
            if (static_cast<int>(sc.weights.size()) < nv)
            {
              sc.weights.resize(static_cast<size_t>(nv) * 2u);
            }
            poly->EvaluatePosition(p, closest, subId, pcoords, dist2, sc.weights.data());
          }

          if (vtkMath::IsFinite(dist2) && dist2 >= 0.0 && dist2 < minDist2)
          {
            minDist2 = dist2;
          }
        }

        if (minDist2 < VTK_DOUBLE_MAX && vtkMath::IsFinite(minDist2))
        {
          thickness->SetValue(pid, std::sqrt(minDist2));
        }
        else
        {
          thickness->SetValue(pid, invalidV);
        }
      }
    });

  vtkPointData* outPD = output->GetPointData();
  if (this->ThicknessArrayName && this->ThicknessArrayName[0] != '\0')
  {
    outPD->RemoveArray(this->ThicknessArrayName);
  }
  outPD->AddArray(thickness);

  if (this->EnableThicknessRepair)
  {
    const double thr = this->ThicknessThreshold;
    if (!(thr > 0.0) || !vtkMath::IsFinite(thr))
    {
      vtkWarningMacro("ThicknessRepair is on but ThicknessThreshold is not a positive finite value; skipping motion.");
    }
    else
    {
      vtkPoints* outPts = output->GetPoints();
      const double maxDisp = this->MaxDisplacement;
      vtkSMPTools::For(0, nPts, [&](vtkIdType b, vtkIdType e) {
        double rp[3], rnp[3];
        for (vtkIdType pid = b; pid < e; ++pid)
        {
          const double t = thickness->GetValue(pid);
          if (!vtkMath::IsFinite(t) || t < 0.0 || t >= thr)
          {
            continue;
          }

          double push = thr - t;
          if (maxDisp > 0.0 && vtkMath::IsFinite(maxDisp) && push > maxDisp)
          {
            push = maxDisp;
          }

          pointNormals->GetTuple(pid, rnp);
          UnitOrZ(rnp);

          outPts->GetPoint(pid, rp);
          outPts->SetPoint(pid, rp[0] + push * rnp[0], rp[1] + push * rnp[1], rp[2] + push * rnp[2]);
        }
      });
    }
  }

  return 1;
}

VTK_ABI_NAMESPACE_END
