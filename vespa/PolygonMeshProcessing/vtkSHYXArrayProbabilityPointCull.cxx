#include "vtkSHYXArrayProbabilityPointCull.h"

#include <vtkCellArray.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMinimalStandardRandomSequence.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#ifdef VESPA_USE_SMP
#include <vtkSMPTools.h>
#include <vtkSMPThreadLocal.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

vtkStandardNewMacro(vtkSHYXArrayProbabilityPointCull);

namespace
{
bool IsUniformSelection(const std::string& name)
{
    return name.empty() || name == "(Uniform)";
}

/** Per-point retention probability: scalar used as probability, clamped to [0, 1] only (no range remap). */
double ScalarToKeepProbability(double raw)
{
    return std::max(0.0, std::min(1.0, raw));
}

#ifdef VESPA_USE_SMP
/** Parallel-safe U(0,1); identical draw for given (seed, pointId). Not vtkMinimalStandardSequence. */
uint64_t SplitMix64(uint64_t z)
{
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

double DeterministicUniform01(int seed, vtkIdType i)
{
    uint64_t k = static_cast<uint64_t>(static_cast<uint32_t>(seed));
    k ^= 0x9e3779b9781f4854ULL * (static_cast<uint64_t>(static_cast<int64_t>(i)) + 1ULL);
    return (SplitMix64(k) >> 11) * (1.0 / 9007199254740992.0);
}
#endif
} // namespace

//------------------------------------------------------------------------------
vtkSHYXArrayProbabilityPointCull::vtkSHYXArrayProbabilityPointCull()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkSHYXArrayProbabilityPointCull::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXArrayProbabilityPointCull::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXArrayProbabilityPointCull::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "WeightArrayName: " << this->WeightArrayName << "\n";
    os << indent << "Seed: " << this->Seed << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXArrayProbabilityPointCull::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkDataSet* input = vtkDataSet::GetData(inputVector[0], 0);
    vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);

    if (!input || input->GetNumberOfPoints() == 0)
    {
        vtkErrorMacro(<< "Input is empty or null.");
        return 0;
    }

    const vtkIdType nPts = input->GetNumberOfPoints();
    vtkPointData* inPD = input->GetPointData();
    vtkPoints* inPts = input->GetPoints();

    vtkDataArray* weightArray = nullptr;
    bool uniformMode = IsUniformSelection(this->WeightArrayName);

    if (!uniformMode)
    {
        weightArray = inPD->GetArray(this->WeightArrayName.c_str());
        if (!weightArray)
        {
            vtkWarningMacro(<< "Weight array '" << this->WeightArrayName
                            << "' not found. Keeping all points (uniform).");
            uniformMode = true;
        }
    }

    std::vector<vtkIdType> kept;
    kept.reserve(static_cast<size_t>(nPts));

    if (uniformMode)
    {
        kept.resize(static_cast<size_t>(nPts));
#ifdef VESPA_USE_SMP
        vtkSMPTools::For(
            0, nPts, [&](vtkIdType begin, vtkIdType end) {
                for (vtkIdType i = begin; i < end; ++i)
                {
                    kept[static_cast<size_t>(i)] = i;
                }
            });
#else
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            kept[static_cast<size_t>(i)] = i;
        }
#endif
    }
    else
    {
#ifdef VESPA_USE_SMP
        vtkSMPThreadLocal<std::vector<vtkIdType>> threadKept;
        vtkSMPTools::For(
            0, nPts,
            [&](vtkIdType begin, vtkIdType end)
            {
                std::vector<vtkIdType>& local = threadKept.Local();
                for (vtkIdType i = begin; i < end; ++i)
                {
                    const double p = ScalarToKeepProbability(weightArray->GetTuple1(i));
                    if (DeterministicUniform01(this->Seed, i) < p)
                    {
                        local.push_back(i);
                    }
                }
            });
        for (auto it = threadKept.begin(); it != threadKept.end(); ++it)
        {
            kept.insert(kept.end(), it->begin(), it->end());
        }
        std::sort(kept.begin(), kept.end());
#else
        vtkNew<vtkMinimalStandardRandomSequence> rng;
        rng->SetSeed(this->Seed);
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            const double p = ScalarToKeepProbability(weightArray->GetTuple1(i));
            rng->Next();
            if (rng->GetValue() < p)
            {
                kept.push_back(i);
            }
        }
#endif
    }

    const vtkIdType nOut = static_cast<vtkIdType>(kept.size());

    vtkNew<vtkPoints> outPts;
    if (inPts)
    {
        outPts->SetDataType(inPts->GetDataType());
    }
    else
    {
        outPts->SetDataTypeToDouble();
    }
    outPts->SetNumberOfPoints(nOut);

    vtkPointData* outPD = output->GetPointData();
    outPD->CopyAllocate(inPD, nOut);

#ifdef VESPA_USE_SMP
    vtkSMPTools::For(
        0, nOut,
        [&](vtkIdType begin, vtkIdType end)
        {
            for (vtkIdType j = begin; j < end; ++j)
            {
                const vtkIdType src = kept[static_cast<size_t>(j)];
                std::array<double, 3> x = { { 0., 0., 0. } };
                input->GetPoint(src, x.data());
                outPts->SetPoint(j, x.data());
                outPD->CopyData(inPD, src, j);
            }
        });
#else
    for (vtkIdType j = 0; j < nOut; ++j)
    {
        const vtkIdType src = kept[static_cast<size_t>(j)];
        std::array<double, 3> x = { { 0., 0., 0. } };
        input->GetPoint(src, x.data());
        outPts->SetPoint(j, x.data());
        outPD->CopyData(inPD, src, j);
    }
#endif

    vtkNew<vtkCellArray> verts;
    verts->AllocateEstimate(nOut, 1);
    for (vtkIdType j = 0; j < nOut; ++j)
    {
        verts->InsertNextCell(1, &j);
    }

    output->SetPoints(outPts);
    output->SetVerts(verts);
    output->Squeeze();

    return 1;
}
