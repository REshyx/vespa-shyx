// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkVortexCoreTest
 * @brief   Same algorithm as VTK vtkVortexCore (parallel vectors vortex cores); plugin copy for VESPA.
 *
 * @sa vtkVortexCore, vtkParallelVectors
 */

#ifndef vtkVortexCoreTest_h
#define vtkVortexCoreTest_h

#include "vtkVortexCoreTestModule.h"
#include "vtkPolyDataAlgorithm.h"

VTK_ABI_NAMESPACE_BEGIN
class VTKVORTEXCORETEST_EXPORT vtkVortexCoreTest : public vtkPolyDataAlgorithm
{
public:
  static vtkVortexCoreTest* New();
  vtkTypeMacro(vtkVortexCoreTest, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  ///@{
  vtkSetMacro(HigherOrderMethod, vtkTypeBool);
  vtkGetMacro(HigherOrderMethod, vtkTypeBool);
  vtkBooleanMacro(HigherOrderMethod, vtkTypeBool);
  ///@}

  ///@{
  vtkGetMacro(FasterApproximation, bool);
  vtkSetMacro(FasterApproximation, bool);
  vtkBooleanMacro(FasterApproximation, bool);
  ///@}

protected:
  vtkVortexCoreTest();
  ~vtkVortexCoreTest() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int, vtkInformation*) override;

  vtkTypeBool HigherOrderMethod;

  bool FasterApproximation;

private:
  vtkVortexCoreTest(const vtkVortexCoreTest&) = delete;
  void operator=(const vtkVortexCoreTest&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
