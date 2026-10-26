#include "vtkSHYXInteractiveStreamTracer.h"

#include "vtkDataArray.h"
#include "vtkDataObject.h"
#include "vtkDataSet.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkLineSource.h"
#include "vtkMath.h"
#include "vtkMinimalStandardRandomSequence.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPlaneSource.h"
#include "vtkPointSource.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkSphereSource.h"
#include "vtkStaticPointLocator.h"
#include "vtkStreamTracer.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

VTK_ABI_NAMESPACE_BEGIN

namespace
{
/**
 * Surface used for vertex snapping. A vtkPolyData input is used directly (it may be the
 * vessel/stent surface); anything else is passed through vtkDataSetSurfaceFilter so a volume
 * mesh snaps to its outer wall instead of interior nodes.
 */
vtkPolyData* ExtractSnapSurface(vtkDataSet* input, vtkDataSetSurfaceFilter* surfaceFilter)
{
  if (!input)
  {
    return nullptr;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(input))
  {
    if (pd->GetNumberOfPoints() > 0)
    {
      return pd;
    }
    return nullptr;
  }
  if (!surfaceFilter)
  {
    return nullptr;
  }
  surfaceFilter->SetInputData(input);
  surfaceFilter->Update();
  return surfaceFilter->GetOutput();
}
}

vtkStandardNewMacro(vtkSHYXInteractiveStreamTracer);

//------------------------------------------------------------------------------
vtkSHYXInteractiveStreamTracer::vtkSHYXInteractiveStreamTracer()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(2);
}

//------------------------------------------------------------------------------
void vtkSHYXInteractiveStreamTracer::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SeedShape: " << this->SeedShape << "\n";
  os << indent << "LinePoint1: (" << this->LinePoint1[0] << ", " << this->LinePoint1[1] << ", "
     << this->LinePoint1[2] << ")\n";
  os << indent << "LinePoint2: (" << this->LinePoint2[0] << ", " << this->LinePoint2[1] << ", "
     << this->LinePoint2[2] << ")\n";
  os << indent << "LineResolution: " << this->LineResolution << "\n";
  os << indent << "LineSnapPoint1: " << (this->LineSnapPoint1 ? "On" : "Off") << "\n";
  os << indent << "LineSnapPoint2: " << (this->LineSnapPoint2 ? "On" : "Off") << "\n";
  os << indent << "SphereCenter: (" << this->SphereCenter[0] << ", " << this->SphereCenter[1]
     << ", " << this->SphereCenter[2] << ")\n";
  os << indent << "SphereRadius: " << this->SphereRadius << "\n";
  os << indent << "SphereSeedMode: " << this->SphereSeedMode << "\n";
  os << indent << "SphereInteriorPointCount: " << this->SphereInteriorPointCount << "\n";
  os << indent << "SphereRandomSeed: " << this->SphereRandomSeed << "\n";
  os << indent << "SphereThetaResolution: " << this->SphereThetaResolution << "\n";
  os << indent << "SpherePhiResolution: " << this->SpherePhiResolution << "\n";
  os << indent << "SphereSnapCenter: " << (this->SphereSnapCenter ? "On" : "Off") << "\n";
  os << indent << "RectangleOrigin: (" << this->RectangleOrigin[0] << ", "
     << this->RectangleOrigin[1] << ", " << this->RectangleOrigin[2] << ")\n";
  os << indent << "RectanglePoint1: (" << this->RectanglePoint1[0] << ", "
     << this->RectanglePoint1[1] << ", " << this->RectanglePoint1[2] << ")\n";
  os << indent << "RectanglePoint2: (" << this->RectanglePoint2[0] << ", "
     << this->RectanglePoint2[1] << ", " << this->RectanglePoint2[2] << ")\n";
  os << indent << "RectangleResolutionX: " << this->RectangleResolutionX << "\n";
  os << indent << "RectangleResolutionY: " << this->RectangleResolutionY << "\n";
  os << indent << "RectangleSnapOrigin: " << (this->RectangleSnapOrigin ? "On" : "Off") << "\n";
  os << indent << "IntegrationDirection: " << this->IntegrationDirection << "\n";
  os << indent << "IntegratorType: " << this->IntegratorType << "\n";
  os << indent << "IntegrationStepUnit: " << this->IntegrationStepUnit << "\n";
  os << indent << "InitialIntegrationStep: " << this->InitialIntegrationStep << "\n";
  os << indent << "MaximumPropagation: " << this->MaximumPropagation << "\n";
  os << indent << "MaximumNumberOfSteps: " << this->MaximumNumberOfSteps << "\n";
  os << indent << "TerminalSpeed: " << this->TerminalSpeed << "\n";
  os << indent << "InterpolatorType: " << this->InterpolatorType << "\n";
  os << indent << "MaximumError: " << this->MaximumError << "\n";
  os << indent << "ComputeVorticity: " << (this->ComputeVorticity ? "On" : "Off") << "\n";
}

//------------------------------------------------------------------------------
int vtkSHYXInteractiveStreamTracer::FillInputPortInformation(int port, vtkInformation* info)
{
  if (port == 0)
  {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkSHYXInteractiveStreamTracer::FillOutputPortInformation(int port, vtkInformation* info)
{
  if (port == 0 || port == 1)
  {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
    return 1;
  }
  return 0;
}

//------------------------------------------------------------------------------
bool vtkSHYXInteractiveStreamTracer::SnapPointsToSurface(
  vtkDataSet* input, double* points, int count, const int* enabled)
{
  if (!input || !points || count <= 0)
  {
    return false;
  }

  bool anyEnabled = false;
  for (int i = 0; i < count; ++i)
  {
    if (!enabled || enabled[i])
    {
      anyEnabled = true;
      break;
    }
  }
  if (!anyEnabled)
  {
    return false;
  }

  vtkNew<vtkDataSetSurfaceFilter> surfaceFilter;
  vtkPolyData* surface = ExtractSnapSurface(input, surfaceFilter);
  if (!surface || surface->GetNumberOfPoints() == 0)
  {
    return false;
  }

  vtkNew<vtkStaticPointLocator> locator;
  locator->SetDataSet(surface);
  locator->BuildLocator();

  bool changed = false;
  for (int i = 0; i < count; ++i)
  {
    if (enabled && !enabled[i])
    {
      continue;
    }
    double* p = points + 3 * i;
    const vtkIdType id = locator->FindClosestPoint(p);
    if (id < 0)
    {
      continue;
    }
    double q[3];
    surface->GetPoint(id, q);
    if (q[0] != p[0] || q[1] != p[1] || q[2] != p[2])
    {
      p[0] = q[0];
      p[1] = q[1];
      p[2] = q[2];
      changed = true;
    }
  }
  return changed;
}

//------------------------------------------------------------------------------
bool vtkSHYXInteractiveStreamTracer::SnapPointToSurface(
  vtkDataSet* input, const double in[3], double out[3])
{
  double p[3] = { in[0], in[1], in[2] };
  const int enabled = 1;
  vtkSHYXInteractiveStreamTracer::SnapPointsToSurface(input, p, 1, &enabled);
  out[0] = p[0];
  out[1] = p[1];
  out[2] = p[2];
  return true;
}

//------------------------------------------------------------------------------
int vtkSHYXInteractiveStreamTracer::RequestData(
  vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
  vtkPolyData* streamlinesOut = vtkPolyData::GetData(outputVector, 0);
  vtkPolyData* seedsOut = vtkPolyData::GetData(outputVector, 1);

  if (!input || !streamlinesOut || !seedsOut)
  {
    vtkErrorMacro("A flow field (vtkDataSet) is required on input port 0.");
    return 0;
  }

  vtkDataArray* velocity = this->GetInputArrayToProcess(0, inputVector);
  if (!velocity)
  {
    vtkErrorMacro("Could not access the input vector field. Pick a 3-component array in Vectors.");
    return 0;
  }
  if (velocity->GetNumberOfComponents() != 3)
  {
    vtkErrorMacro("The selected Vectors array must have 3 components.");
    return 0;
  }

  // -------------------------------------------------------------------------
  // Build the seed geometry (with optional surface-vertex snapping)
  // -------------------------------------------------------------------------
  vtkSmartPointer<vtkPolyDataAlgorithm> seedSource;

  switch (this->SeedShape)
  {
    case vtkSHYXInteractiveStreamTracer::Line:
    {
      double p1[3] = { this->LinePoint1[0], this->LinePoint1[1], this->LinePoint1[2] };
      double p2[3] = { this->LinePoint2[0], this->LinePoint2[1], this->LinePoint2[2] };
      double pts[6] = { p1[0], p1[1], p1[2], p2[0], p2[1], p2[2] };
      const int enabled[2] = { this->LineSnapPoint1 ? 1 : 0, this->LineSnapPoint2 ? 1 : 0 };
      if (enabled[0] || enabled[1])
      {
        vtkSHYXInteractiveStreamTracer::SnapPointsToSurface(input, pts, 2, enabled);
      }

      vtkNew<vtkLineSource> line;
      line->SetPoint1(pts[0], pts[1], pts[2]);
      line->SetPoint2(pts[3], pts[4], pts[5]);
      line->SetResolution(this->LineResolution);
      seedSource = line.GetPointer();
      break;
    }
    case vtkSHYXInteractiveStreamTracer::Sphere:
    {
      double center[3] = {
        this->SphereCenter[0], this->SphereCenter[1], this->SphereCenter[2]
      };
      if (this->SphereSnapCenter)
      {
        const int enabled = 1;
        vtkSHYXInteractiveStreamTracer::SnapPointsToSurface(input, center, 1, &enabled);
      }

      double radius = this->SphereRadius;
      if (!(radius > 0.0) || !std::isfinite(radius))
      {
        double b[6];
        input->GetBounds(b);
        radius = 0.15 * std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 });
      }

      vtkNew<vtkSphereSource> sphere;
      sphere->SetCenter(center);
      sphere->SetRadius(radius);
      if (this->SphereSeedMode == vtkSHYXInteractiveStreamTracer::SphereInterior)
      {
        // Uniform inside the ball: rho = R * u^(1/3). Seeded so repeated Applies match.
        vtkNew<vtkMinimalStandardRandomSequence> sequence;
        sequence->SetSeed(this->SphereRandomSeed);
        vtkNew<vtkPointSource> points;
        points->SetCenter(center);
        points->SetRadius(radius);
        points->SetNumberOfPoints(this->SphereInteriorPointCount);
        points->SetDistributionToUniform();
        points->SetRandomSequence(sequence);
        seedSource = points.GetPointer();
        break;
      }
      // Surface mode: the vertex count follows the theta/phi tessellation, not a point count.
      sphere->SetThetaResolution(this->SphereThetaResolution);
      sphere->SetPhiResolution(this->SpherePhiResolution);
      seedSource = sphere.GetPointer();
      break;
    }
    case vtkSHYXInteractiveStreamTracer::Rectangle:
    default:
    {
      double origin[3] = {
        this->RectangleOrigin[0], this->RectangleOrigin[1], this->RectangleOrigin[2]
      };
      double point1[3] = {
        this->RectanglePoint1[0], this->RectanglePoint1[1], this->RectanglePoint1[2]
      };
      double point2[3] = {
        this->RectanglePoint2[0], this->RectanglePoint2[1], this->RectanglePoint2[2]
      };

      if (this->RectangleSnapOrigin)
      {
        double snapped[3] = { origin[0], origin[1], origin[2] };
        const int enabled = 1;
        if (vtkSHYXInteractiveStreamTracer::SnapPointsToSurface(input, snapped, 1, &enabled))
        {
          const double delta[3] = {
            snapped[0] - origin[0], snapped[1] - origin[1], snapped[2] - origin[2]
          };
          origin[0] = snapped[0];
          origin[1] = snapped[1];
          origin[2] = snapped[2];
          point1[0] += delta[0];
          point1[1] += delta[1];
          point1[2] += delta[2];
          point2[0] += delta[0];
          point2[1] += delta[1];
          point2[2] += delta[2];
        }
      }

      // Degenerate rectangle (Point1/Point2 collinear with Origin): fall back to a plane in XY.
      double e1[3] = { point1[0] - origin[0], point1[1] - origin[1], point1[2] - origin[2] };
      double e2[3] = { point2[0] - origin[0], point2[1] - origin[1], point2[2] - origin[2] };
      double n[3];
      vtkMath::Cross(e1, e2, n);
      if (vtkMath::Normalize(n) < 1e-12)
      {
        double b[6];
        input->GetBounds(b);
        const double size =
          std::max({ b[1] - b[0], b[3] - b[2], b[5] - b[4], 1e-6 }) * 0.25;
        point1[0] = origin[0] + size;
        point1[1] = origin[1];
        point1[2] = origin[2];
        point2[0] = origin[0];
        point2[1] = origin[1] + size;
        point2[2] = origin[2];
      }

      vtkNew<vtkPlaneSource> plane;
      plane->SetOrigin(origin);
      plane->SetPoint1(point1);
      plane->SetPoint2(point2);
      plane->SetResolution(this->RectangleResolutionX, this->RectangleResolutionY);
      seedSource = plane.GetPointer();
      break;
    }
  }

  if (!seedSource)
  {
    vtkErrorMacro("Could not build the seed geometry.");
    return 0;
  }

  seedSource->Update();
  vtkPolyData* seeds = vtkPolyData::SafeDownCast(seedSource->GetOutput());
  if (!seeds || seeds->GetNumberOfPoints() == 0)
  {
    vtkWarningMacro("Seed geometry has no points; outputs cleared.");
    streamlinesOut->Initialize();
    seedsOut->Initialize();
    return 1;
  }

  seedsOut->ShallowCopy(seeds);

  // -------------------------------------------------------------------------
  // vtkStreamTracer from the seed geometry
  // -------------------------------------------------------------------------
  vtkNew<vtkStreamTracer> tracer;
  tracer->SetInputData(input);
  tracer->SetSourceData(seeds);
  tracer->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, velocity->GetName());
  tracer->SetIntegrationDirection(this->IntegrationDirection);
  tracer->SetIntegratorType(this->IntegratorType);
  tracer->SetIntegrationStepUnit(this->IntegrationStepUnit);
  tracer->SetInitialIntegrationStep(this->InitialIntegrationStep);
  tracer->SetMaximumPropagation(this->MaximumPropagation);
  tracer->SetMaximumNumberOfSteps(this->MaximumNumberOfSteps);
  tracer->SetTerminalSpeed(this->TerminalSpeed);
  tracer->SetInterpolatorType(this->InterpolatorType);
  tracer->SetMaximumError(this->MaximumError);
  tracer->SetComputeVorticity(this->ComputeVorticity != 0);
  tracer->Update();

  vtkPolyData* lines = tracer->GetOutput();
  if (!lines)
  {
    streamlinesOut->Initialize();
    return 1;
  }
  streamlinesOut->ShallowCopy(lines);
  return 1;
}

VTK_ABI_NAMESPACE_END
