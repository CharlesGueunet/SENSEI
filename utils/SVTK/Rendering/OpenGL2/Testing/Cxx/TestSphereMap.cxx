/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkActor.h"
#include "svtkCamera.h"
#include "svtkImageData.h"
#include "svtkInteractorStyleTrackballCamera.h"
#include "svtkJPEGReader.h"
#include "svtkNew.h"
#include "svtkOpenGLPolyDataMapper.h"
#include "svtkOpenGLRenderWindow.h"
#include "svtkPLYReader.h"
#include "svtkPolyDataNormals.h"
#include "svtkProperty.h"
#include "svtkRegressionTestImage.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkShaderProgram.h"
#include "svtkShaderProperty.h"
#include "svtkSkybox.h"
#include "svtkSmartPointer.h"
#include "svtkTestUtilities.h"
#include "svtkTexture.h"

#include "svtkLight.h"

//----------------------------------------------------------------------------
int TestSphereMap(int argc, char* argv[])
{
  svtkNew<svtkRenderer> renderer;
  renderer->SetBackground(0.0, 0.0, 0.0);
  svtkNew<svtkRenderWindow> renderWindow;
  renderWindow->SetSize(400, 400);
  renderWindow->AddRenderer(renderer);
  svtkNew<svtkRenderWindowInteractor> iren;
  iren->SetRenderWindow(renderWindow);

  svtkNew<svtkLight> light;
  light->SetLightTypeToSceneLight();
  light->SetPosition(1.0, 7.0, 1.0);
  renderer->AddLight(light);

  const char* fileName = svtkTestUtilities::ExpandDataFileName(argc, argv, "Data/bunny.ply");
  svtkNew<svtkPLYReader> reader;
  reader->SetFileName(fileName);

  delete[] fileName;

  svtkNew<svtkPolyDataNormals> norms;
  norms->SetInputConnection(reader->GetOutputPort());

  svtkNew<svtkTexture> texture;
  texture->InterpolateOn();

  // mipmapping works on many systems but is not
  // core 3.2 for cube maps. SVTK will silently
  // ignore it if it is not supported. We commented it
  // out here to make valid images easier
  // texture->MipmapOn();

  const char* fName = svtkTestUtilities::ExpandDataFileName(argc, argv, "Data/wintersun.jpg");
  svtkNew<svtkJPEGReader> imgReader;
  imgReader->SetFileName(fName);
  texture->SetInputConnection(imgReader->GetOutputPort());
  delete[] fName;

  svtkNew<svtkOpenGLPolyDataMapper> mapper;
  mapper->SetInputConnection(norms->GetOutputPort());

  svtkNew<svtkActor> actor;
  actor->SetPosition(0, 0, 0);
  actor->SetScale(6.0, 6.0, 6.0);
  actor->GetProperty()->SetSpecular(0.8);
  actor->GetProperty()->SetSpecularPower(20);
  actor->GetProperty()->SetDiffuse(0.1);
  actor->GetProperty()->SetAmbient(0.1);
  actor->GetProperty()->SetDiffuseColor(1.0, 0.0, 0.4);
  actor->GetProperty()->SetAmbientColor(0.4, 0.0, 1.0);
  renderer->AddActor(actor);
  actor->SetTexture(texture);
  actor->SetMapper(mapper);

  svtkShaderProperty* sp = actor->GetShaderProperty();

  sp->AddVertexShaderReplacement("//SVTK::PositionVC::Dec", // replace
    true,                                                  // before the standard replacements
    "//SVTK::PositionVC::Dec\n"                             // we still want the default
    "out vec3 TexCoords;\n",
    false // only do it once
  );
  sp->AddVertexShaderReplacement("//SVTK::PositionVC::Impl", // replace
    true,                                                   // before the standard replacements
    "//SVTK::PositionVC::Impl\n"                             // we still want the default
    "vec3 camPos = -MCVCMatrix[3].xyz * mat3(MCVCMatrix);\n"
    "TexCoords.xyz = reflect(vertexMC.xyz - camPos, normalize(normalMC));\n",
    false // only do it once
  );
  sp->AddFragmentShaderReplacement("//SVTK::Light::Dec", // replace
    true,                                               // before the standard replacements
    "//SVTK::Light::Dec\n"                               // we still want the default
    "in vec3 TexCoords;\n",
    false // only do it once
  );
  sp->AddFragmentShaderReplacement("//SVTK::Light::Impl", // replace
    true,                                                // before the standard replacements
    "//SVTK::Light::Impl\n"
    "  float phix = length(vec2(TexCoords.x, TexCoords.z));\n"
    "  vec3 skyColor = texture(actortexture, vec2(0.5*atan(TexCoords.z, TexCoords.x)/3.1415927 + "
    "0.5, atan(TexCoords.y,phix)/3.1415927 + 0.5)).xyz;\n"
    "  gl_FragData[0] = vec4(ambientColor + diffuse + specular + specularColor*skyColor, "
    "opacity);\n", // we still want the default
    false          // only do it once
  );

  svtkNew<svtkSkybox> world;
  world->SetProjectionToSphere();
  world->SetTexture(texture);
  renderer->AddActor(world);

  renderer->GetActiveCamera()->SetPosition(0.0, 0.55, 2.0);
  renderer->GetActiveCamera()->SetFocalPoint(0.0, 0.55, 0.0);
  renderer->GetActiveCamera()->SetViewAngle(60.0);
  renderer->GetActiveCamera()->Zoom(1.1);
  renderer->GetActiveCamera()->Azimuth(0);
  renderer->GetActiveCamera()->Elevation(5);
  renderer->ResetCameraClippingRange();

  renderWindow->Render();

  svtkNew<svtkInteractorStyleTrackballCamera> style;
  renderWindow->GetInteractor()->SetInteractorStyle(style);

  int retVal = svtkRegressionTestImage(renderWindow);
  if (retVal == svtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }

  return !retVal;
}