from vtk import vtkPlaneSource, vtkShrinkPolyData
from vespa import vtkCGALDelaunay

ps = vtkPlaneSource()
ps.SetXResolution(5)
ps.SetYResolution(5)

pd = vtkShrinkPolyData()
pd.SetInputConnection(ps.GetOutputPort())

d2 = vtkCGALDelaunay.vtkCGALDelaunay2()
d2.SetInputConnection(pd.GetOutputPort())
d2.Update()
