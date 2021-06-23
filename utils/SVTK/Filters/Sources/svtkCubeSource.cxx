/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkCubeSource.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkCubeSource.h"

#include "svtkCellArray.h"
#include "svtkFloatArray.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPoints.h"
#include "svtkPolyData.h"
#include "svtkStreamingDemandDrivenPipeline.h"

#include <cmath>

svtkStandardNewMacro(svtkCubeSource);

//-----------------------------------------------------------------------------
svtkCubeSource::svtkCubeSource(double xL, double yL, double zL)
{
  this->XLength = fabs(xL);
  this->YLength = fabs(yL);
  this->ZLength = fabs(zL);

  this->Center[0] = 0.0;
  this->Center[1] = 0.0;
  this->Center[2] = 0.0;

  this->OutputPointsPrecision = SINGLE_PRECISION;

  this->SetNumberOfInputPorts(0);
}

//-----------------------------------------------------------------------------
int svtkCubeSource::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* outputVector)
{
  // get the info object
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the output
  svtkPolyData* output = svtkPolyData::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  double x[3], n[3], tc[3];
  int numPolys = 6, numPts = 24;
  int i, j, k;
  svtkIdType pts[4];
  svtkPoints* newPoints;
  svtkFloatArray* newNormals;
  svtkFloatArray* newTCoords; // CCS 7/27/98 Added for Texture Mapping
  svtkCellArray* newPolys;

  //
  // Set things up; allocate memory
  //
  newPoints = svtkPoints::New();

  // Set the desired precision for the points in the output.
  if (this->OutputPointsPrecision == svtkAlgorithm::DOUBLE_PRECISION)
  {
    newPoints->SetDataType(SVTK_DOUBLE);
  }
  else
  {
    newPoints->SetDataType(SVTK_FLOAT);
  }

  newPoints->Allocate(numPts);
  newNormals = svtkFloatArray::New();
  newNormals->SetNumberOfComponents(3);
  newNormals->Allocate(numPts);
  newNormals->SetName("Normals");
  newTCoords = svtkFloatArray::New();
  newTCoords->SetNumberOfComponents(2);
  newTCoords->Allocate(numPts);
  newTCoords->SetName("TCoords");

  newPolys = svtkCellArray::New();
  newPolys->AllocateEstimate(numPolys, 4);

  //
  // Generate points and normals
  //
  for (x[0] = this->Center[0] - this->XLength / 2.0, n[0] = (-1.0), n[1] = n[2] = 0.0, i = 0; i < 2;
       i++, x[0] += this->XLength, n[0] += 2.0)
  {
    for (x[1] = this->Center[1] - this->YLength / 2.0, j = 0; j < 2; j++, x[1] += this->YLength)
    {
      tc[1] = x[1] + 0.5;
      for (x[2] = this->Center[2] - this->ZLength / 2.0, k = 0; k < 2; k++, x[2] += this->ZLength)
      {
        tc[0] = (x[2] + 0.5) * (1 - 2 * i);
        newPoints->InsertNextPoint(x);
        newTCoords->InsertNextTuple(tc);
        newNormals->InsertNextTuple(n);
      }
    }
  }
  pts[0] = 0;
  pts[1] = 1;
  pts[2] = 3;
  pts[3] = 2;
  newPolys->InsertNextCell(4, pts);
  pts[0] = 4;
  pts[1] = 6;
  pts[2] = 7;
  pts[3] = 5;
  newPolys->InsertNextCell(4, pts);

  for (x[1] = this->Center[1] - this->YLength / 2.0, n[1] = (-1.0), n[0] = n[2] = 0.0, i = 0; i < 2;
       i++, x[1] += this->YLength, n[1] += 2.0)
  {
    for (x[0] = this->Center[0] - this->XLength / 2.0, j = 0; j < 2; j++, x[0] += this->XLength)
    {
      tc[0] = (x[0] + 0.5) * (2 * i - 1);
      for (x[2] = this->Center[2] - this->ZLength / 2.0, k = 0; k < 2; k++, x[2] += this->ZLength)
      {
        tc[1] = (x[2] + 0.5) * -1;
        newPoints->InsertNextPoint(x);
        newTCoords->InsertNextTuple(tc);
        newNormals->InsertNextTuple(n);
      }
    }
  }
  pts[0] = 8;
  pts[1] = 10;
  pts[2] = 11;
  pts[3] = 9;
  newPolys->InsertNextCell(4, pts);
  pts[0] = 12;
  pts[1] = 13;
  pts[2] = 15;
  pts[3] = 14;
  newPolys->InsertNextCell(4, pts);

  for (x[2] = this->Center[2] - this->ZLength / 2.0, n[2] = (-1.0), n[0] = n[1] = 0.0, i = 0; i < 2;
       i++, x[2] += this->ZLength, n[2] += 2.0)
  {
    for (x[1] = this->Center[1] - this->YLength / 2.0, j = 0; j < 2; j++, x[1] += this->YLength)
    {
      tc[1] = x[1] + 0.5;
      for (x[0] = this->Center[0] - this->XLength / 2.0, k = 0; k < 2; k++, x[0] += this->XLength)
      {
        tc[0] = (x[0] + 0.5) * (2 * i - 1);
        newPoints->InsertNextPoint(x);
        newTCoords->InsertNextTuple(tc);
        newNormals->InsertNextTuple(n);
      }
    }
  }
  pts[0] = 16;
  pts[1] = 18;
  pts[2] = 19;
  pts[3] = 17;
  newPolys->InsertNextCell(4, pts);
  pts[0] = 20;
  pts[1] = 21;
  pts[2] = 23;
  pts[3] = 22;
  newPolys->InsertNextCell(4, pts);

  //
  // Update ourselves and release memory
  //
  output->SetPoints(newPoints);
  newPoints->Delete();

  output->GetPointData()->SetNormals(newNormals);
  newNormals->Delete();

  output->GetPointData()->SetTCoords(newTCoords);
  newTCoords->Delete();

  newPolys->Squeeze(); // since we've estimated size; reclaim some space
  output->SetPolys(newPolys);
  newPolys->Delete();

  return 1;
}

//-----------------------------------------------------------------------------
// Convenience method allows creation of cube by specifying bounding box.
void svtkCubeSource::SetBounds(
  double xMin, double xMax, double yMin, double yMax, double zMin, double zMax)
{
  double bounds[6];
  bounds[0] = xMin;
  bounds[1] = xMax;
  bounds[2] = yMin;
  bounds[3] = yMax;
  bounds[4] = zMin;
  bounds[5] = zMax;
  this->SetBounds(bounds);
}

//-----------------------------------------------------------------------------
void svtkCubeSource::SetBounds(const double bounds[6])
{
  this->SetXLength(bounds[1] - bounds[0]);
  this->SetYLength(bounds[3] - bounds[2]);
  this->SetZLength(bounds[5] - bounds[4]);

  this->SetCenter(
    (bounds[1] + bounds[0]) / 2.0, (bounds[3] + bounds[2]) / 2.0, (bounds[5] + bounds[4]) / 2.0);
}

//-----------------------------------------------------------------------------
void svtkCubeSource::GetBounds(double bounds[6])
{
  bounds[0] = this->Center[0] - (this->XLength / 2.0);
  bounds[1] = this->Center[0] + (this->XLength / 2.0);
  bounds[2] = this->Center[1] - (this->YLength / 2.0);
  bounds[3] = this->Center[1] + (this->YLength / 2.0);
  bounds[4] = this->Center[2] - (this->ZLength / 2.0);
  bounds[5] = this->Center[2] + (this->ZLength / 2.0);
}

//-----------------------------------------------------------------------------
void svtkCubeSource::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "X Length: " << this->XLength << "\n";
  os << indent << "Y Length: " << this->YLength << "\n";
  os << indent << "Z Length: " << this->ZLength << "\n";
  os << indent << "Center: (" << this->Center[0] << ", " << this->Center[1] << ", "
     << this->Center[2] << ")\n";
  os << indent << "Output Points Precision: " << this->OutputPointsPrecision << "\n";
}