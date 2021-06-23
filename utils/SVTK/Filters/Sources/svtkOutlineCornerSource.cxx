/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkOutlineCornerSource.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkOutlineCornerSource.h"

#include "svtkCellArray.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkObjectFactory.h"
#include "svtkPoints.h"
#include "svtkPolyData.h"

svtkStandardNewMacro(svtkOutlineCornerSource);

//----------------------------------------------------------------------------
svtkOutlineCornerSource::svtkOutlineCornerSource()
  : svtkOutlineSource()
{
  this->CornerFactor = 0.2;
}

//----------------------------------------------------------------------------
int svtkOutlineCornerSource::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* outputVector)
{
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  double* bounds;
  double inner_bounds[6];

  int i, j, k;

  // Initialize
  double delta;

  bounds = this->Bounds;
  for (i = 0; i < 3; i++)
  {
    delta = (bounds[2 * i + 1] - bounds[2 * i]) * this->CornerFactor;
    inner_bounds[2 * i] = bounds[2 * i] + delta;
    inner_bounds[2 * i + 1] = bounds[2 * i + 1] - delta;
  }

  // Allocate storage and create outline
  svtkPoints* newPts;
  svtkCellArray* newLines;
  svtkPolyData* output = svtkPolyData::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  newPts = svtkPoints::New();

  // Set the desired precision for the points in the output.
  if (this->OutputPointsPrecision == svtkAlgorithm::DOUBLE_PRECISION)
  {
    newPts->SetDataType(SVTK_DOUBLE);
  }
  else
  {
    newPts->SetDataType(SVTK_FLOAT);
  }

  newPts->Allocate(32);
  newLines = svtkCellArray::New();
  newLines->AllocateEstimate(24, 2);

  double x[3];
  svtkIdType pts[2];

  int pid = 0;

  // 32 points and 24 lines
  for (i = 0; i <= 1; i++)
  {
    for (j = 2; j <= 3; j++)
    {
      for (k = 4; k <= 5; k++)
      {
        pts[0] = pid;
        x[0] = bounds[i];
        x[1] = bounds[j];
        x[2] = bounds[k];
        newPts->InsertPoint(pid++, x);

        pts[1] = pid;
        x[0] = inner_bounds[i];
        x[1] = bounds[j];
        x[2] = bounds[k];
        newPts->InsertPoint(pid++, x);
        newLines->InsertNextCell(2, pts);

        pts[1] = pid;
        x[0] = bounds[i];
        x[1] = inner_bounds[j];
        x[2] = bounds[k];
        newPts->InsertPoint(pid++, x);
        newLines->InsertNextCell(2, pts);

        pts[1] = pid;
        x[0] = bounds[i];
        x[1] = bounds[j];
        x[2] = inner_bounds[k];
        newPts->InsertPoint(pid++, x);
        newLines->InsertNextCell(2, pts);
      }
    }
  }

  // Update selves and release memory
  output->SetPoints(newPts);
  newPts->Delete();

  output->SetLines(newLines);
  newLines->Delete();

  return 1;
}

//----------------------------------------------------------------------------
void svtkOutlineCornerSource::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "CornerFactor: " << this->CornerFactor << "\n";
  os << indent << "Output Points Precision: " << this->OutputPointsPrecision << "\n";
}