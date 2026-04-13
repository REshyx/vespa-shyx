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
    enum RepresentationModeType
    {
        REPRESENTATION_SURFACE = 0,
        REPRESENTATION_VOLUME = 1,
        REPRESENTATION_POINT_GAUSSIAN = 2
    };

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

    vtkGetMacro(RepresentationMode, int);
    vtkSetMacro(RepresentationMode, int);

    vtkGetMacro(IntegrationScale, double);
    vtkSetMacro(IntegrationScale, double);

    vtkGetMacro(TimeScale, double);
    vtkSetMacro(TimeScale, double);

    vtkGetMacro(Opacity, double);
    vtkSetMacro(Opacity, double);

    vtkGetMacro(Trunc, double);
    vtkSetMacro(Trunc, double);

    vtkGetMacro(Pow, double);
    vtkSetMacro(Pow, double);

    vtkGetMacro(Time, double);
    vtkSetMacro(Time, double);

    vtkGetMacro(AnimationArrayName, std::string);
    vtkSetMacro(AnimationArrayName, std::string);

    vtkGetMacro(AnimatedOpacityArrayName, std::string);
    vtkSetMacro(AnimatedOpacityArrayName, std::string);

    vtkGetMacro(PointGaussianRadiusArrayName, std::string);
    vtkSetMacro(PointGaussianRadiusArrayName, std::string);

    vtkGetMacro(VolumeDensityArrayName, std::string);
    vtkSetMacro(VolumeDensityArrayName, std::string);

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
    int         RepresentationMode = REPRESENTATION_SURFACE;
    double      IntegrationScale = 20.0;
    double      TimeScale = 0.15;
    double      Opacity = 0.8;
    double      Trunc = 1.2;
    double      Pow = 1.5;
    double      Time = 0.0;
    std::string AnimationArrayName = "IntegrationTime";
    std::string AnimatedOpacityArrayName = "AnimatedOpacity";
    std::string PointGaussianRadiusArrayName = "AnimatedPointRadius";
    std::string VolumeDensityArrayName = "AnimatedVolumeDensity";

    vtkPiecewiseFunction* CurveTransferFunction;
    vtkCallbackCommand*   CurveObserverCommand = nullptr;
    unsigned long         CurveObserverTag = 0;

private:
    vtkArrayCurveMapper(const vtkArrayCurveMapper&) = delete;
    void operator=(const vtkArrayCurveMapper&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
