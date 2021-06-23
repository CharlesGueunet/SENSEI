/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkInteractiveArea.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include <algorithm>

#include "svtkCommand.h"
#include "svtkContextClip.h"
#include "svtkContextMouseEvent.h"
#include "svtkContextScene.h"
#include "svtkContextTransform.h"
#include "svtkInteractiveArea.h"
#include "svtkObjectFactory.h"
#include "svtkPlotGrid.h"
#include "svtkTransform2D.h"
#include "svtkVectorOperators.h"

//@{
/**
 * Hold mouse action key-mappings and other action related resources.
 */
class svtkInteractiveArea::MouseActions
{
public:
  enum
  {
    MaxAction = 1
  };

  MouseActions()
  {
    this->Pan() = svtkContextMouseEvent::LEFT_BUTTON;
    // this->Zoom() = svtkContextMouseEvent::MIDDLE_BUTTON;
  }

  short& Pan() { return Data[0]; }
  // short& Zoom() { return Data[1]; }

  /**
   *  The box created as the mouse is dragged around the screen.
   */
  svtkRectf MouseBox;

private:
  MouseActions(MouseActions const&) = delete;
  void operator=(MouseActions const*) = delete;

  short Data[MaxAction];
};
//@}

////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
svtkStandardNewMacro(svtkInteractiveArea);

//------------------------------------------------------------------------------
svtkInteractiveArea::svtkInteractiveArea()
  : Superclass()
  , Actions(new MouseActions)
{
  Superclass::Interactive = true;
}

//------------------------------------------------------------------------------
svtkInteractiveArea::~svtkInteractiveArea()
{
  delete Actions;
}

//------------------------------------------------------------------------------
void svtkInteractiveArea::PrintSelf(ostream& os, svtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void svtkInteractiveArea::SetAxisRange(svtkRectd const& data)
{
  /// TODO This might be a hack. The intention is to only reset the axis range in
  // Superclass::LayoutAxes at initialization and not during interaction.
  if (!this->Scene->GetDirty())
  {
    Superclass::SetAxisRange(data);
  }
}

//------------------------------------------------------------------------------
bool svtkInteractiveArea::Paint(svtkContext2D* painter)
{
  return Superclass::Paint(painter);
}

//------------------------------------------------------------------------------
bool svtkInteractiveArea::Hit(const svtkContextMouseEvent& mouse)
{
  if (!this->Interactive)
  {
    return false;
  }

  svtkVector2i const pos(mouse.GetScreenPos());
  svtkVector2i const bottomLeft = this->DrawAreaGeometry.GetBottomLeft();
  svtkVector2i const topRight = this->DrawAreaGeometry.GetTopRight();

  if (pos[0] > bottomLeft[0] && pos[0] < topRight[0] && pos[1] > bottomLeft[1] &&
    pos[1] < topRight[1])
  {
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------
bool svtkInteractiveArea::MouseWheelEvent(const svtkContextMouseEvent& svtkNotUsed(mouse), int delta)
{
  // Adjust the grid (delta stands for the number of wheel clicks)
  this->RecalculateTickSpacing(this->TopAxis, delta);
  this->RecalculateTickSpacing(this->BottomAxis, delta);
  this->RecalculateTickSpacing(this->LeftAxis, delta);
  this->RecalculateTickSpacing(this->RightAxis, delta);

  // Mark the scene as dirty
  this->Scene->SetDirty(true);

  // ComputeViewTransform is called through Superclass::Paint
  this->InvokeEvent(svtkCommand::InteractionEvent);
  return true;
}

//------------------------------------------------------------------------------
bool svtkInteractiveArea::MouseMoveEvent(const svtkContextMouseEvent& mouse)
{
  if (mouse.GetButton() == this->Actions->Pan())
  {
    // Figure out how much the mouse has moved by in plot coordinates - pan
    svtkVector2d screenPos(mouse.GetScreenPos().Cast<double>().GetData());
    svtkVector2d lastScreenPos(mouse.GetLastScreenPos().Cast<double>().GetData());
    svtkVector2d pos(0.0, 0.0);
    svtkVector2d last(0.0, 0.0);

    // Go from screen to scene coordinates to work out the delta
    svtkAxis* xAxis = this->BottomAxis;
    svtkAxis* yAxis = this->LeftAxis;
    svtkTransform2D* transform = this->Transform->GetTransform();
    transform->InverseTransformPoints(screenPos.GetData(), pos.GetData(), 1);
    transform->InverseTransformPoints(lastScreenPos.GetData(), last.GetData(), 1);
    svtkVector2d delta = last - pos;
    delta[0] /= xAxis->GetScalingFactor();
    delta[1] /= yAxis->GetScalingFactor();

    // Now move the axis and recalculate the transform
    delta[0] = delta[0] > 0 ? std::min(delta[0], xAxis->GetMaximumLimit() - xAxis->GetMaximum())
                            : std::max(delta[0], xAxis->GetMinimumLimit() - xAxis->GetMinimum());

    delta[1] = delta[1] > 0 ? std::min(delta[1], yAxis->GetMaximumLimit() - yAxis->GetMaximum())
                            : std::max(delta[1], yAxis->GetMinimumLimit() - yAxis->GetMinimum());

    xAxis->SetMinimum(xAxis->GetMinimum() + delta[0]);
    xAxis->SetMaximum(xAxis->GetMaximum() + delta[0]);
    yAxis->SetMinimum(yAxis->GetMinimum() + delta[1]);
    yAxis->SetMaximum(yAxis->GetMaximum() + delta[1]);

    // Mark the scene as dirty
    this->Scene->SetDirty(true);

    // ComputeViewTransform is called through Superclass::Paint
    this->InvokeEvent(svtkCommand::InteractionEvent);
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------
bool svtkInteractiveArea::MouseButtonPressEvent(const svtkContextMouseEvent& mouse)
{
  if (mouse.GetButton() == this->Actions->Pan())
  {
    this->Actions->MouseBox.Set(mouse.GetPos().GetX(), mouse.GetPos().GetY(), 0.0, 0.0);
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------
void svtkInteractiveArea::RecalculateTickSpacing(svtkAxis* axis, int const numClicks)
{
  double min = axis->GetMinimum();
  double max = axis->GetMaximum();
  double const increment = (max - min) * 0.1;

  if (increment > 0.0)
  {
    min += numClicks * increment;
    max -= numClicks * increment;
  }
  else
  {
    min -= numClicks * increment;
    max += numClicks * increment;
  }

  axis->SetMinimum(min);
  axis->SetMaximum(max);
  axis->RecalculateTickSpacing();
}

//------------------------------------------------------------------------------
void svtkInteractiveArea::ComputeViewTransform()
{
  double const minX = this->BottomAxis->GetMinimum();
  double const minY = this->LeftAxis->GetMinimum();

  svtkVector2d origin(minX, minY);
  svtkVector2d scale(this->BottomAxis->GetMaximum() - minX, this->LeftAxis->GetMaximum() - minY);

  svtkVector2d shift(0.0, 0.0);
  svtkVector2d factor(1.0, 1.0);
  /// TODO Cache and only compute if zoom changed
  this->ComputeZoom(origin, scale, shift, factor);

  this->BottomAxis->SetScalingFactor(factor[0]);
  this->BottomAxis->SetShift(shift[0]);
  this->LeftAxis->SetScalingFactor(factor[1]);
  this->LeftAxis->SetShift(shift[1]);

  // Update transform
  this->Transform->Identity();

  svtkRecti& boundsPixel = this->DrawAreaGeometry;
  float const xOrigin = static_cast<float>(boundsPixel.GetLeft());
  float const yOrigin = static_cast<float>(boundsPixel.GetBottom());
  this->Transform->Translate(xOrigin, yOrigin);

  float const xScalePixels = this->DrawAreaGeometry.GetWidth() / scale[0];
  float const yScalePixels = this->DrawAreaGeometry.GetHeight() / scale[1];
  this->Transform->Scale(xScalePixels, yScalePixels);

  float const xTrans = -(this->BottomAxis->GetMinimum() + shift[0]) * factor[0];
  float const yTrans = -(this->LeftAxis->GetMinimum() + shift[1]) * factor[1];
  this->Transform->Translate(xTrans, yTrans);
}

//------------------------------------------------------------------------------
void svtkInteractiveArea::ComputeZoom(
  svtkVector2d const& origin, svtkVector2d& scale, svtkVector2d& shift, svtkVector2d& factor)
{
  for (int i = 0; i < 2; ++i)
  {
    if (log10(fabs(origin[i]) / scale[i]) > 2)
    {
      shift[i] = floor(log10(fabs(origin[i]) / scale[i]) / 3.0) * 3.0;
      shift[i] = -origin[i];
    }
    if (fabs(log10(scale[i])) > 10)
    {
      // We need to scale the transform to show all data, do this in blocks.
      factor[i] = pow(10.0, floor(log10(scale[i]) / 10.0) * -10.0);
      scale[i] = scale[i] * factor[i];
    }
  }
}