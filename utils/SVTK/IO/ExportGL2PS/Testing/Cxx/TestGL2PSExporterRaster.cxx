/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestGL2PSExporterRaster.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkGL2PSExporter.h"
#include "svtkRegressionTestImage.h"
#include "svtkTestUtilities.h"

#include "svtkActor.h"
#include "svtkCamera.h"
#include "svtkConeSource.h"
#include "svtkCubeAxesActor2D.h"
#include "svtkLogoRepresentation.h"
#include "svtkNew.h"
#include "svtkPNGReader.h"
#include "svtkPolyDataMapper.h"
#include "svtkProperty.h"
#include "svtkProperty2D.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkSmartPointer.h"
#include "svtkTestingInteractor.h"
#include "svtkTextActor.h"
#include "svtkTextProperty.h"

#include <string>

int TestGL2PSExporterRaster(int argc, char* argv[])
{
  svtkNew<svtkConeSource> coneSource;
  svtkNew<svtkPolyDataMapper> coneMapper;
  svtkNew<svtkActor> coneActor;
  coneSource->SetResolution(25);
  coneMapper->SetInputConnection(coneSource->GetOutputPort());
  coneActor->SetMapper(coneMapper);
  coneActor->GetProperty()->SetColor(0.5, 0.5, 1.0);

  svtkNew<svtkCubeAxesActor2D> axes;
  axes->SetInputConnection(coneSource->GetOutputPort());
  axes->SetFontFactor(2.0);
  axes->SetCornerOffset(0.0);
  axes->GetProperty()->SetColor(0.0, 0.0, 0.0);

  svtkNew<svtkTextActor> text1;
  text1->SetDisplayPosition(250, 435);
  text1->SetInput("Test\nmultiline\ntext"); // Won't render properly
  text1->GetTextProperty()->SetFontSize(18);
  text1->GetTextProperty()->SetFontFamilyToArial();
  text1->GetTextProperty()->SetJustificationToCentered();
  text1->GetTextProperty()->BoldOn();
  text1->GetTextProperty()->ItalicOn();
  text1->GetTextProperty()->SetColor(0.0, 0.0, 1.0);

  svtkNew<svtkTextActor> text2;
  text2->SetDisplayPosition(400, 250);
  text2->SetInput("Test rotated text");
  text2->GetTextProperty()->SetFontSize(22);
  text2->GetTextProperty()->SetFontFamilyToTimes();
  text2->GetTextProperty()->SetJustificationToCentered();
  text2->GetTextProperty()->SetVerticalJustificationToCentered();
  text2->GetTextProperty()->BoldOn();
  text2->GetTextProperty()->SetOrientation(45);
  text2->GetTextProperty()->SetColor(1.0, 0.0, 0.0);

  svtkNew<svtkTextActor> text3;
  text3->SetDisplayPosition(20, 40);
  text3->SetInput("Bag");
  text3->GetTextProperty()->SetFontSize(45);
  text3->GetTextProperty()->SetFontFamilyToCourier();
  text3->GetTextProperty()->SetJustificationToLeft();
  text3->GetTextProperty()->SetVerticalJustificationToBottom();
  text3->GetTextProperty()->BoldOn();
  text3->GetTextProperty()->SetOrientation(0);
  text3->GetTextProperty()->SetColor(0.2, 1.0, 0.2);

  svtkNew<svtkTextActor> text4;
  text4->SetDisplayPosition(120, 40);
  text4->SetInput("Bag");
  text4->GetTextProperty()->SetFontSize(45);
  text4->GetTextProperty()->SetFontFamilyToCourier();
  text4->GetTextProperty()->SetJustificationToLeft();
  text4->GetTextProperty()->SetVerticalJustificationToCentered();
  text4->GetTextProperty()->BoldOn();
  text4->GetTextProperty()->SetOrientation(0);
  text4->GetTextProperty()->SetColor(0.2, 1.0, 0.2);

  svtkNew<svtkTextActor> text5;
  text5->SetDisplayPosition(220, 40);
  text5->SetInput("Bag");
  text5->GetTextProperty()->SetFontSize(45);
  text5->GetTextProperty()->SetFontFamilyToCourier();
  text5->GetTextProperty()->SetJustificationToLeft();
  text5->GetTextProperty()->SetVerticalJustificationToTop();
  text5->GetTextProperty()->BoldOn();
  text5->GetTextProperty()->SetOrientation(0);
  text5->GetTextProperty()->SetColor(0.2, 1.0, 0.2);

  svtkNew<svtkRenderer> ren;
  axes->SetCamera(ren->GetActiveCamera());
  ren->AddActor(coneActor);
  ren->AddActor(axes);
  ren->AddActor(text1);
  ren->AddActor(text2);
  ren->AddActor(text3);
  ren->AddActor(text4);
  ren->AddActor(text5);
  ren->SetBackground(0.8, 0.8, 0.8);

  // logo
  char* fname = svtkTestUtilities::ExpandDataFileName(argc, argv, "Data/svtk-transparent.png");

  svtkNew<svtkPNGReader> reader;
  reader->SetFileName(fname);
  reader->Update();
  delete[] fname;

  svtkNew<svtkLogoRepresentation> logo;
  logo->SetImage(reader->GetOutput());
  logo->ProportionalResizeOn();
  logo->SetPosition(0.8, 0.0);
  logo->SetPosition2(0.1, 0.1);
  logo->GetImageProperty()->SetOpacity(0.8);
  logo->SetRenderer(ren);
  ren->AddActor(logo);

  svtkNew<svtkRenderWindow> renWin;
  renWin->AddRenderer(ren);

  svtkNew<svtkRenderWindowInteractor> iren;
  iren->SetRenderWindow(renWin);

  svtkSmartPointer<svtkCamera> camera = ren->GetActiveCamera();
  ren->ResetCamera();
  camera->Azimuth(30);

  renWin->SetSize(500, 500);
  renWin->Render();

  svtkNew<svtkGL2PSExporter> exp;
  exp->SetRenderWindow(renWin);
  exp->SetFileFormatToPS();
  exp->CompressOff();
  exp->SetSortToBSP();
  exp->DrawBackgroundOn();
  exp->Write3DPropsAsRasterImageOn();

  std::string fileprefix =
    svtkTestingInteractor::TempDirectory + std::string("/TestGL2PSExporterRaster");

  exp->SetFilePrefix(fileprefix.c_str());
  exp->Write();

  exp->SetFileFormatToPDF();
  exp->Write();

  renWin->SetMultiSamples(0);
  renWin->GetInteractor()->Initialize();
  renWin->GetInteractor()->Start();

  return EXIT_SUCCESS;
}