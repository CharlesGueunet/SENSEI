/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestEllipticalButtonSource.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include <svtkEllipticalButtonSource.h>
#include <svtkMinimalStandardRandomSequence.h>
#include <svtkSmartPointer.h>

int TestEllipticalButtonSource(int svtkNotUsed(argc), char* svtkNotUsed(argv)[])
{
  svtkSmartPointer<svtkMinimalStandardRandomSequence> randomSequence =
    svtkSmartPointer<svtkMinimalStandardRandomSequence>::New();
  randomSequence->SetSeed(1);

  svtkSmartPointer<svtkEllipticalButtonSource> ellipticalButtonSource =
    svtkSmartPointer<svtkEllipticalButtonSource>::New();
  ellipticalButtonSource->SetCircumferentialResolution(8);
  ellipticalButtonSource->SetShoulderResolution(8);
  ellipticalButtonSource->SetTextureResolution(8);
  ellipticalButtonSource->SetRadialRatio(1.0);
  ellipticalButtonSource->SetShoulderTextureCoordinate(0.0, 0.0);
  ellipticalButtonSource->SetTextureDimensions(100, 100);
  ellipticalButtonSource->SetTextureStyleToProportional();
  ellipticalButtonSource->TwoSidedOff();

  ellipticalButtonSource->SetOutputPointsPrecision(svtkAlgorithm::SINGLE_PRECISION);

  double center[3];
  for (unsigned int i = 0; i < 3; ++i)
  {
    randomSequence->Next();
    center[i] = randomSequence->GetValue();
  }
  ellipticalButtonSource->SetCenter(center);

  randomSequence->Next();
  double depth = randomSequence->GetValue();
  ellipticalButtonSource->SetDepth(depth);

  randomSequence->Next();
  double height = randomSequence->GetValue();
  ellipticalButtonSource->SetHeight(height);

  randomSequence->Next();
  double width = randomSequence->GetValue();
  ellipticalButtonSource->SetWidth(width);

  ellipticalButtonSource->Update();

  svtkSmartPointer<svtkPolyData> polyData = ellipticalButtonSource->GetOutput();
  svtkSmartPointer<svtkPoints> points = polyData->GetPoints();

  if (points->GetDataType() != SVTK_FLOAT)
  {
    return EXIT_FAILURE;
  }

  ellipticalButtonSource->SetOutputPointsPrecision(svtkAlgorithm::DOUBLE_PRECISION);

  for (unsigned int i = 0; i < 3; ++i)
  {
    randomSequence->Next();
    center[i] = randomSequence->GetValue();
  }
  ellipticalButtonSource->SetCenter(center);

  randomSequence->Next();
  depth = randomSequence->GetValue();
  ellipticalButtonSource->SetDepth(depth);

  randomSequence->Next();
  height = randomSequence->GetValue();
  ellipticalButtonSource->SetHeight(height);

  randomSequence->Next();
  width = randomSequence->GetValue();
  ellipticalButtonSource->SetWidth(width);

  ellipticalButtonSource->Update();

  polyData = ellipticalButtonSource->GetOutput();
  points = polyData->GetPoints();

  if (points->GetDataType() != SVTK_DOUBLE)
  {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}