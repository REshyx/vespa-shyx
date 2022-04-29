from vtk import vtkSphereSource, vtkShrinkPolyData
from vespa import vtkCGALDelaunay

sp = vtkSphereSource()

pd = vtkShrinkPolyData()
pd.SetInputConnection(sp.GetOutputPort())

d2 = vtkCGALDelaunay.vtkCGALDelaunay2()
d2.SetInputConnection(pd.GetOutputPort())
d2.Update()
