/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestColorByPointDataStringArray.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkRegressionTestImage.h"
#include "svtkTestUtilities.h"

#include <svtkActor.h>
#include <svtkDiscretizableColorTransferFunction.h>
#include <svtkNew.h>
#include <svtkPointData.h>
#include <svtkPolyData.h>
#include <svtkPolyDataMapper.h>
#include <svtkRenderWindow.h>
#include <svtkRenderWindowInteractor.h>
#include <svtkRenderer.h>
#include <svtkSphereSource.h>
#include <svtkStdString.h>
#include <svtkStringArray.h>

int TestColorByPointDataStringArray(int argc, char* argv[])
{
  svtkNew<svtkSphereSource> sphere;
  sphere->Update();

  svtkNew<svtkPolyData> polydata;
  polydata->ShallowCopy(sphere->GetOutput());

  // Set up string array associated with cells
  svtkNew<svtkStringArray> sArray;
  sArray->SetName("color");
  sArray->SetNumberOfComponents(1);
  sArray->SetNumberOfTuples(polydata->GetNumberOfPoints());

  svtkVariant colors[5];
  colors[0] = "red";
  colors[1] = "blue";
  colors[2] = "green";
  colors[3] = "yellow";
  colors[4] = "cyan";

  // Round-robin assignment of color strings
  for (int i = 0; i < polydata->GetNumberOfPoints(); ++i)
  {
    sArray->SetValue(i, colors[i % 5].ToString());
  }

  svtkPointData* cd = polydata->GetPointData();
  cd->AddArray(sArray);

  // Set up transfer function
  svtkNew<svtkDiscretizableColorTransferFunction> tfer;
  tfer->IndexedLookupOn();
  tfer->SetNumberOfIndexedColors(5);
  tfer->SetIndexedColor(0, 1.0, 0.0, 0.0);
  tfer->SetIndexedColor(1, 0.0, 0.0, 1.0);
  tfer->SetIndexedColor(2, 0.0, 1.0, 0.0);
  tfer->SetIndexedColor(3, 1.0, 1.0, 0.0);
  tfer->SetIndexedColor(4, 0.0, 1.0, 1.0);

  svtkStdString red("red");
  tfer->SetAnnotation(red, red);
  svtkStdString blue("blue");
  tfer->SetAnnotation(blue, blue);
  svtkStdString green("green");
  tfer->SetAnnotation(green, green);
  svtkStdString yellow("yellow");
  tfer->SetAnnotation(yellow, yellow);
  svtkStdString cyan("cyan");
  tfer->SetAnnotation(cyan, cyan);

  svtkNew<svtkPolyDataMapper> mapper;
  mapper->SetInputDataObject(polydata);
  mapper->SetLookupTable(tfer);
  mapper->ScalarVisibilityOn();
  mapper->SetScalarModeToUsePointFieldData();
  mapper->SelectColorArray("color");

  svtkNew<svtkActor> actor;
  actor->SetMapper(mapper);

  svtkNew<svtkRenderer> renderer;
  renderer->AddActor(actor);

  svtkNew<svtkRenderWindow> renderWindow;
  renderWindow->AddRenderer(renderer);

  svtkNew<svtkRenderWindowInteractor> iren;
  iren->SetRenderWindow(renderWindow);

  renderWindow->Render();

  int retVal = svtkRegressionTestImage(renderWindow);
  if (retVal == svtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }

  return !retVal;
}