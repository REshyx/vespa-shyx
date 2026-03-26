#include "vtkSHYXBidirectionalStreamlineMerge.h"

#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkGenerateIds.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

#ifdef VESPA_USE_SMP
#include <vtkSMPTools.h>
#endif

#include <array>
#include <cmath>
#include <map>
#include <random>
#include <vector>

vtkStandardNewMacro(vtkSHYXBidirectionalStreamlineMerge);

namespace
{
bool IsPolylineCell(int t)
{
  return t == VTK_LINE || t == VTK_POLY_LINE;
}

vtkIdType ReadSeedId(vtkDataArray* a, vtkIdType tupleIdx)
{
  if (!a || tupleIdx < 0 || tupleIdx >= a->GetNumberOfTuples())
  {
    return VTK_ID_MAX;
  }
  return static_cast<vtkIdType>(a->GetTuple1(tupleIdx));
}

/** True if junction endpoints are the same point: equal ids, or identical coordinates (strict per-component ==). */
bool JunctionPointsCoincide(vtkPolyData* pd, vtkIdType idFwdFirst, vtkIdType idBwdAtJunction)
{
  if (!pd || idFwdFirst < 0 || idBwdAtJunction < 0)
  {
    return false;
  }
  if (idFwdFirst == idBwdAtJunction)
  {
    return true;
  }
  vtkPoints* pts = pd->GetPoints();
  if (!pts)
  {
    return false;
  }
  double p0[3], p1[3];
  pts->GetPoint(idFwdFirst, p0);
  pts->GetPoint(idBwdAtJunction, p1);
  return p0[0] == p1[0] && p0[1] == p1[1] && p0[2] == p1[2];
}

void BuildMergedIds(vtkPolyData* pd, vtkIdType cellFwd, vtkIdType cellBwd, std::vector<vtkIdType>* merged)
{
  vtkNew<vtkIdList> fwd;
  vtkNew<vtkIdList> bwd;
  pd->GetCellPoints(cellFwd, fwd);
  pd->GetCellPoints(cellBwd, bwd);

  merged->clear();
  const vtkIdType nb = bwd->GetNumberOfIds();
  merged->reserve(static_cast<size_t>(nb + fwd->GetNumberOfIds()));

  for (vtkIdType i = nb - 1; i >= 0; --i)
  {
    merged->push_back(bwd->GetId(i));
  }

  const vtkIdType na = fwd->GetNumberOfIds();
  vtkIdType skipFirst = 0;
  if (na > 0 && !merged->empty())
  {
    if (JunctionPointsCoincide(pd, fwd->GetId(0), merged->back()))
    {
      skipFirst = 1;
    }
  }
  for (vtkIdType i = skipFirst; i < na; ++i)
  {
    merged->push_back(fwd->GetId(i));
  }
}

void AddScalarToArrayRange(vtkDataArray* arr, vtkIdType outStart, vtkIdType nPts, double offset)
{
  if (!arr || nPts <= 0)
  {
    return;
  }
  const int nc = arr->GetNumberOfComponents();
  for (vtkIdType i = 0; i < nPts; ++i)
  {
    const vtkIdType idx = outStart + i;
    for (int c = 0; c < nc; ++c)
    {
      arr->SetComponent(idx, c, arr->GetComponent(idx, c) + offset);
    }
  }
}

void AppendPolylinePoints(vtkPolyData* input, const std::vector<vtkIdType>& ptIds, vtkPoints* outPts,
  vtkPointData* outPD, vtkPointData* inPD)
{
  std::array<double, 3> x = { { 0., 0., 0. } };
  for (vtkIdType src : ptIds)
  {
    input->GetPoint(src, x.data());
    const vtkIdType dst = outPts->InsertNextPoint(x.data());
    outPD->CopyData(inPD, src, dst);
  }
}

double IntegrandAt(vtkDataArray* a, vtkIdType inputPtId)
{
  const int nc = a->GetNumberOfComponents();
  if (nc <= 0)
  {
    return 0.;
  }
  if (nc == 1)
  {
    return a->GetTuple1(inputPtId);
  }
  double s = 0.;
  for (int c = 0; c < nc; ++c)
  {
    const double v = a->GetComponent(inputPtId, c);
    s += v * v;
  }
  return std::sqrt(s);
}

/** Cumulative trapezoid sum along vertex index: each step adds 0.5*(f_i+f_{i+1}), no segment-length factor. */
void IntegrateOneMergedLine(const std::vector<vtkIdType>& srcIds, vtkIdType outStart, vtkDataArray* integrand,
  vtkDoubleArray* outIntegral)
{
  const vtkIdType n = static_cast<vtkIdType>(srcIds.size());
  if (n <= 0)
  {
    return;
  }
  outIntegral->SetTuple1(outStart, 0.);
  for (vtkIdType i = 0; i + 1 < n; ++i)
  {
    const vtkIdType oa = outStart + i;
    const vtkIdType ob = outStart + i + 1;
    const double fa = IntegrandAt(integrand, srcIds[static_cast<size_t>(i)]);
    const double fb = IntegrandAt(integrand, srcIds[static_cast<size_t>(i + 1)]);
    const double seg = 0.5 * (fa + fb);
    const double prev = outIntegral->GetTuple1(oa);
    outIntegral->SetTuple1(ob, prev + seg);
  }
}

/** Backward difference along vertex index: delta[i] = f_i - f_{i-1} (per component); first vertex NaN. */
void DeltaOneMergedLine(const std::vector<vtkIdType>& srcIds, vtkIdType outStart, vtkDataArray* integrand,
  vtkDoubleArray* outDelta)
{
  const vtkIdType n = static_cast<vtkIdType>(srcIds.size());
  if (n <= 0)
  {
    return;
  }
  const int nc = integrand->GetNumberOfComponents();
  if (nc <= 0)
  {
    return;
  }
  const double qnan = vtkMath::Nan();
  std::vector<double> va(static_cast<size_t>(nc));
  std::vector<double> vb(static_cast<size_t>(nc));
  for (vtkIdType i = 0; i < n; ++i)
  {
    const vtkIdType oidx = outStart + i;
    if (i == 0)
    {
      for (int c = 0; c < nc; ++c)
      {
        outDelta->SetComponent(oidx, c, qnan);
      }
      continue;
    }
    integrand->GetTuple(srcIds[static_cast<size_t>(i - 1)], va.data());
    integrand->GetTuple(srcIds[static_cast<size_t>(i)], vb.data());
    for (int c = 0; c < nc; ++c)
    {
      outDelta->SetComponent(oidx, c, vb[static_cast<size_t>(c)] - va[static_cast<size_t>(c)]);
    }
  }
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXBidirectionalStreamlineMerge::vtkSHYXBidirectionalStreamlineMerge()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkSHYXBidirectionalStreamlineMerge::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXBidirectionalStreamlineMerge::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXBidirectionalStreamlineMerge::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SeedIdsArrayName: " << this->SeedIdsArrayName << "\n";
  os << indent << "SeedIdsOnCells: " << this->SeedIdsOnCells << "\n";
  os << indent << "GeneratePointIdArray: " << this->GeneratePointIdArray << "\n";
  os << indent << "PointIdsArrayName: " << this->PointIdsArrayName << "\n";
  os << indent << "ComputeArcLengthIntegral: " << this->ComputeArcLengthIntegral << "\n";
  os << indent << "IntegrandArrayName: " << this->IntegrandArrayName << "\n";
  os << indent << "IntegralArrayName: " << this->IntegralArrayName << "\n";
  os << indent << "ComputeIntegrandDelta: " << this->ComputeIntegrandDelta << "\n";
  os << indent << "DeltaArrayName: " << this->DeltaArrayName << "\n";
  os << indent << "EnablePerStreamlineRandomOffset: " << this->EnablePerStreamlineRandomOffset << "\n";
  os << indent << "RandomOffsetArrayName: " << this->RandomOffsetArrayName << "\n";
  os << indent << "RandomOffsetRangeMax: " << this->RandomOffsetRangeMax << "\n";
  os << indent << "RandomOffsetSeed: " << this->RandomOffsetSeed << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXBidirectionalStreamlineMerge::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

  if (!input)
  {
    vtkErrorMacro(<< "Input is null.");
    return 0;
  }

  vtkPointData* inPD = input->GetPointData();
  vtkCellData* inCD = input->GetCellData();

  vtkDataArray* seedCell = nullptr;
  vtkDataArray* seedPoint = nullptr;
  if (this->SeedIdsOnCells)
  {
    seedCell = inCD->GetArray(this->SeedIdsArrayName.c_str());
    if (!seedCell)
    {
      vtkErrorMacro(<< "Cell data array '" << this->SeedIdsArrayName << "' not found.");
      return 0;
    }
  }
  else
  {
    seedPoint = inPD->GetArray(this->SeedIdsArrayName.c_str());
    if (!seedPoint)
    {
      vtkErrorMacro(<< "Point data array '" << this->SeedIdsArrayName << "' not found.");
      return 0;
    }
  }

  const vtkIdType nCells = input->GetNumberOfCells();
  std::map<vtkIdType, std::vector<vtkIdType>> bySeed;

  for (vtkIdType cid = 0; cid < nCells; ++cid)
  {
    const int ctype = input->GetCellType(cid);
    if (!IsPolylineCell(ctype))
    {
      continue;
    }

    vtkIdType seed = VTK_ID_MAX;
    if (this->SeedIdsOnCells)
    {
      seed = ReadSeedId(seedCell, cid);
    }
    else
    {
      vtkNew<vtkIdList> pts;
      input->GetCellPoints(cid, pts);
      if (pts->GetNumberOfIds() < 1)
      {
        continue;
      }
      seed = ReadSeedId(seedPoint, pts->GetId(0));
    }

    if (seed == VTK_ID_MAX)
    {
      vtkWarningMacro(<< "Cell " << cid << " has invalid SeedIds; skipped.");
      continue;
    }
    bySeed[seed].push_back(cid);
  }

  if (bySeed.empty())
  {
    vtkWarningMacro(<< "No LINE/POLY_LINE cells with valid SeedIds; output empty.");
    output->Initialize();
    return 1;
  }

  struct LineWork
  {
    std::vector<vtkIdType> srcPointIds;
    /** Input cell id to copy cell-data from (forward / first segment). */
    vtkIdType attribSourceCellId = -1;
  };
  std::vector<LineWork> prepared;
  prepared.reserve(bySeed.size());

  for (auto& kv : bySeed)
  {
    std::vector<vtkIdType>& cells = kv.second;
    if (cells.size() > 2)
    {
      vtkWarningMacro(<< "Seed id " << kv.first << " has " << cells.size()
                      << " polylines; only the first two (forward / backward) are merged.");
    }

    LineWork lw;
    if (cells.size() >= 2)
    {
      lw.attribSourceCellId = cells[0];
      BuildMergedIds(input, cells[0], cells[1], &lw.srcPointIds);
    }
    else if (cells.size() == 1)
    {
      lw.attribSourceCellId = cells[0];
      vtkNew<vtkIdList> pts;
      input->GetCellPoints(cells[0], pts);
      const vtkIdType n = pts->GetNumberOfIds();
      lw.srcPointIds.resize(static_cast<size_t>(n));
      for (vtkIdType i = 0; i < n; ++i)
      {
        lw.srcPointIds[static_cast<size_t>(i)] = pts->GetId(i);
      }
    }
    else
    {
      continue;
    }

    if (static_cast<vtkIdType>(lw.srcPointIds.size()) < 2)
    {
      vtkWarningMacro(<< "Seed id " << kv.first << " has fewer than 2 points after merge; skipped.");
      continue;
    }
    prepared.push_back(std::move(lw));
  }

  if (prepared.empty())
  {
    vtkWarningMacro(<< "No valid merged polylines; output empty.");
    output->Initialize();
    return 1;
  }

  vtkIdType estPts = 0;
  for (const LineWork& lw : prepared)
  {
    estPts += static_cast<vtkIdType>(lw.srcPointIds.size());
  }

  vtkNew<vtkPoints> outPts;
  if (input->GetPoints())
  {
    outPts->SetDataType(input->GetPoints()->GetDataType());
  }
  else
  {
    outPts->SetDataTypeToDouble();
  }

  vtkPointData* outPD = output->GetPointData();
  outPD->CopyAllocate(inPD, estPts);

  vtkNew<vtkCellArray> emptyVerts;
  vtkNew<vtkCellArray> emptyPolys;
  vtkNew<vtkCellArray> emptyStrips;
  output->SetVerts(emptyVerts);
  output->SetPolys(emptyPolys);
  output->SetStrips(emptyStrips);

  vtkNew<vtkCellArray> outLines;
  outLines->AllocateEstimate(static_cast<vtkIdType>(prepared.size()), 8);

  vtkNew<vtkIdList> cellPts;

  for (const LineWork& lw : prepared)
  {
    const vtkIdType outStart = outPts->GetNumberOfPoints();
    AppendPolylinePoints(input, lw.srcPointIds, outPts, outPD, inPD);

    cellPts->Reset();
    const vtkIdType nLine = static_cast<vtkIdType>(lw.srcPointIds.size());
    for (vtkIdType i = 0; i < nLine; ++i)
    {
      cellPts->InsertNextId(outStart + i);
    }
    outLines->InsertNextCell(cellPts);
  }

  output->SetPoints(outPts);
  output->SetLines(outLines);

  vtkCellData* outCD = output->GetCellData();
  const vtkIdType nOutCells = static_cast<vtkIdType>(prepared.size());
  outCD->CopyAllocate(inCD, nOutCells);
  for (vtkIdType outCid = 0; outCid < nOutCells; ++outCid)
  {
    const vtkIdType srcCid = prepared[static_cast<size_t>(outCid)].attribSourceCellId;
    if (srcCid >= 0)
    {
      outCD->CopyData(inCD, srcCid, outCid);
    }
  }

  if (this->EnablePerStreamlineRandomOffset)
  {
    if (this->RandomOffsetArrayName.empty())
    {
      vtkWarningMacro(<< "EnablePerStreamlineRandomOffset is on but RandomOffsetArrayName is empty; skipping.");
    }
    else
    {
      vtkDataArray* randArr = output->GetPointData()->GetArray(this->RandomOffsetArrayName.c_str());
      if (!randArr)
      {
        vtkErrorMacro(<< "Random offset array '" << this->RandomOffsetArrayName << "' not found on output.");
        return 0;
      }
      double rMax = this->RandomOffsetRangeMax;
      if (rMax < 0.0)
      {
        vtkWarningMacro(<< "RandomOffsetRangeMax is negative; using 0.");
        rMax = 0.0;
      }
      std::mt19937 rng(static_cast<std::mt19937::result_type>(this->RandomOffsetSeed));
      vtkIdType acc = 0;
      for (size_t li = 0; li < prepared.size(); ++li)
      {
        const vtkIdType outStart = acc;
        const vtkIdType nLine = static_cast<vtkIdType>(prepared[li].srcPointIds.size());
        double offset = 0.0;
        if (rMax > 0.0)
        {
          std::uniform_real_distribution<double> uni(0.0, rMax);
          offset = uni(rng);
        }
        AddScalarToArrayRange(randArr, outStart, nLine, offset);
        acc += nLine;
      }
    }
  }

  if (this->ComputeArcLengthIntegral || this->ComputeIntegrandDelta)
  {
    if (this->IntegrandArrayName.empty())
    {
      vtkWarningMacro(<< "Along-line integral and/or integrand delta is on but IntegrandArrayName is empty; "
                         "skipping those arrays.");
    }
    else
    {
      vtkDataArray* integrand = inPD->GetArray(this->IntegrandArrayName.c_str());
      if (!integrand)
      {
        vtkErrorMacro(<< "Integrand array '" << this->IntegrandArrayName << "' not found.");
        return 0;
      }

      const vtkIdType nOutPts = outPts->GetNumberOfPoints();
      std::vector<vtkIdType> outStarts(prepared.size());
      vtkIdType acc = 0;
      for (size_t li = 0; li < prepared.size(); ++li)
      {
        outStarts[li] = acc;
        acc += static_cast<vtkIdType>(prepared[li].srcPointIds.size());
      }

      if (this->ComputeArcLengthIntegral)
      {
        vtkNew<vtkDoubleArray> intArr;
        intArr->SetName(this->IntegralArrayName.c_str());
        intArr->SetNumberOfComponents(1);
        intArr->SetNumberOfTuples(nOutPts);
        const double qnan = vtkMath::Nan();
        for (vtkIdType i = 0; i < nOutPts; ++i)
        {
          intArr->SetTuple1(i, qnan);
        }

#ifdef VESPA_USE_SMP
        vtkSMPTools::For(0, static_cast<vtkIdType>(prepared.size()),
          [&](vtkIdType begin, vtkIdType end)
          {
            for (vtkIdType li = begin; li < end; ++li)
            {
              IntegrateOneMergedLine(
                prepared[static_cast<size_t>(li)].srcPointIds, outStarts[static_cast<size_t>(li)], integrand,
                intArr);
            }
          });
#else
        for (size_t li = 0; li < prepared.size(); ++li)
        {
          IntegrateOneMergedLine(prepared[li].srcPointIds, outStarts[li], integrand, intArr);
        }
#endif
        output->GetPointData()->AddArray(intArr);
      }

      if (this->ComputeIntegrandDelta)
      {
        const int nc = integrand->GetNumberOfComponents();
        if (nc <= 0)
        {
          vtkWarningMacro(<< "Integrand has zero components; skipping integrand delta.");
        }
        else
        {
          vtkNew<vtkDoubleArray> deltaArr;
          deltaArr->SetName(this->DeltaArrayName.c_str());
          deltaArr->SetNumberOfComponents(nc);
          deltaArr->SetNumberOfTuples(nOutPts);
          const double qnan = vtkMath::Nan();
          for (vtkIdType i = 0; i < nOutPts; ++i)
          {
            for (int c = 0; c < nc; ++c)
            {
              deltaArr->SetComponent(i, c, qnan);
            }
          }

#ifdef VESPA_USE_SMP
          vtkSMPTools::For(0, static_cast<vtkIdType>(prepared.size()),
            [&](vtkIdType begin, vtkIdType end)
            {
              for (vtkIdType li = begin; li < end; ++li)
              {
                DeltaOneMergedLine(
                  prepared[static_cast<size_t>(li)].srcPointIds, outStarts[static_cast<size_t>(li)], integrand,
                  deltaArr);
              }
            });
#else
          for (size_t li = 0; li < prepared.size(); ++li)
          {
            DeltaOneMergedLine(prepared[li].srcPointIds, outStarts[li], integrand, deltaArr);
          }
#endif
          output->GetPointData()->AddArray(deltaArr);
        }
      }
    }
  }

  if (this->GeneratePointIdArray)
  {
    vtkNew<vtkGenerateIds> gen;
    gen->SetInputData(output);
    gen->SetPointIdsArrayName(this->PointIdsArrayName);
    gen->SetPointIds(true);
    gen->SetCellIds(false);
    gen->SetFieldData(false);
    gen->Update();
    output->ShallowCopy(gen->GetOutput());
  }

  return 1;
}
