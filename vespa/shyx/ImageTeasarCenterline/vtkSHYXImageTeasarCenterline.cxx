#include "vtkSHYXImageTeasarCenterline.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSetAttributes.h"
#include "vtkFloatArray.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXImageTeasarCenterline);

namespace
{

constexpr float kNoSite = 1.0e20f;

struct Crop
{
  int x0 = 0;
  int y0 = 0;
  int z0 = 0;
  int sx = 0;
  int sy = 0;
  int sz = 0;

  int Count() const { return this->sx * this->sy * this->sz; }

  int Index(int x, int y, int z) const { return x + this->sx * (y + this->sy * z); }

  void Coord(int id, int& x, int& y, int& z) const
  {
    x = id % this->sx;
    const int yz = id / this->sx;
    y = yz % this->sy;
    z = yz / this->sy;
  }
};

struct Settings
{
  double scale = 1.5;
  double constRadius = 0.0;
  double pdrfScale = 100000.0;
  int pdrfExponent = 4;
  int maxPaths = 0;
  bool fixBranching = true;
  bool fixBorders = true;
};

struct Sink
{
  vtkImageData* image = nullptr;
  const int* extent = nullptr;
  vtkPoints* points = nullptr;
  vtkCellArray* lines = nullptr;
  vtkFloatArray* radius = nullptr;
  vtkIntArray* pointObject = nullptr;
  vtkIntArray* pointComponent = nullptr;
  vtkIntArray* pathId = nullptr;
  vtkIntArray* cellObject = nullptr;
  vtkIntArray* cellComponent = nullptr;
  int nextPath = 0;
  std::unordered_map<int, vtkIdType> pointOf;
};

void ImageIJK(vtkIdType id, const int dims[3], int& i, int& j, int& k)
{
  i = static_cast<int>(id % dims[0]);
  const vtkIdType t = id / dims[0];
  j = static_cast<int>(t % dims[1]);
  k = static_cast<int>(t / dims[1]);
}

double ShellThickness(const double spacing[3])
{
  return 1.01 * std::max(spacing[0], std::max(spacing[1], spacing[2]));
}

void Dt1d(const float* f, int n, double spacing, float* dest)
{
  if (n <= 0)
  {
    return;
  }
  if (n == 1 || !(spacing > 0.0))
  {
    dest[0] = f[0];
    return;
  }

  std::vector<int> v(static_cast<size_t>(n));
  std::vector<double> z(static_cast<size_t>(n) + 1);
  const double sp2 = spacing * spacing;
  const double inf = std::numeric_limits<double>::infinity();
  int k = 0;
  v[0] = 0;
  z[0] = -inf;
  z[1] = inf;

  auto intersect = [&](int q, int vk) {
    const double fq = static_cast<double>(f[q]) + sp2 * static_cast<double>(q) * q;
    const double fv = static_cast<double>(f[vk]) + sp2 * static_cast<double>(vk) * vk;
    return (fq - fv) / (2.0 * sp2 * static_cast<double>(q - vk));
  };

  for (int q = 1; q < n; ++q)
  {
    int vk = v[static_cast<size_t>(k)];
    double s = intersect(q, vk);
    while (k > 0 && s <= z[static_cast<size_t>(k)])
    {
      --k;
      vk = v[static_cast<size_t>(k)];
      s = intersect(q, vk);
    }
    ++k;
    v[static_cast<size_t>(k)] = q;
    z[static_cast<size_t>(k)] = s;
    z[static_cast<size_t>(k) + 1] = inf;
  }

  k = 0;
  for (int q = 0; q < n; ++q)
  {
    while (z[static_cast<size_t>(k) + 1] < static_cast<double>(q))
    {
      ++k;
    }
    const int vk = v[static_cast<size_t>(k)];
    const double dq = static_cast<double>(q - vk);
    dest[q] = static_cast<float>(sp2 * dq * dq + static_cast<double>(f[vk]));
  }
}

void EuclideanDistance(const Crop& crop, const std::vector<std::uint8_t>& inside,
  const double spacing[3], std::vector<float>& edt)
{
  const int n = crop.Count();
  edt.assign(static_cast<size_t>(n), 0.f);
  for (int i = 0; i < n; ++i)
  {
    edt[static_cast<size_t>(i)] = inside[static_cast<size_t>(i)] ? kNoSite : 0.f;
  }

  std::vector<float> line;
  std::vector<float> transformed;
  for (int z = 0; z < crop.sz; ++z)
  {
    for (int y = 0; y < crop.sy; ++y)
    {
      line.resize(static_cast<size_t>(crop.sx));
      for (int x = 0; x < crop.sx; ++x)
      {
        line[static_cast<size_t>(x)] = edt[static_cast<size_t>(crop.Index(x, y, z))];
      }
      transformed.resize(line.size());
      Dt1d(line.data(), crop.sx, spacing[0], transformed.data());
      for (int x = 0; x < crop.sx; ++x)
      {
        edt[static_cast<size_t>(crop.Index(x, y, z))] = transformed[static_cast<size_t>(x)];
      }
    }
  }
  for (int z = 0; z < crop.sz; ++z)
  {
    for (int x = 0; x < crop.sx; ++x)
    {
      line.resize(static_cast<size_t>(crop.sy));
      for (int y = 0; y < crop.sy; ++y)
      {
        line[static_cast<size_t>(y)] = edt[static_cast<size_t>(crop.Index(x, y, z))];
      }
      transformed.resize(line.size());
      Dt1d(line.data(), crop.sy, spacing[1], transformed.data());
      for (int y = 0; y < crop.sy; ++y)
      {
        edt[static_cast<size_t>(crop.Index(x, y, z))] = transformed[static_cast<size_t>(y)];
      }
    }
  }
  for (int y = 0; y < crop.sy; ++y)
  {
    for (int x = 0; x < crop.sx; ++x)
    {
      line.resize(static_cast<size_t>(crop.sz));
      for (int z = 0; z < crop.sz; ++z)
      {
        line[static_cast<size_t>(z)] = edt[static_cast<size_t>(crop.Index(x, y, z))];
      }
      transformed.resize(line.size());
      Dt1d(line.data(), crop.sz, spacing[2], transformed.data());
      for (int z = 0; z < crop.sz; ++z)
      {
        edt[static_cast<size_t>(crop.Index(x, y, z))] = transformed[static_cast<size_t>(z)];
      }
    }
  }

  for (int i = 0; i < n; ++i)
  {
    float& value = edt[static_cast<size_t>(i)];
    if (!inside[static_cast<size_t>(i)] || !(value < 1.0e19f) || value < 0.f)
    {
      value = 0.f;
    }
    else
    {
      value = std::sqrt(value);
    }
  }
}

bool Dijkstra(vtkAlgorithm* self, const Crop& crop, const std::vector<std::uint8_t>& inside,
  const double spacing[3], int root, const float* penalty, std::vector<double>& dist,
  std::vector<int>& parent)
{
  const int n = crop.Count();
  dist.assign(static_cast<size_t>(n), std::numeric_limits<double>::infinity());
  parent.assign(static_cast<size_t>(n), -1);
  if (root < 0 || root >= n || !inside[static_cast<size_t>(root)])
  {
    return true;
  }

  using Item = std::pair<double, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
  dist[static_cast<size_t>(root)] = 0.0;
  heap.emplace(0.0, root);
  int pops = 0;
  while (!heap.empty())
  {
    const double du = heap.top().first;
    const int u = heap.top().second;
    heap.pop();
    if (du > dist[static_cast<size_t>(u)])
    {
      continue;
    }
    if ((++pops & 16383) == 0 && self->CheckAbort())
    {
      return false;
    }
    int x = 0;
    int y = 0;
    int z = 0;
    crop.Coord(u, x, y, z);
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dy = -1; dy <= 1; ++dy)
      {
        for (int dx = -1; dx <= 1; ++dx)
        {
          if (dx == 0 && dy == 0 && dz == 0)
          {
            continue;
          }
          const int xx = x + dx;
          const int yy = y + dy;
          const int zz = z + dz;
          if (xx < 0 || yy < 0 || zz < 0 || xx >= crop.sx || yy >= crop.sy || zz >= crop.sz)
          {
            continue;
          }
          const int v = crop.Index(xx, yy, zz);
          if (!inside[static_cast<size_t>(v)])
          {
            continue;
          }
          const double step = std::sqrt((dx * spacing[0]) * (dx * spacing[0]) +
            (dy * spacing[1]) * (dy * spacing[1]) + (dz * spacing[2]) * (dz * spacing[2]));
          const double weight =
            penalty ? static_cast<double>(penalty[static_cast<size_t>(v)]) * step : step;
          const double nd = du + weight;
          if (nd < dist[static_cast<size_t>(v)])
          {
            dist[static_cast<size_t>(v)] = nd;
            parent[static_cast<size_t>(v)] = u;
            heap.emplace(nd, v);
          }
        }
      }
    }
  }
  return !self->CheckAbort();
}

void CopyFinite(const std::vector<double>& dist, const std::vector<std::uint8_t>& inside,
  std::vector<float>& field)
{
  field.assign(dist.size(), 0.f);
  for (size_t i = 0; i < dist.size(); ++i)
  {
    if (inside[i] && std::isfinite(dist[i]))
    {
      field[i] = static_cast<float>(dist[i]);
    }
  }
}

int ArgMax(const std::vector<float>& field, const std::vector<std::uint8_t>& inside,
  const std::vector<std::uint8_t>& blocked)
{
  int best = -1;
  float bestValue = -std::numeric_limits<float>::infinity();
  for (int i = 0; i < static_cast<int>(field.size()); ++i)
  {
    if (!inside[static_cast<size_t>(i)] || blocked[static_cast<size_t>(i)])
    {
      continue;
    }
    if (field[static_cast<size_t>(i)] > bestValue)
    {
      bestValue = field[static_cast<size_t>(i)];
      best = i;
    }
  }
  return best;
}

int Recenter(const Crop& crop, const std::vector<float>& field, int far,
  const std::vector<std::uint8_t>& inside, const std::vector<std::uint8_t>& blocked,
  const double spacing[3])
{
  if (far < 0)
  {
    return far;
  }
  const float shell = static_cast<float>(ShellThickness(spacing));
  const float d0 = field[static_cast<size_t>(far)];
  double cx = 0.0;
  double cy = 0.0;
  double cz = 0.0;
  int count = 0;
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (!inside[static_cast<size_t>(i)] || blocked[static_cast<size_t>(i)])
    {
      continue;
    }
    if (field[static_cast<size_t>(i)] < d0 - shell)
    {
      continue;
    }
    int x = 0;
    int y = 0;
    int z = 0;
    crop.Coord(i, x, y, z);
    cx += x * spacing[0];
    cy += y * spacing[1];
    cz += z * spacing[2];
    ++count;
  }
  if (count <= 1)
  {
    return far;
  }
  cx /= count;
  cy /= count;
  cz /= count;

  int best = far;
  double bestDist = std::numeric_limits<double>::infinity();
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (!inside[static_cast<size_t>(i)] || blocked[static_cast<size_t>(i)])
    {
      continue;
    }
    int x = 0;
    int y = 0;
    int z = 0;
    crop.Coord(i, x, y, z);
    const double dx = x * spacing[0] - cx;
    const double dy = y * spacing[1] - cy;
    const double dz = z * spacing[2] - cz;
    const double dd = dx * dx + dy * dy + dz * dz;
    if (dd < bestDist)
    {
      bestDist = dd;
      best = i;
    }
  }
  return best;
}

void InvalidateShell(const Crop& crop, const std::vector<float>& field, int far,
  const std::vector<std::uint8_t>& inside, std::vector<std::uint8_t>& blocked,
  const double spacing[3])
{
  if (far < 0)
  {
    return;
  }
  const float shell = static_cast<float>(ShellThickness(spacing));
  const float d0 = field[static_cast<size_t>(far)];
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (inside[static_cast<size_t>(i)] && field[static_cast<size_t>(i)] >= d0 - shell)
    {
      blocked[static_cast<size_t>(i)] = 1;
    }
  }
}

void InvalidateCubes(const Crop& crop, const std::vector<int>& path, const std::vector<float>& edt,
  const std::vector<std::uint8_t>& inside, std::vector<std::uint8_t>& blocked,
  const double spacing[3], double scale, double constRadius)
{
  for (int id : path)
  {
    const double radius = std::max(0.0, scale * static_cast<double>(edt[static_cast<size_t>(id)]) +
        constRadius);
    const int rx = spacing[0] > 0.0 ? static_cast<int>(std::ceil(radius / spacing[0])) : 0;
    const int ry = spacing[1] > 0.0 ? static_cast<int>(std::ceil(radius / spacing[1])) : 0;
    const int rz = spacing[2] > 0.0 ? static_cast<int>(std::ceil(radius / spacing[2])) : 0;
    int x = 0;
    int y = 0;
    int z = 0;
    crop.Coord(id, x, y, z);
    const int xLo = std::max(0, x - rx);
    const int yLo = std::max(0, y - ry);
    const int zLo = std::max(0, z - rz);
    const int xHi = std::min(crop.sx - 1, x + rx);
    const int yHi = std::min(crop.sy - 1, y + ry);
    const int zHi = std::min(crop.sz - 1, z + rz);
    for (int zz = zLo; zz <= zHi; ++zz)
    {
      for (int yy = yLo; yy <= yHi; ++yy)
      {
        for (int xx = xLo; xx <= xHi; ++xx)
        {
          const int vid = crop.Index(xx, yy, zz);
          if (!inside[static_cast<size_t>(vid)])
          {
            continue;
          }
          const double dx = std::abs(xx - x) * spacing[0];
          const double dy = std::abs(yy - y) * spacing[1];
          const double dz = std::abs(zz - z) * spacing[2];
          if (std::max(dx, std::max(dy, dz)) <= radius + 1e-8)
          {
            blocked[static_cast<size_t>(vid)] = 1;
          }
        }
      }
    }
    blocked[static_cast<size_t>(id)] = 1;
  }
}

void FaceXYZ(int axis, int fixed, int u, int v, int& x, int& y, int& z)
{
  if (axis == 0)
  {
    x = fixed;
    y = u;
    z = v;
  }
  else if (axis == 1)
  {
    x = u;
    y = fixed;
    z = v;
  }
  else
  {
    x = u;
    y = v;
    z = fixed;
  }
}

std::vector<int> BorderTargets(const Crop& crop, const int dims[3],
  const std::vector<std::uint8_t>& inside, const std::vector<float>& edt, const double spacing[3])
{
  std::vector<std::uint8_t> seen(static_cast<size_t>(crop.Count()), 0);
  std::vector<int> targets;
  const int faceSize[3][2] = {
    { crop.sy, crop.sz },
    { crop.sx, crop.sz },
    { crop.sx, crop.sy },
  };

  for (int axis = 0; axis < 3; ++axis)
  {
    const int imageSize = dims[axis];
    const int origin = axis == 0 ? crop.x0 : (axis == 1 ? crop.y0 : crop.z0);
    const int cropSize = axis == 0 ? crop.sx : (axis == 1 ? crop.sy : crop.sz);
    for (int side = 0; side < 2; ++side)
    {
      const int imageFixed = side == 0 ? 0 : imageSize - 1;
      const int fixed = imageFixed - origin;
      if (fixed < 0 || fixed >= cropSize)
      {
        continue;
      }
      const int nu = faceSize[axis][0];
      const int nv = faceSize[axis][1];
      for (int v0 = 0; v0 < nv; ++v0)
      {
        for (int u0 = 0; u0 < nu; ++u0)
        {
          int x0 = 0;
          int y0 = 0;
          int z0 = 0;
          FaceXYZ(axis, fixed, u0, v0, x0, y0, z0);
          const int seed = crop.Index(x0, y0, z0);
          if (!inside[static_cast<size_t>(seed)] || seen[static_cast<size_t>(seed)])
          {
            continue;
          }
          std::vector<int> component;
          std::queue<std::pair<int, int>> queue;
          queue.emplace(u0, v0);
          seen[static_cast<size_t>(seed)] = 1;
          while (!queue.empty())
          {
            const int u = queue.front().first;
            const int v = queue.front().second;
            queue.pop();
            int x = 0;
            int y = 0;
            int z = 0;
            FaceXYZ(axis, fixed, u, v, x, y, z);
            component.push_back(crop.Index(x, y, z));
            for (int dv = -1; dv <= 1; ++dv)
            {
              for (int du = -1; du <= 1; ++du)
              {
                if (du == 0 && dv == 0)
                {
                  continue;
                }
                const int uu = u + du;
                const int vv = v + dv;
                if (uu < 0 || vv < 0 || uu >= nu || vv >= nv)
                {
                  continue;
                }
                int xx = 0;
                int yy = 0;
                int zz = 0;
                FaceXYZ(axis, fixed, uu, vv, xx, yy, zz);
                const int nid = crop.Index(xx, yy, zz);
                if (!inside[static_cast<size_t>(nid)] || seen[static_cast<size_t>(nid)])
                {
                  continue;
                }
                seen[static_cast<size_t>(nid)] = 1;
                queue.emplace(uu, vv);
              }
            }
          }

          double cx = 0.0;
          double cy = 0.0;
          double cz = 0.0;
          float maxEdt = -1.f;
          for (int id : component)
          {
            int x = 0;
            int y = 0;
            int z = 0;
            crop.Coord(id, x, y, z);
            cx += x * spacing[0];
            cy += y * spacing[1];
            cz += z * spacing[2];
            maxEdt = std::max(maxEdt, edt[static_cast<size_t>(id)]);
          }
          cx /= static_cast<double>(component.size());
          cy /= static_cast<double>(component.size());
          cz /= static_cast<double>(component.size());
          const float keep = maxEdt - 1e-4f * std::max(1.f, maxEdt);
          int best = component.front();
          double bestDist = std::numeric_limits<double>::infinity();
          for (int id : component)
          {
            if (edt[static_cast<size_t>(id)] < keep)
            {
              continue;
            }
            int x = 0;
            int y = 0;
            int z = 0;
            crop.Coord(id, x, y, z);
            const double dx = x * spacing[0] - cx;
            const double dy = y * spacing[1] - cy;
            const double dz = z * spacing[2] - cz;
            const double dd = dx * dx + dy * dy + dz * dz;
            if (dd < bestDist)
            {
              bestDist = dd;
              best = id;
            }
          }
          targets.push_back(best);
        }
      }
    }
  }

  std::sort(targets.begin(), targets.end());
  targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  return targets;
}

bool Trace(int target, int root, const std::vector<int>& parent, const std::vector<std::uint8_t>& skel,
  std::vector<int>& jointToLeaf)
{
  std::vector<int> chain;
  int cur = target;
  const int limit = static_cast<int>(parent.size()) + 2;
  for (int guard = 0; cur >= 0 && guard < limit; ++guard)
  {
    chain.push_back(cur);
    if (!skel.empty() && skel[static_cast<size_t>(cur)] && chain.size() > 1)
    {
      break;
    }
    if (cur == root)
    {
      break;
    }
    const int previous = parent[static_cast<size_t>(cur)];
    if (previous < 0)
    {
      return false;
    }
    cur = previous;
  }
  if (chain.size() < 2)
  {
    return false;
  }
  const bool closed = chain.back() == root || skel[static_cast<size_t>(chain.back())];
  if (!closed)
  {
    return false;
  }
  jointToLeaf.assign(chain.rbegin(), chain.rend());
  return true;
}

void AddPolyline(Sink& sink, const Crop& crop, const std::vector<int>& jointToLeaf,
  const std::vector<float>& edt, int objectId, int componentId)
{
  std::vector<vtkIdType> ids;
  ids.reserve(jointToLeaf.size());
  for (int id : jointToLeaf)
  {
    const auto found = sink.pointOf.find(id);
    if (found != sink.pointOf.end())
    {
      ids.push_back(found->second);
      continue;
    }
    int x = 0;
    int y = 0;
    int z = 0;
    crop.Coord(id, x, y, z);
    double point[3];
    sink.image->TransformIndexToPhysicalPoint(
      sink.extent[0] + crop.x0 + x, sink.extent[2] + crop.y0 + y, sink.extent[4] + crop.z0 + z, point);
    const vtkIdType pid = sink.points->InsertNextPoint(point);
    sink.radius->InsertNextValue(edt[static_cast<size_t>(id)]);
    sink.pointObject->InsertNextValue(objectId);
    sink.pointComponent->InsertNextValue(componentId);
    sink.pointOf.emplace(id, pid);
    ids.push_back(pid);
  }
  if (ids.size() < 2)
  {
    return;
  }
  sink.lines->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
  sink.pathId->InsertNextValue(sink.nextPath++);
  sink.cellObject->InsertNextValue(objectId);
  sink.cellComponent->InsertNextValue(componentId);
}

bool Skeletonize(vtkAlgorithm* self, Sink& sink, const int dims[3], const double spacing[3],
  const std::vector<vtkIdType>& voxels, int objectId, int componentId, const Settings& settings)
{
  if (voxels.size() < 2)
  {
    return true;
  }
  int bmin[3] = { dims[0], dims[1], dims[2] };
  int bmax[3] = { -1, -1, -1 };
  for (vtkIdType id : voxels)
  {
    int i = 0;
    int j = 0;
    int k = 0;
    ImageIJK(id, dims, i, j, k);
    bmin[0] = std::min(bmin[0], i);
    bmin[1] = std::min(bmin[1], j);
    bmin[2] = std::min(bmin[2], k);
    bmax[0] = std::max(bmax[0], i);
    bmax[1] = std::max(bmax[1], j);
    bmax[2] = std::max(bmax[2], k);
  }

  Crop crop;
  crop.x0 = std::max(0, bmin[0] - 1);
  crop.y0 = std::max(0, bmin[1] - 1);
  crop.z0 = std::max(0, bmin[2] - 1);
  const int x1 = std::min(dims[0] - 1, bmax[0] + 1);
  const int y1 = std::min(dims[1] - 1, bmax[1] + 1);
  const int z1 = std::min(dims[2] - 1, bmax[2] + 1);
  crop.sx = x1 - crop.x0 + 1;
  crop.sy = y1 - crop.y0 + 1;
  crop.sz = z1 - crop.z0 + 1;
  if (crop.sx <= 0 || crop.sy <= 0 || crop.sz <= 0)
  {
    return true;
  }
  const vtkIdType cropCount64 = static_cast<vtkIdType>(crop.sx) * crop.sy * crop.sz;
  if (cropCount64 > static_cast<vtkIdType>(std::numeric_limits<int>::max()))
  {
    vtkErrorWithObjectMacro(self, << "Component crop is too large to index.");
    return false;
  }

  std::vector<std::uint8_t> inside(static_cast<size_t>(crop.Count()), 0);
  for (vtkIdType id : voxels)
  {
    int i = 0;
    int j = 0;
    int k = 0;
    ImageIJK(id, dims, i, j, k);
    inside[static_cast<size_t>(crop.Index(i - crop.x0, j - crop.y0, k - crop.z0))] = 1;
  }

  std::vector<float> edt;
  EuclideanDistance(crop, inside, spacing, edt);
  if (self->CheckAbort())
  {
    return false;
  }

  int seed = -1;
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (inside[static_cast<size_t>(i)])
    {
      seed = i;
      break;
    }
  }
  if (seed < 0)
  {
    return true;
  }

  std::vector<double> dist;
  std::vector<int> parent;
  std::vector<float> geodesic;
  std::vector<std::uint8_t> blocked(static_cast<size_t>(crop.Count()), 0);
  if (!Dijkstra(self, crop, inside, spacing, seed, nullptr, dist, parent))
  {
    return false;
  }
  CopyFinite(dist, inside, geodesic);
  const int farRoot = ArgMax(geodesic, inside, blocked);
  const int root = Recenter(crop, geodesic, farRoot, inside, blocked, spacing);
  if (!Dijkstra(self, crop, inside, spacing, root, nullptr, dist, parent))
  {
    return false;
  }
  CopyFinite(dist, inside, geodesic);

  float emax = 0.f;
  float dmax = 0.f;
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (!inside[static_cast<size_t>(i)])
    {
      continue;
    }
    emax = std::max(emax, edt[static_cast<size_t>(i)]);
    dmax = std::max(dmax, geodesic[static_cast<size_t>(i)]);
  }
  std::vector<float> penalty(static_cast<size_t>(crop.Count()), 0.f);
  for (int i = 0; i < crop.Count(); ++i)
  {
    if (!inside[static_cast<size_t>(i)])
    {
      continue;
    }
    const float eTerm =
      emax > 0.f ? std::max(0.f, 1.f - edt[static_cast<size_t>(i)] / emax) : 0.f;
    const float dTerm = dmax > 0.f ? geodesic[static_cast<size_t>(i)] / dmax : 0.f;
    penalty[static_cast<size_t>(i)] = static_cast<float>(
      settings.pdrfScale * std::pow(static_cast<double>(eTerm), settings.pdrfExponent) +
      static_cast<double>(dTerm));
  }

  std::vector<int> borders;
  if (settings.fixBorders)
  {
    borders = BorderTargets(crop, dims, inside, edt, spacing);
  }

  std::vector<std::uint8_t> skel(static_cast<size_t>(crop.Count()), 0);
  sink.pointOf.clear();
  bool needDijkstra = true;
  int nPaths = 0;
  size_t borderIndex = 0;
  const int pathLimit = crop.Count() + static_cast<int>(borders.size()) + 2;
  int guard = 0;
  while (guard++ < pathLimit)
  {
    if (settings.maxPaths > 0 && nPaths >= settings.maxPaths)
    {
      break;
    }
    if (self->CheckAbort())
    {
      return false;
    }

    int far = -1;
    int target = -1;
    if (borderIndex < borders.size())
    {
      far = borders[borderIndex++];
      if (far < 0 || !inside[static_cast<size_t>(far)] || blocked[static_cast<size_t>(far)])
      {
        continue;
      }
      target = far;
    }
    else
    {
      far = ArgMax(geodesic, inside, blocked);
      if (far < 0 || geodesic[static_cast<size_t>(far)] <= 1e-6f)
      {
        break;
      }
      target = Recenter(crop, geodesic, far, inside, blocked, spacing);
      if (target < 0 || !inside[static_cast<size_t>(target)] || blocked[static_cast<size_t>(target)])
      {
        InvalidateShell(crop, geodesic, far, inside, blocked, spacing);
        continue;
      }
    }

    if (needDijkstra || settings.fixBranching)
    {
      if (!Dijkstra(self, crop, inside, spacing, root, penalty.data(), dist, parent))
      {
        return false;
      }
      needDijkstra = false;
    }

    std::vector<int> poly;
    if (Trace(target, root, parent, skel, poly))
    {
      AddPolyline(sink, crop, poly, edt, objectId, componentId);
      for (int id : poly)
      {
        skel[static_cast<size_t>(id)] = 1;
        blocked[static_cast<size_t>(id)] = 1;
        if (settings.fixBranching)
        {
          penalty[static_cast<size_t>(id)] = 0.f;
        }
      }
      InvalidateCubes(crop, poly, edt, inside, blocked, spacing, settings.scale, settings.constRadius);
      ++nPaths;
    }
    InvalidateShell(crop, geodesic, far, inside, blocked, spacing);
  }
  return !self->CheckAbort();
}

int Classify(double value, int mode, double threshold, int labelValue)
{
  if (!std::isfinite(value))
  {
    return 0;
  }
  if (mode == vtkSHYXImageTeasarCenterline::THRESHOLD)
  {
    return value >= threshold ? 1 : 0;
  }
  const double rounded = std::round(value);
  if (std::abs(value - rounded) > 1e-4 || rounded <= 0.0 ||
    rounded > static_cast<double>(std::numeric_limits<int>::max()))
  {
    return 0;
  }
  const int label = static_cast<int>(rounded);
  if (labelValue != 0 && label != labelValue)
  {
    return 0;
  }
  return label;
}

} // namespace

vtkSHYXImageTeasarCenterline::vtkSHYXImageTeasarCenterline()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, vtkDataSetAttributes::SCALARS);
}

void vtkSHYXImageTeasarCenterline::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "ForegroundMode: " << this->ForegroundMode << "\n";
  os << indent << "Threshold: " << this->Threshold << "\n";
  os << indent << "LabelValue: " << this->LabelValue << "\n";
  os << indent << "Scale: " << this->Scale << "\n";
  os << indent << "ConstRadius: " << this->ConstRadius << "\n";
  os << indent << "PdrfScale: " << this->PdrfScale << "\n";
  os << indent << "PdrfExponent: " << this->PdrfExponent << "\n";
  os << indent << "DustThreshold: " << this->DustThreshold << "\n";
  os << indent << "MaxPaths: " << this->MaxPaths << "\n";
  os << indent << "FixBranching: " << this->FixBranching << "\n";
  os << indent << "FixBorders: " << this->FixBorders << "\n";
}

int vtkSHYXImageTeasarCenterline::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkImageData");
    return 1;
  }
  return 0;
}

int vtkSHYXImageTeasarCenterline::RequestUpdateExtent(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  int ext[6];
  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), ext);
  inInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), ext, 6);
  return 1;
}

int vtkSHYXImageTeasarCenterline::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkImageData* input = vtkImageData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> lines;
  vtkNew<vtkFloatArray> radius;
  vtkNew<vtkIntArray> pointObject;
  vtkNew<vtkIntArray> pointComponent;
  vtkNew<vtkIntArray> pathId;
  vtkNew<vtkIntArray> cellObject;
  vtkNew<vtkIntArray> cellComponent;
  radius->SetName("MaximumInscribedSphereRadius");
  pointObject->SetName("ObjectId");
  pointComponent->SetName("ComponentId");
  pathId->SetName("PathId");
  cellObject->SetName("ObjectId");
  cellComponent->SetName("ComponentId");
  output->SetPoints(points);
  output->SetLines(lines);
  output->GetPointData()->AddArray(radius);
  output->GetPointData()->AddArray(pointObject);
  output->GetPointData()->AddArray(pointComponent);
  output->GetPointData()->SetActiveScalars("MaximumInscribedSphereRadius");
  output->GetCellData()->AddArray(pathId);
  output->GetCellData()->AddArray(cellObject);
  output->GetCellData()->AddArray(cellComponent);

  vtkDataArray* scalars = this->GetInputArrayToProcess(0, inputVector);
  if (!scalars)
  {
    scalars = input->GetPointData() ? input->GetPointData()->GetScalars() : nullptr;
  }
  if (!scalars)
  {
    vtkErrorMacro(<< "No point-centered scalar array to process.");
    return 0;
  }
  if (scalars->GetNumberOfComponents() != 1)
  {
    vtkErrorMacro(<< "SHYX Image TEASAR Centerlines requires a 1-component point scalar array.");
    return 0;
  }
  if (scalars->GetNumberOfTuples() != input->GetNumberOfPoints())
  {
    vtkErrorMacro(<< "Selected array is not a point-centered scalar on this vtkImageData.");
    return 0;
  }

  int extent[6];
  input->GetExtent(extent);
  const int dims[3] = {
    extent[1] - extent[0] + 1, extent[3] - extent[2] + 1, extent[5] - extent[4] + 1
  };
  if (dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0)
  {
    vtkErrorMacro(<< "Input extent is empty.");
    return 0;
  }
  const vtkIdType nPoints =
    static_cast<vtkIdType>(dims[0]) * dims[1] * dims[2];
  if (nPoints != input->GetNumberOfPoints())
  {
    vtkErrorMacro(<< "Extent and point count disagree.");
    return 0;
  }

  double spacing[3];
  input->GetSpacing(spacing);
  for (int axis = 0; axis < 3; ++axis)
  {
    if (!(spacing[axis] > 0.0))
    {
      vtkErrorMacro(<< "Spacing must be positive on every axis.");
      return 0;
    }
  }

  std::vector<int> labels;
  try
  {
    labels.assign(static_cast<size_t>(nPoints), 0);
  }
  catch (const std::bad_alloc&)
  {
    vtkErrorMacro(<< "Not enough memory to classify the volume.");
    return 0;
  }
  for (vtkIdType id = 0; id < nPoints; ++id)
  {
    labels[static_cast<size_t>(id)] =
      Classify(scalars->GetComponent(id, 0), this->ForegroundMode, this->Threshold, this->LabelValue);
  }

  Settings settings;
  settings.scale = this->Scale;
  settings.constRadius = this->ConstRadius;
  settings.pdrfScale = this->PdrfScale;
  settings.pdrfExponent = this->PdrfExponent;
  settings.maxPaths = this->MaxPaths;
  settings.fixBranching = this->FixBranching != 0;
  settings.fixBorders = this->FixBorders != 0;

  Sink sink;
  sink.image = input;
  sink.extent = extent;
  sink.points = points;
  sink.lines = lines;
  sink.radius = radius;
  sink.pointObject = pointObject;
  sink.pointComponent = pointComponent;
  sink.pathId = pathId;
  sink.cellObject = cellObject;
  sink.cellComponent = cellComponent;

  std::vector<std::uint8_t> visited(static_cast<size_t>(nPoints), 0);
  int componentId = 0;
  int skippedDust = 0;
  vtkIdType processed = 0;
  const int neighbor[26][3] = {
    { -1, -1, -1 }, { 0, -1, -1 }, { 1, -1, -1 }, { -1, 0, -1 }, { 0, 0, -1 }, { 1, 0, -1 },
    { -1, 1, -1 }, { 0, 1, -1 }, { 1, 1, -1 }, { -1, -1, 0 }, { 0, -1, 0 }, { 1, -1, 0 },
    { -1, 0, 0 }, { 1, 0, 0 }, { -1, 1, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { -1, -1, 1 },
    { 0, -1, 1 }, { 1, -1, 1 }, { -1, 0, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { -1, 1, 1 },
    { 0, 1, 1 }, { 1, 1, 1 },
  };

  for (vtkIdType seed = 0; seed < nPoints; ++seed)
  {
    if (visited[static_cast<size_t>(seed)] || labels[static_cast<size_t>(seed)] == 0)
    {
      continue;
    }
    if (this->CheckAbort())
    {
      return 0;
    }
    const int label = labels[static_cast<size_t>(seed)];
    std::vector<vtkIdType> voxels;
    std::queue<vtkIdType> queue;
    queue.push(seed);
    visited[static_cast<size_t>(seed)] = 1;
    while (!queue.empty())
    {
      const vtkIdType id = queue.front();
      queue.pop();
      voxels.push_back(id);
      int i = 0;
      int j = 0;
      int k = 0;
      ImageIJK(id, dims, i, j, k);
      for (const auto& delta : neighbor)
      {
        const int ii = i + delta[0];
        const int jj = j + delta[1];
        const int kk = k + delta[2];
        if (ii < 0 || jj < 0 || kk < 0 || ii >= dims[0] || jj >= dims[1] || kk >= dims[2])
        {
          continue;
        }
        const vtkIdType nid = static_cast<vtkIdType>(ii) +
          static_cast<vtkIdType>(dims[0]) * (static_cast<vtkIdType>(jj) +
            static_cast<vtkIdType>(dims[1]) * kk);
        if (visited[static_cast<size_t>(nid)] || labels[static_cast<size_t>(nid)] != label)
        {
          continue;
        }
        visited[static_cast<size_t>(nid)] = 1;
        queue.push(nid);
      }
    }

    processed += static_cast<vtkIdType>(voxels.size());
    this->UpdateProgress(std::min(0.99, static_cast<double>(processed) / static_cast<double>(nPoints)));
    if (this->DustThreshold > 0 && voxels.size() < static_cast<size_t>(this->DustThreshold))
    {
      ++skippedDust;
      continue;
    }
    ++componentId;
    if (!Skeletonize(this, sink, dims, spacing, voxels, label, componentId, settings))
    {
      return 0;
    }
  }

  if (skippedDust > 0)
  {
    vtkDebugMacro(<< "Skipped " << skippedDust << " components smaller than DustThreshold.");
  }
  return 1;
}

VTK_ABI_NAMESPACE_END
