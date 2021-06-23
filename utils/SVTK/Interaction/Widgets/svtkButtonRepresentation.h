/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkButtonRepresentation.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkButtonRepresentation
 * @brief   abstract class defines the representation for a svtkButtonWidget
 *
 * This abstract class is used to specify how the svtkButtonWidget should
 * interact with representations of the svtkButtonWidget. This class may be
 * subclassed so that alternative representations can be created. The class
 * defines an API, and a default implementation, that the svtkButtonWidget
 * interacts with to render itself in the scene.
 *
 * The svtkButtonWidget assumes an n-state button so that traversal methods
 * are available for changing, querying and manipulating state. Derived
 * classed determine the actual appearance. The state is represented by an
 * integral value 0<=state<numStates.
 *
 * To use this representation, always begin by specifying the number of states.
 * Then follow with the necessary information to represent each state (done through
 * a subclass API).
 *
 * @sa
 * svtkButtonWidget
 */

#ifndef svtkButtonRepresentation_h
#define svtkButtonRepresentation_h

#include "svtkInteractionWidgetsModule.h" // For export macro
#include "svtkWidgetRepresentation.h"

class SVTKINTERACTIONWIDGETS_EXPORT svtkButtonRepresentation : public svtkWidgetRepresentation
{
public:
  //@{
  /**
   * Standard methods for the class.
   */
  svtkTypeMacro(svtkButtonRepresentation, svtkWidgetRepresentation);
  void PrintSelf(ostream& os, svtkIndent indent) override;
  //@}

  //@{
  /**
   * Retrieve the current button state.
   */
  svtkSetClampMacro(NumberOfStates, int, 1, SVTK_INT_MAX);
  //@}

  //@{
  /**
   * Retrieve the current button state.
   */
  svtkGetMacro(State, int);
  //@}

  //@{
  /**
   * Manipulate the state. Note that the NextState() and PreviousState() methods
   * use modulo traversal. The "state" integral value will be clamped within
   * the possible state values (0<=state<NumberOfStates). Note that subclasses
   * will override these methods in many cases.
   */
  virtual void SetState(int state);
  virtual void NextState();
  virtual void PreviousState();
  //@}

  enum _InteractionState
  {
    Outside = 0,
    Inside
  };

  //@{
  /**
   * These methods control the appearance of the button as it is being
   * interacted with. Subclasses will behave differently depending on their
   * particulars.  HighlightHovering is used when the mouse pointer moves
   * over the button. HighlightSelecting is set when the button is selected.
   * Otherwise, the HighlightNormal is used. The Highlight() method will throw
   * a svtkCommand::HighlightEvent.
   */
  enum _HighlightState
  {
    HighlightNormal,
    HighlightHovering,
    HighlightSelecting
  };
  void Highlight(int) override;
  svtkGetMacro(HighlightState, int);
  //@}

  /**
   * Satisfy some of svtkProp's API.
   */
  void ShallowCopy(svtkProp* prop) override;

protected:
  svtkButtonRepresentation();
  ~svtkButtonRepresentation() override;

  // Values
  int NumberOfStates;
  int State;
  int HighlightState;

private:
  svtkButtonRepresentation(const svtkButtonRepresentation&) = delete;
  void operator=(const svtkButtonRepresentation&) = delete;
};

#endif