# Global import
import vtkCGAL
import vtkmodules.vtkCommonCore

# Specific import
from vtkCGAL import vtkCGALPMP

bo = vtkCGALPMP.vtkCGALBooleanOperation()
help(bo)

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
help(rm)

rf = vtkCGALPMP.vtkCGALRegionFairing()
help(rf)
