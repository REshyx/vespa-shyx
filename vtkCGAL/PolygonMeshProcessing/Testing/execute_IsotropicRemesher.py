from vtk import vtkSphereSource
from vtkCGAL import vtkCGALPMP

pd = vtkSphereSource()

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
rm.SetInputConnection(pd.GetOutputPort())
rm.SetTargetLength(0.1)
rm.Update()
