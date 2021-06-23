/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkSubdivideTetra.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkSubdivideTetra.h"

#include "svtkCellType.h"
#include "svtkGenericCell.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMergePoints.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkUnstructuredGrid.h"

svtkStandardNewMacro(svtkSubdivideTetra);

//----------------------------------------------------------------------------
// Description:
// Construct with all types of clipping turned off.
svtkSubdivideTetra::svtkSubdivideTetra() = default;

//----------------------------------------------------------------------------
int svtkSubdivideTetra::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  // get the info objects
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  svtkUnstructuredGrid* input =
    svtkUnstructuredGrid::SafeDownCast(inInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkUnstructuredGrid* output =
    svtkUnstructuredGrid::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  svtkIdType numPts = input->GetNumberOfPoints();
  svtkIdType numCells = input->GetNumberOfCells();
  svtkPoints* inPts = input->GetPoints();
  svtkIdType cellId, i;
  svtkIdType pts[4];
  svtkGenericCell* cell;
  svtkPointData* pd = input->GetPointData();
  svtkPointData* outputPD = output->GetPointData();
  svtkPoints* newPts;
  svtkIdType ptId;

  double weights[4], x0[3], x1[3], x2[3], x3[3], x[3];
  int p0, p1, p2, p3;
  svtkIdType center, e01, e02, e03, e12, e13, e23;
  svtkMergePoints* locator;

  svtkDebugMacro(<< "Executing mesh subdivide");

  if (input->IsHomogeneous() == 0 || input->GetCellType(0) != SVTK_TETRA)
  {
    svtkErrorMacro(<< "all cells must be tetrahedra.");
    return 1;
  }

  // Copy original points and point data
  newPts = svtkPoints::New();
  newPts->Allocate(5 * numPts, numPts);
  outputPD->InterpolateAllocate(pd, 5 * numPts, numPts);

  output->Allocate(numCells);
  output->SetPoints(newPts);

  locator = svtkMergePoints::New();
  locator->InitPointInsertion(newPts, input->GetBounds());

  for (ptId = 0; ptId < numPts; ptId++)
  {
    locator->InsertNextPoint(inPts->GetPoint(ptId));
    outputPD->CopyData(pd, ptId, ptId);
  }

  cell = svtkGenericCell::New();

  // loop over tetrahedra, generating sixteen new ones for each. This is
  // done by introducing mid-edge nodes and a single mid-tetra node.
  for (cellId = 0; cellId < numCells; cellId++)
  {
    input->GetCell(cellId, cell);

    // get tetra points
    cell->Points->GetPoint(0, x0);
    cell->Points->GetPoint(1, x1);
    cell->Points->GetPoint(2, x2);
    cell->Points->GetPoint(3, x3);

    p0 = cell->PointIds->GetId(0);
    p1 = cell->PointIds->GetId(1);
    p2 = cell->PointIds->GetId(2);
    p3 = cell->PointIds->GetId(3);

    // compute center point
    weights[0] = weights[1] = weights[2] = weights[3] = 0.25;
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.25 * (x0[i] + x1[i] + x2[i] + x3[i]);
    }
    center = locator->InsertNextPoint(x);
    outputPD->InterpolatePoint(pd, center, cell->PointIds, weights);

    // compute edge points
    // edge 0-1
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x1[i] + x0[i]);
    }
    e01 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e01, p0, p1, 0.5);

    // edge 1-2
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x2[i] + x1[i]);
    }
    e12 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e12, p1, p2, 0.5);

    // edge 2-0
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x2[i] + x0[i]);
    }
    e02 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e02, p2, p0, 0.5);

    // edge 0-3
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x3[i] + x0[i]);
    }
    e03 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e03, p0, p3, 0.5);

    // edge 1-3
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x3[i] + x1[i]);
    }
    e13 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e13, p1, p3, 0.5);

    // edge 2-3
    for (i = 0; i < 3; i++)
    {
      x[i] = 0.5 * (x3[i] + x2[i]);
    }
    e23 = locator->InsertNextPoint(x);
    outputPD->InterpolateEdge(pd, e23, p2, p3, 0.5);

    // Now create tetrahedra
    // First, four tetra from each vertex
    pts[0] = p0;
    pts[1] = e01;
    pts[2] = e02;
    pts[3] = e03;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[0] = p1;
    pts[1] = e01;
    pts[2] = e12;
    pts[3] = e13;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[0] = p2;
    pts[1] = e02;
    pts[2] = e12;
    pts[3] = e23;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[0] = p3;
    pts[1] = e03;
    pts[2] = e13;
    pts[3] = e23;
    output->InsertNextCell(SVTK_TETRA, 4, pts);

    // Now four tetra from cut-off tetra corners
    pts[0] = center;
    pts[1] = e01;
    pts[2] = e02;
    pts[3] = e03;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e01;
    pts[2] = e12;
    pts[3] = e13;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e02;
    pts[2] = e12;
    pts[3] = e23;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e03;
    pts[2] = e13;
    pts[3] = e23;
    output->InsertNextCell(SVTK_TETRA, 4, pts);

    // Now four tetra from triangles on tetra faces
    pts[0] = center;
    pts[1] = e01;
    pts[2] = e12;
    pts[3] = e02;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e01;
    pts[2] = e13;
    pts[3] = e03;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e12;
    pts[2] = e23;
    pts[3] = e13;
    output->InsertNextCell(SVTK_TETRA, 4, pts);
    pts[1] = e02;
    pts[2] = e23;
    pts[3] = e03;
    output->InsertNextCell(SVTK_TETRA, 4, pts);

  } // for all cells
  cell->Delete();

  svtkDebugMacro(<< "Subdivided " << numCells << " cells");

  locator->Delete();
  newPts->Delete();
  output->Squeeze();

  return 1;
}

//----------------------------------------------------------------------------
void svtkSubdivideTetra::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}