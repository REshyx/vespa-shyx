#include "vtkSurfaceTipExtractor.h"

#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"
#include "vtkPointData.h"
#include "vtkCellData.h"
#include "vtkDoubleArray.h"
#include "vtkIdList.h"
#include "vtkMath.h"
#include "vtkAbstractPointLocator.h"
#include "vtkSmartPointer.h"

#include <queue>
#include <vector>

vtkStandardNewMacro(vtkSurfaceTipExtractor);

vtkSurfaceTipExtractor::vtkSurfaceTipExtractor()
{
    this->SearchRadius = 5.0;
}

vtkSurfaceTipExtractor::~vtkSurfaceTipExtractor() = default;

void vtkSurfaceTipExtractor::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "SearchRadius: " << this->SearchRadius << "\n";
}

int vtkSurfaceTipExtractor::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
    vtkInformation* outInfo = outputVector->GetInformationObject(0);

    vtkPolyData* input = vtkPolyData::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
    vtkPolyData* output = vtkPolyData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

    if (!input || input->GetNumberOfPoints() == 0)
    {
        return 1;
    }

    vtkIdType numPoints = input->GetNumberOfPoints();

    output->CopyStructure(input);
    output->GetPointData()->PassData(input->GetPointData());
    output->GetCellData()->PassData(input->GetCellData());
    output->BuildLinks();

    input->BuildPointLocator();
    vtkAbstractPointLocator* locator = input->GetPointLocator();

    vtkSmartPointer<vtkDoubleArray> tipScoreArray = vtkSmartPointer<vtkDoubleArray>::New();
    tipScoreArray->SetName("TipScore");
    tipScoreArray->SetNumberOfValues(numPoints);

    vtkSmartPointer<vtkIdList> candidateIds = vtkSmartPointer<vtkIdList>::New();
    vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();
    vtkSmartPointer<vtkIdList> pointIds = vtkSmartPointer<vtkIdList>::New();

    std::vector<bool> isCandidate(numPoints, false);

    for (vtkIdType i = 0; i < numPoints; ++i)
    {
        if (i % 1000 == 0)
        {
            this->UpdateProgress(static_cast<double>(i) / numPoints);
            if (this->CheckAbort())
                break;
        }

        double currentPt[3];
        output->GetPoint(i, currentPt);

        locator->FindPointsWithinRadius(this->SearchRadius, currentPt, candidateIds);
        vtkIdType numCandidates = candidateIds->GetNumberOfIds();

        for (vtkIdType j = 0; j < numCandidates; ++j)
        {
            isCandidate[candidateIds->GetId(j)] = true;
        }

        std::queue<vtkIdType> bfsQueue;
        bfsQueue.push(i);
        isCandidate[i] = false;

        double centroid[3] = { 0.0, 0.0, 0.0 };
        int count = 0;

        while (!bfsQueue.empty())
        {
            vtkIdType curr = bfsQueue.front();
            bfsQueue.pop();

            double p[3];
            output->GetPoint(curr, p);
            centroid[0] += p[0];
            centroid[1] += p[1];
            centroid[2] += p[2];
            count++;

            output->GetPointCells(curr, cellIds);
            for (vtkIdType c = 0; c < cellIds->GetNumberOfIds(); ++c)
            {
                output->GetCellPoints(cellIds->GetId(c), pointIds);
                for (vtkIdType ptIdx = 0; ptIdx < pointIds->GetNumberOfIds(); ++ptIdx)
                {
                    vtkIdType neighborId = pointIds->GetId(ptIdx);
                    if (isCandidate[neighborId])
                    {
                        isCandidate[neighborId] = false;
                        bfsQueue.push(neighborId);
                    }
                }
            }
        }

        for (vtkIdType j = 0; j < numCandidates; ++j)
        {
            isCandidate[candidateIds->GetId(j)] = false;
        }

        if (count > 0)
        {
            centroid[0] /= count;
            centroid[1] /= count;
            centroid[2] /= count;
            double score = std::sqrt(vtkMath::Distance2BetweenPoints(currentPt, centroid));
            tipScoreArray->SetValue(i, score);
        }
        else
        {
            tipScoreArray->SetValue(i, 0.0);
        }
    }

    output->GetPointData()->AddArray(tipScoreArray);
    output->GetPointData()->SetActiveScalars("TipScore");

    return 1;
}
