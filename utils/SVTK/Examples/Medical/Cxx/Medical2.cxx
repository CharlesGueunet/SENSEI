/*=========================================================================

  Program:   Visualization Toolkit
  Module:    Medical2.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
//
// This example reads a volume dataset, extracts two isosurfaces that
// represent the skin and bone, and then displays them.
//

#include <svtkActor.h>
#include <svtkCamera.h>
#include <svtkContourFilter.h>
#include <svtkOutlineFilter.h>
#include <svtkPolyDataMapper.h>
#include <svtkPolyDataNormals.h>
#include <svtkProperty.h>
#include <svtkRegressionTestImage.h>
#include <svtkRenderWindow.h>
#include <svtkRenderWindowInteractor.h>
#include <svtkRenderer.h>
#include <svtkSmartPointer.h>
#include <svtkStripper.h>
#include <svtkVolume16Reader.h>

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    cout << "Usage: " << argv[0] << " DATADIR/headsq/quarter" << endl;
    return EXIT_FAILURE;
  }

  // Create the renderer, the render window, and the interactor. The renderer
  // draws into the render window, the interactor enables mouse- and
  // keyboard-based interaction with the data within the render window.
  //
  svtkSmartPointer<svtkRenderer> aRenderer = svtkSmartPointer<svtkRenderer>::New();
  svtkSmartPointer<svtkRenderWindow> renWin = svtkSmartPointer<svtkRenderWindow>::New();
  renWin->AddRenderer(aRenderer);

  svtkSmartPointer<svtkRenderWindowInteractor> iren =
    svtkSmartPointer<svtkRenderWindowInteractor>::New();
  iren->SetRenderWindow(renWin);

  // The following reader is used to read a series of 2D slices (images)
  // that compose the volume. The slice dimensions are set, and the
  // pixel spacing. The data Endianness must also be specified. The reader
  // usese the FilePrefix in combination with the slice number to construct
  // filenames using the format FilePrefix.%d. (In this case the FilePrefix
  // is the root name of the file: quarter.)
  svtkSmartPointer<svtkVolume16Reader> v16 = svtkSmartPointer<svtkVolume16Reader>::New();
  v16->SetDataDimensions(64, 64);
  v16->SetDataByteOrderToLittleEndian();
  v16->SetFilePrefix(argv[1]);
  v16->SetImageRange(1, 93);
  v16->SetDataSpacing(3.2, 3.2, 1.5);

  // An isosurface, or contour value of 500 is known to correspond to the
  // skin of the patient. Once generated, a svtkPolyDataNormals filter is
  // is used to create normals for smooth surface shading during rendering.
  // The triangle stripper is used to create triangle strips from the
  // isosurface; these render much faster on many systems.
  svtkSmartPointer<svtkContourFilter> skinExtractor = svtkSmartPointer<svtkContourFilter>::New();
  skinExtractor->SetInputConnection(v16->GetOutputPort());
  skinExtractor->SetValue(0, 500);

  svtkSmartPointer<svtkPolyDataNormals> skinNormals = svtkSmartPointer<svtkPolyDataNormals>::New();
  skinNormals->SetInputConnection(skinExtractor->GetOutputPort());
  skinNormals->SetFeatureAngle(60.0);

  svtkSmartPointer<svtkStripper> skinStripper = svtkSmartPointer<svtkStripper>::New();
  skinStripper->SetInputConnection(skinNormals->GetOutputPort());

  svtkSmartPointer<svtkPolyDataMapper> skinMapper = svtkSmartPointer<svtkPolyDataMapper>::New();
  skinMapper->SetInputConnection(skinStripper->GetOutputPort());
  skinMapper->ScalarVisibilityOff();

  svtkSmartPointer<svtkActor> skin = svtkSmartPointer<svtkActor>::New();
  skin->SetMapper(skinMapper);
  skin->GetProperty()->SetDiffuseColor(1, .49, .25);
  skin->GetProperty()->SetSpecular(.3);
  skin->GetProperty()->SetSpecularPower(20);
  skin->GetProperty()->SetOpacity(.5);

  // An isosurface, or contour value of 1150 is known to correspond to the
  // skin of the patient. Once generated, a svtkPolyDataNormals filter is
  // is used to create normals for smooth surface shading during rendering.
  // The triangle stripper is used to create triangle strips from the
  // isosurface; these render much faster on may systems.
  svtkSmartPointer<svtkContourFilter> boneExtractor = svtkSmartPointer<svtkContourFilter>::New();
  boneExtractor->SetInputConnection(v16->GetOutputPort());
  boneExtractor->SetValue(0, 1150);

  svtkSmartPointer<svtkPolyDataNormals> boneNormals = svtkSmartPointer<svtkPolyDataNormals>::New();
  boneNormals->SetInputConnection(boneExtractor->GetOutputPort());
  boneNormals->SetFeatureAngle(60.0);

  svtkSmartPointer<svtkStripper> boneStripper = svtkSmartPointer<svtkStripper>::New();
  boneStripper->SetInputConnection(boneNormals->GetOutputPort());

  svtkSmartPointer<svtkPolyDataMapper> boneMapper = svtkSmartPointer<svtkPolyDataMapper>::New();
  boneMapper->SetInputConnection(boneStripper->GetOutputPort());
  boneMapper->ScalarVisibilityOff();

  svtkSmartPointer<svtkActor> bone = svtkSmartPointer<svtkActor>::New();
  bone->SetMapper(boneMapper);
  bone->GetProperty()->SetDiffuseColor(1, 1, .9412);

  // An outline provides context around the data.
  //
  svtkSmartPointer<svtkOutlineFilter> outlineData = svtkSmartPointer<svtkOutlineFilter>::New();
  outlineData->SetInputConnection(v16->GetOutputPort());

  svtkSmartPointer<svtkPolyDataMapper> mapOutline = svtkSmartPointer<svtkPolyDataMapper>::New();
  mapOutline->SetInputConnection(outlineData->GetOutputPort());

  svtkSmartPointer<svtkActor> outline = svtkSmartPointer<svtkActor>::New();
  outline->SetMapper(mapOutline);
  outline->GetProperty()->SetColor(0, 0, 0);

  // It is convenient to create an initial view of the data. The FocalPoint
  // and Position form a vector direction. Later on (ResetCamera() method)
  // this vector is used to position the camera to look at the data in
  // this direction.
  svtkSmartPointer<svtkCamera> aCamera = svtkSmartPointer<svtkCamera>::New();
  aCamera->SetViewUp(0, 0, -1);
  aCamera->SetPosition(0, 1, 0);
  aCamera->SetFocalPoint(0, 0, 0);
  aCamera->ComputeViewPlaneNormal();
  aCamera->Azimuth(30.0);
  aCamera->Elevation(30.0);

  // Actors are added to the renderer. An initial camera view is created.
  // The Dolly() method moves the camera towards the FocalPoint,
  // thereby enlarging the image.
  aRenderer->AddActor(outline);
  aRenderer->AddActor(skin);
  aRenderer->AddActor(bone);
  aRenderer->SetActiveCamera(aCamera);
  aRenderer->ResetCamera();
  aCamera->Dolly(1.5);

  // Set a background color for the renderer and set the size of the
  // render window (expressed in pixels).
  aRenderer->SetBackground(.2, .3, .4);
  renWin->SetSize(640, 480);

  // Note that when camera movement occurs (as it does in the Dolly()
  // method), the clipping planes often need adjusting. Clipping planes
  // consist of two planes: near and far along the view direction. The
  // near plane clips out objects in front of the plane; the far plane
  // clips out objects behind the plane. This way only what is drawn
  // between the planes is actually rendered.
  aRenderer->ResetCameraClippingRange();

  // For testing, check if "-V" is used to provide a regression test image
  if (argc >= 4 && strcmp(argv[2], "-V") == 0)
  {
    renWin->Render();
    int retVal = svtkRegressionTestImage(renWin);

    if (retVal == svtkTesting::FAILED)
    {
      return EXIT_FAILURE;
    }
    else if (retVal != svtkTesting::DO_INTERACTOR)
    {
      return EXIT_SUCCESS;
    }
  }

  // Initialize the event loop and then start it.
  iren->Initialize();
  iren->Start();

  return EXIT_SUCCESS;
}