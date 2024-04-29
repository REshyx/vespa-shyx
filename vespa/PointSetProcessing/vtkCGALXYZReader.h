/**
 * @class   vtkCGALXYZReader
 * @brief   Normal estimation from an unorganized point set.
 *
 * vtkCGALXYZReader reads point clouds (.las, .off, .ply, .xyz) using
 * CGAL point cloud data reader functionality.
 */

#ifndef vtkCGALXYZReader_h
#define vtkCGALXYZReader_h

#include "vtkCGALPSPModule.h" // For export macro
#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkDataArraySelection.h" // for ivars
#include <string>                  //For std::string

class vtkDataArraySelection;
class vtkDataSet;
class vtkDoubleArray;
class vtkIntArray;
class vtkStdString;
class vtkStringArray;
class vtkAIMToolsPrivate;

class VTKCGALPSP_EXPORT vtkCGALXYZReader : public vtkCGALPolyDataAlgorithm
{
public:
  vtkTypeMacro(vtkCGALXYZReader, vtkCGALPolyDataAlgorithm);
  static vtkCGALXYZReader *New();
  void PrintSelf(ostream &os, vtkIndent indent) override;

  virtual void SetFileName(VTK_FILEPATH const char *filename);
  vtkGetFilePathMacro(FileName);

protected:
  vtkCGALXYZReader();
  ~vtkCGALXYZReader() override;

  int RequestData(vtkInformation *request, vtkInformationVector **inputVector,
                  vtkInformationVector *outputVector) override;

  int FillOutputPortInformation(int port, vtkInformation *info) override;

private:
  vtkCGALXYZReader(const vtkCGALXYZReader &) = delete;
  void operator=(const vtkCGALXYZReader &) = delete;

  char *FileName;
};

#endif // vtkCGALXYZReader_h
