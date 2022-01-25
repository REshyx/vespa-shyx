#include <iostream>

#include "vtkNew.h"

#include "vtkCGALIsotropicRemesher.h"

int TestPMPInstance(int, char*[])
{
  vtkNew<vtkCGALIsotropicRemesher> rm;

  return 0;
}

