/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkLineWidget.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkLineWidget.h"

#include "svtkActor.h"
#include "svtkAssemblyNode.h"
#include "svtkAssemblyPath.h"
#include "svtkCallbackCommand.h"
#include "svtkCamera.h"
#include "svtkCellPicker.h"
#include "svtkCommand.h"
#include "svtkDoubleArray.h"
#include "svtkFloatArray.h"
#include "svtkMath.h"
#include "svtkObjectFactory.h"
#include "svtkPickingManager.h"
#include "svtkPlanes.h"
#include "svtkPointWidget.h"
#include "svtkPolyData.h"
#include "svtkPolyDataMapper.h"
#include "svtkProperty.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkSphereSource.h"

svtkStandardNewMacro(svtkLineWidget);

//----------------------------------------------------------------------------
// This class is used to coordinate the interaction between the point widget
// at the center of the line and the line widget. When the line is selected
// (as compared to the handles), a point widget appears at the selection
// point, which can be manipulated in the usual way.
class svtkPWCallback : public svtkCommand
{
public:
  static svtkPWCallback* New() { return new svtkPWCallback; }
  void Execute(svtkObject* svtkNotUsed(caller), unsigned long, void*) override
  {
    double x[3];
    this->PointWidget->GetPosition(x);
    this->LineWidget->SetLinePosition(x);
  }
  svtkPWCallback()
    : LineWidget(nullptr)
    , PointWidget(nullptr)
  {
  }
  svtkLineWidget* LineWidget;
  svtkPointWidget* PointWidget;
};

//----------------------------------------------------------------------------
// This class is used to coordinate the interaction between the point widget
// (point 1) and the line widget.
class svtkPW1Callback : public svtkCommand
{
public:
  static svtkPW1Callback* New() { return new svtkPW1Callback; }
  void Execute(svtkObject* svtkNotUsed(caller), unsigned long, void*) override
  {
    double x[3];
    this->PointWidget->GetPosition(x);
    this->LineWidget->SetPoint1(x);
  }
  svtkPW1Callback()
    : LineWidget(nullptr)
    , PointWidget(nullptr)
  {
  }
  svtkLineWidget* LineWidget;
  svtkPointWidget* PointWidget;
};

//----------------------------------------------------------------------------
// This class is used to coordinate the interaction between the point widget
// (point 2) and the line widget.
class svtkPW2Callback : public svtkCommand
{
public:
  static svtkPW2Callback* New() { return new svtkPW2Callback; }
  void Execute(svtkObject* svtkNotUsed(caller), unsigned long, void*) override
  {
    double x[3];
    this->PointWidget->GetPosition(x);
    this->LineWidget->SetPoint2(x);
  }
  svtkPW2Callback()
    : LineWidget(nullptr)
    , PointWidget(nullptr)
  {
  }
  svtkLineWidget* LineWidget;
  svtkPointWidget* PointWidget;
};

//----------------------------------------------------------------------------
// Begin the definition of the svtkLineWidget methods
//
svtkLineWidget::svtkLineWidget()
{
  this->State = svtkLineWidget::Start;
  this->EventCallbackCommand->SetCallback(svtkLineWidget::ProcessEvents);

  this->Align = svtkLineWidget::XAxis;

  // Build the representation of the widget
  int i;
  // Represent the line
  this->LineSource = svtkLineSource::New();
  this->LineSource->SetResolution(5);
  this->LineMapper = svtkPolyDataMapper::New();
  this->LineMapper->SetInputConnection(this->LineSource->GetOutputPort());
  this->LineActor = svtkActor::New();
  this->LineActor->SetMapper(this->LineMapper);

  // Create the handles
  this->Handle = new svtkActor*[2];
  this->HandleMapper = new svtkPolyDataMapper*[2];
  this->HandleGeometry = new svtkSphereSource*[2];
  for (i = 0; i < 2; i++)
  {
    this->HandleGeometry[i] = svtkSphereSource::New();
    this->HandleGeometry[i]->SetThetaResolution(16);
    this->HandleGeometry[i]->SetPhiResolution(8);
    this->HandleMapper[i] = svtkPolyDataMapper::New();
    this->HandleMapper[i]->SetInputConnection(this->HandleGeometry[i]->GetOutputPort());
    this->Handle[i] = svtkActor::New();
    this->Handle[i]->SetMapper(this->HandleMapper[i]);
  }

  // Define the point coordinates
  double bounds[6];
  bounds[0] = -0.5;
  bounds[1] = 0.5;
  bounds[2] = -0.5;
  bounds[3] = 0.5;
  bounds[4] = -0.5;
  bounds[5] = 0.5;
  this->PlaceFactor = 1.0; // overload parent's value

  // Initial creation of the widget, serves to initialize it
  this->PlaceWidget(bounds);
  this->ClampToBounds = 0;

  // Manage the picking stuff
  this->HandlePicker = svtkCellPicker::New();
  this->HandlePicker->SetTolerance(0.001);
  for (i = 0; i < 2; i++)
  {
    this->HandlePicker->AddPickList(this->Handle[i]);
  }
  this->HandlePicker->PickFromListOn();

  this->LinePicker = svtkCellPicker::New();
  this->LinePicker->SetTolerance(0.005); // need some fluff
  this->LinePicker->AddPickList(this->LineActor);
  this->LinePicker->PickFromListOn();

  this->CurrentHandle = nullptr;

  // Set up the initial properties
  this->CreateDefaultProperties();

  // Create the point widgets and associated callbacks
  this->PointWidget = svtkPointWidget::New();
  this->PointWidget->AllOff();
  this->PointWidget->SetHotSpotSize(0.5);

  this->PointWidget1 = svtkPointWidget::New();
  this->PointWidget1->AllOff();
  this->PointWidget1->SetHotSpotSize(0.5);

  this->PointWidget2 = svtkPointWidget::New();
  this->PointWidget2->AllOff();
  this->PointWidget2->SetHotSpotSize(0.5);

  this->PWCallback = svtkPWCallback::New();
  this->PWCallback->LineWidget = this;
  this->PWCallback->PointWidget = this->PointWidget;
  this->PW1Callback = svtkPW1Callback::New();
  this->PW1Callback->LineWidget = this;
  this->PW1Callback->PointWidget = this->PointWidget1;
  this->PW2Callback = svtkPW2Callback::New();
  this->PW2Callback->LineWidget = this;
  this->PW2Callback->PointWidget = this->PointWidget2;

  // Very tricky, the point widgets watch for their own
  // interaction events.
  this->PointWidget->AddObserver(svtkCommand::InteractionEvent, this->PWCallback, 0.0);
  this->PointWidget1->AddObserver(svtkCommand::InteractionEvent, this->PW1Callback, 0.0);
  this->PointWidget2->AddObserver(svtkCommand::InteractionEvent, this->PW2Callback, 0.0);
  this->CurrentPointWidget = nullptr;
}

//----------------------------------------------------------------------------
svtkLineWidget::~svtkLineWidget()
{
  this->LineActor->Delete();
  this->LineMapper->Delete();
  this->LineSource->Delete();

  for (int i = 0; i < 2; i++)
  {
    this->HandleGeometry[i]->Delete();
    this->HandleMapper[i]->Delete();
    this->Handle[i]->Delete();
  }
  delete[] this->Handle;
  delete[] this->HandleMapper;
  delete[] this->HandleGeometry;

  this->HandlePicker->Delete();
  this->LinePicker->Delete();

  this->HandleProperty->Delete();
  this->SelectedHandleProperty->Delete();
  this->LineProperty->Delete();
  this->SelectedLineProperty->Delete();

  this->PointWidget->RemoveObserver(this->PWCallback);
  this->PointWidget1->RemoveObserver(this->PW1Callback);
  this->PointWidget2->RemoveObserver(this->PW2Callback);
  this->PointWidget->Delete();
  this->PointWidget1->Delete();
  this->PointWidget2->Delete();
  this->PWCallback->Delete();
  this->PW1Callback->Delete();
  this->PW2Callback->Delete();
}

//----------------------------------------------------------------------------
void svtkLineWidget::SetEnabled(int enabling)
{
  if (!this->Interactor)
  {
    svtkErrorMacro(<< "The interactor must be set prior to enabling/disabling widget");
    return;
  }

  if (enabling) //-----------------------------------------------------------
  {
    svtkDebugMacro(<< "Enabling line widget");

    if (this->Enabled) // already enabled, just return
    {
      return;
    }

    if (!this->CurrentRenderer)
    {
      this->SetCurrentRenderer(this->Interactor->FindPokedRenderer(
        this->Interactor->GetLastEventPosition()[0], this->Interactor->GetLastEventPosition()[1]));
      if (this->CurrentRenderer == nullptr)
      {
        return;
      }
    }

    this->PointWidget->SetCurrentRenderer(this->CurrentRenderer);
    this->PointWidget1->SetCurrentRenderer(this->CurrentRenderer);
    this->PointWidget2->SetCurrentRenderer(this->CurrentRenderer);

    this->Enabled = 1;

    // listen for the following events
    svtkRenderWindowInteractor* i = this->Interactor;
    i->AddObserver(svtkCommand::MouseMoveEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(svtkCommand::LeftButtonPressEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(svtkCommand::LeftButtonReleaseEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(svtkCommand::MiddleButtonPressEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(
      svtkCommand::MiddleButtonReleaseEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(svtkCommand::RightButtonPressEvent, this->EventCallbackCommand, this->Priority);
    i->AddObserver(svtkCommand::RightButtonReleaseEvent, this->EventCallbackCommand, this->Priority);

    // Add the line
    this->CurrentRenderer->AddActor(this->LineActor);
    this->LineActor->SetProperty(this->LineProperty);

    // turn on the handles
    for (int j = 0; j < 2; j++)
    {
      this->CurrentRenderer->AddActor(this->Handle[j]);
      this->Handle[j]->SetProperty(this->HandleProperty);
    }

    this->BuildRepresentation();
    this->SizeHandles();
    this->RegisterPickers();

    this->InvokeEvent(svtkCommand::EnableEvent, nullptr);
  }

  else // disabling----------------------------------------------------------
  {
    svtkDebugMacro(<< "Disabling line widget");

    if (!this->Enabled) // already disabled, just return
    {
      return;
    }

    this->Enabled = 0;

    // don't listen for events any more
    this->Interactor->RemoveObserver(this->EventCallbackCommand);

    // turn off the line
    this->CurrentRenderer->RemoveActor(this->LineActor);

    // turn off the handles
    for (int i = 0; i < 2; i++)
    {
      this->CurrentRenderer->RemoveActor(this->Handle[i]);
    }

    if (this->CurrentPointWidget)
    {
      this->CurrentPointWidget->EnabledOff();
    }

    this->CurrentHandle = nullptr;
    this->InvokeEvent(svtkCommand::DisableEvent, nullptr);
    this->SetCurrentRenderer(nullptr);
    this->UnRegisterPickers();
  }

  this->Interactor->Render();
}

//----------------------------------------------------------------------
void svtkLineWidget::RegisterPickers()
{
  svtkPickingManager* pm = this->GetPickingManager();
  if (!pm)
  {
    return;
  }
  pm->AddPicker(this->HandlePicker, this);
  pm->AddPicker(this->LinePicker, this);
}

//----------------------------------------------------------------------------
void svtkLineWidget::ProcessEvents(
  svtkObject* svtkNotUsed(object), unsigned long event, void* clientdata, void* svtkNotUsed(calldata))
{
  svtkLineWidget* self = reinterpret_cast<svtkLineWidget*>(clientdata);

  // okay, let's do the right thing
  switch (event)
  {
    case svtkCommand::LeftButtonPressEvent:
      self->OnLeftButtonDown();
      break;
    case svtkCommand::LeftButtonReleaseEvent:
      self->OnLeftButtonUp();
      break;
    case svtkCommand::MiddleButtonPressEvent:
      self->OnMiddleButtonDown();
      break;
    case svtkCommand::MiddleButtonReleaseEvent:
      self->OnMiddleButtonUp();
      break;
    case svtkCommand::RightButtonPressEvent:
      self->OnRightButtonDown();
      break;
    case svtkCommand::RightButtonReleaseEvent:
      self->OnRightButtonUp();
      break;
    case svtkCommand::MouseMoveEvent:
      self->OnMouseMove();
      break;
  }
}

//----------------------------------------------------------------------
void svtkLineWidget::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  if (this->HandleProperty)
  {
    os << indent << "Handle Property: " << this->HandleProperty << "\n";
  }
  else
  {
    os << indent << "Handle Property: (none)\n";
  }
  if (this->SelectedHandleProperty)
  {
    os << indent << "Selected Handle Property: " << this->SelectedHandleProperty << "\n";
  }
  else
  {
    os << indent << "Selected Handle Property: (none)\n";
  }

  if (this->LineProperty)
  {
    os << indent << "Line Property: " << this->LineProperty << "\n";
  }
  else
  {
    os << indent << "Line Property: (none)\n";
  }
  if (this->SelectedLineProperty)
  {
    os << indent << "Selected Line Property: " << this->SelectedLineProperty << "\n";
  }
  else
  {
    os << indent << "Selected Line Property: (none)\n";
  }

  os << indent << "Constrain To Bounds: " << (this->ClampToBounds ? "On\n" : "Off\n");

  os << indent << "Align with: ";
  switch (this->Align)
  {
    case XAxis:
      os << "X Axis";
      break;
    case YAxis:
      os << "Y Axis";
      break;
    case ZAxis:
      os << "Z Axis";
      break;
    default:
      os << "None";
  }
  int res = this->LineSource->GetResolution();
  double* pt1 = this->LineSource->GetPoint1();
  double* pt2 = this->LineSource->GetPoint2();

  os << indent << "Resolution: " << res << "\n";
  os << indent << "Point 1: (" << pt1[0] << ", " << pt1[1] << ", " << pt1[2] << ")\n";
  os << indent << "Point 2: (" << pt2[0] << ", " << pt2[1] << ", " << pt2[2] << ")\n";
}

//----------------------------------------------------------------------------
void svtkLineWidget::BuildRepresentation()
{
  // int res = this->LineSource->GetResolution();
  double* pt1 = this->LineSource->GetPoint1();
  double* pt2 = this->LineSource->GetPoint2();

  this->HandleGeometry[0]->SetCenter(pt1);
  this->HandleGeometry[1]->SetCenter(pt2);
}

//----------------------------------------------------------------------------
void svtkLineWidget::SizeHandles()
{
  double radius = this->svtk3DWidget::SizeHandles(1.0);
  this->HandleGeometry[0]->SetRadius(radius);
  this->HandleGeometry[1]->SetRadius(radius);
}

//----------------------------------------------------------------------------
int svtkLineWidget::HighlightHandle(svtkProp* prop)
{
  // first unhighlight anything picked
  if (this->CurrentHandle)
  {
    this->CurrentHandle->SetProperty(this->HandleProperty);
  }

  // set the current handle
  this->CurrentHandle = static_cast<svtkActor*>(prop);

  // find the current handle
  if (this->CurrentHandle)
  {
    this->ValidPick = 1;
    this->HandlePicker->GetPickPosition(this->LastPickPosition);
    this->CurrentHandle->SetProperty(this->SelectedHandleProperty);
    return (this->CurrentHandle == this->Handle[0] ? 0 : 1);
  }
  return -1;
}

//----------------------------------------------------------------------------
int svtkLineWidget::ForwardEvent(unsigned long event)
{
  if (!this->CurrentPointWidget)
  {
    return 0;
  }

  this->CurrentPointWidget->ProcessEvents(this, event, this->CurrentPointWidget, nullptr);

  return 1;
}

//----------------------------------------------------------------------------
// assumed current handle is set
void svtkLineWidget::EnablePointWidget()
{
  // Set up the point widgets
  double x[3];
  if (this->CurrentHandle) // picking the handles
  {
    if (this->CurrentHandle == this->Handle[0])
    {
      this->CurrentPointWidget = this->PointWidget1;
      this->LineSource->GetPoint1(x);
    }
    else
    {
      this->CurrentPointWidget = this->PointWidget2;
      this->LineSource->GetPoint2(x);
    }
  }
  else // picking the line
  {
    this->CurrentPointWidget = this->PointWidget;
    this->LinePicker->GetPickPosition(x);
    this->LastPosition[0] = x[0];
    this->LastPosition[1] = x[1];
    this->LastPosition[2] = x[2];
  }

  double bounds[6];
  for (int i = 0; i < 3; i++)
  {
    bounds[2 * i] = x[i] - 0.1 * this->InitialLength;
    bounds[2 * i + 1] = x[i] + 0.1 * this->InitialLength;
  }

  // Note: translation mode is disabled and enabled to control
  // the proper positioning of the bounding box.
  this->CurrentPointWidget->SetInteractor(this->Interactor);
  this->CurrentPointWidget->TranslationModeOff();
  this->CurrentPointWidget->SetPlaceFactor(1.0);
  this->CurrentPointWidget->PlaceWidget(bounds);
  this->CurrentPointWidget->TranslationModeOn();
  this->CurrentPointWidget->SetPosition(x);
  this->CurrentPointWidget->SetCurrentRenderer(this->CurrentRenderer);
  this->CurrentPointWidget->On();
}

//----------------------------------------------------------------------------
// assumed current handle is set
void svtkLineWidget::DisablePointWidget()
{
  if (this->CurrentPointWidget)
  {
    this->CurrentPointWidget->Off();
  }
  this->CurrentPointWidget = nullptr;
}

//----------------------------------------------------------------------------
void svtkLineWidget::HighlightHandles(int highlight)
{
  if (highlight)
  {
    this->ValidPick = 1;
    this->HandlePicker->GetPickPosition(this->LastPickPosition);
    this->Handle[0]->SetProperty(this->SelectedHandleProperty);
    this->Handle[1]->SetProperty(this->SelectedHandleProperty);
  }
  else
  {
    this->Handle[0]->SetProperty(this->HandleProperty);
    this->Handle[1]->SetProperty(this->HandleProperty);
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::HighlightLine(int highlight)
{
  if (highlight)
  {
    this->ValidPick = 1;
    this->LinePicker->GetPickPosition(this->LastPickPosition);
    this->LineActor->SetProperty(this->SelectedLineProperty);
  }
  else
  {
    this->LineActor->SetProperty(this->LineProperty);
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnLeftButtonDown()
{
  int forward = 0;

  int X = this->Interactor->GetEventPosition()[0];
  int Y = this->Interactor->GetEventPosition()[1];

  // Okay, make sure that the pick is in the current renderer
  if (!this->CurrentRenderer || !this->CurrentRenderer->IsInViewport(X, Y))
  {
    this->State = svtkLineWidget::Outside;
    return;
  }

  // Okay, we can process this. Try to pick handles first;
  // if no handles picked, then try to pick the line.
  svtkAssemblyPath* path = this->GetAssemblyPath(X, Y, 0., this->HandlePicker);

  if (path != nullptr)
  {
    this->EventCallbackCommand->SetAbortFlag(1);
    this->StartInteraction();
    this->InvokeEvent(svtkCommand::StartInteractionEvent, nullptr);
    this->State = svtkLineWidget::MovingHandle;
    this->HighlightHandle(path->GetFirstNode()->GetViewProp());
    this->EnablePointWidget();
    forward = this->ForwardEvent(svtkCommand::LeftButtonPressEvent);
  }
  else
  {
    path = this->GetAssemblyPath(X, Y, 0., this->LinePicker);

    if (path != nullptr)
    {
      this->EventCallbackCommand->SetAbortFlag(1);
      this->StartInteraction();
      this->InvokeEvent(svtkCommand::StartInteractionEvent, nullptr);
      this->State = svtkLineWidget::MovingLine;
      this->HighlightLine(1);
      this->EnablePointWidget();
      forward = this->ForwardEvent(svtkCommand::LeftButtonPressEvent);
    }
    else
    {
      this->State = svtkLineWidget::Outside;
      this->HighlightHandle(nullptr);
      return;
    }
  }

  if (!forward)
  {
    this->Interactor->Render();
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnLeftButtonUp()
{
  if (this->State == svtkLineWidget::Outside || this->State == svtkLineWidget::Start)
  {
    return;
  }

  this->State = svtkLineWidget::Start;
  this->HighlightHandle(nullptr);
  this->HighlightLine(0);

  this->SizeHandles();

  int forward = this->ForwardEvent(svtkCommand::LeftButtonReleaseEvent);
  this->DisablePointWidget();

  this->EventCallbackCommand->SetAbortFlag(1);
  this->EndInteraction();
  this->InvokeEvent(svtkCommand::EndInteractionEvent, nullptr);
  if (!forward)
  {
    this->Interactor->Render();
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnMiddleButtonDown()
{
  int forward = 0;

  int X = this->Interactor->GetEventPosition()[0];
  int Y = this->Interactor->GetEventPosition()[1];

  // Okay, make sure that the pick is in the current renderer
  if (!this->CurrentRenderer || !this->CurrentRenderer->IsInViewport(X, Y))
  {
    this->State = svtkLineWidget::Outside;
    return;
  }

  // Okay, we can process this. Try to pick handles first;
  // if no handles picked, then pick the bounding box.
  svtkAssemblyPath* path = this->GetAssemblyPath(X, Y, 0., this->HandlePicker);

  if (path != nullptr)
  {
    this->EventCallbackCommand->SetAbortFlag(1);
    this->StartInteraction();
    this->InvokeEvent(svtkCommand::StartInteractionEvent, nullptr);
    this->State = svtkLineWidget::MovingLine;
    this->HighlightHandles(1);
    this->HighlightLine(1);
    this->EnablePointWidget();
    forward = this->ForwardEvent(svtkCommand::LeftButtonPressEvent);
  }
  else
  {
    path = this->GetAssemblyPath(X, Y, 0., this->LinePicker);

    if (path != nullptr)
    {
      this->EventCallbackCommand->SetAbortFlag(1);
      this->StartInteraction();
      this->InvokeEvent(svtkCommand::StartInteractionEvent, nullptr);
      // The highlight methods set the LastPickPosition, so they are ordered
      this->HighlightHandles(1);
      this->HighlightLine(1);
      this->State = svtkLineWidget::MovingLine;
      this->EnablePointWidget();
      forward = this->ForwardEvent(svtkCommand::LeftButtonPressEvent);
    }
    else
    {
      this->State = svtkLineWidget::Outside;
      return;
    }
  }

  if (!forward)
  {
    this->Interactor->Render();
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnMiddleButtonUp()
{
  if (this->State == svtkLineWidget::Outside || this->State == svtkLineWidget::Start)
  {
    return;
  }

  this->State = svtkLineWidget::Start;
  this->HighlightLine(0);
  this->HighlightHandles(0);

  this->SizeHandles();

  int forward = this->ForwardEvent(svtkCommand::LeftButtonReleaseEvent);
  this->DisablePointWidget();

  this->EventCallbackCommand->SetAbortFlag(1);
  this->EndInteraction();
  this->InvokeEvent(svtkCommand::EndInteractionEvent, nullptr);
  if (!forward)
  {
    this->Interactor->Render();
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnRightButtonDown()
{
  int X = this->Interactor->GetEventPosition()[0];
  int Y = this->Interactor->GetEventPosition()[1];

  // Okay, make sure that the pick is in the current renderer
  if (!this->CurrentRenderer || !this->CurrentRenderer->IsInViewport(X, Y))
  {
    this->State = svtkLineWidget::Outside;
    return;
  }

  // Okay, we can process this. Try to pick handles first;
  // if no handles picked, then pick the bounding box.
  svtkAssemblyPath* path = this->GetAssemblyPath(X, Y, 0., this->HandlePicker);

  if (path != nullptr)
  {
    this->HighlightLine(1);
    this->HighlightHandles(1);
    this->State = svtkLineWidget::Scaling;
  }
  else
  {
    path = this->GetAssemblyPath(X, Y, 0., this->LinePicker);

    if (path != nullptr)
    {
      this->HighlightHandles(1);
      this->HighlightLine(1);
      this->State = svtkLineWidget::Scaling;
    }
    else
    {
      this->State = svtkLineWidget::Outside;
      this->HighlightLine(0);
      return;
    }
  }

  this->EventCallbackCommand->SetAbortFlag(1);
  this->StartInteraction();
  this->InvokeEvent(svtkCommand::StartInteractionEvent, nullptr);
  this->Interactor->Render();
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnRightButtonUp()
{
  if (this->State == svtkLineWidget::Outside || this->State == svtkLineWidget::Start)
  {
    return;
  }

  this->State = svtkLineWidget::Start;
  this->HighlightLine(0);
  this->HighlightHandles(0);

  this->SizeHandles();

  this->EventCallbackCommand->SetAbortFlag(1);
  this->EndInteraction();
  this->InvokeEvent(svtkCommand::EndInteractionEvent, nullptr);
  this->Interactor->Render();
}

//----------------------------------------------------------------------------
void svtkLineWidget::OnMouseMove()
{
  // See whether we're active
  if (this->State == svtkLineWidget::Outside || this->State == svtkLineWidget::Start)
  {
    return;
  }

  int X = this->Interactor->GetEventPosition()[0];
  int Y = this->Interactor->GetEventPosition()[1];

  // Do different things depending on state
  // Calculations everybody does
  double focalPoint[4], pickPoint[4], prevPickPoint[4];
  double z;

  svtkCamera* camera = this->CurrentRenderer->GetActiveCamera();
  if (!camera)
  {
    return;
  }

  // Compute the two points defining the motion vector
  this->ComputeWorldToDisplay(
    this->LastPickPosition[0], this->LastPickPosition[1], this->LastPickPosition[2], focalPoint);
  z = focalPoint[2];
  this->ComputeDisplayToWorld(double(this->Interactor->GetLastEventPosition()[0]),
    double(this->Interactor->GetLastEventPosition()[1]), z, prevPickPoint);
  this->ComputeDisplayToWorld(double(X), double(Y), z, pickPoint);

  // Process the motion
  int forward = 0;
  if (this->State == svtkLineWidget::MovingHandle)
  {
    forward = this->ForwardEvent(svtkCommand::MouseMoveEvent);
  }
  else if (this->State == svtkLineWidget::MovingLine)
  {
    forward = this->ForwardEvent(svtkCommand::MouseMoveEvent);
  }
  else if (this->State == svtkLineWidget::Scaling)
  {
    this->Scale(prevPickPoint, pickPoint, X, Y);
  }

  // Interact, if desired
  this->EventCallbackCommand->SetAbortFlag(1);
  this->InvokeEvent(svtkCommand::InteractionEvent, nullptr);
  if (!forward)
  {
    this->Interactor->Render();
  }
}

//----------------------------------------------------------------------------
void svtkLineWidget::Scale(double* p1, double* p2, int svtkNotUsed(X), int Y)
{
  // Get the motion vector
  double v[3];
  v[0] = p2[0] - p1[0];
  v[1] = p2[1] - p1[1];
  v[2] = p2[2] - p1[2];

  // int res = this->LineSource->GetResolution();
  double* pt1 = this->LineSource->GetPoint1();
  double* pt2 = this->LineSource->GetPoint2();

  double center[3];
  center[0] = (pt1[0] + pt2[0]) / 2.0;
  center[1] = (pt1[1] + pt2[1]) / 2.0;
  center[2] = (pt1[2] + pt2[2]) / 2.0;

  // Compute the scale factor
  double sf = svtkMath::Norm(v) / sqrt(svtkMath::Distance2BetweenPoints(pt1, pt2));
  if (Y > this->Interactor->GetLastEventPosition()[1])
  {
    sf = 1.0 + sf;
  }
  else
  {
    sf = 1.0 - sf;
  }

  // Move the end points
  double point1[3], point2[3];
  for (int i = 0; i < 3; i++)
  {
    point1[i] = sf * (pt1[i] - center[i]) + center[i];
    point2[i] = sf * (pt2[i] - center[i]) + center[i];
  }

  this->LineSource->SetPoint1(point1);
  this->LineSource->SetPoint2(point2);
  this->LineSource->Update();

  this->BuildRepresentation();
}

//----------------------------------------------------------------------------
void svtkLineWidget::CreateDefaultProperties()
{
  // Handle properties
  this->HandleProperty = svtkProperty::New();
  this->HandleProperty->SetColor(1, 1, 1);

  this->SelectedHandleProperty = svtkProperty::New();
  this->SelectedHandleProperty->SetColor(1, 0, 0);

  // Line properties
  this->LineProperty = svtkProperty::New();
  this->LineProperty->SetRepresentationToWireframe();
  this->LineProperty->SetAmbient(1.0);
  this->LineProperty->SetAmbientColor(1.0, 1.0, 1.0);
  this->LineProperty->SetLineWidth(2.0);

  this->SelectedLineProperty = svtkProperty::New();
  this->SelectedLineProperty->SetRepresentationToWireframe();
  this->SelectedLineProperty->SetAmbient(1.0);
  this->SelectedLineProperty->SetAmbientColor(0.0, 1.0, 0.0);
  this->SelectedLineProperty->SetLineWidth(2.0);
}

//----------------------------------------------------------------------------
void svtkLineWidget::PlaceWidget(double bds[6])
{
  int i;
  double bounds[6], center[3];

  this->AdjustBounds(bds, bounds, center);

  if (this->Align == svtkLineWidget::YAxis)
  {
    this->LineSource->SetPoint1(center[0], bounds[2], center[2]);
    this->LineSource->SetPoint2(center[0], bounds[3], center[2]);
  }
  else if (this->Align == svtkLineWidget::ZAxis)
  {
    this->LineSource->SetPoint1(center[0], center[1], bounds[4]);
    this->LineSource->SetPoint2(center[0], center[1], bounds[5]);
  }
  else if (this->Align == svtkLineWidget::XAxis) // default or x-aligned
  {
    this->LineSource->SetPoint1(bounds[0], center[1], center[2]);
    this->LineSource->SetPoint2(bounds[1], center[1], center[2]);
  }
  this->LineSource->Update();

  for (i = 0; i < 6; i++)
  {
    this->InitialBounds[i] = bounds[i];
  }
  this->InitialLength = sqrt((bounds[1] - bounds[0]) * (bounds[1] - bounds[0]) +
    (bounds[3] - bounds[2]) * (bounds[3] - bounds[2]) +
    (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));

  // Position the handles at the end of the lines
  this->BuildRepresentation();
  this->SizeHandles();
}

//----------------------------------------------------------------------------
void svtkLineWidget::SetPoint1(double x, double y, double z)
{
  double xyz[3];
  xyz[0] = x;
  xyz[1] = y;
  xyz[2] = z;

  if (this->ClampToBounds)
  {
    this->ClampPosition(xyz);
    this->PointWidget1->SetPosition(xyz);
  }
  this->LineSource->SetPoint1(xyz);
  this->BuildRepresentation();
}

//----------------------------------------------------------------------------
void svtkLineWidget::SetPoint2(double x, double y, double z)
{
  double xyz[3];
  xyz[0] = x;
  xyz[1] = y;
  xyz[2] = z;

  if (this->ClampToBounds)
  {
    this->ClampPosition(xyz);
    this->PointWidget2->SetPosition(xyz);
  }
  this->LineSource->SetPoint2(xyz);
  this->BuildRepresentation();
}

//----------------------------------------------------------------------------
void svtkLineWidget::SetLinePosition(double x[3])
{
  double p1[3], p2[3], v[3];

  // vector of motion
  v[0] = x[0] - this->LastPosition[0];
  v[1] = x[1] - this->LastPosition[1];
  v[2] = x[2] - this->LastPosition[2];

  // update position
  this->GetPoint1(p1);
  this->GetPoint2(p2);
  for (int i = 0; i < 3; i++)
  {
    p1[i] += v[i];
    p2[i] += v[i];
  }

  // See whether we can move
  if (this->ClampToBounds && (!this->InBounds(p1) || !this->InBounds(p2)))
  {
    this->PointWidget->SetPosition(this->LastPosition);
    return;
  }

  this->SetPoint1(p1);
  this->SetPoint2(p2);

  // remember last position
  this->LastPosition[0] = x[0];
  this->LastPosition[1] = x[1];
  this->LastPosition[2] = x[2];
}

//----------------------------------------------------------------------------
void svtkLineWidget::ClampPosition(double x[3])
{
  for (int i = 0; i < 3; i++)
  {
    if (x[i] < this->InitialBounds[2 * i])
    {
      x[i] = this->InitialBounds[2 * i];
    }
    if (x[i] > this->InitialBounds[2 * i + 1])
    {
      x[i] = this->InitialBounds[2 * i + 1];
    }
  }
}

//----------------------------------------------------------------------------
int svtkLineWidget::InBounds(double x[3])
{
  for (int i = 0; i < 3; i++)
  {
    if (x[i] < this->InitialBounds[2 * i] || x[i] > this->InitialBounds[2 * i + 1])
    {
      return 0;
    }
  }
  return 1;
}

//----------------------------------------------------------------------------
void svtkLineWidget::GetPolyData(svtkPolyData* pd)
{
  pd->ShallowCopy(this->LineSource->GetOutput());
}