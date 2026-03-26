// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef vtkAnimatedStreamlineCompositePolyDataMapper_h
#define vtkAnimatedStreamlineCompositePolyDataMapper_h

#include "vtkAnimatedStreamlineRepresentationModule.h"
#include "vtkCompositePolyDataMapper.h"

#include "vtkCompositePolyDataMapperDelegator.h" // vtkSmartPointer<Delegator> in PreRender signature

#include "vtkType.h" // vtkMTimeType

#include <string>

class vtkDataObject;

class VTKANIMATEDSTREAMLINEREPRESENTATION_EXPORT vtkAnimatedStreamlineCompositePolyDataMapper
  : public vtkCompositePolyDataMapper
{
public:
  static vtkAnimatedStreamlineCompositePolyDataMapper* New();
  vtkTypeMacro(vtkAnimatedStreamlineCompositePolyDataMapper, vtkCompositePolyDataMapper);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * True if any vtkPolyData leaf has a non-empty point data array named \a arrayName.
   */
  static bool DatasetTreeHasPointArray(vtkDataObject* dobj, const char* arrayName);

  /** Remove legacy ASR_IntegrationTimeNorm arrays if present. */
  static void RemoveLegacyAnimatedStreamlineArrays(vtkDataObject* dobj);

  /**
   * Copy animation coordinates from \a xArrayName and optional \a yArrayName into mesh tcoords
   * (x, y). If \a yArrayName is null, empty, \c "None", or \c "(Uniform)" (VESPA convention), y is 1.0.
   * Single-component arrays use the value as-is; multi-component arrays use \c sqrt(x*x+y*y+z*z)
   * per point (z=0 when the array has only two components).
   */
  static void FillPointArrayAsTextureCoordinates(
    vtkDataObject* dobj, const char* xArrayName, const char* yArrayName);

  vtkSetStringMacro(AnimationCoordinateArray);
  vtkGetStringMacro(AnimationCoordinateArray);

  vtkSetStringMacro(AnimationCoordinateYArray);
  vtkGetStringMacro(AnimationCoordinateYArray);

  /**
   * Last render pass found the selected animation coordinate array on at least one polydata leaf.
   */
  bool GetLastInputHadAnimationCoordinateArray() const
  {
    return this->LastInputHadAnimationCoordinateArray;
  }

protected:
  vtkAnimatedStreamlineCompositePolyDataMapper();
  ~vtkAnimatedStreamlineCompositePolyDataMapper() override;

  void PreRender(const std::vector<vtkSmartPointer<vtkCompositePolyDataMapperDelegator>>& delegators,
    vtkRenderer* ren, vtkActor* act) override;

  bool LastInputHadAnimationCoordinateArray = false;
  char* AnimationCoordinateArray = nullptr;
  char* AnimationCoordinateYArray = nullptr;

  /** Max \c vtkDataArray::GetMTime() for the selected animation array over polydata leaves (not input tree MTime). */
  vtkMTimeType LastAnimationSourceArrayMTime = 0;
  /** Max \c vtkPoints::GetMTime() over polydata leaves when last animation tcoords were built. */
  vtkMTimeType LastAnimationPointsMTime = 0;
  /** Animation array name used when \c LastAnimationSourceArrayMTime was recorded. */
  std::string LastTextureCoordArrayKey;

private:
  vtkAnimatedStreamlineCompositePolyDataMapper(const vtkAnimatedStreamlineCompositePolyDataMapper&) =
    delete;
  void operator=(const vtkAnimatedStreamlineCompositePolyDataMapper&) = delete;
};

#endif
