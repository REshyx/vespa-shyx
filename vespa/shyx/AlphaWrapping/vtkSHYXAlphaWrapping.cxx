#include "vtkSHYXAlphaWrapping.h"

#include "vtkCGALAlphaWrapping.h"

#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>

#include <algorithm>

vtkStandardNewMacro(vtkSHYXAlphaWrapping);

namespace
{

constexpr double kAlphaScale = 0.05;
constexpr double kOffsetScale = 0.03;

double LongestAabbSide(vtkPolyData* mesh)
{
  double b[6];
  mesh->GetBounds(b);
  const double dx = b[1] - b[0];
  const double dy = b[3] - b[2];
  const double dz = b[5] - b[4];
  return std::max(dx, std::max(dy, dz));
}

double ResolveLength(double value, double longest, double scale)
{
  if (value > 0.0)
  {
    return value;
  }
  return scale * longest;
}

void WriteUsedLength(vtkPolyData* output, const char* name, double value)
{
  vtkNew<vtkDoubleArray> arr;
  arr->SetName(name);
  arr->SetNumberOfTuples(1);
  arr->SetValue(0, value);
  output->GetFieldData()->RemoveArray(name);
  output->GetFieldData()->AddArray(arr);
}

} // namespace

//------------------------------------------------------------------------------
void vtkSHYXAlphaWrapping::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Alpha: " << this->Alpha << "\n";
  os << indent << "Offset: " << this->Offset << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXAlphaWrapping::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkPolyData* input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  if (!input || !output)
  {
    vtkErrorMacro("Missing input or output vtkPolyData.");
    return 0;
  }
  if (input->GetNumberOfPoints() < 1)
  {
    vtkErrorMacro("Input must have at least one point.");
    return 0;
  }

  const double longest = LongestAabbSide(input);
  if (!(longest > 0.0) && (this->Alpha <= 0.0 || this->Offset <= 0.0))
  {
    vtkErrorMacro("Input bounding box is degenerate; set positive Alpha and Offset.");
    return 0;
  }

  const double alpha = ResolveLength(this->Alpha, longest, kAlphaScale);
  const double offset = ResolveLength(this->Offset, longest, kOffsetScale);
  if (!(alpha > 0.0) || !(offset > 0.0))
  {
    vtkErrorMacro("Resolved Alpha (" << alpha << ") and Offset (" << offset
                                     << ") must be positive.");
    return 0;
  }

  vtkNew<vtkCGALAlphaWrapping> aw;
  aw->SetInputData(input);
  aw->SetAbsoluteThresholds(true);
  aw->SetAlpha(alpha);
  aw->SetOffset(offset);
  aw->SetUpdateAttributes(this->GetUpdateAttributes());
  aw->Update();

  vtkPolyData* wrapped = aw->GetOutput();
  if (!wrapped || wrapped->GetNumberOfPoints() < 1)
  {
    vtkErrorMacro("Alpha wrapping produced an empty mesh.");
    return 0;
  }

  output->ShallowCopy(wrapped);
  WriteUsedLength(output, "SHYXAlphaWrappingAlpha", alpha);
  WriteUsedLength(output, "SHYXAlphaWrappingOffset", offset);
  return 1;
}
