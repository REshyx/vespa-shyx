/**
 * @class   vtkArrayCurveMapper
 * @brief   Map an array through a piecewise transfer curve to a new scalar range.
 *
 * vtkArrayCurveMapper takes a user-selected point-data array (scalar or
 * vector) and evaluates a piecewise-linear transfer curve
 * (vtkPiecewiseFunction) whose X axis spans InputRange and Y axis spans
 * OutputRange.  For vector arrays the magnitude is used.  Input values
 * are clamped to [InputRangeMin, InputRangeMax] before curve evaluation.
 * The mapped values are stored as a new scalar (1-component) point-data
 * array on the output.
 */

#ifndef vtkArrayCurveMapper_h
#define vtkArrayCurveMapper_h

#include "vtkDataSetAlgorithm.h"
#include "vtkArrayCurveMapperModule.h"

#include <string>

class vtkCallbackCommand;
class vtkPiecewiseFunction;

VTK_ABI_NAMESPACE_BEGIN

class VTKARRAYCURVEMAPPER_EXPORT vtkArrayCurveMapper : public vtkDataSetAlgorithm
{
public:
    static vtkArrayCurveMapper* New();
    vtkTypeMacro(vtkArrayCurveMapper, vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent) override;

    vtkGetMacro(InputArrayName, std::string);
    vtkSetMacro(InputArrayName, std::string);

    vtkGetMacro(OutputArrayName, std::string);
    vtkSetMacro(OutputArrayName, std::string);

    vtkGetMacro(InputRangeMin, double);
    vtkSetMacro(InputRangeMin, double);

    vtkGetMacro(InputRangeMax, double);
    vtkSetMacro(InputRangeMax, double);

    vtkGetMacro(OutputRangeMin, double);
    vtkSetMacro(OutputRangeMin, double);

    vtkGetMacro(OutputRangeMax, double);
    vtkSetMacro(OutputRangeMax, double);

    virtual void SetCurveTransferFunction(vtkPiecewiseFunction* pwf);
    vtkGetObjectMacro(CurveTransferFunction, vtkPiecewiseFunction);

protected:
    vtkArrayCurveMapper();
    ~vtkArrayCurveMapper() override;

    int FillInputPortInformation(int port, vtkInformation* info) override;
    int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;

    std::string InputArrayName;
    std::string OutputArrayName = "MappedArray";
    double      InputRangeMin  = 0.0;
    double      InputRangeMax  = 1.0;
    double      OutputRangeMin = 0.0;
    double      OutputRangeMax = 1.0;

    vtkPiecewiseFunction* CurveTransferFunction;
    vtkCallbackCommand*   CurveObserverCommand = nullptr;
    unsigned long         CurveObserverTag = 0;

private:
    vtkArrayCurveMapper(const vtkArrayCurveMapper&) = delete;
    void operator=(const vtkArrayCurveMapper&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
