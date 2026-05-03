#include "vtkCGALPolyDataAlgorithm.h"

// VTK related includes
#include <vtkCellData.h>
#include <vtkGenericCell.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkProbeFilter.h>
#include <vtkSMPThreadLocalObject.h>
#include <vtkSMPTools.h>
#include <vtkStaticCellLocator.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

vtkStandardNewMacro(vtkCGALPolyDataAlgorithm);

namespace
{

std::string CGALPolyDataAlgCellAttrLogPath()
{
#ifdef _WIN32
  const char* tmp = std::getenv("TEMP");
  return tmp ? (std::string(tmp) + "\\vtkCGALPolyDataAlgorithm_cell_attr_log.txt")
             : "vtkCGALPolyDataAlgorithm_cell_attr_log.txt";
#else
  const char* tmp = std::getenv("TMPDIR");
  return (tmp ? std::string(tmp) : std::string("/tmp")) + "/vtkCGALPolyDataAlgorithm_cell_attr_log.txt";
#endif
}

void CGALPolyDataAlgCellAttrLog(const std::string& msg)
{
  std::ofstream f(CGALPolyDataAlgCellAttrLogPath(), std::ios::app);
  if (f)
  {
    f << msg << '\n';
    f.flush();
  }
}

/**
 * Default-on micro-benchmark for vtkSMPTools in this process (same log as cell-attribute diagnostics).
 * Set environment variable VESPA_VTKCGAL_SMP_PROBE=0 to skip (e.g. if this line crashes on your build).
 * If ParaView exits before [smp-probe] appears, vtkSMPTools is unstable in this plugin/DLL setup.
 */
bool SMPProbeDisabledByEnv()
{
  const char* flag = std::getenv("VESPA_VTKCGAL_SMP_PROBE");
  if (flag == nullptr || flag[0] == '\0')
  {
    return false;
  }
  if (flag[0] == '0' && flag[1] == '\0')
  {
    return true;
  }
  std::string s(flag);
  for (char& c : s)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s == "off" || s == "false" || s == "no";
}

void RunVTKCGALSMPSelfTestIfRequested()
{
  if (SMPProbeDisabledByEnv())
  {
    CGALPolyDataAlgCellAttrLog("[smp-probe] skipped (VESPA_VTKCGAL_SMP_PROBE disables probe)");
    return;
  }
  constexpr vtkIdType n = 1024;
  std::atomic<vtkIdType> sum{ 0 };
  vtkSMPTools::For(0, n, [&](vtkIdType begin, vtkIdType end) {
    vtkIdType chunk = 0;
    for (vtkIdType i = begin; i < end; ++i)
    {
      chunk += i;
    }
    sum.fetch_add(chunk, std::memory_order_relaxed);
  });
  const vtkIdType expected = (n - 1) * n / 2;
  const vtkIdType got = sum.load();
  CGALPolyDataAlgCellAttrLog(std::string("[smp-probe] vtkSMPTools::For sum(0..") + std::to_string(n - 1) +
    ")=" + std::to_string(static_cast<long long>(got)) + " expected=" +
    std::to_string(static_cast<long long>(expected)) + (got == expected ? " OK" : " MISMATCH"));
}

void ComputeCellCentroid(vtkPolyData* pd, vtkIdType cellId, vtkGenericCell* genCell, double* centroid)
{
  pd->GetCell(cellId, genCell);
  const int numPts = genCell->GetNumberOfPoints();
  centroid[0]      = 0.0;
  centroid[1]      = 0.0;
  centroid[2]      = 0.0;
  if (numPts <= 0)
  {
    return;
  }
  double x[3];
  for (int i = 0; i < numPts; ++i)
  {
    pd->GetPoint(genCell->GetPointId(i), x);
    centroid[0] += x[0];
    centroid[1] += x[1];
    centroid[2] += x[2];
  }
  const double inv = 1.0 / static_cast<double>(numPts);
  centroid[0] *= inv;
  centroid[1] *= inv;
  centroid[2] *= inv;
}

} // namespace

//------------------------------------------------------------------------------
void vtkCGALPolyDataAlgorithm::PrintSelf(ostream& os, vtkIndent indent)
{
  os << indent << "UpdateAttributes:" << this->UpdateAttributes << std::endl;
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
// Cell-attribute mapping: vtkPolyData must call BuildCells() on the main thread before any
// concurrent GetCell/GetPoint-style access (VTK discourse, dgobbi 2023). FindClosestPoint with
// vtkGenericCell* is the thread-safe overload on vtkAbstractCellLocator. CopyData stays serial.
// Default [smp-probe]; VESPA_VTKCGAL_SMP_PROBE=0 skips.
//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::interpolateAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (this->UpdateAttributes)
  {
    vtkNew<vtkProbeFilter> probe;
    probe->SetInputData(vtkMesh);
    probe->SetSourceData(input);
    probe->SpatialMatchOn();
    probe->PassCellArraysOff();
    probe->Update();

    vtkMesh->ShallowCopy(probe->GetOutput());

    vtkCellData* inCD  = input->GetCellData();
    vtkCellData* outCD = vtkMesh->GetCellData();
    const vtkIdType nCells = vtkMesh->GetNumberOfCells();
    if (inCD->GetNumberOfArrays() > 0 && nCells > 0)
    {
      // Required for thread-safe vtkPolyData::GetCell / point id walks (Kitware discourse #12809).
      input->BuildCells();
      vtkMesh->BuildCells();

      vtkNew<vtkStaticCellLocator> locator;
      locator->SetDataSet(input);
      locator->BuildLocator();

      outCD->CopyAllocate(inCD, nCells);

      vtkPolyData* mesh            = vtkMesh;
      vtkStaticCellLocator* locPtr = locator;

      std::ofstream(CGALPolyDataAlgCellAttrLogPath()).close();
      CGALPolyDataAlgCellAttrLog(
        "[1] vtkCGALPolyDataAlgorithm interpolateAttributes (cell data via nearest cell): "
        "outCells=" +
        std::to_string(nCells) + " outPts=" + std::to_string(vtkMesh->GetNumberOfPoints()) +
        " inCells=" + std::to_string(input->GetNumberOfCells()) +
        " inPts=" + std::to_string(input->GetNumberOfPoints()) +
        " inCellArrays=" + std::to_string(inCD->GetNumberOfArrays()));

      RunVTKCGALSMPSelfTestIfRequested();

      const auto cellAttrT0 = std::chrono::steady_clock::now();

      std::vector<vtkIdType> srcCellIds(static_cast<size_t>(nCells), -1);
      vtkSMPThreadLocalObject<vtkGenericCell> cellTLS;
      vtkSMPTools::For(0, nCells, [&](vtkIdType begin, vtkIdType end) {
        vtkGenericCell* genCell = cellTLS.Local();
        double          centroid[3];
        double          closestPt[3];
        for (vtkIdType outCellId = begin; outCellId < end; ++outCellId)
        {
          ComputeCellCentroid(mesh, outCellId, genCell, centroid);
          vtkIdType srcCellId = -1;
          int       subId     = -1;
          double    dist2     = 0.0;
          locPtr->FindClosestPoint(centroid, closestPt, genCell, srcCellId, subId, dist2);
          srcCellIds[static_cast<size_t>(outCellId)] = srcCellId;
        }
      });

      const auto afterSmpPass = std::chrono::steady_clock::now();
      const auto smpPassMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(afterSmpPass - cellAttrT0).count();
      CGALPolyDataAlgCellAttrLog("[2] centroid + nearest-cell (SMP) done: elapsed_ms=" +
        std::to_string(smpPassMs));

      const auto copyT0 = std::chrono::steady_clock::now();
      for (vtkIdType outCellId = 0; outCellId < nCells; ++outCellId)
      {
        const vtkIdType srcCellId = srcCellIds[static_cast<size_t>(outCellId)];
        if (srcCellId >= 0)
        {
          outCD->CopyData(inCD, srcCellId, outCellId);
        }
      }
      const auto copyT1 = std::chrono::steady_clock::now();
      const auto copyMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(copyT1 - copyT0).count();
      CGALPolyDataAlgCellAttrLog("[3] CopyData serial done: elapsed_ms=" + std::to_string(copyMs));

      const auto totalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(copyT1 - cellAttrT0).count();
      CGALPolyDataAlgCellAttrLog("[4] cell attr map total: elapsed_ms=" + std::to_string(totalMs));
    }
  }
  return true;
}

//------------------------------------------------------------------------------
bool vtkCGALPolyDataAlgorithm::copyAttributes(vtkPolyData* input, vtkPolyData* vtkMesh)
{
  if (this->UpdateAttributes)
  {
    vtkMesh->GetPointData()->ShallowCopy(input->GetPointData());
    vtkMesh->GetCellData()->ShallowCopy(input->GetCellData());
  }

  return true;
}
