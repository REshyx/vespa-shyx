# Global import
import vespa
import vtkmodules.vtkCommonCore

# Specific import
from vespa import vtkCGALPMP

bo = vtkCGALPMP.vtkCGALBooleanOperation()
help(bo)

rm = vtkCGALPMP.vtkCGALIsotropicRemesher()
help(rm)

ce = vtkCGALPMP.vtkCGALMeshChecker()
help(ce)

de = vtkCGALPMP.vtkCGALMeshDeformation()
help(de)

su = vtkCGALPMP.vtkCGALMeshSubdivision()
help(su)

rf = vtkCGALPMP.vtkCGALRegionFairing()
help(rf)

pf = vtkCGALPMP.vtkCGALPatchFilling()
help(pf)
