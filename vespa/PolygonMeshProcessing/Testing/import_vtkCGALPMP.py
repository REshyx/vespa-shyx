# Global import
import vespa
import vtkmodules.vtkCommonCore

# Specific import
from vespa import vtkCGALPMP

bo = vtkCGALPMP.vtkCGALBooleanOperation()
help(bo)

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
help(rm)

rf = vtkCGALPMP.vtkCGALRegionFairing()
help(rf)

pf = vtkCGALPMP.vtkCGALPatchFilling()
help(pf)
