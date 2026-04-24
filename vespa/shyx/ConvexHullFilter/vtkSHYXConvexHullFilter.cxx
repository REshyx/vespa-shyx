#include "vtkSHYXConvexHullFilter.h"

#include "vtkCleanPolyData.h"
#include "vtkDataObject.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkDelaunay3D.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"
#include "vtkTriangleFilter.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXConvexHullFilter);

vtkSHYXConvexHullFilter::vtkSHYXConvexHullFilter()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

void vtkSHYXConvexHullFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CleanInputPoints: " << this->CleanInputPoints << "\n";
  os << indent << "CleanInputTolerance: " << this->CleanInputTolerance << "\n";
  os << indent << "CleanInputToleranceIsAbsolute: " << this->CleanInputToleranceIsAbsolute << "\n";
  os << indent << "Tolerance: " << this->Tolerance << "\n";
  os << indent << "Offset: " << this->Offset << "\n";
  os << indent << "BoundingTriangulation: " << this->BoundingTriangulation << "\n";
}

int vtkSHYXConvexHullFilter::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXConvexHullFilter::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

int vtkSHYXConvexHullFilter::RequestData(
  vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0], 0);
  vtkPolyData* output = vtkPolyData::GetData(outputVector, 0);
  if (!input || !output)
  {
    vtkErrorMacro(<< "Null input or output.");
    return 0;
  }

  const vtkIdType nPts = input->GetNumberOfPoints();
  if (nPts < 4)
  {
    vtkWarningMacro(<< "Convex hull needs at least four non-coplanar points; input has " << nPts << ".");
    output->Initialize();
    return 1;
  }

  vtkNew<vtkCleanPolyData> cleaner;
  vtkNew<vtkDelaunay3D> delaunay;
  if (this->CleanInputPoints)
  {
    cleaner->SetInputData(input);
    cleaner->SetToleranceIsAbsolute(this->CleanInputToleranceIsAbsolute);
    cleaner->SetTolerance(this->CleanInputTolerance);
    cleaner->Update();
    vtkPolyData* cleaned = cleaner->GetOutput();
    if (!cleaned || cleaned->GetNumberOfPoints() < 4)
    {
      vtkWarningMacro(<< "After cleaning, fewer than four points remain; output cleared.");
      output->Initialize();
      return 1;
    }
    delaunay->SetInputConnection(cleaner->GetOutputPort());
  }
  else
  {
    delaunay->SetInputData(input);
  }

  delaunay->SetAlpha(0.0);
  delaunay->SetTolerance(this->Tolerance);
  delaunay->SetOffset(this->Offset);
  delaunay->SetBoundingTriangulation(this->BoundingTriangulation);

  vtkNew<vtkDataSetSurfaceFilter> surface;
  surface->SetInputConnection(delaunay->GetOutputPort());

  vtkNew<vtkTriangleFilter> tri;
  tri->SetInputConnection(surface->GetOutputPort());
  tri->Update();

  vtkPolyData* outHull = tri->GetOutput();
  if (!outHull || outHull->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro(<< "Convex hull extraction produced empty output.");
    output->Initialize();
    return 1;
  }

  output->ShallowCopy(outHull);
  return 1;
}

VTK_ABI_NAMESPACE_END
