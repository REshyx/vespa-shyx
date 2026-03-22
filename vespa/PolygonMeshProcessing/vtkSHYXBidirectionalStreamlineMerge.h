/**
 * @class   vtkSHYXBidirectionalStreamlineMerge
 * @brief   Merge bidirectional streamline segments that share the same SeedIds value.
 *
 * Bidirectional stream tracing usually emits two polylines per seed (forward and backward integration
 * order in the file). This filter groups \c LINE / \c POLY_LINE cells by a \c SeedIds array (cell data
 * by default, or point data sampled at the first point of each polyline). For each seed that has
 * exactly two segments, the **second** segment in input cell order is treated as the backward branch:
 * it is reversed, concatenated with the first segment, and the duplicate seed vertex (shared endpoint)
 * is removed when point ids match. Single-segment seeds are copied unchanged.
 *
 * Optionally runs vtkGenerateIds to add a point-data array of output point indices (0 .. N-1).
 * Optionally computes a cumulative sum along each merged polyline (vertex order) from a chosen
 * input point array: each segment adds \f$ \tfrac12(f_i+f_{i+1}) \f$ (trapezoid in index; **not**
 * multiplied by geometric segment length). Scalars use the value; vectors use magnitude.
 *
 * @sa vtkStreamTracer, vtkGenerateIds
 */

#ifndef vtkSHYXBidirectionalStreamlineMerge_h
#define vtkSHYXBidirectionalStreamlineMerge_h

#include "vtkCGALPMPModule.h"
#include "vtkPolyDataAlgorithm.h"

#include <string>

class VTKCGALPMP_EXPORT vtkSHYXBidirectionalStreamlineMerge : public vtkPolyDataAlgorithm
{
public:
  static vtkSHYXBidirectionalStreamlineMerge* New();
  vtkTypeMacro(vtkSHYXBidirectionalStreamlineMerge, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  /** Name of the array holding seed identifiers (int/id-like scalars). Default "SeedIds". */
  vtkGetMacro(SeedIdsArrayName, std::string);
  vtkSetMacro(SeedIdsArrayName, std::string);
  ///@}

  ///@{
  /**
   * If true (default), \c SeedIds is read from cell data (one value per polyline).
   * If false, the first point of each polyline supplies \c SeedIds from point data.
   */
  vtkGetMacro(SeedIdsOnCells, int);
  vtkSetMacro(SeedIdsOnCells, int);
  vtkBooleanMacro(SeedIdsOnCells, int);
  ///@}

  ///@{
  /** When on (default), run vtkGenerateIds and store output point indices in \c PointIdsArrayName. */
  vtkGetMacro(GeneratePointIdArray, int);
  vtkSetMacro(GeneratePointIdArray, int);
  vtkBooleanMacro(GeneratePointIdArray, int);
  ///@}

  ///@{
  /** Point-data array name created by vtkGenerateIds. Default "PointIds". */
  vtkGetMacro(PointIdsArrayName, std::string);
  vtkSetMacro(PointIdsArrayName, std::string);
  ///@}

  ///@{
  /** If on, append cumulative along-line sum (trapezoid in index, no × segment length). Default off. */
  vtkGetMacro(ComputeArcLengthIntegral, int);
  vtkSetMacro(ComputeArcLengthIntegral, int);
  vtkBooleanMacro(ComputeArcLengthIntegral, int);
  ///@}

  ///@{
  /** Input point-data array to integrate (scalar; vectors use magnitude per point). Used when integral is on. */
  vtkGetMacro(IntegrandArrayName, std::string);
  vtkSetMacro(IntegrandArrayName, std::string);
  ///@}

  ///@{
  /** Output point-data scalar name for the cumulative integral. Default "StreamlineIntegral". */
  vtkGetMacro(IntegralArrayName, std::string);
  vtkSetMacro(IntegralArrayName, std::string);
  ///@}

protected:
  vtkSHYXBidirectionalStreamlineMerge();
  ~vtkSHYXBidirectionalStreamlineMerge() override = default;

  int FillInputPortInformation(int port, vtkInformation* info) override;
  int FillOutputPortInformation(int port, vtkInformation* info) override;
  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

  std::string SeedIdsArrayName{ "SeedIds" };
  int         SeedIdsOnCells = 1;
  int         GeneratePointIdArray = 1;
  std::string PointIdsArrayName{ "PointIds" };
  int         ComputeArcLengthIntegral = 0;
  std::string IntegrandArrayName;
  std::string IntegralArrayName{ "StreamlineIntegral" };

private:
  vtkSHYXBidirectionalStreamlineMerge(const vtkSHYXBidirectionalStreamlineMerge&) = delete;
  void operator=(const vtkSHYXBidirectionalStreamlineMerge&) = delete;
};

#endif
