#include "vtkSHYXGrayHistogramPiecewiseFunctionItem.h"

#include <vtkAxis.h>
#include <vtkBrush.h>
#include <vtkContext2D.h>
#include <vtkContextScene.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPen.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlotBar.h>
#include <vtkPoints2D.h>
#include <vtkTable.h>

#include <algorithm>
#include <cmath>
#include <vector>

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkSHYXGrayHistogramPiecewiseFunctionItem);
vtkCxxSetObjectMacro(vtkSHYXGrayHistogramPiecewiseFunctionItem, MappedHistogramTable, vtkTable);

//------------------------------------------------------------------------------
vtkSHYXGrayHistogramPiecewiseFunctionItem::~vtkSHYXGrayHistogramPiecewiseFunctionItem()
{
    this->SetMappedHistogramTable(nullptr);
}

//------------------------------------------------------------------------------
bool vtkSHYXGrayHistogramPiecewiseFunctionItem::ConfigurePlotBar()
{
  // vtkPlotBar draws from data y = 0 and couples bar height to the Y-axis transform.
  // Histogram is painted manually in data space anchored at ymin.
  this->PlotBar->SetVisible(false);
  return false;
}

//------------------------------------------------------------------------------
void vtkSHYXGrayHistogramPiecewiseFunctionItem::PaintInputHistogram(vtkContext2D* painter)
{
  if (!this->HistogramTable || this->HistogramTable->GetNumberOfColumns() < 2 || !this->GetXAxis() ||
    !this->GetYAxis())
  {
    return;
  }

  vtkDataArray* counts = vtkDataArray::FastDownCast(this->HistogramTable->GetColumn(1));
  vtkDoubleArray* binExtent = vtkDoubleArray::SafeDownCast(this->HistogramTable->GetColumn(0));
  if (!counts || !binExtent)
  {
    return;
  }

  double valueRange[2] = { 0.0, 0.0 };
  counts->GetRange(valueRange);
  if (!(valueRange[1] > 0.0))
  {
    return;
  }

  double yRange[2] = { 0.0, 1.0 };
  this->GetYAxis()->GetUnscaledRange(yRange);
  const double ymin = yRange[0];
  const double ySpan = yRange[1] - ymin;
  if (!(ySpan > 0.0))
  {
    return;
  }

  const int nBin = this->HistogramTable->GetNumberOfRows();
  if (nBin < 1)
  {
    return;
  }

  double barWidth = ySpan * 0.01;
  if (nBin > 1)
  {
    const double range = binExtent->GetValue(nBin - 1) - binExtent->GetValue(0);
    const double delta = range / (nBin - 1);
    barWidth = (range + delta) / nBin;
  }

  vtkNew<vtkBrush> brush;
  brush->SetColorF(0.72, 0.73, 0.78, 1.0);
  painter->ApplyBrush(brush);
  vtkNew<vtkPen> pen;
  pen->SetLineType(vtkPen::NO_PEN);
  painter->ApplyPen(pen);

  const double extentFraction = std::max(0.0, this->HistogramHeightFraction);
  const double heightScale = extentFraction * ySpan / valueRange[1];
  for (int i = 0; i < nBin; ++i)
  {
    const double xc = binExtent->GetValue(i);
    const double count = counts->GetTuple1(i);
    const double barHeight = count * heightScale;
    if (!(barHeight > 0.0))
    {
      continue;
    }
    painter->DrawRect(xc - barWidth * 0.5, ymin, barWidth, barHeight);
  }
}

//------------------------------------------------------------------------------
void vtkSHYXGrayHistogramPiecewiseFunctionItem::PaintMappedHistogram(vtkContext2D* painter)
{
  if (!this->MappedHistogramTable || this->MappedHistogramTable->GetNumberOfColumns() < 2 ||
    !this->GetXAxis() || !this->GetYAxis())
  {
    return;
  }

  vtkDataArray* counts = vtkDataArray::FastDownCast(this->MappedHistogramTable->GetColumn(1));
  vtkDoubleArray* binExtent = vtkDoubleArray::FastDownCast(this->MappedHistogramTable->GetColumn(0));
  if (!counts || !binExtent)
  {
    return;
  }

  double valueRange[2] = { 0.0, 0.0 };
  counts->GetRange(valueRange);
  if (!(valueRange[1] > 0.0))
  {
    return;
  }

  double xRange[2] = { 0.0, 1.0 };
  this->GetXAxis()->GetUnscaledRange(xRange);
  const double xmin = xRange[0];
  const double xSpan = xRange[1] - xmin;
  if (!(xSpan > 0.0))
  {
    return;
  }

  const int nBin = this->MappedHistogramTable->GetNumberOfRows();
  if (nBin < 1)
  {
    return;
  }

  double barHeight = xSpan * 0.01;
  if (nBin > 1)
  {
    const double range = binExtent->GetValue(nBin - 1) - binExtent->GetValue(0);
    const double delta = range / (nBin - 1);
    barHeight = (range + delta) / nBin;
  }

  vtkNew<vtkBrush> brush;
  brush->SetColorF(0.25, 0.55, 0.95, 0.40);
  painter->ApplyBrush(brush);
  vtkNew<vtkPen> pen;
  pen->SetLineType(vtkPen::NO_PEN);
  painter->ApplyPen(pen);

  const double extentFraction = std::max(0.0, this->HistogramHeightFraction);
  const double widthScale = extentFraction * xSpan / valueRange[1];
  for (int i = 0; i < nBin; ++i)
  {
    const double yc = binExtent->GetValue(i);
    const double count = counts->GetTuple1(i);
    const double barWidth = count * widthScale;
    if (!(barWidth > 0.0))
    {
      continue;
    }
    painter->DrawRect(xmin, yc - barHeight * 0.5, barWidth, barHeight);
  }
}

//------------------------------------------------------------------------------
bool vtkSHYXGrayHistogramPiecewiseFunctionItem::Paint(vtkContext2D* painter)
{
  this->TextureWidth = this->GetScene()->GetSceneWidth();
  if (this->Texture == nullptr || this->Texture->GetMTime() < this->GetMTime())
  {
    this->ComputeTexture();
  }

  if (this->HistogramTable && this->HistogramTable->GetNumberOfColumns() >= 2)
  {
    this->PaintInputHistogram(painter);
  }
  if (this->MappedHistogramTable && this->MappedHistogramTable->GetNumberOfColumns() >= 2)
  {
    this->PaintMappedHistogram(painter);
  }

  const int size = this->Shape->GetNumberOfPoints();
  if (this->PolyLinePen->GetLineType() != vtkPen::NO_PEN && size >= 2)
  {
    const vtkRectd& ss = this->ShiftScale;

    vtkNew<vtkPoints2D> transformedShape;
    transformedShape->SetNumberOfPoints(size);
    for (int i = 0; i < size; ++i)
    {
      double point[2];
      this->Shape->GetPoint(i, point);
      point[0] = (point[0] + ss[0]) * ss[2];
      point[1] = (point[1] + ss[1]) * ss[3];
      transformedShape->SetPoint(i, point);
    }
    painter->ApplyPen(this->PolyLinePen);
    painter->DrawPoly(transformedShape);
  }

  return true;
}

//------------------------------------------------------------------------------
void vtkSHYXGrayHistogramPiecewiseFunctionItem::ComputeTexture()
{
  double bounds[4];
  this->GetBounds(bounds);
  if (bounds[0] == bounds[1] || !this->PiecewiseFunction)
  {
    return;
  }
  if (this->Texture == nullptr)
  {
    this->Texture = vtkImageData::New();
  }

  const int dimension = this->GetTextureWidth();
  std::vector<double> values(static_cast<std::size_t>(dimension));
  this->Texture->SetExtent(0, dimension - 1, 0, 0, 0, 0);
  this->Texture->AllocateScalars(VTK_UNSIGNED_CHAR, 4);

  this->PiecewiseFunction->GetTable(bounds[0], bounds[1], dimension, values.data());
  unsigned char* ptr = reinterpret_cast<unsigned char*>(this->Texture->GetScalarPointer(0, 0, 0));

  double yRange[2] = { bounds[2], bounds[3] };
  if (this->GetYAxis())
  {
    this->GetYAxis()->GetUnscaledRange(yRange);
  }
  const double ySpan = yRange[1] - yRange[0];

  if (this->MaskAboveCurve || this->PolyLinePen->GetLineType() != vtkPen::NO_PEN)
  {
    this->Shape->SetNumberOfPoints(dimension);
    const double step = (bounds[1] - bounds[0]) / dimension;
    for (int i = 0; i < dimension; ++i)
    {
      const double yValue = values[static_cast<std::size_t>(i)];
      this->Shape->SetPoint(i, bounds[0] + step * i, yValue);

      double alpha = 1.0;
      if (ySpan > 0.0)
      {
        alpha = (yValue - yRange[0]) / ySpan;
      }
      alpha = std::min(1.0, std::max(0.0, alpha));
      this->Pen->GetColor(ptr);
      ptr[3] = static_cast<unsigned char>(alpha * this->Opacity * 255 + 0.5);
      ptr += 4;
    }
    this->Shape->Modified();
  }
  else
  {
    for (int i = 0; i < dimension; ++i)
    {
      double alpha = 1.0;
      if (ySpan > 0.0)
      {
        alpha = (values[static_cast<std::size_t>(i)] - yRange[0]) / ySpan;
      }
      alpha = std::min(1.0, std::max(0.0, alpha));
      this->Pen->GetColor(ptr);
      ptr[3] = static_cast<unsigned char>(alpha * this->Opacity * 255 + 0.5);
      ptr += 4;
    }
  }
}
