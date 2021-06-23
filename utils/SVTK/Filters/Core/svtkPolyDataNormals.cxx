/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPolyDataNormals.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkPolyDataNormals.h"

#include "svtkCellArray.h"
#include "svtkCellData.h"
#include "svtkFloatArray.h"
#include "svtkIdList.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMath.h"
#include "svtkNew.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPolyData.h"
#include "svtkPolygon.h"
#include "svtkPriorityQueue.h"
#include "svtkTriangleStrip.h"

#include "svtkNew.h"

svtkStandardNewMacro(svtkPolyDataNormals);

// Construct with feature angle=30, splitting and consistency turned on,
// flipNormals turned off, and non-manifold traversal turned on.
svtkPolyDataNormals::svtkPolyDataNormals()
{
  this->FeatureAngle = 30.0;
  this->Splitting = 1;
  this->Consistency = 1;
  this->FlipNormals = 0;
  this->ComputePointNormals = 1;
  this->ComputeCellNormals = 0;
  this->NonManifoldTraversal = 1;
  this->AutoOrientNormals = 0;
  // some internal data
  this->NumFlips = 0;
  this->OutputPointsPrecision = svtkAlgorithm::DEFAULT_PRECISION;
  this->Wave = nullptr;
  this->Wave2 = nullptr;
  this->CellIds = nullptr;
  this->CellPoints = nullptr;
  this->NeighborPoints = nullptr;
  this->Map = nullptr;
  this->OldMesh = nullptr;
  this->NewMesh = nullptr;
  this->Visited = nullptr;
  this->PolyNormals = nullptr;
  this->CosAngle = 0.0;
}

#define SVTK_CELL_NOT_VISITED 0
#define SVTK_CELL_VISITED 1

// Generate normals for polygon meshes
int svtkPolyDataNormals::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  // get the info objects
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  svtkPolyData* input = svtkPolyData::SafeDownCast(inInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkPolyData* output = svtkPolyData::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  svtkIdType npts = 0;
  const svtkIdType* pts = nullptr;
  svtkIdType numNewPts;
  double flipDirection = 1.0;
  svtkIdType numVerts, numLines, numPolys, numStrips;
  svtkIdType cellId;
  svtkIdType numPts;
  svtkPoints* inPts;
  svtkCellArray *inPolys, *inStrips, *polys;
  svtkPoints* newPts = nullptr;
  svtkFloatArray* newNormals;
  svtkPointData *pd, *outPD;
  svtkDataSetAttributes* outCD = output->GetCellData();
  double n[3];
  svtkCellArray* newPolys;
  svtkIdType ptId, oldId;

  svtkDebugMacro(<< "Generating surface normals");

  numVerts = input->GetNumberOfVerts();
  numLines = input->GetNumberOfLines();
  numPolys = input->GetNumberOfPolys();
  numStrips = input->GetNumberOfStrips();
  if ((numPts = input->GetNumberOfPoints()) < 1)
  {
    svtkDebugMacro(<< "No data to generate normals for!");
    return 1;
  }

  // If there is nothing to do, pass the data through
  if ((this->ComputePointNormals == 0 && this->ComputeCellNormals == 0) ||
    (numPolys < 1 && numStrips < 1))
  { // don't do anything! pass data through
    output->CopyStructure(input);
    output->GetPointData()->PassData(input->GetPointData());
    output->GetCellData()->PassData(input->GetCellData());
    return 1;
  }

  if (numStrips < 1)
  {
    output->GetCellData()->PassData(input->GetCellData());
  }

  // Load data into cell structure.  We need two copies: one is a
  // non-writable mesh used to perform topological queries.  The other
  // is used to write into and modify the connectivity of the mesh.
  //
  inPts = input->GetPoints();
  inPolys = input->GetPolys();
  inStrips = input->GetStrips();

  this->OldMesh = svtkPolyData::New();
  this->OldMesh->SetPoints(inPts);
  if (numStrips > 0) // have to decompose strips into triangles
  {
    svtkDataSetAttributes* inCD = input->GetCellData();
    // When we have triangle strips, make sure to create and copy
    // the cell data appropriately. Since strips are broken into
    // triangles, cell data cannot be passed as it is and needs to
    // be copied tuple by tuple.
    outCD->CopyAllocate(inCD);
    if (numPolys > 0)
    {
      polys = svtkCellArray::New();
      polys->DeepCopy(inPolys);
      svtkNew<svtkIdList> ids;
      ids->SetNumberOfIds(numPolys);
      for (svtkIdType i = 0; i < numPolys; i++)
      {
        ids->SetId(i, i);
      }
      outCD->CopyData(inCD, ids, ids);
    }
    else
    {
      polys = svtkCellArray::New();
      polys->AllocateEstimate(numStrips, 5);
    }
    svtkIdType inCellIdx = numPolys;
    svtkIdType outCellIdx = numPolys;
    for (inStrips->InitTraversal(); inStrips->GetNextCell(npts, pts); inCellIdx++)
    {
      svtkTriangleStrip::DecomposeStrip(npts, pts, polys);
      // Copy the cell data for the strip to each triangle.
      for (svtkIdType i = 0; i < npts - 2; i++)
      {
        outCD->CopyData(inCD, inCellIdx, outCellIdx++);
      }
    }
    this->OldMesh->SetPolys(polys);
    polys->Delete();
    numPolys = polys->GetNumberOfCells(); // added some new triangles
  }
  else
  {
    this->OldMesh->SetPolys(inPolys);
    polys = inPolys;
  }
  this->OldMesh->BuildLinks();
  this->UpdateProgress(0.10);

  pd = input->GetPointData();
  outPD = output->GetPointData();

  this->NewMesh = svtkPolyData::New();
  this->NewMesh->SetPoints(inPts);
  // create a copy because we're modifying it
  newPolys = svtkCellArray::New();
  newPolys->DeepCopy(polys);
  this->NewMesh->SetPolys(newPolys);
  this->NewMesh->BuildCells(); // builds connectivity

  // The visited array keeps track of which polygons have been visited.
  //
  if (this->Consistency || this->Splitting || this->AutoOrientNormals)
  {
    this->Visited = new int[numPolys];
    memset(this->Visited, SVTK_CELL_NOT_VISITED, numPolys * sizeof(int));
    this->CellIds = svtkIdList::New();
    this->CellIds->Allocate(SVTK_CELL_SIZE);
    this->CellPoints = svtkIdList::New();
    this->CellPoints->Allocate(SVTK_CELL_SIZE);
    this->NeighborPoints = svtkIdList::New();
    this->NeighborPoints->Allocate(SVTK_CELL_SIZE);
  }
  else
  {
    this->Visited = nullptr;
  }

  //  Traverse all polygons insuring proper direction of ordering.  This
  //  works by propagating a wave from a seed polygon to the polygon's
  //  edge neighbors. Each neighbor may be reordered to maintain consistency
  //  with its (already checked) neighbors.
  //
  this->NumFlips = 0;
  if (this->AutoOrientNormals)
  {
    // No need to check this->Consistency. It's implied.

    // Ok, here's the basic idea: the "left-most" polygon should
    // have its outward pointing normal facing left. If it doesn't,
    // reverse the vertex order. Then use it as the seed for other
    // connected polys. To find left-most polygon, first find left-most
    // point, and examine neighboring polys and see which one
    // has a normal that's "most aligned" with the X-axis. This process
    // will need to be repeated to handle all connected components in
    // the mesh. Report bugs/issues to cvolpe@ara.com.
    int foundLeftmostCell;
    svtkIdType leftmostCellID = -1, currentPointID, currentCellID;
    svtkIdType* leftmostCells;
    svtkIdType nleftmostCells;
    const svtkIdType* cellPts;
    svtkIdType nCellPts;
    int cIdx;
    double bestNormalAbsXComponent;
    int bestReverseFlag;
    svtkPriorityQueue* leftmostPoints = svtkPriorityQueue::New();
    this->Wave = svtkIdList::New();
    this->Wave->Allocate(numPolys / 4 + 1, numPolys);
    this->Wave2 = svtkIdList::New();
    this->Wave2->Allocate(numPolys / 4 + 1, numPolys);

    // Put all the points in the priority queue, based on x coord
    // So that we can find leftmost point
    leftmostPoints->Allocate(numPts);
    for (ptId = 0; ptId < numPts; ptId++)
    {
      leftmostPoints->Insert(inPts->GetPoint(ptId)[0], ptId);
    }

    // Repeat this while loop as long as the queue is not empty,
    // because there may be multiple connected components, each of
    // which needs to be seeded independently with a correctly
    // oriented polygon.
    while (leftmostPoints->GetNumberOfItems())
    {
      foundLeftmostCell = 0;
      // Keep iterating through leftmost points and cells located at
      // those points until I've got a leftmost point with
      // unvisited cells attached and I've found the best cell
      // at that point
      do
      {
        currentPointID = leftmostPoints->Pop();
        this->OldMesh->GetPointCells(currentPointID, nleftmostCells, leftmostCells);
        bestNormalAbsXComponent = 0.0;
        bestReverseFlag = 0;
        for (cIdx = 0; cIdx < nleftmostCells; cIdx++)
        {
          currentCellID = leftmostCells[cIdx];
          if (this->Visited[currentCellID] == SVTK_CELL_VISITED)
          {
            continue;
          }
          this->OldMesh->GetCellPoints(currentCellID, nCellPts, cellPts);
          svtkPolygon::ComputeNormal(inPts, nCellPts, cellPts, n);
          // Ok, see if this leftmost cell candidate is the best
          // so far
          if (fabs(n[0]) > bestNormalAbsXComponent)
          {
            bestNormalAbsXComponent = fabs(n[0]);
            leftmostCellID = currentCellID;
            // If the current leftmost cell's normal is pointing to the
            // right, then the vertex ordering is wrong
            bestReverseFlag = (n[0] > 0);
            foundLeftmostCell = 1;
          } // if this normal is most x-aligned so far
        }   // for each cell at current leftmost point
      } while (leftmostPoints->GetNumberOfItems() && !foundLeftmostCell);
      if (foundLeftmostCell)
      {
        // We've got the seed for a connected component! But do
        // we need to flip it first? We do, if it was pointed the wrong
        // way to begin with, or if the user requested flipping all
        // normals, but if both are true, then we leave it as it is.
        if (bestReverseFlag ^ this->FlipNormals)
        {
          this->NewMesh->ReverseCell(leftmostCellID);
          this->NumFlips++;
        }
        this->Wave->InsertNextId(leftmostCellID);
        this->Visited[leftmostCellID] = SVTK_CELL_VISITED;
        this->TraverseAndOrder();
        this->Wave->Reset();
        this->Wave2->Reset();
      } // if found leftmost cell
    }   // Still some points in the queue
    this->Wave->Delete();
    this->Wave2->Delete();
    leftmostPoints->Delete();
    svtkDebugMacro(<< "Reversed ordering of " << this->NumFlips << " polygons");
  } // automatically orient normals
  else
  {
    if (this->Consistency)
    {
      this->Wave = svtkIdList::New();
      this->Wave->Allocate(numPolys / 4 + 1, numPolys);
      this->Wave2 = svtkIdList::New();
      this->Wave2->Allocate(numPolys / 4 + 1, numPolys);
      for (cellId = 0; cellId < numPolys; cellId++)
      {
        if (this->Visited[cellId] == SVTK_CELL_NOT_VISITED)
        {
          if (this->FlipNormals)
          {
            this->NumFlips++;
            this->NewMesh->ReverseCell(cellId);
          }
          this->Wave->InsertNextId(cellId);
          this->Visited[cellId] = SVTK_CELL_VISITED;
          this->TraverseAndOrder();
        }

        this->Wave->Reset();
        this->Wave2->Reset();
      }

      this->Wave->Delete();
      this->Wave2->Delete();
      svtkDebugMacro(<< "Reversed ordering of " << this->NumFlips << " polygons");
    } // Consistent ordering
  }   // don't automatically orient normals

  this->UpdateProgress(0.333);

  //  Initial pass to compute polygon normals without effects of neighbors
  //
  this->PolyNormals = svtkFloatArray::New();
  this->PolyNormals->SetNumberOfComponents(3);
  this->PolyNormals->SetName("Normals");
  this->PolyNormals->SetNumberOfTuples(numVerts + numLines + numPolys);

  svtkIdType offsetCells = numVerts + numLines;
  n[0] = 1.0;
  n[1] = 0.0;
  n[2] = 0.0;
  for (cellId = 0; cellId < offsetCells; cellId++)
  {
    // add a default value for vertices and lines
    // normals do not have meaningful values, we set them to X
    this->PolyNormals->SetTuple(cellId, n);
  }

  for (cellId = 0, newPolys->InitTraversal(); newPolys->GetNextCell(npts, pts); cellId++)
  {
    if ((cellId % 1000) == 0)
    {
      this->UpdateProgress(0.333 + 0.333 * (double)cellId / (double)numPolys);
      if (this->GetAbortExecute())
      {
        break;
      }
    }
    svtkPolygon::ComputeNormal(inPts, npts, pts, n);
    this->PolyNormals->SetTuple(offsetCells + cellId, n);
  }

  // Split mesh if sharp features
  if (this->Splitting)
  {
    //  Traverse all nodes; evaluate loops and feature edges.  If feature
    //  edges found, split mesh creating new nodes.  Update polygon
    // connectivity.
    //
    this->CosAngle = cos(svtkMath::RadiansFromDegrees(this->FeatureAngle));
    //  Splitting will create new points.  We have to create index array
    // to map new points into old points.
    //
    this->Map = svtkIdList::New();
    this->Map->SetNumberOfIds(numPts);
    for (svtkIdType i = 0; i < numPts; i++)
    {
      this->Map->SetId(i, i);
    }

    for (ptId = 0; ptId < numPts; ptId++)
    {
      this->MarkAndSplit(ptId);
    } // for all input points

    numNewPts = this->Map->GetNumberOfIds();

    svtkDebugMacro(<< "Created " << numNewPts - numPts << " new points");

    //  Now need to map attributes of old points into new points.
    //
    outPD->CopyNormalsOff();
    outPD->CopyAllocate(pd, numNewPts);

    newPts = svtkPoints::New();

    // set precision for the points in the output
    if (this->OutputPointsPrecision == svtkAlgorithm::DEFAULT_PRECISION)
    {
      svtkPointSet* inputPointSet = svtkPointSet::SafeDownCast(input);
      if (inputPointSet)
      {
        newPts->SetDataType(inputPointSet->GetPoints()->GetDataType());
      }
      else
      {
        newPts->SetDataType(SVTK_FLOAT);
      }
    }
    else if (this->OutputPointsPrecision == svtkAlgorithm::SINGLE_PRECISION)
    {
      newPts->SetDataType(SVTK_FLOAT);
    }
    else if (this->OutputPointsPrecision == svtkAlgorithm::DOUBLE_PRECISION)
    {
      newPts->SetDataType(SVTK_DOUBLE);
    }

    newPts->SetNumberOfPoints(numNewPts);
    for (ptId = 0; ptId < numNewPts; ptId++)
    {
      oldId = this->Map->GetId(ptId);
      newPts->SetPoint(ptId, inPts->GetPoint(oldId));
      outPD->CopyData(pd, oldId, ptId);
    }
    this->Map->Delete();
  } // splitting

  else // no splitting, so no new points
  {
    numNewPts = numPts;
    outPD->CopyNormalsOff();
    outPD->PassData(pd);
  }

  if (this->Consistency || this->Splitting)
  {
    delete[] this->Visited;
    this->CellIds->Delete();
    this->CellIds = nullptr;
    this->CellPoints->Delete();
    this->CellPoints = nullptr;
    this->NeighborPoints->Delete();
    this->NeighborPoints = nullptr;
  }

  this->UpdateProgress(0.80);

  //  Finally, traverse all elements, computing polygon normals and
  //  accumulating them at the vertices.
  //
  if (this->FlipNormals && !this->Consistency)
  {
    flipDirection = -1.0;
  }

  newNormals = svtkFloatArray::New();
  newNormals->SetNumberOfComponents(3);
  newNormals->SetNumberOfTuples(numNewPts);
  newNormals->SetName("Normals");
  float* fNormals = newNormals->WritePointer(0, 3 * numNewPts);
  std::fill_n(fNormals, 3 * numNewPts, 0);

  float* fPolyNormals = this->PolyNormals->WritePointer(3 * offsetCells, 3 * numPolys);

  if (this->ComputePointNormals)
  {
    for (cellId = 0, newPolys->InitTraversal(); newPolys->GetNextCell(npts, pts); ++cellId)
    {
      for (svtkIdType i = 0; i < npts; ++i)
      {
        fNormals[3 * pts[i]] += fPolyNormals[3 * cellId];
        fNormals[3 * pts[i] + 1] += fPolyNormals[3 * cellId + 1];
        fNormals[3 * pts[i] + 2] += fPolyNormals[3 * cellId + 2];
      }
    }

    for (svtkIdType i = 0; i < numNewPts; ++i)
    {
      const double length =
        sqrt(fNormals[3 * i] * fNormals[3 * i] + fNormals[3 * i + 1] * fNormals[3 * i + 1] +
          fNormals[3 * i + 2] * fNormals[3 * i + 2]) *
        flipDirection;
      if (length != 0.0)
      {
        fNormals[3 * i] /= length;
        fNormals[3 * i + 1] /= length;
        fNormals[3 * i + 2] /= length;
      }
    }
  }

  //  Update ourselves.  If no new nodes have been created (i.e., no
  //  splitting), we can simply pass data through.
  //
  if (!this->Splitting)
  {
    output->SetPoints(inPts);
  }

  //  If there is splitting, then have to send down the new data.
  //
  else
  {
    output->SetPoints(newPts);
    newPts->Delete();
  }

  if (this->ComputeCellNormals)
  {
    outCD->SetNormals(this->PolyNormals);
  }
  this->PolyNormals->Delete();

  if (this->ComputePointNormals)
  {
    outPD->SetNormals(newNormals);
  }
  newNormals->Delete();

  output->SetPolys(newPolys);
  newPolys->Delete();

  // copy the original vertices and lines to the output
  output->SetVerts(input->GetVerts());
  output->SetLines(input->GetLines());

  this->OldMesh->Delete();
  this->NewMesh->Delete();

  return 1;
}

//  Propagate wave of consistently ordered polygons.
//
void svtkPolyDataNormals::TraverseAndOrder()
{
  svtkIdType i, k;
  int j, l, j1;
  svtkIdType numIds, cellId;
  const svtkIdType* pts;
  const svtkIdType* neiPts;
  svtkIdType npts;
  svtkIdType numNeiPts;
  svtkIdType neighbor;
  svtkIdList* tmpWave;

  // propagate wave until nothing left in wave
  while ((numIds = this->Wave->GetNumberOfIds()) > 0)
  {
    for (i = 0; i < numIds; i++)
    {
      cellId = this->Wave->GetId(i);

      // Store the results here in a svtkIdList, since passing npts/pts directly
      // would result in the data getting invalidated by the later call to
      // NewMesh->GetCellPoints.
      this->NewMesh->GetCellPoints(cellId, this->CellPoints);
      npts = this->CellPoints->GetNumberOfIds();
      pts = this->CellPoints->GetPointer(0);

      for (j = 0, j1 = 1; j < npts; ++j, (j1 = (++j1 < npts) ? j1 : 0)) // for each edge neighbor
      {
        this->OldMesh->GetCellEdgeNeighbors(cellId, pts[j], pts[j1], this->CellIds);

        //  Check the direction of the neighbor ordering.  Should be
        //  consistent with us (i.e., if we are n1->n2,
        // neighbor should be n2->n1).
        if (this->CellIds->GetNumberOfIds() == 1 || this->NonManifoldTraversal)
        {
          for (k = 0; k < this->CellIds->GetNumberOfIds(); k++)
          {
            if (this->Visited[this->CellIds->GetId(k)] == SVTK_CELL_NOT_VISITED)
            {
              neighbor = this->CellIds->GetId(k);
              this->NewMesh->GetCellPoints(neighbor, this->NeighborPoints);
              numNeiPts = this->NeighborPoints->GetNumberOfIds();
              neiPts = this->NeighborPoints->GetPointer(0);

              for (l = 0; l < numNeiPts; l++)
              {
                if (neiPts[l] == pts[j1])
                {
                  break;
                }
              }

              //  Have to reverse ordering if neighbor not consistent
              //
              if (neiPts[(l + 1) % numNeiPts] != pts[j])
              {
                this->NumFlips++;
                this->NewMesh->ReverseCell(neighbor);
              }
              this->Visited[neighbor] = SVTK_CELL_VISITED;
              this->Wave2->InsertNextId(neighbor);
            } // if cell not visited
          }   // for each edge neighbor
        }     // for manifold or non-manifold traversal allowed
      }       // for all edges of this polygon
    }         // for all cells in wave

    // swap wave and proceed with propagation
    tmpWave = this->Wave;
    this->Wave = this->Wave2;
    this->Wave2 = tmpWave;
    this->Wave2->Reset();
  } // while wave still propagating
}

//
//  Mark polygons around vertex.  Create new vertex (if necessary) and
//  replace (i.e., split mesh).
//
void svtkPolyDataNormals::MarkAndSplit(svtkIdType ptId)
{
  int i, j;

  // Get the cells using this point and make sure that we have to do something
  svtkIdType ncells;
  svtkIdType* cells;
  this->OldMesh->GetPointCells(ptId, ncells, cells);
  if (ncells <= 1)
  {
    return; // point does not need to be further disconnected
  }

  // Start moving around the "cycle" of points using the point. Label
  // each point as requiring a visit. Then label each subregion of cells
  // connected to this point that are connected (and not separated by
  // a feature edge) with a given region number. For each N regions
  // created, N-1 duplicate (split) points are created. The split point
  // replaces the current point ptId in the polygons connectivity array.
  //
  // Start by initializing the cells as unvisited
  for (i = 0; i < ncells; i++)
  {
    this->Visited[cells[i]] = -1;
  }

  // Loop over all cells and mark the region that each is in.
  //
  svtkIdType numPts;
  const svtkIdType* pts;
  int numRegions = 0;
  svtkIdType spot, neiPt[2], nei, cellId, neiCellId;
  double thisNormal[3], neiNormal[3];
  for (j = 0; j < ncells; j++) // for all cells connected to point
  {
    if (this->Visited[cells[j]] < 0) // for all unvisited cells
    {
      this->Visited[cells[j]] = numRegions;
      // okay, mark all the cells connected to this seed cell and using ptId
      this->OldMesh->GetCellPoints(cells[j], numPts, pts);

      // find the two edges
      for (spot = 0; spot < numPts; spot++)
      {
        if (pts[spot] == ptId)
        {
          break;
        }
      }

      if (spot == 0)
      {
        neiPt[0] = pts[spot + 1];
        neiPt[1] = pts[numPts - 1];
      }
      else if (spot == (numPts - 1))
      {
        neiPt[0] = pts[spot - 1];
        neiPt[1] = pts[0];
      }
      else
      {
        neiPt[0] = pts[spot + 1];
        neiPt[1] = pts[spot - 1];
      }

      for (i = 0; i < 2; i++) // for each of the two edges of the seed cell
      {
        cellId = cells[j];
        nei = neiPt[i];
        while (cellId >= 0) // while we can grow this region
        {
          this->OldMesh->GetCellEdgeNeighbors(cellId, ptId, nei, this->CellIds);
          if (this->CellIds->GetNumberOfIds() == 1 &&
            this->Visited[(neiCellId = this->CellIds->GetId(0))] < 0)
          {
            this->PolyNormals->GetTuple(cellId, thisNormal);
            this->PolyNormals->GetTuple(neiCellId, neiNormal);

            if (svtkMath::Dot(thisNormal, neiNormal) > CosAngle)
            {
              // visit and arrange to visit next edge neighbor
              this->Visited[neiCellId] = numRegions;
              cellId = neiCellId;
              this->OldMesh->GetCellPoints(cellId, numPts, pts);

              for (spot = 0; spot < numPts; spot++)
              {
                if (pts[spot] == ptId)
                {
                  break;
                }
              }

              if (spot == 0)
              {
                nei = (pts[spot + 1] != nei ? pts[spot + 1] : pts[numPts - 1]);
              }
              else if (spot == (numPts - 1))
              {
                nei = (pts[spot - 1] != nei ? pts[spot - 1] : pts[0]);
              }
              else
              {
                nei = (pts[spot + 1] != nei ? pts[spot + 1] : pts[spot - 1]);
              }

            } // if not separated by edge angle
            else
            {
              cellId = -1; // separated by edge angle
            }
          } // if can move to edge neighbor
          else
          {
            cellId = -1; // separated by previous visit, boundary, or non-manifold
          }
        } // while visit wave is propagating
      }   // for each of the two edges of the starting cell
      numRegions++;
    } // if cell is unvisited
  }   // for all cells connected to point ptId

  if (numRegions <= 1)
  {
    return; // a single region, no splitting ever required
  }

  // Okay, for all cells not in the first region, the ptId is
  // replaced with a new ptId, which is a duplicate of the first
  // point, but disconnected topologically.
  //
  svtkIdType lastId = this->Map->GetNumberOfIds();
  svtkIdType replacementPoint;
  for (j = 0; j < ncells; j++)
  {
    if (this->Visited[cells[j]] > 0) // replace point if splitting needed
    {
      replacementPoint = lastId + this->Visited[cells[j]] - 1;
      this->Map->InsertId(replacementPoint, ptId);
      this->NewMesh->ReplaceCellPoint(cells[j], ptId, replacementPoint);
    } // if not in first regions and requiring splitting
  }   // for all cells connected to ptId
}

void svtkPolyDataNormals::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Feature Angle: " << this->FeatureAngle << "\n";
  os << indent << "Splitting: " << (this->Splitting ? "On\n" : "Off\n");
  os << indent << "Consistency: " << (this->Consistency ? "On\n" : "Off\n");
  os << indent << "Flip Normals: " << (this->FlipNormals ? "On\n" : "Off\n");
  os << indent << "Auto Orient Normals: " << (this->AutoOrientNormals ? "On\n" : "Off\n");
  os << indent << "Num Flips: " << this->NumFlips << endl;
  os << indent << "Compute Point Normals: " << (this->ComputePointNormals ? "On\n" : "Off\n");
  os << indent << "Compute Cell Normals: " << (this->ComputeCellNormals ? "On\n" : "Off\n");
  os << indent << "Non-manifold Traversal: " << (this->NonManifoldTraversal ? "On\n" : "Off\n");
  os << indent << "Precision of the output points: " << this->OutputPointsPrecision << "\n";
}