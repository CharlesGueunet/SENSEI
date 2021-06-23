/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestStructuredGridLIC2DSlice.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkActor.h"
#include "svtkCamera.h"
#include "svtkDataSetMapper.h"
#include "svtkDataSetSurfaceFilter.h"
#include "svtkExtractGrid.h"
#include "svtkImageData.h"
#include "svtkImageExtractComponents.h"
#include "svtkImageShiftScale.h"
#include "svtkPNGReader.h"
#include "svtkPNGWriter.h"
#include "svtkPointData.h"
#include "svtkPolyDataMapper.h"
#include "svtkProperty.h"
#include "svtkRegressionTestImage.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkSmartPointer.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkStructuredGridLIC2D.h"
#include "svtkTestUtilities.h"
#include "svtkTexture.h"
#include "svtkTimerLog.h"
#include "svtkXMLStructuredGridReader.h"
#include <string>
#include <svtksys/CommandLineArguments.hxx>

// --------------------------------------------------------------------------
static inline int CLAMP(int a, int low, int high)
{
  a = (a < low) ? low : a;
  a = (a > high) ? high : a;
  return a;
}

// --------------------------------------------------------------------------
int svtkStructuredGridLIC2DTestDriver(int argc, char* argv[])
{
  std::string filename;
  std::string noise_filename;
  int resolution = 10;
  int magnification = 1;
  std::string outputpath;
  int num_partitions = 1;
  int num_steps = 40;
  int slice_dir = 2; // 0 == X, 1 == Y, 2 == Z
  int slice = 0;
  int test_mode = 0;
  double zoom_factor = 2.8;

  svtksys::CommandLineArguments arg;
  arg.StoreUnusedArguments(1);
  arg.Initialize(argc, argv);

  typedef svtksys::CommandLineArguments argT;
  arg.AddArgument("--data", argT::EQUAL_ARGUMENT, &filename,
    "(required) Enter dataset to load (currently only *.vts files are supported");
  arg.AddArgument("--res", argT::EQUAL_ARGUMENT, &resolution,
    "(optional: default 10) Number of sample per unit distance");
  arg.AddArgument(
    "--mag", argT::EQUAL_ARGUMENT, &magnification, "(optional: default 1) Magnification");
  arg.AddArgument("--output", argT::EQUAL_ARGUMENT, &outputpath, "(optional) Output png image");
  arg.AddArgument("--partitions", argT::EQUAL_ARGUMENT, &num_partitions,
    "(optional: default 1) Number of partitions");
  arg.AddArgument("--num-steps", argT::EQUAL_ARGUMENT, &num_steps,
    "(optional: default 40) Number of steps in each direction");
  arg.AddArgument("--noise", argT::EQUAL_ARGUMENT, &noise_filename,
    "(optional) Specify the filename to a png image file to use as the noise texture");
  arg.AddArgument("--slice", argT::EQUAL_ARGUMENT, &slice,
    "(optional: default 0) The slice number in the direction specified by slice-dir. "
    "This is ignored for 2D data.");
  arg.AddArgument("--slice-dir", argT::EQUAL_ARGUMENT, &slice_dir,
    "(optional: default 2 (Z slices)) The slice direction: 0 for X slices, 1 for Y slices and 2 "
    "for Z slices. "
    "This is ignored for 2D data.");
  arg.AddArgument(
    "--test-mode", argT::EQUAL_ARGUMENT, &test_mode, "(optional: default 0) run as ctest or demo");
  arg.AddArgument(
    "--zoom-factor", argT::EQUAL_ARGUMENT, &zoom_factor, "(optional: default 2.8) set camera zoom");

  if (!arg.Parse() || filename == "")
  {
    cerr << "Problem parsing arguments." << endl;
    cerr << arg.GetHelp() << endl;
    return 1;
  }

  if (magnification < 1)
  {
    cout << "WARNING: Magnification \'" << magnification
         << "\' is invalid."
            " Forcing a magnification of 1.";
    magnification = 1;
  }

  if (num_steps < 1)
  {
    cout << "WARNING: Number of steps cannot be less than 1. Forcing 10.";
    num_steps = 10;
  }

  if (slice_dir < 0 || slice_dir > 2)
  {
    cout << "WARNING: Invalid slice-dir (" << slice_dir << "). Forcing Z slices";
    slice_dir = 2;
  }

  svtkSmartPointer<svtkXMLStructuredGridReader> reader =
    svtkSmartPointer<svtkXMLStructuredGridReader>::New();

  reader->SetFileName(filename.c_str());
  reader->Update();

  int dataDesc = SVTK_XY_PLANE;
  switch (slice_dir)
  {
    case 0:
      dataDesc = SVTK_YZ_PLANE;
      break;
    case 1:
      dataDesc = SVTK_XZ_PLANE;
      break;
    case 2:
    default:
      dataDesc = SVTK_XY_PLANE;
  }

  int extent[6];
  int voi[6];
  reader->GetOutput()->GetExtent(extent);
  memcpy(voi, extent, 6 * sizeof(int));

  // If data is 2D, then override the slice-dir
  if (extent[0] == extent[1])
  {
    dataDesc = SVTK_YZ_PLANE;
    slice = 0;
  }
  else if (extent[2] == extent[3])
  {
    dataDesc = SVTK_XZ_PLANE;
    slice = 0;
  }
  else if (extent[4] == extent[5])
  {
    dataDesc = SVTK_XY_PLANE;
    slice = 0;
  }
  else
  {
    switch (dataDesc)
    {
      case SVTK_XY_PLANE:
        voi[4] = voi[5] = CLAMP(extent[4] + slice, extent[4], extent[5]);
        break;

      case SVTK_YZ_PLANE:
        voi[0] = voi[1] = CLAMP(extent[0] + slice, extent[0], extent[1]);
        break;

      case SVTK_XZ_PLANE:
        voi[2] = voi[3] = CLAMP(extent[2] + slice, extent[2], extent[3]);
        break;
    }
  }

  svtkSmartPointer<svtkExtractGrid> extractVOI = svtkSmartPointer<svtkExtractGrid>::New();

  extractVOI->SetInputConnection(reader->GetOutputPort());
  extractVOI->SetVOI(voi);

  svtkSmartPointer<svtkRenderWindow> renWin = svtkSmartPointer<svtkRenderWindow>::New();

  svtkSmartPointer<svtkRenderer> renderer = svtkSmartPointer<svtkRenderer>::New();

  svtkSmartPointer<svtkRenderWindowInteractor> iren =
    svtkSmartPointer<svtkRenderWindowInteractor>::New();

  renWin->AddRenderer(renderer);
  iren->SetRenderWindow(renWin);

  renWin->Render();

  svtkSmartPointer<svtkStructuredGridLIC2D> filter = svtkSmartPointer<svtkStructuredGridLIC2D>::New();

  if (filter->SetContext(renWin) == 0)
  {
    cout << "Required OpenGL extensions / GPU not supported." << endl;
    return 0;
  }

  filter->SetInputConnection(extractVOI->GetOutputPort());

  if (noise_filename != "")
  {
    svtkSmartPointer<svtkPNGReader> pngReader = svtkSmartPointer<svtkPNGReader>::New();

    pngReader->SetFileName(noise_filename.c_str());
    filter->SetInputConnection(1, pngReader->GetOutputPort(0));
  }

  filter->SetSteps(num_steps);
  filter->SetStepSize(0.01 / magnification);
  filter->SetMagnification(magnification);

  for (int kk = 0; kk < num_partitions; kk++)
  {
    cout << "*****************" << endl;
    filter->UpdatePiece(kk, num_partitions, 0);
    if (filter->GetFBOSuccess() == 0 || filter->GetLICSuccess() == 0)
    {
      return 0;
    }

    svtkSmartPointer<svtkImageData> clone = svtkSmartPointer<svtkImageData>::New();

    clone->ShallowCopy(filter->GetOutputDataObject(1));

    double range[2];
    clone->GetPointData()->GetScalars()->GetRange(range);

    svtkSmartPointer<svtkImageShiftScale> caster = svtkSmartPointer<svtkImageShiftScale>::New();

    caster->SetInputData(clone);
    caster->SetOutputScalarTypeToUnsignedChar();
    caster->SetShift(-range[0]);
    caster->SetScale(255.0 / (range[1] - range[0]));
    caster->Update();

    // svtkPNGWriter* writer = svtkPNGWriter::New();
    // writer->SetFileName("/tmp/lic.png");
    // writer->SetInputConnection(caster->GetOutputPort());
    // writer->Write();
    // writer->Delete();

    svtkSmartPointer<svtkTexture> texture = svtkSmartPointer<svtkTexture>::New();

    texture->SetInputConnection(caster->GetOutputPort());

    svtkSmartPointer<svtkStructuredGrid> clone2 = svtkSmartPointer<svtkStructuredGrid>::New();

    clone2->ShallowCopy(filter->GetOutput(0));

    svtkSmartPointer<svtkDataSetSurfaceFilter> surfaceFilter =
      svtkSmartPointer<svtkDataSetSurfaceFilter>::New();

    surfaceFilter->SetInputData(clone2);

    svtkSmartPointer<svtkPolyDataMapper> mapper = svtkSmartPointer<svtkPolyDataMapper>::New();

    mapper->SetInputConnection(surfaceFilter->GetOutputPort());
    mapper->SetScalarVisibility(0);

    svtkSmartPointer<svtkActor> actor = svtkSmartPointer<svtkActor>::New();

    actor->SetMapper(mapper);
    actor->SetTexture(texture);

    renderer->AddActor(actor);
  }

  svtkSmartPointer<svtkTesting> tester = svtkSmartPointer<svtkTesting>::New();

  for (int cc = 0; cc < argc; cc++)
  {
    tester->AddArgument(argv[cc]);
  }
  tester->SetRenderWindow(renWin);

  renderer->SetBackground(0.2, 0.1, 0.2);
  renderer->ResetCamera();
  renderer->GetActiveCamera()->Zoom(zoom_factor);

  if (test_mode)
  {
    switch (dataDesc)
    {
      case SVTK_YZ_PLANE:
        renderer->GetActiveCamera()->Azimuth(90);
        break;
      case SVTK_XZ_PLANE:
        renderer->GetActiveCamera()->Elevation(90);
        renderer->GetActiveCamera()->SetViewUp(0, 0, -1);
        break;
    }
  }

  renWin->Render();
  int reply =
    (!tester->IsValidImageSpecified() || (tester->RegressionTest(10) == svtkTesting::PASSED))
    ? /*success*/ 0
    : /*failure*/ 1;

  if (tester->IsInteractiveModeSpecified() || !test_mode)
  {
    iren->Start();
  }

  return reply;
}

// --------------------------------------------------------------------------
int StructuredGridLIC2DDemo(int argc, char* argv[])
{
  return svtkStructuredGridLIC2DTestDriver(argc, argv);
}