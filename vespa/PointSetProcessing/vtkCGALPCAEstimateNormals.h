/**
 * @class   vtkCGALPCAEstimateNormals
 * @brief   Poisson surface reconstruction.
 *
 * vtkCGALPCAEstimateNormals 
 * adapted from
 * https://doc.cgal.org/latest/Poisson_surface_reconstruction_3/Poisson_surface_reconstruction_3_2poisson_reconstruction_function_8cpp-example.html
 */

#ifndef vtkCGALPCAEstimateNormals_h
#define vtkCGALPCAEstimateNormals_h

#include "vtkCGALPolyDataAlgorithm.h"

#include "vtkCGALPSPModule.h" // For export macro

class VTKCGALPSP_EXPORT vtkCGALPCAEstimateNormals : public vtkCGALPolyDataAlgorithm
{
public:
  static vtkCGALPCAEstimateNormals* New();
  vtkTypeMacro(vtkCGALPCAEstimateNormals, vtkCGALPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /**
   * Get/set the smoothing method (1 - tangential relaxation; 2 - angle and area smoothing).
   * Default is 1.
   **/
  vtkGetMacro(Neighborhood, unsigned int);
  vtkSetMacro(Neighborhood, unsigned int);
  ///@}
  ///@{

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(NumberOfNeighbors, unsigned int);
  vtkSetMacro(NumberOfNeighbors, unsigned int);
  ///@}

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(OrientNormals, bool);
  vtkSetMacro(OrientNormals, bool);

  /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(RadiusFactor, double);
  vtkSetMacro(RadiusFactor, double);

  ///@}
    /**
   * Get/set the number of iterations used in the smoothing process.
   * Default is 10.
   **/
  vtkGetMacro(DeleteUnoriented, bool);
  vtkSetMacro(DeleteUnoriented, bool);
  ///@}
protected:
  vtkCGALPCAEstimateNormals()           = default;
  ~vtkCGALPCAEstimateNormals() override = default;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

private:
  vtkCGALPCAEstimateNormals(const vtkCGALPCAEstimateNormals&) = delete;
  void operator=(const vtkCGALPCAEstimateNormals&)       = delete;

  unsigned int Neighborhood;
  unsigned int NumberOfNeighbors;
  double RadiusFactor;
  bool OrientNormals;
  bool DeleteUnoriented;
};

#endif
