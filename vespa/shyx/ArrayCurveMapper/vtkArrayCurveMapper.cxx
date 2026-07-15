#include "vtkArrayCurveMapper.h"

#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPiecewiseFunction.h>
#include <vtkCellData.h>
#include <vtkPointData.h>

#include <algorithm>
#include <cmath>

vtkStandardNewMacro(vtkArrayCurveMapper);

namespace
{
void onCurveModified(vtkObject* vtkNotUsed(caller), unsigned long vtkNotUsed(event), void* clientData,
    void* vtkNotUsed(callData))
{
    auto* self = static_cast<vtkArrayCurveMapper*>(clientData);
    self->Modified();
}

/** Prefer cell data when both point and cell arrays match (same as other SHYX filters). */
bool ResolveInputArray(vtkDataSet* input, const char* arrayName, vtkDataArray*& outArray, bool& isPointData)
{
    outArray = nullptr;
    isPointData = false;
    if (!input || !arrayName || arrayName[0] == '\0')
    {
        return false;
    }

    vtkDataArray* const cellArr = input->GetCellData()->GetArray(arrayName);
    vtkDataArray* const ptArr = input->GetPointData()->GetArray(arrayName);
    const vtkIdType nCells = input->GetNumberOfCells();
    const vtkIdType nPts = input->GetNumberOfPoints();
    const bool cellOk = (cellArr != nullptr && cellArr->GetNumberOfTuples() == nCells);
    const bool ptOk = (ptArr != nullptr && ptArr->GetNumberOfTuples() == nPts);

    if (cellOk && ptOk)
    {
        outArray = cellArr;
        isPointData = false;
        return true;
    }
    if (cellOk)
    {
        outArray = cellArr;
        isPointData = false;
        return true;
    }
    if (ptOk)
    {
        outArray = ptArr;
        isPointData = true;
        return true;
    }
    return false;
}
} // namespace

//------------------------------------------------------------------------------
vtkArrayCurveMapper::vtkArrayCurveMapper()
    : CurveTransferFunction(nullptr)
    , CurveObserverTag(0)
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkArrayCurveMapper::~vtkArrayCurveMapper()
{
    this->SetCurveTransferFunction(nullptr);
}

//------------------------------------------------------------------------------
void vtkArrayCurveMapper::SetCurveTransferFunction(vtkPiecewiseFunction* pwf)
{
    if (this->CurveTransferFunction == pwf)
    {
        return;
    }
    if (this->CurveTransferFunction)
    {
        if (this->CurveObserverTag != 0)
        {
            this->CurveTransferFunction->RemoveObserver(this->CurveObserverTag);
            this->CurveObserverTag = 0;
        }
        if (this->CurveObserverCommand)
        {
            this->CurveObserverCommand->Delete();
            this->CurveObserverCommand = nullptr;
        }
        this->CurveTransferFunction->UnRegister(this);
    }
    this->CurveTransferFunction = pwf;
    if (this->CurveTransferFunction)
    {
        this->CurveTransferFunction->Register(this);
        this->CurveObserverCommand = vtkCallbackCommand::New();
        this->CurveObserverCommand->SetCallback(onCurveModified);
        this->CurveObserverCommand->SetClientData(this);
        this->CurveObserverTag =
            this->CurveTransferFunction->AddObserver(vtkCommand::ModifiedEvent, this->CurveObserverCommand, 0.0f);
    }
    this->Modified();
}

//------------------------------------------------------------------------------
int vtkArrayCurveMapper::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkArrayCurveMapper::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "InputArrayName: " << this->InputArrayName << std::endl;
    os << indent << "OutputArrayName: " << this->OutputArrayName << std::endl;
    os << indent << "InputRange: [" << this->InputRangeMin << ", " << this->InputRangeMax << "]" << std::endl;
    os << indent << "OutputRange: [" << this->OutputRangeMin << ", " << this->OutputRangeMax << "]" << std::endl;
    os << indent << "CurveTransferFunction: " << this->CurveTransferFunction << std::endl;
}

//------------------------------------------------------------------------------
int vtkArrayCurveMapper::RequestData(
    vtkInformation* vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
    vtkDataSet* output = vtkDataSet::GetData(outputVector);

    if (!input)
    {
        vtkErrorMacro("Input is null.");
        return 0;
    }

    output->ShallowCopy(input);

    if (this->InputArrayName.empty() || !this->CurveTransferFunction)
    {
        return 1;
    }

    vtkDataArray* srcArray = nullptr;
    bool isPointData = false;
    if (!ResolveInputArray(input, this->InputArrayName.c_str(), srcArray, isPointData))
    {
        vtkErrorMacro("Array '" << this->InputArrayName
                                << "' not found on input point or cell data.");
        return 0;
    }

    vtkPiecewiseFunction* curve = this->CurveTransferFunction;
    if (curve->GetSize() < 2)
    {
        curve->AddPoint(this->InputRangeMin, this->OutputRangeMin);
        curve->AddPoint(this->InputRangeMax, this->OutputRangeMax);
    }

    const int numComp = srcArray->GetNumberOfComponents();
    const vtkIdType numPts = srcArray->GetNumberOfTuples();
    const bool isVector = (numComp > 1);

    double inMin = this->InputRangeMin;
    double inMax = this->InputRangeMax;
    double outMin = this->OutputRangeMin;
    double outMax = this->OutputRangeMax;

    vtkNew<vtkDoubleArray> dstArray;
    dstArray->SetName(this->OutputArrayName.c_str());
    dstArray->SetNumberOfComponents(1);
    dstArray->SetNumberOfTuples(numPts);

    std::vector<double> tuple(numComp);

    for (vtkIdType i = 0; i < numPts; ++i)
    {
        double raw;
        if (isVector)
        {
            srcArray->GetTuple(i, tuple.data());
            double sumSq = 0.0;
            for (int c = 0; c < numComp; ++c)
            {
                sumSq += tuple[c] * tuple[c];
            }
            raw = std::sqrt(sumSq);
        }
        else
        {
            raw = srcArray->GetTuple1(i);
        }

        raw = std::max(inMin, std::min(inMax, raw));
        // Curve Y is in physical output units (clamped to OutputRange).
        double mapped = std::max(outMin, std::min(outMax, curve->GetValue(raw)));
        dstArray->SetValue(i, mapped);
    }

    if (isPointData)
    {
        output->GetPointData()->AddArray(dstArray);
    }
    else
    {
        output->GetCellData()->AddArray(dstArray);
    }
    return 1;
}
