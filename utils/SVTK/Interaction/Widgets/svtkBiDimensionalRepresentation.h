/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkBiDimensionalRepresentation.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkBiDimensionalRepresentation
 * @brief   represent the svtkBiDimensionalWidget
 *
 * The svtkBiDimensionalRepresentation is used to represent the
 * bi-dimensional measure of an object. This representation
 * consists of two perpendicular lines defined by four
 * svtkHandleRepresentations. The four handles can be independently
 * manipulated consistent with the orthogonal constraint on the lines. (Note:
 * the four points are referred to as Point1, Point2, Point3 and
 * Point4. Point1 and Point2 define the first line; and Point3 and Point4
 * define the second orthogonal line.) This particular class is an abstract
 * class, contrete subclasses (e.g., svtkBiDimensionalRepresentation2D) actual
 * implement the widget.
 *
 * To create this widget, you click to place the first two points. The third
 * point is mirrored with the fourth point; when you place the third point
 * (which is orthogonal to the lined defined by the first two points), the
 * fourth point is dropped as well. After definition, the four points can be
 * moved (in constrained fashion, preserving orthogonality). Further, the
 * entire widget can be translated by grabbing the center point of the widget;
 * each line can be moved along the other line; and the entire widget can be
 * rotated around its center point.
 *
 * @sa
 * svtkAngleWidget svtkHandleRepresentation svtkBiDimensionalRepresentation2D
 */

#ifndef svtkBiDimensionalRepresentation_h
#define svtkBiDimensionalRepresentation_h

#include "svtkInteractionWidgetsModule.h" // For export macro
#include "svtkWidgetRepresentation.h"

class svtkHandleRepresentation;

class SVTKINTERACTIONWIDGETS_EXPORT svtkBiDimensionalRepresentation : public svtkWidgetRepresentation
{
public:
  //@{
  /**
   * Standard SVTK methods.
   */
  svtkTypeMacro(svtkBiDimensionalRepresentation, svtkWidgetRepresentation);
  void PrintSelf(ostream& os, svtkIndent indent) override;
  //@}

  //@{
  /**
   * Methods to Set/Get the coordinates of the four points defining
   * this representation. Note that methods are available for both
   * display and world coordinates.
   */
  virtual void SetPoint1WorldPosition(double pos[3]);
  virtual void SetPoint2WorldPosition(double pos[3]);
  virtual void SetPoint3WorldPosition(double pos[3]);
  virtual void SetPoint4WorldPosition(double pos[3]);
  virtual void GetPoint1WorldPosition(double pos[3]);
  virtual void GetPoint2WorldPosition(double pos[3]);
  virtual void GetPoint3WorldPosition(double pos[3]);
  virtual void GetPoint4WorldPosition(double pos[3]);
  virtual void SetPoint1DisplayPosition(double pos[3]);
  virtual void SetPoint2DisplayPosition(double pos[3]);
  virtual void SetPoint3DisplayPosition(double pos[3]);
  virtual void SetPoint4DisplayPosition(double pos[3]);
  virtual void GetPoint1DisplayPosition(double pos[3]);
  virtual void GetPoint2DisplayPosition(double pos[3]);
  virtual void GetPoint3DisplayPosition(double pos[3]);
  virtual void GetPoint4DisplayPosition(double pos[3]);
  //@}

  //@{
  /**
   * Set/Get the handle representations used within the
   * svtkBiDimensionalRepresentation2D. (Note: properties can be set by
   * grabbing these representations and setting the properties
   * appropriately.)
   */
  svtkGetObjectMacro(Point1Representation, svtkHandleRepresentation);
  svtkGetObjectMacro(Point2Representation, svtkHandleRepresentation);
  svtkGetObjectMacro(Point3Representation, svtkHandleRepresentation);
  svtkGetObjectMacro(Point4Representation, svtkHandleRepresentation);
  //@}

  //@{
  /**
   * Special methods for turning off the lines that define the bi-dimensional
   * measure. Generally these methods are used by the svtkBiDimensionalWidget to
   * control the appearance of the widget. Note: turning off Line1 actually turns
   * off Line1 and Line2.
   */
  svtkSetMacro(Line1Visibility, svtkTypeBool);
  svtkGetMacro(Line1Visibility, svtkTypeBool);
  svtkBooleanMacro(Line1Visibility, svtkTypeBool);
  svtkSetMacro(Line2Visibility, svtkTypeBool);
  svtkGetMacro(Line2Visibility, svtkTypeBool);
  svtkBooleanMacro(Line2Visibility, svtkTypeBool);
  //@}

  //@{
  /**
   * This method is used to specify the type of handle representation to use
   * for the four internal svtkHandleRepresentations within
   * svtkBiDimensionalRepresentation.  To use this method, create a dummy
   * svtkHandleRepresentation (or subclass), and then invoke this method with
   * this dummy. Then the svtkBiDimensionalRepresentation uses this dummy to
   * clone four svtkHandleRepresentations of the same type. Make sure you set the
   * handle representation before the widget is enabled. (The method
   * InstantiateHandleRepresentation() is invoked by the svtkBiDimensionalWidget
   * for the purposes of cloning.)
   */
  void SetHandleRepresentation(svtkHandleRepresentation* handle);
  virtual void InstantiateHandleRepresentation();
  //@}

  //@{
  /**
   * The tolerance representing the distance to the representation (in
   * pixels) in which the cursor is considered near enough to the
   * representation to be active.
   */
  svtkSetClampMacro(Tolerance, int, 1, 100);
  svtkGetMacro(Tolerance, int);
  //@}

  /**
   * Return the length of the line defined by (Point1,Point2). This is the
   * distance in the world coordinate system.
   */
  virtual double GetLength1();

  /**
   * Return the length of the line defined by (Point3,Point4). This is the
   * distance in the world coordinate system.
   */
  virtual double GetLength2();

  //@{
  /**
   * Specify the format to use for labelling the distance. Note that an empty
   * string results in no label, or a format string without a "%" character
   * will not print the distance value.
   */
  svtkSetStringMacro(LabelFormat);
  svtkGetStringMacro(LabelFormat);
  //@}

  // Used to communicate about the state of the representation
  enum
  {
    Outside = 0,
    NearP1,
    NearP2,
    NearP3,
    NearP4,
    OnL1Inner,
    OnL1Outer,
    OnL2Inner,
    OnL2Outer,
    OnCenter
  };

  //@{
  /**
   * Toggle whether to display the label above or below the widget.
   * Defaults to 1.
   */
  svtkSetMacro(ShowLabelAboveWidget, svtkTypeBool);
  svtkGetMacro(ShowLabelAboveWidget, svtkTypeBool);
  svtkBooleanMacro(ShowLabelAboveWidget, svtkTypeBool);
  //@}

  //@{
  /**
   * Set/get the id to display in the label.
   */
  void SetID(svtkIdType id);
  svtkGetMacro(ID, svtkIdType);
  //@}

  /**
   * Get the text shown in the widget's label.
   */
  virtual char* GetLabelText() = 0;

  //@{
  /**
   * Get the position of the widget's label in display coordinates.
   */
  virtual double* GetLabelPosition() = 0;
  virtual void GetLabelPosition(double pos[3]) = 0;
  virtual void GetWorldLabelPosition(double pos[3]) = 0;
  //@}

  //@{
  /**
   * These are methods that satisfy svtkWidgetRepresentation's API.
   */
  virtual void StartWidgetDefinition(double e[2]) = 0;
  virtual void Point2WidgetInteraction(double e[2]) = 0;
  virtual void Point3WidgetInteraction(double e[2]) = 0;
  virtual void StartWidgetManipulation(double e[2]) = 0;
  //@}

protected:
  svtkBiDimensionalRepresentation();
  ~svtkBiDimensionalRepresentation() override;

  // Keep track if modifier is set
  int Modifier;

  // The handle and the rep used to close the handles
  svtkHandleRepresentation* HandleRepresentation;
  svtkHandleRepresentation* Point1Representation;
  svtkHandleRepresentation* Point2Representation;
  svtkHandleRepresentation* Point3Representation;
  svtkHandleRepresentation* Point4Representation;

  // Selection tolerance for the handles
  int Tolerance;

  // Visibility of the lines
  svtkTypeBool Line1Visibility;
  svtkTypeBool Line2Visibility;

  svtkIdType ID;
  int IDInitialized;

  // Internal variables
  double P1World[3];
  double P2World[3];
  double P3World[3];
  double P4World[3];
  double P21World[3];
  double P43World[3];
  double T21;
  double T43;
  double CenterWorld[3];
  double StartEventPositionWorld[4];

  // Format for printing the distance
  char* LabelFormat;

  // toggle to determine whether to place text above or below widget
  svtkTypeBool ShowLabelAboveWidget;

private:
  svtkBiDimensionalRepresentation(const svtkBiDimensionalRepresentation&) = delete;
  void operator=(const svtkBiDimensionalRepresentation&) = delete;
};

#endif