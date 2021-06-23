/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkInteractorStyleTrackballCamera.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkInteractorStyleTrackballCamera
 * @brief   interactive manipulation of the camera
 *
 * svtkInteractorStyleTrackballCamera allows the user to interactively
 * manipulate (rotate, pan, etc.) the camera, the viewpoint of the scene.  In
 * trackball interaction, the magnitude of the mouse motion is proportional
 * to the camera motion associated with a particular mouse binding. For
 * example, small left-button motions cause small changes in the rotation of
 * the camera around its focal point. For a 3-button mouse, the left button
 * is for rotation, the right button for zooming, the middle button for
 * panning, ctrl + left button for spinning, and shift + right button for
 * environment rotation. (With fewer mouse buttons, ctrl + shift + left button
 * is for zooming, and shift + left button is for panning.)
 *
 * @sa
 * svtkInteractorStyleTrackballActor svtkInteractorStyleJoystickCamera
 * svtkInteractorStyleJoystickActor
 */

#ifndef svtkInteractorStyleTrackballCamera_h
#define svtkInteractorStyleTrackballCamera_h

#include "svtkInteractionStyleModule.h" // For export macro
#include "svtkInteractorStyle.h"

class SVTKINTERACTIONSTYLE_EXPORT svtkInteractorStyleTrackballCamera : public svtkInteractorStyle
{
public:
  static svtkInteractorStyleTrackballCamera* New();
  svtkTypeMacro(svtkInteractorStyleTrackballCamera, svtkInteractorStyle);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Event bindings controlling the effects of pressing mouse buttons
   * or moving the mouse.
   */
  void OnMouseMove() override;
  void OnLeftButtonDown() override;
  void OnLeftButtonUp() override;
  void OnMiddleButtonDown() override;
  void OnMiddleButtonUp() override;
  void OnRightButtonDown() override;
  void OnRightButtonUp() override;
  void OnMouseWheelForward() override;
  void OnMouseWheelBackward() override;
  //@}

  // These methods for the different interactions in different modes
  // are overridden in subclasses to perform the correct motion. Since
  // they are called by OnTimer, they do not have mouse coord parameters
  // (use interactor's GetEventPosition and GetLastEventPosition)
  void Rotate() override;
  void Spin() override;
  void Pan() override;
  void Dolly() override;
  void EnvironmentRotate() override;

  //@{
  /**
   * Set the apparent sensitivity of the interactor style to mouse motion.
   */
  svtkSetMacro(MotionFactor, double);
  svtkGetMacro(MotionFactor, double);
  //@}

protected:
  svtkInteractorStyleTrackballCamera();
  ~svtkInteractorStyleTrackballCamera() override;

  double MotionFactor;

  virtual void Dolly(double factor);

private:
  svtkInteractorStyleTrackballCamera(const svtkInteractorStyleTrackballCamera&) = delete;
  void operator=(const svtkInteractorStyleTrackballCamera&) = delete;
};

#endif