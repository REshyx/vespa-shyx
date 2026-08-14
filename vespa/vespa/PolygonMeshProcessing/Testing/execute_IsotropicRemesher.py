from vtk import vtkSphereSource
from vespa import vtkCGALPMP

pd = vtkSphereSource()

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
rm.SetInputConnection(pd.GetOutputPort())
rm.SetTargetLength(0.01)
rm.SetProtectAngle(90)
rm.Update()
