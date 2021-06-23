/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestTStripsTCoords.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

// .NAME Test of svtkGLSLShaderDeviceAdapter
// .SECTION Description
// this program tests the shader support in svtkRendering.

#include "svtkActor.h"
#include "svtkImageData.h"
#include "svtkJPEGReader.h"
#include "svtkPlaneSource.h"
#include "svtkPointData.h"
#include "svtkPolyData.h"
#include "svtkPolyDataMapper.h"
#include "svtkProperty.h"
#include "svtkRegressionTestImage.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkStripper.h"
#include "svtkTestUtilities.h"
#include "svtkTexture.h"
#include "svtkTriangleFilter.h"

int TestTStripsTCoords(int argc, char* argv[])
{
  char* fname = svtkTestUtilities::ExpandDataFileName(argc, argv, "Data/beach.jpg");

  svtkJPEGReader* JPEGReader = svtkJPEGReader::New();
  JPEGReader->SetFileName(fname);
  JPEGReader->Update();

  delete[] fname;

  svtkTexture* texture = svtkTexture::New();
  texture->SetInputConnection(JPEGReader->GetOutputPort());
  JPEGReader->Delete();
  texture->InterpolateOn();

  svtkPlaneSource* planeSource = svtkPlaneSource::New();
  planeSource->Update();

  svtkTriangleFilter* triangleFilter = svtkTriangleFilter::New();
  triangleFilter->SetInputConnection(planeSource->GetOutputPort());
  planeSource->Delete();

  svtkStripper* stripper = svtkStripper::New();
  stripper->SetInputConnection(triangleFilter->GetOutputPort());
  triangleFilter->Delete();
  stripper->Update();

  svtkPolyData* polyData = stripper->GetOutput();
  polyData->Register(nullptr);
  stripper->Delete();
  polyData->GetPointData()->SetNormals(nullptr);

  svtkPolyDataMapper* mapper = svtkPolyDataMapper::New();
  mapper->SetInputData(polyData);
  polyData->Delete();

  svtkActor* actor = svtkActor::New();
  actor->GetProperty()->SetTexture("mytexture", texture);
  texture->Delete();
  actor->SetMapper(mapper);
  mapper->Delete();

  svtkRenderer* renderer = svtkRenderer::New();
  renderer->AddActor(actor);
  actor->Delete();
  renderer->SetBackground(0.5, 0.7, 0.7);

  svtkRenderWindow* renWin = svtkRenderWindow::New();
  renWin->AddRenderer(renderer);

  svtkRenderWindowInteractor* interactor = svtkRenderWindowInteractor::New();
  interactor->SetRenderWindow(renWin);

  renWin->SetSize(400, 400);
  renWin->Render();
  interactor->Initialize();
  renWin->Render();

  int retVal = svtkRegressionTestImage(renWin);
  if (retVal == svtkRegressionTester::DO_INTERACTOR)
  {
    interactor->Start();
  }

  renderer->Delete();
  renWin->Delete();
  interactor->Delete();

  return !retVal;
}