#include "vtkSHYXImageMorphology.h"

#include "vtkAlgorithm.h"
#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSetAttributes.h"
#include "vtkFieldData.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSMPTools.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXImageMorphology);

namespace
{

int MakeOdd(int n)
{
  n = std::max(1, n);
  if ((n % 2) == 0)
  {
    ++n;
  }
  return n;
}

struct Off
{
  int dx;
  int dy;
  int dz;
};

bool InsideShape(int shape, int dx, int dy, int dz, int rx, int ry, int rz)
{
  switch (shape)
  {
    case vtkSHYXImageMorphology::BOX:
      return true;
    case vtkSHYXImageMorphology::CROSS:
      return (dy == 0 && dz == 0) || (dx == 0 && dz == 0) || (dx == 0 && dy == 0);
    case vtkSHYXImageMorphology::ELLIPSOID:
    default:
    {
      const double nx = rx > 0 ? static_cast<double>(dx) / static_cast<double>(rx)
                               : (dx == 0 ? 0.0 : 2.0);
      const double ny = ry > 0 ? static_cast<double>(dy) / static_cast<double>(ry)
                               : (dy == 0 ? 0.0 : 2.0);
      const double nz = rz > 0 ? static_cast<double>(dz) / static_cast<double>(rz)
                               : (dz == 0 ? 0.0 : 2.0);
      return (nx * nx + ny * ny + nz * nz) <= (1.0 + 1e-12);
    }
  }
}

void AppendSE(int shape, int nx, int ny, int nz, std::vector<Off>& out)
{
  nx = MakeOdd(nx);
  ny = MakeOdd(ny);
  nz = MakeOdd(nz);
  const int rx = nx / 2;
  const int ry = ny / 2;
  const int rz = nz / 2;
  for (int dz = -rz; dz <= rz; ++dz)
  {
    for (int dy = -ry; dy <= ry; ++dy)
    {
      for (int dx = -rx; dx <= rx; ++dx)
      {
        if (InsideShape(shape, dx, dy, dz, rx, ry, rz))
        {
          out.push_back(Off{ dx, dy, dz });
        }
      }
    }
  }
}

int PackOff(const Off& o)
{
  return ((o.dx + 512) << 20) | ((o.dy + 512) << 10) | (o.dz + 512);
}

void BuildForegroundSE(int shape, const int ksz[3], bool zIs2D, std::vector<Off>& fg)
{
  fg.clear();
  const int nz = zIs2D ? 1 : ksz[2];
  AppendSE(shape, ksz[0], ksz[1], nz, fg);
}

void BuildHitOrMissSEs(int shape, const int fgSz[3], const int bgSz[3], bool zIs2D,
  std::vector<Off>& fg, std::vector<Off>& bg)
{
  BuildForegroundSE(shape, fgSz, zIs2D, fg);
  int bgUse[3] = { std::max(MakeOdd(bgSz[0]), MakeOdd(fgSz[0]) + 2),
    std::max(MakeOdd(bgSz[1]), MakeOdd(fgSz[1]) + 2),
    std::max(MakeOdd(bgSz[2]), MakeOdd(fgSz[2]) + 2) };
  if (zIs2D)
  {
    bgUse[2] = 1;
  }
  std::vector<Off> large;
  AppendSE(shape, bgUse[0], bgUse[1], bgUse[2], large);

  std::unordered_set<int> fgKeys;
  fgKeys.reserve(fg.size() * 2);
  for (const Off& o : fg)
  {
    fgKeys.insert(PackOff(o));
  }
  bg.clear();
  bg.reserve(large.size());
  for (const Off& o : large)
  {
    if (fgKeys.find(PackOff(o)) == fgKeys.end())
    {
      bg.push_back(o);
    }
  }
}

vtkIdType VoxelIndex(int i, int j, int k, const int ext[6], vtkIdType dimX, vtkIdType dimXY)
{
  return static_cast<vtkIdType>(i - ext[0]) + static_cast<vtkIdType>(j - ext[2]) * dimX +
    static_cast<vtkIdType>(k - ext[4]) * dimXY;
}

template <typename T>
T SampleClamp(
  const T* in, int x, int y, int z, const int ext[6], vtkIdType dimX, vtkIdType dimXY)
{
  const int i = std::min(ext[1], std::max(ext[0], x));
  const int j = std::min(ext[3], std::max(ext[2], y));
  const int k = std::min(ext[5], std::max(ext[4], z));
  return in[VoxelIndex(i, j, k, ext, dimX, dimXY)];
}

template <typename T>
T SaturatingSub(T a, T b)
{
  if (std::is_floating_point<T>::value)
  {
    return static_cast<T>(static_cast<double>(a) - static_cast<double>(b));
  }
  const double d = static_cast<double>(a) - static_cast<double>(b);
  const double lo = static_cast<double>(std::numeric_limits<T>::lowest());
  const double hi = static_cast<double>(std::numeric_limits<T>::max());
  return static_cast<T>(std::min(hi, std::max(lo, d)));
}

struct Grid
{
  const int* Ext;
  vtkIdType DimX;
  vtkIdType DimXY;
  const std::vector<Off>* SE;
};

template <typename T>
void DilateGray(const T* in, T* out, const Grid& g, vtkSHYXImageMorphology* self)
{
  const int* ext = g.Ext;
  vtkSMPTools::For(ext[4], ext[5] + 1, [&](vtkIdType z0, vtkIdType z1) {
    for (int z = static_cast<int>(z0); z < static_cast<int>(z1); ++z)
    {
      if (self->GetAbortExecute())
      {
        return;
      }
      for (int y = ext[2]; y <= ext[3]; ++y)
      {
        for (int x = ext[0]; x <= ext[1]; ++x)
        {
          T m = std::numeric_limits<T>::lowest();
          for (const Off& o : *g.SE)
          {
            const T v = SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, g.DimX, g.DimXY);
            if (v > m)
            {
              m = v;
            }
          }
          out[VoxelIndex(x, y, z, ext, g.DimX, g.DimXY)] = m;
        }
      }
    }
  });
}

template <typename T>
void ErodeGray(const T* in, T* out, const Grid& g, vtkSHYXImageMorphology* self)
{
  const int* ext = g.Ext;
  vtkSMPTools::For(ext[4], ext[5] + 1, [&](vtkIdType z0, vtkIdType z1) {
    for (int z = static_cast<int>(z0); z < static_cast<int>(z1); ++z)
    {
      if (self->GetAbortExecute())
      {
        return;
      }
      for (int y = ext[2]; y <= ext[3]; ++y)
      {
        for (int x = ext[0]; x <= ext[1]; ++x)
        {
          T m = std::numeric_limits<T>::max();
          for (const Off& o : *g.SE)
          {
            const T v = SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, g.DimX, g.DimXY);
            if (v < m)
            {
              m = v;
            }
          }
          out[VoxelIndex(x, y, z, ext, g.DimX, g.DimXY)] = m;
        }
      }
    }
  });
}

template <typename T>
void DilateBin(const T* in, T* out, const Grid& g, T fg, T bg, vtkSHYXImageMorphology* self)
{
  const int* ext = g.Ext;
  vtkSMPTools::For(ext[4], ext[5] + 1, [&](vtkIdType z0, vtkIdType z1) {
    for (int z = static_cast<int>(z0); z < static_cast<int>(z1); ++z)
    {
      if (self->GetAbortExecute())
      {
        return;
      }
      for (int y = ext[2]; y <= ext[3]; ++y)
      {
        for (int x = ext[0]; x <= ext[1]; ++x)
        {
          const vtkIdType i = VoxelIndex(x, y, z, ext, g.DimX, g.DimXY);
          const T cur = in[i];
          if (cur != fg && cur != bg)
          {
            out[i] = cur;
            continue;
          }
          bool foundFg = false;
          for (const Off& o : *g.SE)
          {
            if (SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, g.DimX, g.DimXY) == fg)
            {
              foundFg = true;
              break;
            }
          }
          out[i] = foundFg ? fg : cur;
        }
      }
    }
  });
}

template <typename T>
void ErodeBin(const T* in, T* out, const Grid& g, T fg, T bg, vtkSHYXImageMorphology* self)
{
  const int* ext = g.Ext;
  vtkSMPTools::For(ext[4], ext[5] + 1, [&](vtkIdType z0, vtkIdType z1) {
    for (int z = static_cast<int>(z0); z < static_cast<int>(z1); ++z)
    {
      if (self->GetAbortExecute())
      {
        return;
      }
      for (int y = ext[2]; y <= ext[3]; ++y)
      {
        for (int x = ext[0]; x <= ext[1]; ++x)
        {
          const vtkIdType i = VoxelIndex(x, y, z, ext, g.DimX, g.DimXY);
          const T cur = in[i];
          if (cur != fg && cur != bg)
          {
            out[i] = cur;
            continue;
          }
          bool foundBg = false;
          for (const Off& o : *g.SE)
          {
            if (SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, g.DimX, g.DimXY) == bg)
            {
              foundBg = true;
              break;
            }
          }
          out[i] = foundBg ? bg : cur;
        }
      }
    }
  });
}

template <typename T>
void HitOrMiss(const T* in, T* out, const Grid& gFg, const std::vector<Off>& bgSE, T fg, T bg,
  vtkSHYXImageMorphology* self)
{
  const int* ext = gFg.Ext;
  vtkSMPTools::For(ext[4], ext[5] + 1, [&](vtkIdType z0, vtkIdType z1) {
    for (int z = static_cast<int>(z0); z < static_cast<int>(z1); ++z)
    {
      if (self->GetAbortExecute())
      {
        return;
      }
      for (int y = ext[2]; y <= ext[3]; ++y)
      {
        for (int x = ext[0]; x <= ext[1]; ++x)
        {
          bool hitFg = true;
          for (const Off& o : *gFg.SE)
          {
            if (SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, gFg.DimX, gFg.DimXY) != fg)
            {
              hitFg = false;
              break;
            }
          }
          bool hitBg = true;
          if (hitFg)
          {
            for (const Off& o : bgSE)
            {
              if (SampleClamp(in, x + o.dx, y + o.dy, z + o.dz, ext, gFg.DimX, gFg.DimXY) != bg)
              {
                hitBg = false;
                break;
              }
            }
          }
          out[VoxelIndex(x, y, z, ext, gFg.DimX, gFg.DimXY)] = (hitFg && hitBg) ? fg : bg;
        }
      }
    }
  });
}

template <typename T>
void SubtractArrays(const T* a, const T* b, T* out, vtkIdType n)
{
  vtkSMPTools::For(0, n, [&](vtkIdType begin, vtkIdType end) {
    for (vtkIdType i = begin; i < end; ++i)
    {
      out[i] = SaturatingSub(a[i], b[i]);
    }
  });
}

template <typename T, typename Op>
void RepeatOp(Op op, const T* in, T* out, std::vector<T>& scratch, int nIters, vtkIdType n)
{
  nIters = std::max(1, nIters);
  if (nIters == 1)
  {
    op(in, out);
    return;
  }
  scratch.resize(static_cast<size_t>(n));
  op(in, scratch.data());
  T* ping = scratch.data();
  T* pong = out;
  for (int i = 1; i < nIters; ++i)
  {
    op(ping, pong);
    T* tmp = ping;
    ping = pong;
    pong = tmp;
  }
  if (ping != out)
  {
    std::copy(ping, ping + n, out);
  }
}

} // namespace

vtkSHYXImageMorphology::vtkSHYXImageMorphology()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, vtkDataSetAttributes::SCALARS);
}

void vtkSHYXImageMorphology::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Operation: " << this->Operation << "\n";
  os << indent << "ValueMode: " << this->ValueMode << "\n";
  os << indent << "KernelShape: " << this->KernelShape << "\n";
  os << indent << "KernelSize: (" << this->KernelSize[0] << ", " << this->KernelSize[1] << ", "
     << this->KernelSize[2] << ")\n";
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << "\n";
  os << indent << "ForegroundValue: " << this->ForegroundValue << "\n";
  os << indent << "BackgroundValue: " << this->BackgroundValue << "\n";
  os << indent << "BackgroundKernelSize: (" << this->BackgroundKernelSize[0] << ", "
     << this->BackgroundKernelSize[1] << ", " << this->BackgroundKernelSize[2] << ")\n";
}

void vtkSHYXImageMorphology::SetKernelSize(int sx, int sy, int sz)
{
  sx = MakeOdd(sx);
  sy = MakeOdd(sy);
  sz = MakeOdd(sz);
  if (this->KernelSize[0] == sx && this->KernelSize[1] == sy && this->KernelSize[2] == sz)
  {
    return;
  }
  this->KernelSize[0] = sx;
  this->KernelSize[1] = sy;
  this->KernelSize[2] = sz;
  this->Modified();
}

void vtkSHYXImageMorphology::SetBackgroundKernelSize(int sx, int sy, int sz)
{
  sx = MakeOdd(sx);
  sy = MakeOdd(sy);
  sz = MakeOdd(sz);
  if (this->BackgroundKernelSize[0] == sx && this->BackgroundKernelSize[1] == sy &&
    this->BackgroundKernelSize[2] == sz)
  {
    return;
  }
  this->BackgroundKernelSize[0] = sx;
  this->BackgroundKernelSize[1] = sy;
  this->BackgroundKernelSize[2] = sz;
  this->Modified();
}

int vtkSHYXImageMorphology::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkImageData");
    return 1;
  }
  return 0;
}

int vtkSHYXImageMorphology::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkImageData");
    return 1;
  }
  return 0;
}

int vtkSHYXImageMorphology::RequestUpdateExtent(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  int ext[6];
  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), ext);
  inInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), ext, 6);
  return 1;
}

int vtkSHYXImageMorphology::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkImageData* input = vtkImageData::GetData(inputVector[0], 0);
  vtkImageData* output = vtkImageData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  vtkDataArray* inArr = this->GetInputArrayToProcess(0, inputVector);
  if (!inArr)
  {
    inArr = input->GetPointData() ? input->GetPointData()->GetScalars() : nullptr;
  }
  if (!inArr)
  {
    vtkErrorMacro(<< "No point-centered scalar array to process.");
    return 0;
  }
  if (inArr->GetNumberOfComponents() != 1)
  {
    vtkErrorMacro(<< "SHYX Image Morphology requires a 1-component point scalar array.");
    return 0;
  }
  if (inArr->GetNumberOfTuples() != input->GetNumberOfPoints())
  {
    vtkErrorMacro(<< "Selected array is not a point-centered scalar on this vtkImageData.");
    return 0;
  }

  int ext[6];
  input->GetExtent(ext);
  const bool zIs2D = (ext[4] == ext[5]);
  const vtkIdType dimX = static_cast<vtkIdType>(ext[1] - ext[0] + 1);
  const vtkIdType dimXY = dimX * static_cast<vtkIdType>(ext[3] - ext[2] + 1);
  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts < 1)
  {
    output->CopyStructure(input);
    output->GetPointData()->PassData(input->GetPointData());
    output->GetCellData()->PassData(input->GetCellData());
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }
  if (!inArr->GetVoidPointer(0))
  {
    vtkErrorMacro(<< "Cannot access scalar pointer (unsupported array type).");
    return 0;
  }

  std::vector<Off> se;
  std::vector<Off> bgSe;
  if (this->Operation == HIT_OR_MISS)
  {
    BuildHitOrMissSEs(this->KernelShape, this->KernelSize, this->BackgroundKernelSize, zIs2D, se,
      bgSe);
    if (se.empty())
    {
      vtkErrorMacro(<< "Foreground structuring element is empty.");
      return 0;
    }
  }
  else
  {
    BuildForegroundSE(this->KernelShape, this->KernelSize, zIs2D, se);
    if (se.empty())
    {
      vtkErrorMacro(<< "Structuring element is empty.");
      return 0;
    }
  }

  output->CopyStructure(input);
  output->GetPointData()->PassData(input->GetPointData());
  output->GetCellData()->PassData(input->GetCellData());
  output->GetFieldData()->PassData(input->GetFieldData());

  vtkSmartPointer<vtkDataArray> outArr;
  outArr.TakeReference(inArr->NewInstance());
  outArr->SetName(inArr->GetName());
  outArr->SetNumberOfComponents(1);
  outArr->SetNumberOfTuples(nPts);
  if (!outArr->GetVoidPointer(0))
  {
    vtkErrorMacro(<< "Cannot allocate output scalars.");
    return 0;
  }

  Grid g;
  g.Ext = ext;
  g.DimX = dimX;
  g.DimXY = dimXY;
  g.SE = &se;

  const int op = this->Operation;
  const bool gray = (this->ValueMode == GRAYSCALE) && (op != HIT_OR_MISS);
  const int nIters = this->NumberOfIterations;

  int ok = 1;
  switch (inArr->GetDataType())
  {
    vtkTemplateMacro({
      using T = VTK_TT;
      const T* inPtr = static_cast<const T*>(inArr->GetVoidPointer(0));
      T* outPtr = static_cast<T*>(outArr->GetVoidPointer(0));
      const T fg = static_cast<T>(this->ForegroundValue);
      const T bg = static_cast<T>(this->BackgroundValue);

      auto dilate = [&](const T* s, T* d) {
        if (gray)
        {
          DilateGray(s, d, g, this);
        }
        else
        {
          DilateBin(s, d, g, fg, bg, this);
        }
      };
      auto erode = [&](const T* s, T* d) {
        if (gray)
        {
          ErodeGray(s, d, g, this);
        }
        else
        {
          ErodeBin(s, d, g, fg, bg, this);
        }
      };

      std::vector<T> scratch;
      std::vector<T> bufA;
      std::vector<T> bufB;

      switch (op)
      {
        case DILATE:
          RepeatOp(dilate, inPtr, outPtr, scratch, nIters, nPts);
          break;
        case ERODE:
          RepeatOp(erode, inPtr, outPtr, scratch, nIters, nPts);
          break;
        case OPEN:
          bufA.resize(static_cast<size_t>(nPts));
          bufB.resize(static_cast<size_t>(nPts));
          std::copy(inPtr, inPtr + nPts, bufA.begin());
          for (int i = 0; i < nIters && !this->AbortExecute; ++i)
          {
            erode(bufA.data(), bufB.data());
            dilate(bufB.data(), bufA.data());
            this->UpdateProgress((i + 1) / static_cast<double>(nIters));
          }
          std::copy(bufA.begin(), bufA.end(), outPtr);
          break;
        case CLOSE:
          bufA.resize(static_cast<size_t>(nPts));
          bufB.resize(static_cast<size_t>(nPts));
          std::copy(inPtr, inPtr + nPts, bufA.begin());
          for (int i = 0; i < nIters && !this->AbortExecute; ++i)
          {
            dilate(bufA.data(), bufB.data());
            erode(bufB.data(), bufA.data());
            this->UpdateProgress((i + 1) / static_cast<double>(nIters));
          }
          std::copy(bufA.begin(), bufA.end(), outPtr);
          break;
        case MORPH_GRADIENT:
          bufA.resize(static_cast<size_t>(nPts));
          bufB.resize(static_cast<size_t>(nPts));
          RepeatOp(dilate, inPtr, bufA.data(), scratch, nIters, nPts);
          RepeatOp(erode, inPtr, bufB.data(), scratch, nIters, nPts);
          SubtractArrays(bufA.data(), bufB.data(), outPtr, nPts);
          break;
        case INTERNAL_GRADIENT:
          bufA.resize(static_cast<size_t>(nPts));
          RepeatOp(erode, inPtr, bufA.data(), scratch, nIters, nPts);
          SubtractArrays(inPtr, bufA.data(), outPtr, nPts);
          break;
        case EXTERNAL_GRADIENT:
          bufA.resize(static_cast<size_t>(nPts));
          RepeatOp(dilate, inPtr, bufA.data(), scratch, nIters, nPts);
          SubtractArrays(bufA.data(), inPtr, outPtr, nPts);
          break;
        case WHITE_TOPHAT:
          bufA.resize(static_cast<size_t>(nPts));
          bufB.resize(static_cast<size_t>(nPts));
          std::copy(inPtr, inPtr + nPts, bufA.begin());
          for (int i = 0; i < nIters && !this->AbortExecute; ++i)
          {
            erode(bufA.data(), bufB.data());
            dilate(bufB.data(), bufA.data());
          }
          SubtractArrays(inPtr, bufA.data(), outPtr, nPts);
          break;
        case BLACK_TOPHAT:
          bufA.resize(static_cast<size_t>(nPts));
          bufB.resize(static_cast<size_t>(nPts));
          std::copy(inPtr, inPtr + nPts, bufA.begin());
          for (int i = 0; i < nIters && !this->AbortExecute; ++i)
          {
            dilate(bufA.data(), bufB.data());
            erode(bufB.data(), bufA.data());
          }
          SubtractArrays(bufA.data(), inPtr, outPtr, nPts);
          break;
        case HIT_OR_MISS:
          HitOrMiss(inPtr, outPtr, g, bgSe, fg, bg, this);
          break;
        default:
          vtkErrorMacro(<< "Unknown Operation.");
          ok = 0;
          break;
      }
    });
    default:
      vtkErrorMacro(<< "Unsupported scalar type.");
      return 0;
  }

  if (!ok || this->AbortExecute)
  {
    return this->AbortExecute ? 1 : 0;
  }

  output->GetPointData()->AddArray(outArr);
  if (input->GetPointData()->GetScalars() == inArr)
  {
    output->GetPointData()->SetScalars(outArr);
  }
  this->UpdateProgress(1.0);
  return 1;
}

VTK_ABI_NAMESPACE_END
