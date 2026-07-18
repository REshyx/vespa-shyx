# ParaView Programmable Source script (NOT Programmable Filter, NOT Python Shell).
#
# How to run:
#   1. Sources -> Programmable Source
#   2. In Properties: Output Data Set Type = vtkPolyData   <-- required
#   3. Paste THIS ENTIRE file into the Script box (replace any template)
#   4. Apply
#
# Yellow-ish wire AABB = OPT bounds * 10 (mesh coords)
# Blue arrows = OPT normals (already include Normal scale -1)
# Red verts = box centers

from vtkmodules.vtkCommonCore import vtkDoubleArray, vtkPoints, vtkUnsignedCharArray
from vtkmodules.vtkCommonDataModel import vtkCellArray, vtkPolyData
from vtkmodules.vtkFiltersCore import vtkAppendPolyData, vtkGlyph3D
from vtkmodules.vtkFiltersSources import vtkArrowSource

# --- paste from Inlet OPT (values as shown in the text box) ---
INLET_NX = [-0.102951, -0.814566, 0.977943, 0.582306, -0.959430, -0.103663, -0.391896, -0.789755, -0.492311, -0.375727]
INLET_NY = [-0.053700, -0.061990, -0.145084, -0.674338, -0.097126, -0.678319, -0.905325, -0.516110, -0.539336, 0.769464]
INLET_NZ = [0.993236, 0.576749, 0.150261, 0.454080, -0.264691, 0.727419, 0.163718, 0.331537, 0.683188, 0.516483]
INLET_XI = [5.828543, 9.550394, 4.068103, 4.282883, 9.676365, 5.615402, 6.989916, 9.678225, 8.743782, 6.922775]
INLET_YI = [1.979437, 0.632983, 5.958039, 6.476946, 3.259476, 6.434846, 6.456601, 5.318016, 4.775946, 0.593490]
INLET_ZI = [-23.817584, -19.942583, -18.196831, -18.365674, -18.698123, -18.501733, -17.934599, -18.937503, -19.918523, -20.200272]
INLET_XF = [7.307043, 9.691167, 4.108282, 4.398338, 9.717207, 5.731487, 7.092335, 9.744205, 8.829028, 6.956783]
INLET_YF = [3.294758, 0.952399, 6.117965, 6.587141, 3.392459, 6.536048, 6.508164, 5.415304, 4.863633, 0.614460]
INLET_ZF = [-23.680737, -19.736617, -18.033661, -18.231789, -18.566982, -18.407524, -17.806322, -18.831459, -19.840491, -20.164445]

BOUNDS_RESTORE = 10.0
ARROW_LENGTH = 2.0

n = len(INLET_NX)
for _a in (INLET_NY, INLET_NZ, INLET_XI, INLET_YI, INLET_ZI, INLET_XF, INLET_YF, INLET_ZF):
    if len(_a) != n:
        raise RuntimeError("INLET_* list lengths must match")

# --- AABB wireframes ---
box_pts = vtkPoints()
box_lines = vtkCellArray()
box_rgb = vtkUnsignedCharArray()
box_rgb.SetName("Colors")
box_rgb.SetNumberOfComponents(3)
inlet_ids = vtkDoubleArray()
inlet_ids.SetName("InletId")
edges = (
    (0, 1), (1, 3), (3, 2), (2, 0),
    (4, 5), (5, 7), (7, 6), (6, 4),
    (0, 4), (1, 5), (2, 6), (3, 7),
)
yellow = (255, 220, 40)
for i in range(n):
    x0, x1 = INLET_XI[i] * BOUNDS_RESTORE, INLET_XF[i] * BOUNDS_RESTORE
    y0, y1 = INLET_YI[i] * BOUNDS_RESTORE, INLET_YF[i] * BOUNDS_RESTORE
    z0, z1 = INLET_ZI[i] * BOUNDS_RESTORE, INLET_ZF[i] * BOUNDS_RESTORE
    corners = [
        (x0, y0, z0), (x1, y0, z0), (x0, y1, z0), (x1, y1, z0),
        (x0, y0, z1), (x1, y0, z1), (x0, y1, z1), (x1, y1, z1),
    ]
    base = box_pts.GetNumberOfPoints()
    for c in corners:
        box_pts.InsertNextPoint(*c)
    for a, b in edges:
        box_lines.InsertNextCell(2)
        box_lines.InsertCellPoint(base + a)
        box_lines.InsertCellPoint(base + b)
        box_rgb.InsertNextTuple3(*yellow)
        inlet_ids.InsertNextValue(float(i))

boxes = vtkPolyData()
boxes.SetPoints(box_pts)
boxes.SetLines(box_lines)
boxes.GetCellData().SetScalars(box_rgb)
boxes.GetCellData().AddArray(inlet_ids)

# --- centers + normals ---
c_pts = vtkPoints()
normals = vtkDoubleArray()
normals.SetName("Normal")
normals.SetNumberOfComponents(3)
c_verts = vtkCellArray()
c_rgb = vtkUnsignedCharArray()
c_rgb.SetName("Colors")
c_rgb.SetNumberOfComponents(3)
c_ids = vtkDoubleArray()
c_ids.SetName("InletId")
red = (255, 40, 40)
for i in range(n):
    cx = 0.5 * (INLET_XI[i] + INLET_XF[i]) * BOUNDS_RESTORE
    cy = 0.5 * (INLET_YI[i] + INLET_YF[i]) * BOUNDS_RESTORE
    cz = 0.5 * (INLET_ZI[i] + INLET_ZF[i]) * BOUNDS_RESTORE
    c_pts.InsertNextPoint(cx, cy, cz)
    normals.InsertNextTuple3(INLET_NX[i], INLET_NY[i], INLET_NZ[i])
    c_verts.InsertNextCell(1)
    c_verts.InsertCellPoint(i)
    c_rgb.InsertNextTuple3(*red)
    c_ids.InsertNextValue(float(i))

centers = vtkPolyData()
centers.SetPoints(c_pts)
centers.SetVerts(c_verts)
centers.GetPointData().AddArray(normals)
centers.GetPointData().SetVectors(normals)
centers.GetPointData().SetScalars(c_rgb)
centers.GetPointData().AddArray(c_ids)

# --- arrows ---
arrow = vtkArrowSource()
arrow.SetTipResolution(16)
arrow.SetShaftResolution(16)
glyph = vtkGlyph3D()
glyph.SetInputData(centers)
glyph.SetSourceConnection(arrow.GetOutputPort())
glyph.SetVectorModeToUseVector()
glyph.SetScaleModeToDataScalingOff()
glyph.SetScaleFactor(ARROW_LENGTH)
glyph.OrientOn()
glyph.Update()
arrows = vtkPolyData()
arrows.DeepCopy(glyph.GetOutput())
a_rgb = vtkUnsignedCharArray()
a_rgb.SetName("Colors")
a_rgb.SetNumberOfComponents(3)
blue = (50, 150, 255)
for _ in range(arrows.GetNumberOfCells()):
    a_rgb.InsertNextTuple3(*blue)
arrows.GetCellData().SetScalars(a_rgb)

# --- append to Programmable Source output ---
app = vtkAppendPolyData()
app.AddInputData(boxes)
app.AddInputData(centers)
app.AddInputData(arrows)
app.Update()

# Prefer generic output: set "Output Data Set Type" = vtkPolyData in the Source panel.
out = self.GetOutputDataObject(0)
if out is None:
    out = self.GetOutput()
if out is None:
    raise RuntimeError(
        "Programmable Source has no output. Set Output Data Set Type to "
        "'vtkPolyData', click Apply, then run again.")
out.DeepCopy(app.GetOutput())
