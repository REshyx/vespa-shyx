// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqSHYXTransferCurveWidget.h"

#include "QVTKOpenGLNativeWidget.h"
#include "pqCoreUtilities.h"
#include "pqTimer.h"
#include "vtkSHYXTransferChartXY.h"
#include "vtkAxis.h"
#include "vtkBoundingBox.h"
#include "vtkBrush.h"
#include "vtkChartXY.h"
#include "vtkColorTransferFunction.h"
#include "vtkColorTransferFunctionItem.h"
#include "vtkCompositeControlPointsItem.h"
#include "vtkSHYXCompositeControlPointsItem.h"
#include "vtkSHYXGrayHistogramPiecewiseFunctionItem.h"
#include "vtkSHYXOutputRangeHandlesItem.h"
#include "vtkSHYXInputRangeHandlesItem.h"
#include "vtkAxis.h"
#include "vtkContext2D.h"
#include "vtkContextMouseEvent.h"
#include "vtkContextScene.h"
#include "vtkContextView.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkGenericOpenGLRenderWindow.h"
#include "vtkImageData.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPVColorTransferControlPointsItem.h"
#include "vtkPiecewiseControlPointsItem.h"
#include "vtkControlPointsItem.h"
#include "vtkPiecewiseFunction.h"
#include "vtkPiecewiseFunctionItem.h"
#include "vtkRangeHandlesItem.h"
#include "vtkSMCoreUtilities.h"
#include "vtkSmartPointer.h"
#include "vtkVector.h"
#include "vtkWeakPointer.h"

#include <QColorDialog>
#include <QMainWindow>
#include <QPointer>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <cassert>

class pqSHYXTransferCurveWidget::pqInternals
{
  vtkNew<vtkGenericOpenGLRenderWindow> Window;

public:
  QPointer<QVTKOpenGLNativeWidget> Widget;
  vtkNew<vtkSHYXTransferChartXY> ChartXY;
  vtkNew<vtkContextView> ContextView;
  vtkNew<vtkEventQtSlotConnect> VTKConnect;
  vtkNew<vtkBrush> CheckerBrush;

  pqTimer Timer;
  pqTimer RangeTimer;
  pqTimer EditColorPointTimer;

  vtkSmartPointer<vtkSHYXInputRangeHandlesItem> RangeHandlesItem;
  vtkSmartPointer<vtkSHYXOutputRangeHandlesItem> OutputRangeHandlesItem;
  vtkSmartPointer<vtkScalarsToColorsItem> TransferFunctionItem;
  vtkSmartPointer<vtkControlPointsItem> ControlPointsItem;
  unsigned long CurrentPointEditEventId;

  vtkWeakPointer<vtkScalarsToColors> ScalarsToColors;
  vtkWeakPointer<vtkPiecewiseFunction> PiecewiseFunction;

  double OutputYMin = 0.0;
  double OutputYMax = 1.0;
  double CurveBounds[4] = { 0.0, -1.0, 0.0, -1.0 };
  bool UseCurveBounds = false;

  void applyChartOutputRange()
  {
    this->ChartXY->SetOutputRange(vtkVector2d(this->OutputYMin, this->OutputYMax));
  }

  void applyCurveDataBounds()
  {
    if (this->UseCurveBounds)
    {
      if (auto* curveCp = vtkSHYXCompositeControlPointsItem::SafeDownCast(this->ControlPointsItem))
      {
        curveCp->SetCurveDataBounds(
            this->CurveBounds[0], this->CurveBounds[1], this->CurveBounds[2], this->CurveBounds[3]);
      }
      else if (this->ControlPointsItem)
      {
        this->ControlPointsItem->SetValidBounds(this->CurveBounds);
        this->ControlPointsItem->SetUserBounds(this->CurveBounds);
      }
    }
  }

  pqInternals(pqSHYXTransferCurveWidget* editor)
    : Widget(new QVTKOpenGLNativeWidget(editor))
    , CurrentPointEditEventId(0)
  {
    this->Timer.setSingleShot(true);
    this->Timer.setInterval(0);

    this->RangeTimer.setSingleShot(true);
    this->RangeTimer.setInterval(0);

    // A delay is necessary otherwise the color dialog grabs focus
    // too quickly causing #20758.
    this->EditColorPointTimer.setSingleShot(true);
    this->EditColorPointTimer.setInterval(100);

    this->Window->SetMultiSamples(8);

    this->Widget->setEnableHiDPI(true);
    this->Widget->setObjectName("1QVTKWidget0");
    this->Widget->setRenderWindow(this->Window);
    this->Widget->setEnableTouchEventProcessing(false);
    this->ContextView->SetRenderWindow(this->Window);

    this->ChartXY->SetAutoSize(true);
    this->ChartXY->SetShowLegend(false);
    this->ChartXY->SetZoomWithMouseWheel(false);
    this->ContextView->GetScene()->AddItem(this->ChartXY);
    this->ContextView->SetInteractor(this->Widget->interactor());
    this->ContextView->GetRenderWindow()->SetLineSmoothing(true);

    this->ChartXY->SetActionToButton(vtkChart::PAN, -1);
    this->ChartXY->SetActionToButton(vtkChart::ZOOM, -1);
    this->ChartXY->SetActionToButton(vtkChart::SELECT, vtkContextMouseEvent::RIGHT_BUTTON);
    this->ChartXY->SetActionToButton(vtkChart::SELECT_POLYGON, -1);

    this->Widget->setParent(editor);
    QVBoxLayout* layout = new QVBoxLayout(editor);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(this->Widget);

    this->ChartXY->SetAutoAxes(false);
    this->ChartXY->SetHiddenAxisBorder(8);
    for (int cc = 0; cc < 4; cc++)
    {
      this->ChartXY->GetAxis(cc)->SetVisible(false);
      this->ChartXY->GetAxis(cc)->SetBehavior(vtkAxis::FIXED);
    }
  }
  ~pqInternals() { this->cleanup(); }

  void initializeCheckerBoardBrush()
  {
    vtkNew<vtkImageData> texture;
    const int numPtsX = 32;
    const int numPtsY = 32;
    const int ncomp = 3;
    texture->SetDimensions(numPtsX, numPtsY, 1);
    texture->AllocateScalars(VTK_UNSIGNED_CHAR, ncomp);
    // assumes origin at lower left corner in a 32x32 square.
    // create a checkerboard with 4 sub-squares. texture property repeat
    // ensures this pattern is repeated.
    for (int i = 0; i < numPtsY; ++i)
    {
      for (int j = 0; j < numPtsX; ++j)
      {
        double val = 0;
        // lower left
        if (i < numPtsY / 2 && j < numPtsX / 2)
        {
          val = 225; // grey
        }
        // upper right
        else if (i >= numPtsY / 2 && j >= numPtsX / 2)
        {
          val = 225; // grey
        }
        // lower right
        else if (j >= numPtsX / 2)
        {
          val = 255; // white
        }
        // upper left
        else if (i >= numPtsY / 2)
        {
          val = 255; // white
        }
        for (int comp = 0; comp < ncomp; ++comp)
        {
          texture->SetScalarComponentFromDouble(i, j, 0, comp, val);
        }
      }
    }
    this->CheckerBrush->SetTexture(texture);
    this->CheckerBrush->SetTextureProperties(
      vtkBrush::TextureProperty::Repeat | vtkBrush::TextureProperty::Nearest);
  }

  void setUseCheckerBoardBrush(bool use)
  {
    this->ChartXY->SetBackgroundBrush(use ? this->CheckerBrush.GetPointer() : nullptr);
  }

  void cleanup()
  {
    this->RangeTimer.disconnect();
    this->VTKConnect->Disconnect();
    this->ChartXY->ClearPlots();
    if (this->ControlPointsItem && this->CurrentPointEditEventId)
    {
      this->ControlPointsItem->RemoveObserver(this->CurrentPointEditEventId);
      this->CurrentPointEditEventId = 0;
    }
    this->TransferFunctionItem = nullptr;
    this->RangeHandlesItem = nullptr;
    this->OutputRangeHandlesItem = nullptr;
    this->ControlPointsItem = nullptr;
  }
};

//-----------------------------------------------------------------------------
pqSHYXTransferCurveWidget::pqSHYXTransferCurveWidget(QWidget* parentObject)
  : Superclass(parentObject)
  , Internals(new pqInternals(this))
{
  // whenever the rendering timer times out, we render the widget.
  QObject::connect(&this->Internals->Timer, &QTimer::timeout,
    [this]()
    {
      auto renWin = this->Internals->ContextView->GetRenderWindow();
      if (this->isVisible())
      {
        renWin->Render();
      }
    });

  this->Internals->initializeCheckerBoardBrush();
  this->Internals->setUseCheckerBoardBrush(true);
  this->connect(&this->Internals->EditColorPointTimer, SIGNAL(timeout()),
    SLOT(editColorAtCurrentControlPoint()));
}

//-----------------------------------------------------------------------------
pqSHYXTransferCurveWidget::~pqSHYXTransferCurveWidget()
{
  delete this->Internals;
  this->Internals = nullptr;
}

//-----------------------------------------------------------------------------
vtkScalarsToColors* pqSHYXTransferCurveWidget::scalarsToColors() const
{
  return this->Internals->ScalarsToColors;
}

//-----------------------------------------------------------------------------
vtkPiecewiseFunction* pqSHYXTransferCurveWidget::piecewiseFunction() const
{
  return this->Internals->PiecewiseFunction;
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::initialize(
  vtkScalarsToColors* stc, bool stc_editable, vtkPiecewiseFunction* pwf, bool pwf_editable)
{
  this->Internals->cleanup();
  this->Internals->ScalarsToColors = stc;
  this->Internals->PiecewiseFunction = pwf;

  // TODO: If needed, we can support vtkLookupTable.
  vtkColorTransferFunction* ctf = vtkColorTransferFunction::SafeDownCast(stc);

  if (ctf != nullptr && pwf == nullptr)
  {
    vtkNew<vtkColorTransferFunctionItem> item;
    item->SetColorTransferFunction(ctf);
    this->Internals->TransferFunctionItem = item;

    vtkNew<vtkSHYXInputRangeHandlesItem> handlesItem;
    handlesItem->SetColorTransferFunction(ctf);
    handlesItem->SetHandleWidth(4.0);
    this->Internals->RangeHandlesItem = handlesItem;

    if (stc_editable)
    {
      vtkNew<vtkPVColorTransferControlPointsItem> cpItem;
      cpItem->SetColorTransferFunction(ctf);
      cpItem->SetColorFill(true);
      cpItem->SetEndPointsXMovable(false);
      cpItem->SetEndPointsYMovable(false);
      cpItem->SetLabelFormat("%.3f");
      this->Internals->ControlPointsItem = cpItem;

      this->Internals->CurrentPointEditEventId =
        cpItem->AddObserver(vtkControlPointsItem::CurrentPointEditEvent, this,
          &pqSHYXTransferCurveWidget::onCurrentPointEditEvent);
    }
  }
  else if (ctf == nullptr && pwf != nullptr)
  {
    vtkNew<vtkPiecewiseFunctionItem> item;
    item->SetPiecewiseFunction(pwf);

    this->Internals->TransferFunctionItem = item;

    if (pwf_editable)
    {
      vtkNew<vtkPiecewiseControlPointsItem> cpItem;
      cpItem->SetPiecewiseFunction(pwf);
      cpItem->SetEndPointsXMovable(false);
      cpItem->SetEndPointsYMovable(true);
      cpItem->SetLabelFormat("%.3f: %.3f");
      this->Internals->ControlPointsItem = cpItem;
    }
  }
  else if (ctf != nullptr && pwf != nullptr)
  {
    // Piecewise Y holds physical output values, not opacity in [0,1]; avoid vtkCompositeTransferFunctionItem.
    vtkNew<vtkSHYXGrayHistogramPiecewiseFunctionItem> item;
    item->SetPiecewiseFunction(pwf);
    item->SetMaskAboveCurve(false);
    this->Internals->TransferFunctionItem = item;

    vtkNew<vtkSHYXInputRangeHandlesItem> handlesItem;
    handlesItem->SetColorTransferFunction(ctf);
    this->Internals->RangeHandlesItem = handlesItem;

    vtkNew<vtkSHYXOutputRangeHandlesItem> outputHandlesItem;
    this->Internals->OutputRangeHandlesItem = outputHandlesItem;

    if (pwf_editable && stc_editable)
    {
      // NOTE: this hasn't been tested yet.
      vtkNew<vtkSHYXCompositeControlPointsItem> cpItem;
      cpItem->SetPointsFunction(vtkCompositeControlPointsItem::ColorAndOpacityPointsFunction);
      cpItem->SetOpacityFunction(pwf);
      cpItem->SetColorTransferFunction(ctf);
      cpItem->SetEndPointsXMovable(false);
      cpItem->SetEndPointsYMovable(true);
      cpItem->SetUseOpacityPointHandles(true);
      cpItem->SetLabelFormat("%.3f: %.3f");
      this->Internals->ControlPointsItem = cpItem;
    }
    else if (pwf_editable)
    {
      vtkNew<vtkSHYXCompositeControlPointsItem> cpItem;
      cpItem->SetPointsFunction(vtkCompositeControlPointsItem::OpacityPointsFunction);
      cpItem->SetOpacityFunction(pwf);
      cpItem->SetColorTransferFunction(ctf);
      cpItem->SetEndPointsXMovable(false);
      cpItem->SetEndPointsYMovable(true);
      cpItem->SetUseOpacityPointHandles(true);
      cpItem->SetLabelFormat("%.3f: %.3f");
      this->Internals->ControlPointsItem = cpItem;
    }
  }
  else
  {
    return;
  }

  this->Internals->ChartXY->AddPlot(this->Internals->TransferFunctionItem);
  this->Internals->TransferFunctionItem->SetInteractive(false);

  if (this->Internals->ControlPointsItem)
  {
    this->Internals->ControlPointsItem->SetEndPointsRemovable(false);
    this->Internals->ControlPointsItem->SetShowLabels(true);
    this->Internals->ChartXY->AddPlot(this->Internals->ControlPointsItem);

    pqCoreUtilities::connect(this->Internals->ControlPointsItem,
      vtkControlPointsItem::CurrentPointChangedEvent, this, SLOT(onCurrentChangedEvent()));
    pqCoreUtilities::connect(this->Internals->ControlPointsItem, vtkCommand::EndEvent, this,
      SIGNAL(controlPointsModified()));
  }

  if (this->Internals->RangeHandlesItem)
  {
    pqCoreUtilities::connect(this->Internals->RangeHandlesItem, vtkCommand::EndInteractionEvent,
      this, SLOT(onRangeHandlesRangeChanged()));
    pqCoreUtilities::connect(this->Internals->RangeHandlesItem,
      vtkCommand::LeftButtonDoubleClickEvent, this, SIGNAL(rangeHandlesDoubleClicked()));
    pqCoreUtilities::connect(
      this->Internals->RangeHandlesItem, vtkCommand::HighlightEvent, this, SLOT(showUsageStatus()));
    this->Internals->ChartXY->AddPlot(this->Internals->RangeHandlesItem);
  }

  if (this->Internals->OutputRangeHandlesItem)
  {
    this->Internals->OutputRangeHandlesItem->SetOutputRange(
      this->Internals->OutputYMin, this->Internals->OutputYMax);
    pqCoreUtilities::connect(this->Internals->OutputRangeHandlesItem,
      vtkCommand::EndInteractionEvent, this, SLOT(onOutputRangeHandlesRangeChanged()));
    pqCoreUtilities::connect(this->Internals->OutputRangeHandlesItem, vtkCommand::InteractionEvent,
      this, SLOT(onOutputRangeHandlesInteraction()));
    pqCoreUtilities::connect(this->Internals->OutputRangeHandlesItem, vtkCommand::HighlightEvent,
      this, SLOT(showUsageStatus()));
    this->Internals->ChartXY->AddPlot(this->Internals->OutputRangeHandlesItem);
  }

  // If the transfer functions change, we need to re-render the view. This ensures that.
  // In some cases, the delta can be called for the pwf and the ctf, but it is not a problem.
  if (pwf)
  {
    this->Internals->VTKConnect->Connect(
      pwf, vtkCommand::ModifiedEvent, &this->Internals->RangeTimer, SLOT(start()));

    // whenever the range timer times out, we try to change the range
    QObject::connect(&this->Internals->RangeTimer, &QTimer::timeout,
      [pwf, this]()
      {
        if (this->Internals->ChartXY->SetTFRange(vtkVector2d(pwf->GetRange())))
        {
          // The range have actually been changed, rerender and Q_EMIT the signal
          this->render();
          Q_EMIT this->chartRangeModified();
        }
      });
    this->Internals->ChartXY->SetTFRange(vtkVector2d(pwf->GetRange()));
  }
  if (ctf)
  {
    this->Internals->VTKConnect->Connect(
      ctf, vtkCommand::ModifiedEvent, &this->Internals->RangeTimer, SLOT(start()));

    // whenever the range timer times out, we try to change the range
    QObject::connect(&this->Internals->RangeTimer, &QTimer::timeout,
      [ctf, this]()
      {
        if (this->Internals->ChartXY->SetTFRange(vtkVector2d(ctf->GetRange())))
        {
          // The range has actually been changed, rerender and Q_EMIT the signal
          this->render();
          Q_EMIT this->chartRangeModified();
        }
      });
    this->Internals->ChartXY->SetTFRange(vtkVector2d(ctf->GetRange()));
  }

  this->Internals->applyChartOutputRange();
  this->Internals->applyCurveDataBounds();
  if (this->Internals->RangeHandlesItem)
  {
    this->Internals->RangeHandlesItem->Modified();
  }
  if (this->Internals->OutputRangeHandlesItem)
  {
    this->Internals->OutputRangeHandlesItem->SetOutputRange(
      this->Internals->OutputYMin, this->Internals->OutputYMax);
    this->Internals->OutputRangeHandlesItem->Modified();
  }

  pqCoreUtilities::connect(this->Internals->Widget->interactor(), vtkCommand::MouseMoveEvent, this,
    SLOT(showUsageStatus()));
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::onCurrentPointEditEvent()
{
  // defer the invocation to avoid paraview/paraview#20758.
  auto& internals = (*this->Internals);
  internals.EditColorPointTimer.start();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::editColorAtCurrentControlPoint()
{
  vtkColorTransferControlPointsItem* cpitem =
    vtkColorTransferControlPointsItem::SafeDownCast(this->Internals->ControlPointsItem);
  if (cpitem == nullptr)
  {
    return;
  }

  vtkIdType currentIdx = cpitem->GetCurrentPoint();
  if (currentIdx < 0)
  {
    return;
  }

  vtkColorTransferFunction* ctf = cpitem->GetColorTransferFunction();
  assert(ctf != nullptr);

  // Disable the interactor to ignore any events that may be issues
  // from the operating system after the dialog is shown and closed.
  // Fixes #20758.
  this->Internals->Widget->interactor()->Disable();

  double xrgbms[6];
  ctf->GetNodeValue(currentIdx, xrgbms);
  QColor color = QColorDialog::getColor(QColor::fromRgbF(xrgbms[1], xrgbms[2], xrgbms[3]), this,
    "Select Color", QColorDialog::DontUseNativeDialog);
  if (color.isValid())
  {
    xrgbms[1] = color.redF();
    xrgbms[2] = color.greenF();
    xrgbms[3] = color.blueF();
    ctf->SetNodeValue(currentIdx, xrgbms);

    Q_EMIT this->controlPointsModified();
  }

  // Simulate a MouseButtonReleaseEvent that can get lost when the color
  // selector is closed. Fixes #20758.
  vtkContextMouseEvent mouseEvent;
  mouseEvent.SetButton(vtkContextMouseEvent::LEFT_BUTTON);
  cpitem->MouseButtonReleaseEvent(mouseEvent);

  // Re-enable the widget interactor a short time after the dialog closes.
  // Fixes #20758.
  QTimer::singleShot(100, [=]() { this->Internals->Widget->interactor()->Enable(); });
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::onCurrentChangedEvent()
{
  if (this->Internals->ControlPointsItem)
  {
    Q_EMIT this->currentPointChanged(this->Internals->ControlPointsItem->GetCurrentPoint());
  }
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXTransferCurveWidget::currentPoint() const
{
  if (this->Internals->ControlPointsItem)
  {
    return this->Internals->ControlPointsItem->GetCurrentPoint();
  }

  return -1;
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setCurrentPoint(vtkIdType index)
{
  if (this->Internals->ControlPointsItem)
  {
    if (index < -1 || index >= this->Internals->ControlPointsItem->GetNumberOfPoints())
    {
      index = -1;
    }
    this->Internals->ControlPointsItem->SetCurrentPoint(index);
  }
}

//-----------------------------------------------------------------------------
vtkIdType pqSHYXTransferCurveWidget::numberOfControlPoints() const
{
  return this->Internals->ControlPointsItem
    ? this->Internals->ControlPointsItem->GetNumberOfPoints()
    : 0;
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::SetControlPointsFreehandDrawing(bool use)
{
  this->Internals->ControlPointsItem->SetDrawPoints(!use);
  this->Internals->ControlPointsItem->SetStrokeMode(use);
  this->render();
}

//-----------------------------------------------------------------------------
bool pqSHYXTransferCurveWidget::GetControlPointsFreehandDrawing() const
{
  return this->Internals->ControlPointsItem->GetStrokeMode();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::SetLogScaleXAxis(bool logScale)
{
  this->Internals->ChartXY->GetAxis(vtkAxis::BOTTOM)->SetLogScale(logScale);
}

//-----------------------------------------------------------------------------
bool pqSHYXTransferCurveWidget::GetLogScaleXAxis() const
{
  return this->Internals->ChartXY->GetAxis(vtkAxis::BOTTOM)->GetLogScale();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::render()
{
  this->Internals->Timer.start();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setCurrentPointPosition(double xpos)
{
  vtkIdType currentPid = this->currentPoint();
  if (currentPid < 0)
  {
    return;
  }

  vtkIdType numPts = this->Internals->ControlPointsItem->GetNumberOfPoints();
  if (currentPid >= 0)
  {
    double start_point[4];
    this->Internals->ControlPointsItem->GetControlPoint(0, start_point);
    xpos = std::max(start_point[0], xpos);
  }
  if (currentPid <= (numPts - 1))
  {
    double end_point[4];
    this->Internals->ControlPointsItem->GetControlPoint(numPts - 1, end_point);
    xpos = std::min(end_point[0], xpos);
  }

  double point[4];
  this->Internals->ControlPointsItem->GetControlPoint(currentPid, point);
  if (point[0] != xpos)
  {
    point[0] = xpos;
    this->Internals->ControlPointsItem->SetControlPoint(currentPid, point);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setHistogramTable(vtkTable* table)
{
  this->Internals->TransferFunctionItem->SetHistogramTable(table);
  this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setMappedHistogramTable(vtkTable* table)
{
  if (auto* item = vtkSHYXGrayHistogramPiecewiseFunctionItem::SafeDownCast(
          this->Internals->TransferFunctionItem))
  {
    item->SetMappedHistogramTable(table);
    this->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setHistogramHeightFraction(double fraction)
{
  if (auto* item = vtkSHYXGrayHistogramPiecewiseFunctionItem::SafeDownCast(
          this->Internals->TransferFunctionItem))
  {
    item->SetHistogramHeightFraction(fraction);
    this->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setOutputRange(double ymin, double ymax)
{
  this->Internals->OutputYMin = ymin;
  this->Internals->OutputYMax = ymax;
  if (this->Internals->UseCurveBounds)
  {
    this->Internals->CurveBounds[2] = ymin;
    this->Internals->CurveBounds[3] = ymax;
    this->Internals->applyCurveDataBounds();
  }
  if (this->Internals->ChartXY->SetOutputRange(vtkVector2d(ymin, ymax)))
  {
    this->Internals->TransferFunctionItem->Modified();
    this->render();
    Q_EMIT this->chartRangeModified();
  }
  if (this->Internals->RangeHandlesItem)
  {
    this->Internals->RangeHandlesItem->Modified();
    this->render();
  }
  if (this->Internals->OutputRangeHandlesItem)
  {
    this->Internals->OutputRangeHandlesItem->SetOutputRange(ymin, ymax);
    this->Internals->OutputRangeHandlesItem->Modified();
    this->render();
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::setCurveDataBounds(
    double xmin, double xmax, double ymin, double ymax)
{
  this->Internals->CurveBounds[0] = xmin;
  this->Internals->CurveBounds[1] = xmax;
  this->Internals->CurveBounds[2] = ymin;
  this->Internals->CurveBounds[3] = ymax;
  this->Internals->UseCurveBounds = (xmax > xmin) && (ymax > ymin);
  this->Internals->applyCurveDataBounds();
  this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::onRangeHandlesRangeChanged()
{
  if (this->Internals->RangeHandlesItem)
  {
    double range[2];
    this->Internals->RangeHandlesItem->GetHandlesRange(range);
    Q_EMIT this->rangeHandlesRangeChanged(range[0], range[1]);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::onOutputRangeHandlesRangeChanged()
{
  if (this->Internals->OutputRangeHandlesItem)
  {
    double range[2];
    this->Internals->OutputRangeHandlesItem->GetHandlesRange(range);
    Q_EMIT this->outputRangeHandlesRangeChanged(range[0], range[1]);
  }
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::onOutputRangeHandlesInteraction()
{
  if (!this->Internals->OutputRangeHandlesItem)
  {
    return;
  }

  double range[2];
  this->Internals->OutputRangeHandlesItem->GetHandlesRange(range);
  if (!(range[1] > range[0]))
  {
    return;
  }

  this->Internals->OutputYMin = range[0];
  this->Internals->OutputYMax = range[1];
  if (this->Internals->UseCurveBounds)
  {
    this->Internals->CurveBounds[2] = range[0];
    this->Internals->CurveBounds[3] = range[1];
    this->Internals->applyCurveDataBounds();
  }
  if (this->Internals->ChartXY->SetOutputRange(vtkVector2d(range[0], range[1])))
  {
    this->Internals->ChartXY->Update();
    this->Internals->TransferFunctionItem->Modified();
  }
  this->render();
}

//-----------------------------------------------------------------------------
void pqSHYXTransferCurveWidget::showUsageStatus()
{
  QMainWindow* mainWindow = qobject_cast<QMainWindow*>(pqCoreUtilities::mainWidget());
  if (mainWindow)
  {
    if (this->Internals->OutputRangeHandlesItem)
    {
      mainWindow->statusBar()->showMessage(
        tr("Drag side handles to clamp input (X). "
           "Drag top/bottom handles for output range (Y). "
           "Double-click a side handle to fit input range to data."),
        2000);
    }
    else
    {
      mainWindow->statusBar()->showMessage(
        tr("Drag a handle to change the range. "
           "Double click it to set custom range. Return/Enter to edit color."),
        2000);
    }
  }
}
