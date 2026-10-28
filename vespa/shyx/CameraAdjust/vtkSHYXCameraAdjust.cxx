#include "vtkSHYXCameraAdjust.h"

#include "vtkCellArray.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkStringArray.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkSHYXCameraAdjust);

namespace
{
std::vector<std::string> SplitLines(const char* text)
{
  std::vector<std::string> lines;
  if (!text || text[0] == '\0')
  {
    return lines;
  }
  std::stringstream stream(text);
  std::string       line;
  while (std::getline(stream, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

double ToDouble(const std::string& token, double fallback)
{
  char*        end   = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end == token.c_str())
  {
    return fallback;
  }
  return value;
}

/** Parse "a,b,c" (missing values keep the fallback). */
void ParseTriple(const std::string& line, double out[3], const double fallback[3])
{
  out[0] = fallback[0];
  out[1] = fallback[1];
  out[2] = fallback[2];
  std::stringstream stream(line);
  std::string       token;
  for (int i = 0; i < 3 && std::getline(stream, token, ','); ++i)
  {
    out[i] = ToDouble(token, fallback[i]);
  }
}

void ParseLens(
  const std::string& line, double& viewAngle, double& parallelScale, int& parallelProjection)
{
  viewAngle          = 30.0;
  parallelScale      = 1.0;
  parallelProjection = 0;
  std::stringstream stream(line);
  std::string       token;
  if (std::getline(stream, token, ','))
  {
    viewAngle = ToDouble(token, viewAngle);
  }
  if (std::getline(stream, token, ','))
  {
    parallelScale = ToDouble(token, parallelScale);
  }
  if (std::getline(stream, token, ','))
  {
    parallelProjection = ToDouble(token, 0.0) != 0.0 ? 1 : 0;
  }
}
} // namespace

//------------------------------------------------------------------------------
vtkSHYXCameraAdjust::vtkSHYXCameraAdjust()
{
  this->SetNumberOfInputPorts(0);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkSHYXCameraAdjust::~vtkSHYXCameraAdjust()
{
  this->SetCameraNames(nullptr);
  this->SetCameraPositions(nullptr);
  this->SetCameraFocalPoints(nullptr);
  this->SetCameraViewUps(nullptr);
  this->SetCameraLens(nullptr);
}

//------------------------------------------------------------------------------
void vtkSHYXCameraAdjust::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CameraNames: " << (this->CameraNames ? this->CameraNames : "(none)") << "\n";
  os << indent << "CameraPositions: " << (this->CameraPositions ? this->CameraPositions : "(none)")
     << "\n";
  os << indent
     << "CameraFocalPoints: " << (this->CameraFocalPoints ? this->CameraFocalPoints : "(none)")
     << "\n";
  os << indent << "CameraViewUps: " << (this->CameraViewUps ? this->CameraViewUps : "(none)")
     << "\n";
  os << indent << "CameraLens: " << (this->CameraLens ? this->CameraLens : "(none)") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXCameraAdjust::RequestData(
  vtkInformation*, vtkInformationVector**, vtkInformationVector* outputVector)
{
  vtkPolyData* output = vtkPolyData::GetData(outputVector);
  output->Initialize();

  const std::vector<std::string> names     = SplitLines(this->CameraNames);
  const std::vector<std::string> positions = SplitLines(this->CameraPositions);
  const std::vector<std::string> focals    = SplitLines(this->CameraFocalPoints);
  const std::vector<std::string> ups       = SplitLines(this->CameraViewUps);
  const std::vector<std::string> lenses    = SplitLines(this->CameraLens);
  const size_t                   nRows =
    std::max(std::max(names.size(), positions.size()), std::max(focals.size(), ups.size()));
  const size_t n = std::max(nRows, lenses.size());

  vtkNew<vtkPoints>      points;
  vtkNew<vtkCellArray>   verts;
  vtkNew<vtkIntArray>    indexArray;
  vtkNew<vtkStringArray> nameArray;
  vtkNew<vtkDoubleArray> positionArray;
  vtkNew<vtkDoubleArray> focalArray;
  vtkNew<vtkDoubleArray> upArray;
  vtkNew<vtkDoubleArray> lensArray;

  indexArray->SetName("CameraIndex");
  nameArray->SetName("CameraName");
  positionArray->SetName("CameraPositions");
  focalArray->SetName("CameraFocalPoints");
  upArray->SetName("CameraViewUps");
  lensArray->SetName("CameraLens");
  positionArray->SetNumberOfComponents(3);
  focalArray->SetNumberOfComponents(3);
  upArray->SetNumberOfComponents(3);
  lensArray->SetNumberOfComponents(3);

  const double defaultPosition[3] = { 0.0, 0.0, 0.0 };
  const double defaultFocal[3]    = { 0.0, 0.0, -1.0 };
  const double defaultUp[3]       = { 0.0, 1.0, 0.0 };

  for (size_t i = 0; i < n; ++i)
  {
    const bool hasName     = i < names.size();
    const bool hasPosition = i < positions.size();
    const bool hasFocal    = i < focals.size();
    const bool hasUp       = i < ups.size();
    const bool hasLens     = i < lenses.size();
    if (!hasName && !hasPosition && !hasFocal && !hasUp && !hasLens)
    {
      continue;
    }

    double position[3];
    double focal[3];
    double up[3];
    if (hasPosition)
    {
      ParseTriple(positions[i], position, defaultPosition);
    }
    else
    {
      ParseTriple(std::string(), position, defaultPosition);
    }
    if (hasFocal)
    {
      ParseTriple(focals[i], focal, defaultFocal);
    }
    else
    {
      ParseTriple(std::string(), focal, defaultFocal);
    }
    if (hasUp)
    {
      ParseTriple(ups[i], up, defaultUp);
    }
    else
    {
      ParseTriple(std::string(), up, defaultUp);
    }

    double viewAngle          = 30.0;
    double parallelScale      = 1.0;
    int    parallelProjection = 0;
    if (hasLens)
    {
      ParseLens(lenses[i], viewAngle, parallelScale, parallelProjection);
    }

    const vtkIdType id = points->InsertNextPoint(position);
    verts->InsertNextCell(1);
    verts->InsertCellPoint(id);

    indexArray->InsertNextValue(static_cast<int>(i));
    nameArray->InsertNextValue(hasName ? names[i] : std::string());

    positionArray->InsertNextTuple3(position[0], position[1], position[2]);
    focalArray->InsertNextTuple3(focal[0], focal[1], focal[2]);
    upArray->InsertNextTuple3(up[0], up[1], up[2]);
    lensArray->InsertNextTuple3(viewAngle, parallelScale, static_cast<double>(parallelProjection));
  }

  output->SetPoints(points);
  output->SetVerts(verts);
  output->GetPointData()->AddArray(indexArray);
  output->GetPointData()->AddArray(nameArray);
  output->GetFieldData()->AddArray(positionArray);
  output->GetFieldData()->AddArray(focalArray);
  output->GetFieldData()->AddArray(upArray);
  output->GetFieldData()->AddArray(lensArray);
  return 1;
}

VTK_ABI_NAMESPACE_END
