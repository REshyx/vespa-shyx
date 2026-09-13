#include "vtkSHYXSurfaceThickness.h"

#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolygon.h>
#include <vtkSMPThreadLocal.h>
#include <vtkSMPThreadLocalObject.h>
#include <vtkSMPTools.h>
#include <vtkStaticCellLocator.h>
#include <vtkStaticPointLocator.h>

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXSurfaceThickness);

namespace
{
constexpr double kInwardCosMin = 0.2588; // cos(75 deg)
constexpr double kInvalid = -1.0;

double LongestAabbSide(vtkPolyData* mesh)
{
  double b[6];
  mesh->GetBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  return std::max(dx, std::max(dy, dz));
}

void AccumulateAreaNormal(vtkPoints* pts, vtkIdList* ids, double nAcc[3])
{
  const vtkIdType n = ids->GetNumberOfIds();
  if (n < 3)
  {
    return;
  }
  double origin[3];
  pts->GetPoint(ids->GetId(0), origin);
  for (vtkIdType i = 1; i + 1 < n; ++i)
  {
    double p1[3], p2[3], e1[3], e2[3], cr[3];
    pts->GetPoint(ids->GetId(i), p1);
    pts->GetPoint(ids->GetId(i + 1), p2);
    e1[0] = p1[0] - origin[0];
    e1[1] = p1[1] - origin[1];
    e1[2] = p1[2] - origin[2];
    e2[0] = p2[0] - origin[0];
    e2[1] = p2[1] - origin[1];
    e2[2] = p2[2] - origin[2];
    vtkMath::Cross(e1, e2, cr);
    nAcc[0] += cr[0];
    nAcc[1] += cr[1];
    nAcc[2] += cr[2];
  }
}

void ComputePointNormalsAndEdgeLength(vtkPolyData* mesh, std::vector<double>& normals,
  std::vector<double>& edgeLength)
{
  const vtkIdType nPts = mesh->GetNumberOfPoints();
  normals.assign(static_cast<size_t>(nPts) * 3, 0.0);
  std::vector<double> edgeSum(static_cast<size_t>(nPts), 0.0);
  std::vector<int> edgeCount(static_cast<size_t>(nPts), 0);

  vtkPoints* pts = mesh->GetPoints();
  vtkNew<vtkIdList> ids;
  const vtkIdType nCells = mesh->GetNumberOfCells();
  for (vtkIdType c = 0; c < nCells; ++c)
  {
    mesh->GetCellPoints(c, ids);
    const vtkIdType n = ids->GetNumberOfIds();
    if (n < 2)
    {
      continue;
    }
    if (n >= 3)
    {
      double acc[3] = { 0.0, 0.0, 0.0 };
      AccumulateAreaNormal(pts, ids, acc);
      for (vtkIdType i = 0; i < n; ++i)
      {
        const vtkIdType pid = ids->GetId(i);
        normals[static_cast<size_t>(pid) * 3 + 0] += acc[0];
        normals[static_cast<size_t>(pid) * 3 + 1] += acc[1];
        normals[static_cast<size_t>(pid) * 3 + 2] += acc[2];
      }
    }
    for (vtkIdType i = 0; i < n; ++i)
    {
      const vtkIdType a = ids->GetId(i);
      const vtkIdType b = ids->GetId((i + 1) % n);
      if (a == b)
      {
        continue;
      }
      double pa[3], pb[3];
      pts->GetPoint(a, pa);
      pts->GetPoint(b, pb);
      const double len = std::sqrt(vtkMath::Distance2BetweenPoints(pa, pb));
      if (len <= 0.0)
      {
        continue;
      }
      edgeSum[static_cast<size_t>(a)] += len;
      edgeSum[static_cast<size_t>(b)] += len;
      ++edgeCount[static_cast<size_t>(a)];
      ++edgeCount[static_cast<size_t>(b)];
    }
  }

  edgeLength.assign(static_cast<size_t>(nPts), 0.0);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    double* n = &normals[static_cast<size_t>(i) * 3];
    if (vtkMath::Normalize(n) == 0.0)
    {
      n[0] = 0.0;
      n[1] = 0.0;
      n[2] = 1.0;
    }
    const int cnt = edgeCount[static_cast<size_t>(i)];
    edgeLength[static_cast<size_t>(i)] = cnt > 0 ? (edgeSum[static_cast<size_t>(i)] / cnt) : 0.0;
  }
}

void InwardAt(const std::vector<double>& normals, vtkIdType i, bool flip, double inward[3])
{
  const double* n = &normals[static_cast<size_t>(i) * 3];
  const double s = flip ? 1.0 : -1.0;
  inward[0] = s * n[0];
  inward[1] = s * n[1];
  inward[2] = s * n[2];
}

void CollectGeodesicRing(vtkPolyData* mesh, vtkIdType seed, int rings, vtkIdList* cellScratch,
  vtkIdList* ptScratch, std::unordered_set<vtkIdType>& out)
{
  out.clear();
  std::queue<std::pair<vtkIdType, int>> q;
  q.emplace(seed, 0);
  out.insert(seed);
  while (!q.empty())
  {
    const vtkIdType cur = q.front().first;
    const int dist = q.front().second;
    q.pop();
    if (dist >= rings)
    {
      continue;
    }
    mesh->GetPointCells(cur, cellScratch);
    for (vtkIdType ci = 0; ci < cellScratch->GetNumberOfIds(); ++ci)
    {
      mesh->GetCellPoints(cellScratch->GetId(ci), ptScratch);
      for (vtkIdType pi = 0; pi < ptScratch->GetNumberOfIds(); ++pi)
      {
        const vtkIdType nb = ptScratch->GetId(pi);
        if (out.insert(nb).second)
        {
          q.emplace(nb, dist + 1);
        }
      }
    }
  }
}

bool CellContainsPoint(vtkPolyData* mesh, vtkIdType cellId, vtkIdType ptId, vtkIdList* ids)
{
  mesh->GetCellPoints(cellId, ids);
  for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
  {
    if (ids->GetId(i) == ptId)
    {
      return true;
    }
  }
  return false;
}

bool CellNormal(vtkGenericCell* cell, double n[3])
{
  vtkPoints* pts = cell->GetPoints();
  if (!pts || pts->GetNumberOfPoints() < 3)
  {
    return false;
  }
  vtkPolygon::ComputeNormal(pts, n);
  return vtkMath::Normalize(n) > 0.0;
}

bool FirstInwardHit(vtkPolyData* mesh, vtkStaticCellLocator* locator, vtkGenericCell* genCell,
  vtkIdList* along, vtkIdList* cellPts, vtkIdType queryPt, const double origin[3],
  const double end[3], const double queryInward[3], double tol, double offset,
  bool requireOpposite, bool flipNormals, std::vector<std::pair<double, vtkIdType>>& hits)
{
  along->Reset();
  locator->FindCellsAlongLine(origin, end, tol, along);
  hits.clear();
  const double lineLen = std::sqrt(vtkMath::Distance2BetweenPoints(origin, end));
  if (lineLen <= 0.0)
  {
    return false;
  }
  double x[3], pcoords[3];
  for (vtkIdType i = 0; i < along->GetNumberOfIds(); ++i)
  {
    const vtkIdType cid = along->GetId(i);
    if (CellContainsPoint(mesh, cid, queryPt, cellPts))
    {
      continue;
    }
    mesh->GetCell(cid, genCell);
    double t = 0.0;
    int subId = 0;
    if (genCell->IntersectWithLine(origin, end, tol, t, x, pcoords, subId) == 0)
    {
      continue;
    }
    if (t < 0.0 || t > 1.0)
    {
      continue;
    }
    if (requireOpposite)
    {
      double ncell[3];
      if (!CellNormal(genCell, ncell))
      {
        continue;
      }
      if (flipNormals)
      {
        ncell[0] = -ncell[0];
        ncell[1] = -ncell[1];
        ncell[2] = -ncell[2];
      }
      // Opposite walls of a closed tube: both outward normals point away from the
      // lumen, so n_query_outward · n_hit < 0. Outward is -inward.
      const double nQueryOut[3] = { -queryInward[0], -queryInward[1], -queryInward[2] };
      if (vtkMath::Dot(nQueryOut, ncell) >= 0.0)
      {
        continue;
      }
    }
    hits.emplace_back(offset + t * lineLen, cid);
  }
  if (hits.empty())
  {
    return false;
  }
  std::sort(hits.begin(), hits.end(),
    [](const std::pair<double, vtkIdType>& a, const std::pair<double, vtkIdType>& b)
    { return a.first < b.first; });
  return true;
}

double MedianDistance(std::vector<double>& vals)
{
  if (vals.empty())
  {
    return kInvalid;
  }
  std::sort(vals.begin(), vals.end());
  return vals[vals.size() / 2];
}
} // namespace

vtkSHYXSurfaceThickness::vtkSHYXSurfaceThickness()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXSurfaceThickness::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Method: " << this->Method << "\n";
  os << indent << "MaxDistance: " << this->MaxDistance << "\n";
  os << indent << "FlipNormals: " << (this->FlipNormals ? "On" : "Off") << "\n";
  os << indent << "GeodesicRings: " << this->GeodesicRings << "\n";
  os << indent << "InwardOnly: " << (this->InwardOnly ? "On" : "Off") << "\n";
  os << indent << "RayOffset: " << this->RayOffset << "\n";
  os << indent << "RequireOppositeNormal: " << (this->RequireOppositeNormal ? "On" : "Off") << "\n";
  os << indent << "NumberOfRays: " << this->NumberOfRays << "\n";
  os << indent << "ConeAngle: " << this->ConeAngle << "\n";
}

int vtkSHYXSurfaceThickness::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXSurfaceThickness::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input)
  {
    vtkErrorMacro(<< "No input.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts == 0)
  {
    vtkWarningMacro(<< "Input has no points.");
    output->ShallowCopy(input);
    return 1;
  }

  if (input->GetNumberOfPolys() + input->GetNumberOfStrips() == 0)
  {
    vtkErrorMacro(<< "Input has no polygons or strips; thickness needs a surface.");
    return 0;
  }

  output->CopyStructure(input);
  output->GetPointData()->PassData(input->GetPointData());
  output->GetCellData()->PassData(input->GetCellData());
  output->GetFieldData()->PassData(input->GetFieldData());
  output->BuildCells();
  output->BuildLinks();

  std::vector<double> normals;
  std::vector<double> edgeLength;
  ComputePointNormalsAndEdgeLength(output, normals, edgeLength);

  const double aabb = LongestAabbSide(output);
  double maxDist = this->MaxDistance;
  if (!(maxDist > 0.0) || !vtkMath::IsFinite(maxDist))
  {
    maxDist = 0.05 * aabb;
  }
  if (!(maxDist > 0.0) || !vtkMath::IsFinite(maxDist))
  {
    vtkErrorMacro(<< "MaxDistance resolved to a non-positive value.");
    return 0;
  }

  const double coincident = std::max(1e-15, 1e-12 * std::max(aabb, 1.0));
  const double lineTol = std::max(1e-12, 1e-8 * std::max(aabb, 1.0));

  vtkNew<vtkDoubleArray> thickness;
  thickness->SetName("Thickness");
  thickness->SetNumberOfComponents(1);
  thickness->SetNumberOfTuples(nPts);

  vtkNew<vtkDoubleArray> ratio;
  ratio->SetName("ThicknessOverEdgeLength");
  ratio->SetNumberOfComponents(1);
  ratio->SetNumberOfTuples(nPts);

  vtkNew<vtkIntArray> valid;
  valid->SetName("ThicknessValid");
  valid->SetNumberOfComponents(1);
  valid->SetNumberOfTuples(nPts);

  vtkNew<vtkDoubleArray> localH;
  localH->SetName("LocalEdgeLength");
  localH->SetNumberOfComponents(1);
  localH->SetNumberOfTuples(nPts);
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    localH->SetValue(i, edgeLength[static_cast<size_t>(i)]);
  }

  const int method = this->Method;
  const bool flip = this->FlipNormals;
  const int rings = this->GeodesicRings;
  const bool inwardOnly = this->InwardOnly;
  const bool requireOpp = this->RequireOppositeNormal;
  const int nRays = (method == SHAPE_DIAMETER) ? this->NumberOfRays : 1;
  const double coneRad = this->ConeAngle * vtkMath::Pi() / 180.0;
  const double rayOffsetParam = this->RayOffset;

  if (method == SELF_PROXIMITY)
  {
    vtkNew<vtkStaticPointLocator> locator;
    locator->SetDataSet(output);
    locator->BuildLocator();

    vtkSMPThreadLocalObject<vtkIdList> neighTLS;
    vtkSMPThreadLocalObject<vtkIdList> cellTLS;
    vtkSMPThreadLocalObject<vtkIdList> ptTLS;
    vtkSMPThreadLocal<std::unordered_set<vtkIdType>> ringTLS;

    vtkPoints* pts = output->GetPoints();
    vtkSMPTools::For(0, nPts,
      [&](vtkIdType begin, vtkIdType end)
      {
        vtkIdList* neigh = neighTLS.Local();
        vtkIdList* cellScratch = cellTLS.Local();
        vtkIdList* ptScratch = ptTLS.Local();
        std::unordered_set<vtkIdType>& ring = ringTLS.Local();
        double p[3], q[3], inward[3];
        for (vtkIdType i = begin; i < end; ++i)
        {
          pts->GetPoint(i, p);
          InwardAt(normals, i, flip, inward);
          CollectGeodesicRing(output, i, rings, cellScratch, ptScratch, ring);
          neigh->Reset();
          locator->FindPointsWithinRadius(maxDist, p, neigh);
          double best = maxDist + 1.0;
          bool found = false;
          for (vtkIdType k = 0; k < neigh->GetNumberOfIds(); ++k)
          {
            const vtkIdType j = neigh->GetId(k);
            if (j == i || ring.find(j) != ring.end())
            {
              continue;
            }
            pts->GetPoint(j, q);
            const double euc2 = vtkMath::Distance2BetweenPoints(p, q);
            const double euc = std::sqrt(euc2);
            if (euc > maxDist)
            {
              continue;
            }
            if (euc <= coincident)
            {
              best = 0.0;
              found = true;
              continue;
            }
            if (inwardOnly)
            {
              const double toward = (q[0] - p[0]) * inward[0] + (q[1] - p[1]) * inward[1] +
                (q[2] - p[2]) * inward[2];
              if (toward < kInwardCosMin * euc)
              {
                continue;
              }
            }
            if (!found || euc < best)
            {
              best = euc;
              found = true;
            }
          }
          if (found)
          {
            thickness->SetValue(i, best);
            valid->SetValue(i, 1);
            const double h = edgeLength[static_cast<size_t>(i)];
            ratio->SetValue(i, h > 0.0 ? (best / h) : kInvalid);
          }
          else
          {
            thickness->SetValue(i, kInvalid);
            valid->SetValue(i, 0);
            ratio->SetValue(i, kInvalid);
          }
        }
      });
  }
  else
  {
    vtkNew<vtkStaticCellLocator> locator;
    locator->SetDataSet(output);
    locator->BuildLocator();

    vtkSMPThreadLocalObject<vtkGenericCell> genTLS;
    vtkSMPThreadLocalObject<vtkIdList> alongTLS;
    vtkSMPThreadLocalObject<vtkIdList> cellPtsTLS;
    vtkSMPThreadLocal<std::vector<std::pair<double, vtkIdType>>> hitTLS;
    vtkSMPThreadLocal<std::vector<double>> distTLS;

    vtkPoints* pts = output->GetPoints();
    vtkSMPTools::For(0, nPts,
      [&](vtkIdType begin, vtkIdType end)
      {
        vtkGenericCell* genCell = genTLS.Local();
        vtkIdList* along = alongTLS.Local();
        vtkIdList* cellPts = cellPtsTLS.Local();
        auto& hits = hitTLS.Local();
        auto& dists = distTLS.Local();
        double p[3], inward[3], u[3], v[3], origin[3], target[3];
        for (vtkIdType i = begin; i < end; ++i)
        {
          pts->GetPoint(i, p);
          InwardAt(normals, i, flip, inward);
          const double h = edgeLength[static_cast<size_t>(i)];
          double offset = rayOffsetParam;
          if (!(offset > 0.0) || !vtkMath::IsFinite(offset))
          {
            offset = 0.25 * h;
          }
          if (!(offset > 0.0) || !vtkMath::IsFinite(offset))
          {
            offset = 1e-6 * std::max(aabb, 1.0);
          }
          vtkMath::Perpendiculars(inward, u, v, 0.0);
          dists.clear();
          const int rays = std::max(1, nRays);
          for (int r = 0; r < rays; ++r)
          {
            double dir[3] = { inward[0], inward[1], inward[2] };
            if (r > 0 && coneRad > 0.0 && rays > 1)
            {
              const double phi = 2.0 * vtkMath::Pi() * static_cast<double>(r - 1) /
                static_cast<double>(rays - 1);
              const double ca = std::cos(coneRad);
              const double sa = std::sin(coneRad);
              const double cp = std::cos(phi);
              const double sp = std::sin(phi);
              dir[0] = ca * inward[0] + sa * (cp * u[0] + sp * v[0]);
              dir[1] = ca * inward[1] + sa * (cp * u[1] + sp * v[1]);
              dir[2] = ca * inward[2] + sa * (cp * u[2] + sp * v[2]);
              vtkMath::Normalize(dir);
            }
            origin[0] = p[0] + offset * dir[0];
            origin[1] = p[1] + offset * dir[1];
            origin[2] = p[2] + offset * dir[2];
            target[0] = origin[0] + maxDist * dir[0];
            target[1] = origin[1] + maxDist * dir[1];
            target[2] = origin[2] + maxDist * dir[2];
            if (FirstInwardHit(output, locator, genCell, along, cellPts, i, origin, target, inward,
                  lineTol, offset, requireOpp, flip, hits))
            {
              dists.push_back(hits.front().first);
            }
          }
          const double t = MedianDistance(dists);
          if (t >= 0.0)
          {
            thickness->SetValue(i, t);
            valid->SetValue(i, 1);
            ratio->SetValue(i, h > 0.0 ? (t / h) : kInvalid);
          }
          else
          {
            thickness->SetValue(i, kInvalid);
            valid->SetValue(i, 0);
            ratio->SetValue(i, kInvalid);
          }
        }
      });
  }

  vtkPointData* pd = output->GetPointData();
  pd->RemoveArray("Thickness");
  pd->RemoveArray("ThicknessOverEdgeLength");
  pd->RemoveArray("ThicknessValid");
  pd->RemoveArray("LocalEdgeLength");
  pd->AddArray(thickness);
  pd->AddArray(ratio);
  pd->AddArray(valid);
  pd->AddArray(localH);
  pd->SetActiveScalars("Thickness");

  vtkNew<vtkDoubleArray> usedDist;
  usedDist->SetName("SHYXSurfaceThicknessSearchDistance");
  usedDist->SetNumberOfTuples(1);
  usedDist->SetValue(0, maxDist);
  output->GetFieldData()->RemoveArray("SHYXSurfaceThicknessSearchDistance");
  output->GetFieldData()->AddArray(usedDist);

  return 1;
}

VTK_ABI_NAMESPACE_END
