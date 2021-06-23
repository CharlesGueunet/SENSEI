/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkCompassRepresentation.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

/**
 * @class   svtkCompassRepresentation
 * @brief   provide a compass
 *
 * This class is used to represent and render a compass.
 */

#ifndef svtkCompassRepresentation_h
#define svtkCompassRepresentation_h

#include "svtkCenteredSliderRepresentation.h" // to use in a SP
#include "svtkContinuousValueWidgetRepresentation.h"
#include "svtkCoordinate.h"       // For svtkViewportCoordinateMacro
#include "svtkGeovisCoreModule.h" // For export macro
#include "svtkSmartPointer.h"     // used for SmartPointers

class svtkActor2D;
class svtkPoints;
class svtkCellArray;
class svtkPolyData;
class svtkPolyDataMapper2D;
class svtkCoordinate;
class svtkProperty2D;
class svtkPropCollection;
class svtkWindow;
class svtkViewport;
class svtkTransform;
class svtkTransformPolyDataFilter;
class svtkTextProperty;
class svtkTextActor;

class SVTKGEOVISCORE_EXPORT svtkCompassRepresentation : public svtkContinuousValueWidgetRepresentation
{
public:
  /**
   * Instantiate the class.
   */
  static svtkCompassRepresentation* New();

  //@{
  /**
   * Standard methods for the class.
   */
  svtkTypeMacro(svtkCompassRepresentation, svtkContinuousValueWidgetRepresentation);
  void PrintSelf(ostream& os, svtkIndent indent) override;
  //@}

  /**
   * Position the first end point of the slider. Note that this point is an
   * instance of svtkCoordinate, meaning that Point 1 can be specified in a
   * variety of coordinate systems, and can even be relative to another
   * point. To set the point, you'll want to get the Point1Coordinate and
   * then invoke the necessary methods to put it into the correct coordinate
   * system and set the correct initial value.
   */
  svtkCoordinate* GetPoint1Coordinate();

  /**
   * Position the second end point of the slider. Note that this point is an
   * instance of svtkCoordinate, meaning that Point 1 can be specified in a
   * variety of coordinate systems, and can even be relative to another
   * point. To set the point, you'll want to get the Point2Coordinate and
   * then invoke the necessary methods to put it into the correct coordinate
   * system and set the correct initial value.
   */
  svtkCoordinate* GetPoint2Coordinate();

  //@{
  /**
   * Get the slider properties. The properties of the slider when selected
   * and unselected can be manipulated.
   */
  svtkGetObjectMacro(RingProperty, svtkProperty2D);
  //@}

  //@{
  /**
   * Get the selection property. This property is used to modify the
   * appearance of selected objects (e.g., the slider).
   */
  svtkGetObjectMacro(SelectedProperty, svtkProperty2D);
  //@}

  //@{
  /**
   * Set/Get the properties for the label and title text.
   */
  svtkGetObjectMacro(LabelProperty, svtkTextProperty);
  //@}

  //@{
  /**
   * Methods to interface with the svtkSliderWidget. The PlaceWidget() method
   * assumes that the parameter bounds[6] specifies the location in display
   * space where the widget should be placed.
   */
  void PlaceWidget(double bounds[6]) override;
  void BuildRepresentation() override;
  void StartWidgetInteraction(double eventPos[2]) override;
  void WidgetInteraction(double eventPos[2]) override;
  virtual void TiltWidgetInteraction(double eventPos[2]);
  virtual void DistanceWidgetInteraction(double eventPos[2]);
  int ComputeInteractionState(int X, int Y, int modify = 0) override;
  void Highlight(int) override;
  //@}

  //@{
  /**
   * Methods supporting the rendering process.
   */
  void GetActors(svtkPropCollection*) override;
  void ReleaseGraphicsResources(svtkWindow*) override;
  int RenderOverlay(svtkViewport*) override;
  int RenderOpaqueGeometry(svtkViewport*) override;
  //@}

  virtual void SetHeading(double value);
  virtual double GetHeading();
  virtual void SetTilt(double value);
  virtual double GetTilt();
  virtual void UpdateTilt(double time);
  virtual void EndTilt();
  virtual void SetDistance(double value);
  virtual double GetDistance();
  virtual void UpdateDistance(double time);
  virtual void EndDistance();
  void SetRenderer(svtkRenderer* ren) override;

  // Enums are used to describe what is selected
  enum _InteractionState
  {
    Outside = 0,
    Inside,
    Adjusting,
    TiltDown,
    TiltUp,
    TiltAdjusting,
    DistanceOut,
    DistanceIn,
    DistanceAdjusting
  };

protected:
  svtkCompassRepresentation();
  ~svtkCompassRepresentation() override;

  // Positioning the widget
  svtkCoordinate* Point1Coordinate;
  svtkCoordinate* Point2Coordinate;

  // radius values
  double InnerRadius;
  double OuterRadius;

  // tilt and distance rep

  svtkSmartPointer<svtkCenteredSliderRepresentation> TiltRepresentation;
  svtkSmartPointer<svtkCenteredSliderRepresentation> DistanceRepresentation;

  // Define the geometry. It is constructed in canaonical position
  // along the x-axis and then rotated into position.
  svtkTransform* XForm;
  svtkPoints* Points;

  svtkPolyData* Ring;
  svtkTransformPolyDataFilter* RingXForm;
  svtkPolyDataMapper2D* RingMapper;
  svtkActor2D* RingActor;
  svtkProperty2D* RingProperty;

  svtkPolyDataMapper2D* BackdropMapper;
  svtkActor2D* Backdrop;

  svtkTextProperty* LabelProperty;
  svtkTextActor* LabelActor;
  svtkTextProperty* StatusProperty;
  svtkTextActor* StatusActor;

  svtkProperty2D* SelectedProperty;

  // build the tube geometry
  void BuildRing();
  void BuildBackdrop();

  // used for positioning etc
  void GetCenterAndUnitRadius(int center[2], double& radius);

  int HighlightState;

  double Heading;
  double Tilt;
  double Distance;

private:
  svtkCompassRepresentation(const svtkCompassRepresentation&) = delete;
  void operator=(const svtkCompassRepresentation&) = delete;
};

#endif