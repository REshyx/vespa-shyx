from vtk import vtkPlaneSource
from vtkCGAL import vtkCGALPMP

pd = vtkPlaneSource()

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
rm.SetInputConnection(pd.GetOutputPort())
rm.Update()
