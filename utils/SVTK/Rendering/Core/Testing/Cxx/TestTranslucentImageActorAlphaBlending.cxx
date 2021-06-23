/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestTranslucentImageActorAlphaBlending.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// This test covers rendering of translucent image actor with alpha blending.
//
// The command line arguments are:
// -I        => run in interactive mode; unless this is used, the program will
//              not allow interaction and exit

#include "svtkRegressionTestImage.h"
#include "svtkTestUtilities.h"

#include "svtkActor.h"
#include "svtkCamera.h"
#include "svtkImageActor.h"
#include "svtkImageMapper3D.h"
#include "svtkPNGReader.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"

int TestTranslucentImageActorAlphaBlending(int argc, char* argv[])
{
  svtkRenderWindowInteractor* iren = svtkRenderWindowInteractor::New();
  svtkRenderWindow* renWin = svtkRenderWindow::New();
  iren->SetRenderWindow(renWin);
  renWin->Delete();

  svtkRenderer* renderer = svtkRenderer::New();
  renWin->AddRenderer(renderer);
  renderer->Delete();

  svtkImageActor* ia = svtkImageActor::New();
  renderer->AddActor(ia);
  ia->Delete();

  svtkPNGReader* pnmReader = svtkPNGReader::New();
  ia->GetMapper()->SetInputConnection(pnmReader->GetOutputPort());
  pnmReader->Delete();

  char* fname = svtkTestUtilities::ExpandDataFileName(argc, argv, "Data/alphachannel.png");

  pnmReader->SetFileName(fname);
  delete[] fname;

  renderer->SetBackground(0.1, 0.2, 0.4);
  renWin->SetSize(400, 400);

  renWin->Render();
  int retVal = svtkRegressionTestImage(renWin);
  if (retVal == svtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }
  iren->Delete();

  return !retVal;
}