#include "vtkSHYXImageAntiAlias.h"

#include "vtkAlgorithm.h"
#include "vtkCellData.h"
#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSetAttributes.h"
#include "vtkFieldData.h"
#include "vtkImageData.h"
#include "vtkImageResample.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSMPTools.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <cmath>

#include "itkAntiAliasBinaryImageFilter.h"
#include "vtkvmtkITKFilterUtilities.h"

#include <string>

VTK_ABI_NAMESPACE_BEGIN

namespace
{

using AAFloatImage = itk::Image<float, 3>;

constexpr double kResampleIdentityEps = 1e-12;

bool NearlyIdentityFactor(double f)
{
  return std::fabs(f - 1.0) < kResampleIdentityEps;
}

void ApplyResampleGeometry(double factor, bool zIs2D, int ext[6], double spacing[3])
{
  if (NearlyIdentityFactor(factor))
  {
    return;
  }
  for (int axis = 0; axis < 3; ++axis)
  {
    const double f = (zIs2D && axis == 2) ? 1.0 : factor;
    const int wholeMin = static_cast<int>(std::ceil(static_cast<double>(ext[axis * 2]) * f));
    const int wholeMax = static_cast<int>(std::floor(static_cast<double>(ext[axis * 2 + 1]) * f));
    ext[axis * 2] = wholeMin;
    ext[axis * 2 + 1] = wholeMax;
    spacing[axis] /= f;
  }
}

bool RunWhitakerAntiAlias(vtkImageData* workIn, vtkImageData* workOut, double maxRms, int nIters,
  bool useSpacing, vtkAlgorithm* progressHost, std::string& error)
{
  try
  {
    auto inItk = AAFloatImage::New();
    vtkvmtkITKFilterUtilities::VTKToITKImage<AAFloatImage>(workIn, inItk);

    auto aa = itk::AntiAliasBinaryImageFilter<AAFloatImage, AAFloatImage>::New();
    aa->SetInput(inItk);
    aa->SetMaximumRMSError(maxRms);
    aa->SetNumberOfIterations(static_cast<unsigned int>(nIters));
    aa->SetUseImageSpacing(useSpacing);
    if (progressHost)
    {
      vtkvmtkITKFilterUtilities::ConnectProgress(aa.GetPointer(), progressHost);
    }
    aa->Update();

    workOut->CopyStructure(workIn);
    workOut->AllocateScalars(VTK_FLOAT, 1);
    vtkvmtkITKFilterUtilities::ITKToVTKImage<AAFloatImage>(aa->GetOutput(), workOut);
    return true;
  }
  catch (const itk::ExceptionObject& ex)
  {
    error = ex.GetDescription() ? ex.GetDescription() : "ITK AntiAliasBinaryImageFilter failed.";
    return false;
  }
}

} // namespace

vtkStandardNewMacro(vtkSHYXImageAntiAlias);

vtkSHYXImageAntiAlias::vtkSHYXImageAntiAlias()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, vtkDataSetAttributes::SCALARS);
}

void vtkSHYXImageAntiAlias::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "BinarizeMode: " << this->BinarizeMode << "\n";
  os << indent << "Threshold: " << this->Threshold << "\n";
  os << indent << "NumberOfIterations: " << this->NumberOfIterations << "\n";
  os << indent << "MaximumRMSError: " << this->MaximumRMSError << "\n";
  os << indent << "UseImageSpacing: " << (this->UseImageSpacing ? "on" : "off") << "\n";
  os << indent << "ResampleFactor: " << this->ResampleFactor << "\n";
  os << indent << "ResampleInterpolation: " << this->ResampleInterpolation << "\n";
}

int vtkSHYXImageAntiAlias::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkImageData");
    return 1;
  }
  return 0;
}

int vtkSHYXImageAntiAlias::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkImageData");
    return 1;
  }
  return 0;
}

int vtkSHYXImageAntiAlias::RequestInformation(vtkInformation* request,
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if (!this->Superclass::RequestInformation(request, inputVector, outputVector))
  {
    return 0;
  }
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  vtkDataObject::SetPointDataActiveScalarInfo(outInfo, VTK_FLOAT, 1);

  int ext[6];
  double spacing[3];
  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), ext);
  inInfo->Get(vtkDataObject::SPACING(), spacing);
  const bool zIs2D = (ext[4] == ext[5]);
  ApplyResampleGeometry(this->ResampleFactor, zIs2D, ext, spacing);
  outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), ext, 6);
  outInfo->Set(vtkDataObject::SPACING(), spacing, 3);
  return 1;
}

int vtkSHYXImageAntiAlias::RequestUpdateExtent(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  int ext[6];
  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), ext);
  inInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), ext, 6);
  return 1;
}

int vtkSHYXImageAntiAlias::RequestData(vtkInformation* vtkNotUsed(request),
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
    vtkErrorMacro(<< "SHYX Image AntiAlias requires a 1-component point scalar array.");
    return 0;
  }
  if (inArr->GetNumberOfTuples() != input->GetNumberOfPoints())
  {
    vtkErrorMacro(<< "Selected array is not a point-centered scalar on this vtkImageData.");
    return 0;
  }

  const vtkIdType nInPts = input->GetNumberOfPoints();
  if (nInPts < 1)
  {
    output->CopyStructure(input);
    output->GetPointData()->PassData(input->GetPointData());
    output->GetCellData()->PassData(input->GetCellData());
    output->GetFieldData()->PassData(input->GetFieldData());
    return 1;
  }

  vtkNew<vtkImageData> workIn;
  workIn->CopyStructure(input);
  workIn->AllocateScalars(VTK_FLOAT, 1);
  float* workPtr = static_cast<float*>(workIn->GetScalarPointer());
  if (!workPtr)
  {
    vtkErrorMacro(<< "Cannot allocate float work image.");
    return 0;
  }

  const bool binarize = (this->BinarizeMode == THRESHOLD);
  const double thr = this->Threshold;
  vtkSMPTools::For(0, nInPts, [&](vtkIdType begin, vtkIdType end) {
    for (vtkIdType i = begin; i < end; ++i)
    {
      const double v = inArr->GetComponent(i, 0);
      workPtr[i] = binarize ? ((v >= thr) ? 1.f : 0.f) : static_cast<float>(v);
    }
  });

  vtkSmartPointer<vtkImageData> aaInput = workIn;
  vtkNew<vtkImageResample> resample;
  int inExt[6];
  input->GetExtent(inExt);
  const bool zIs2D = (inExt[4] == inExt[5]);
  const bool doResample = !NearlyIdentityFactor(this->ResampleFactor);
  if (doResample)
  {
    resample->SetInputData(workIn);
    const double f = this->ResampleFactor;
    const double fz = zIs2D ? 1.0 : f;
    resample->SetMagnificationFactors(f, f, fz);
    if (this->ResampleInterpolation == LINEAR)
    {
      resample->SetInterpolationModeToLinear();
    }
    else
    {
      resample->SetInterpolationModeToNearestNeighbor();
    }
    resample->Update();
    if (this->AbortExecute)
    {
      return 1;
    }
    aaInput = resample->GetOutput();
    if (!aaInput || aaInput->GetNumberOfPoints() < 1)
    {
      vtkErrorMacro(<< "Resample produced an empty image.");
      return 0;
    }
    if (binarize && this->ResampleInterpolation == LINEAR)
    {
      float* p = static_cast<float*>(aaInput->GetScalarPointer());
      const vtkIdType n = aaInput->GetNumberOfPoints();
      vtkSMPTools::For(0, n, [&](vtkIdType begin, vtkIdType end) {
        for (vtkIdType i = begin; i < end; ++i)
        {
          p[i] = (p[i] >= static_cast<float>(thr)) ? 1.f : 0.f;
        }
      });
    }
  }

  vtkNew<vtkImageData> workOut;
  std::string itkError;
  if (!RunWhitakerAntiAlias(aaInput, workOut, this->MaximumRMSError, this->NumberOfIterations,
        this->UseImageSpacing != 0, this, itkError))
  {
    vtkErrorMacro(<< "ITK AntiAliasBinaryImageFilter: " << itkError);
    return 0;
  }
  if (this->AbortExecute)
  {
    return 1;
  }

  vtkDataArray* itkScalars = workOut->GetPointData() ? workOut->GetPointData()->GetScalars() : nullptr;
  const vtkIdType nOutPts = workOut->GetNumberOfPoints();
  if (!itkScalars || itkScalars->GetNumberOfTuples() != nOutPts)
  {
    vtkErrorMacro(<< "ITK AntiAlias produced an unexpected image.");
    return 0;
  }

  output->CopyStructure(workOut);
  output->GetFieldData()->PassData(input->GetFieldData());
  if (!doResample)
  {
    output->GetPointData()->PassData(input->GetPointData());
    output->GetCellData()->PassData(input->GetCellData());
  }

  vtkSmartPointer<vtkDataArray> outArr;
  outArr.TakeReference(itkScalars->NewInstance());
  outArr->DeepCopy(itkScalars);
  outArr->SetName(inArr->GetName());
  output->GetPointData()->AddArray(outArr);
  output->GetPointData()->SetScalars(outArr);
  this->UpdateProgress(1.0);
  return 1;
}

VTK_ABI_NAMESPACE_END
