/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkmyEx2.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
//
// This example creates a polygonal model of a cone, and then rendered it to
// the screen. It willrotate the cone 360 degrees and then exit. The basic
// setup of source -> mapper -> actor -> renderer -> renderwindow is
// typical of most SVTK programs.
//

//
// First include the required header files for the svtk classes we are using
//
#include "svtkActor.h"
#include "svtkCamera.h"
#include "svtkConeSource.h"
#include "svtkPolyDataMapper.h"
#include "svtkRenderWindow.h"
#include "svtkRenderer.h"

int main()
{
  //
  // Next we create an instance of svtkConeSource and set some of its
  // properties
  //
  svtkConeSource* cone = svtkConeSource::New();
  cone->SetHeight(3.0);
  cone->SetRadius(1.0);
  cone->SetResolution(10);

  //
  // We create an instance of svtkPolyDataMapper to map the polygonal data
  // into graphics primitives. We connect the output of the cone souece
  // to the input of this mapper
  //
  svtkPolyDataMapper* coneMapper = svtkPolyDataMapper::New();
  coneMapper->SetInputConnection(cone->GetOutputPort());

  //
  // Create an actor to represent the cone. The actor coordinates rendering of
  // the graphics primitives for a mapper. We set this actor's mapper to be
  // coneMapper which we created above.
  //
  svtkActor* coneActor = svtkActor::New();
  coneActor->SetMapper(coneMapper);

  //
  // Create the Renderer and assign actors to it. A renderer is like a
  // viewport. It is part or all of a window on the screen and it is
  // responsible for drawing the actors it has.  We also set the background
  // color here
  //
  svtkRenderer* ren1 = svtkRenderer::New();
  ren1->AddActor(coneActor);
  ren1->SetBackground(0.1, 0.2, 0.4);

  //
  // Finally we create the render window which will show up on the screen
  // We put our renderer into the render window using AddRenderer. We also
  // set the size to be 300 pixels by 300
  //
  svtkRenderWindow* renWin = svtkRenderWindow::New();
  renWin->AddRenderer(ren1);
  renWin->SetSize(300, 300);

  //
  // Now we loop over 360 degrees and render the cone each time
  //
  int i;
  for (i = 0; i < 360; ++i)
  {
    // Render the image and rotate the active camera by one degree
    renWin->Render();
    ren1->GetActiveCamera()->Azimuth(1);
  }

  //
  // Free up any objects we created
  //
  cone->Delete();
  coneMapper->Delete();
  coneActor->Delete();
  ren1->Delete();
  renWin->Delete();

  return 0;
}