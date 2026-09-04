#include "vtkSHYXSkeletonEndClipper.h"

#include "vtkCGALSkeletonExtraction.h"
#include "vtkCGALVesselEndClipper.h"

#include <vtkAppendPolyData.h>
#include <vtkDataObject.h>
#include <vtkDoubleArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cstring>
#include <sstream>

vtkStandardNewMacro(vtkSHYXSkeletonEndClipper);

namespace
{
// vtkAppendPolyData intersects point-data arrays. Skeleton lines have none, so
// clip-plane EndpointIndex / ClipNormal / Enabled would be dropped (no Point Label).
vtkSmartPointer<vtkPolyData> SkeletonWithClipPointArrays(vtkPolyData* skeleton)
{
    vtkSmartPointer<vtkPolyData> tagged = vtkSmartPointer<vtkPolyData>::New();
    tagged->ShallowCopy(skeleton);
    const vtkIdType nPts = tagged->GetNumberOfPoints();

    vtkNew<vtkIntArray> endpointIndex;
    endpointIndex->SetName("EndpointIndex");
    endpointIndex->SetNumberOfComponents(1);
    endpointIndex->SetNumberOfTuples(nPts);
    endpointIndex->Fill(-1);

    vtkNew<vtkIntArray> enabled;
    enabled->SetName("Enabled");
    enabled->SetNumberOfComponents(1);
    enabled->SetNumberOfTuples(nPts);
    enabled->Fill(0);

    vtkNew<vtkDoubleArray> clipNormal;
    clipNormal->SetName("ClipNormal");
    clipNormal->SetNumberOfComponents(3);
    clipNormal->SetNumberOfTuples(nPts);
    for (vtkIdType i = 0; i < nPts; ++i)
    {
        clipNormal->SetTuple3(i, 0.0, 0.0, 0.0);
    }

    tagged->GetPointData()->AddArray(endpointIndex);
    tagged->GetPointData()->AddArray(enabled);
    tagged->GetPointData()->AddArray(clipNormal);
    return tagged;
}
}

//------------------------------------------------------------------------------
vtkSHYXSkeletonEndClipper::vtkSHYXSkeletonEndClipper()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(2);
    this->SkeletonFilter = vtkSmartPointer<vtkCGALSkeletonExtraction>::New();
    this->ClipperFilter  = vtkSmartPointer<vtkCGALVesselEndClipper>::New();
}

//------------------------------------------------------------------------------
vtkSHYXSkeletonEndClipper::~vtkSHYXSkeletonEndClipper()
{
    this->SetInteractiveCutPackedString(nullptr);
    this->SetOutputMessageNoModified(nullptr);
}

//------------------------------------------------------------------------------
int vtkSHYXSkeletonEndClipper::FillOutputPortInformation(int port, vtkInformation* info)
{
    if (port == 0 || port == 1)
    {
        info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXSkeletonEndClipper::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "MaxTriangleAngle: " << this->MaxTriangleAngle << std::endl;
    os << indent << "MinEdgeLength: " << this->MinEdgeLength << std::endl;
    os << indent << "MaxIterations: " << this->MaxIterations << std::endl;
    os << indent << "AreaThreshold: " << this->AreaThreshold << std::endl;
    os << indent << "QualitySpeedTradeoff: " << this->QualitySpeedTradeoff << std::endl;
    os << indent << "MediallyCentered: " << this->MediallyCentered << std::endl;
    os << indent << "MediallyCenteredSpeedTradeoff: " << this->MediallyCenteredSpeedTradeoff
       << std::endl;
    os << indent << "ClipOffset: " << this->ClipOffset << std::endl;
    os << indent << "MinBranchLength: " << this->MinBranchLength << std::endl;
    os << indent << "TangentDepth: " << this->TangentDepth << std::endl;
    os << indent << "CapEndpoints: " << this->CapEndpoints << std::endl;
    os << indent << "FairingContinuity: " << this->FairingContinuity << std::endl;
    os << indent << "UseInteractiveCutPlanes: " << (this->UseInteractiveCutPlanes ? 1 : 0)
       << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkSHYXSkeletonEndClipper::SetOutputMessageNoModified(const char* msg)
{
    if ((this->OutputMessage == nullptr && (msg == nullptr || msg[0] == '\0')) ||
        (this->OutputMessage && msg && std::strcmp(this->OutputMessage, msg) == 0))
    {
        return;
    }
    delete[] this->OutputMessage;
    this->OutputMessage = nullptr;
    if (msg && msg[0] != '\0')
    {
        const size_t n        = std::strlen(msg) + 1;
        this->OutputMessage   = new char[n];
        std::memcpy(this->OutputMessage, msg, n);
    }
}

//------------------------------------------------------------------------------
void vtkSHYXSkeletonEndClipper::SetUseInteractiveCutPlanes(bool flag)
{
    if (this->UseInteractiveCutPlanes == flag)
    {
        return;
    }
    this->UseInteractiveCutPlanes = flag;
    if (this->ClipperFilter)
    {
        this->ClipperFilter->SetUseInteractiveCutPlanes(flag);
    }
}

//------------------------------------------------------------------------------
vtkDataArraySelection* vtkSHYXSkeletonEndClipper::GetEndpointSelection()
{
    return this->ClipperFilter ? this->ClipperFilter->GetEndpointSelection() : nullptr;
}

//------------------------------------------------------------------------------
vtkMTimeType vtkSHYXSkeletonEndClipper::GetMTime()
{
    vtkMTimeType mTime = this->Superclass::GetMTime();
    if (vtkDataArraySelection* sel = this->GetEndpointSelection())
    {
        mTime = std::max(mTime, sel->GetMTime());
    }
    return mTime;
}

//------------------------------------------------------------------------------
void vtkSHYXSkeletonEndClipper::ApplyParametersToInnerFilters()
{
    this->SkeletonFilter->SetMaxTriangleAngle(this->MaxTriangleAngle);
    this->SkeletonFilter->SetMinEdgeLength(this->MinEdgeLength);
    this->SkeletonFilter->SetMaxIterations(this->MaxIterations);
    this->SkeletonFilter->SetAreaThreshold(this->AreaThreshold);
    this->SkeletonFilter->SetQualitySpeedTradeoff(this->QualitySpeedTradeoff);
    this->SkeletonFilter->SetMediallyCentered(this->MediallyCentered);
    this->SkeletonFilter->SetMediallyCenteredSpeedTradeoff(this->MediallyCenteredSpeedTradeoff);

    this->ClipperFilter->SetClipOffset(this->ClipOffset);
    this->ClipperFilter->SetMinBranchLength(this->MinBranchLength);
    this->ClipperFilter->SetTangentDepth(this->TangentDepth);
    this->ClipperFilter->SetCapEndpoints(this->CapEndpoints);
    this->ClipperFilter->SetFairingContinuity(this->FairingContinuity);
    this->ClipperFilter->SetUpdateAttributes(this->UpdateAttributes);
    this->ClipperFilter->SetUseInteractiveCutPlanes(this->UseInteractiveCutPlanes);
    this->ClipperFilter->SetInteractiveCutPackedString(this->InteractiveCutPackedString);
}

//------------------------------------------------------------------------------
int vtkSHYXSkeletonEndClipper::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkPolyData* input     = vtkPolyData::GetData(inputVector[0]);
    vtkPolyData* output    = vtkPolyData::GetData(outputVector, 0);
    vtkPolyData* vizOutput = vtkPolyData::GetData(outputVector, 1);

    if (!input || !output)
    {
        vtkErrorMacro("Missing input or output.");
        return 0;
    }

    if (input->GetNumberOfCells() == 0)
    {
        vtkErrorMacro("Input mesh is empty.");
        return 0;
    }

    this->ApplyParametersToInnerFilters();

    this->SkeletonFilter->SetInputData(input);
    this->UpdateProgress(0.05);
    this->SkeletonFilter->Update();
    vtkPolyData* skeleton = this->SkeletonFilter->GetOutput();
    if (!skeleton || skeleton->GetNumberOfLines() == 0)
    {
        vtkErrorMacro("Skeleton extraction produced no line cells.");
        this->SetOutputMessageNoModified("skeleton extraction failed (no lines)");
        return 0;
    }

    this->ClipperFilter->SetInputData(0, input);
    this->ClipperFilter->SetInputData(1, skeleton);
    this->UpdateProgress(0.55);
    this->ClipperFilter->Update();

    vtkPolyData* clipped = this->ClipperFilter->GetOutput(0);
    vtkPolyData* clipViz = this->ClipperFilter->GetOutput(1);
    if (!clipped)
    {
        vtkErrorMacro("End clipper produced no clipped mesh.");
        return 0;
    }

    output->DeepCopy(clipped);

    // Port 1: clip-plane verts/arrows first (2 points per endpoint), then skeleton lines.
    // Point Label VertexOnly labels only the clip-origin verts; skeleton has no verts.
    if (vizOutput)
    {
        const bool haveClip =
            clipViz && (clipViz->GetNumberOfPoints() > 0 || clipViz->GetNumberOfCells() > 0);
        const bool haveSkel = skeleton->GetNumberOfPoints() > 0;

        if (haveClip && haveSkel)
        {
            vtkSmartPointer<vtkPolyData> taggedSkeleton = SkeletonWithClipPointArrays(skeleton);
            vtkNew<vtkAppendPolyData> append;
            append->AddInputData(clipViz);
            append->AddInputData(taggedSkeleton);
            append->Update();
            vizOutput->DeepCopy(append->GetOutput());
        }
        else if (haveClip)
        {
            vizOutput->DeepCopy(clipViz);
        }
        else
        {
            vizOutput->DeepCopy(skeleton);
        }

        if (vizOutput->GetPointData()->GetArray("EndpointIndex"))
        {
            vizOutput->GetPointData()->SetActiveScalars("EndpointIndex");
        }
    }

    std::ostringstream oss;
    oss << "[SHYXSkeletonEndClipper] skeleton points=" << skeleton->GetNumberOfPoints()
        << " lines=" << skeleton->GetNumberOfLines();
    if (const char* clipMsg = this->ClipperFilter->GetOutputMessage())
    {
        oss << " | " << clipMsg;
    }
    this->SetOutputMessageNoModified(oss.str().c_str());

    this->UpdateProgress(1.0);
    return 1;
}
