/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkAbstractInterpolatedVelocityField.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkAbstractInterpolatedVelocityField.h"

#include "svtkClosestPointStrategy.h"
#include "svtkDataArray.h"
#include "svtkDataSet.h"
#include "svtkGenericCell.h"
#include "svtkMath.h"
#include "svtkNew.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"

#include <map>
#include <utility> //make_pair

//----------------------------------------------------------------------------
svtkCxxSetObjectMacro(svtkAbstractInterpolatedVelocityField, FindCellStrategy, svtkFindCellStrategy);

//----------------------------------------------------------------------------
const double svtkAbstractInterpolatedVelocityField::TOLERANCE_SCALE = 1.0E-8;
const double svtkAbstractInterpolatedVelocityField::SURFACE_TOLERANCE_SCALE = 1.0E-5;

// Map FindCell strategies to input datasets. Necessary due to potential composite
// data set input types, where each piece may have a different strategy.
struct svtkStrategyMap : public std::map<svtkPointSet*, svtkFindCellStrategy*>
{
};

//---------------------------------------------------------------------------
svtkAbstractInterpolatedVelocityField::svtkAbstractInterpolatedVelocityField()
{
  this->NumFuncs = 3;     // u, v, w
  this->NumIndepVars = 4; // x, y, z, t
  this->Weights = nullptr;
  this->WeightsSize = 0;

  this->Caching = true; // Caching on by default
  this->CacheHit = 0;
  this->CacheMiss = 0;

  this->LastCellId = -1;
  this->LastDataSet = nullptr;
  this->LastPCoords[0] = 0.0;
  this->LastPCoords[1] = 0.0;
  this->LastPCoords[2] = 0.0;

  this->VectorsType = 0;
  this->VectorsSelection = nullptr;
  this->NormalizeVector = false;
  this->ForceSurfaceTangentVector = false;
  this->SurfaceDataset = false;

  this->Cell = svtkGenericCell::New();
  this->GenCell = svtkGenericCell::New();

  this->FindCellStrategy = nullptr;
  this->StrategyMap = new svtkStrategyMap;
}

//---------------------------------------------------------------------------
svtkAbstractInterpolatedVelocityField::~svtkAbstractInterpolatedVelocityField()
{
  this->NumFuncs = 0;
  this->NumIndepVars = 0;

  this->LastDataSet = nullptr;
  this->SetVectorsSelection(nullptr);

  delete[] this->Weights;
  this->Weights = nullptr;

  if (this->Cell)
  {
    this->Cell->Delete();
    this->Cell = nullptr;
  }

  if (this->GenCell)
  {
    this->GenCell->Delete();
    this->GenCell = nullptr;
  }

  // Need to free strategies associated with each dataset. There is a special
  // case where the strategy cannot be deleted because is has been specified
  // by the user.
  svtkFindCellStrategy* strat;
  for (auto iter = this->StrategyMap->begin(); iter != this->StrategyMap->end(); ++iter)
  {
    strat = iter->second;
    if (strat != nullptr && strat != this->FindCellStrategy)
    {
      strat->Delete();
    }
  }
  delete this->StrategyMap;

  this->SetFindCellStrategy(nullptr);
}

//---------------------------------------------------------------------------
int svtkAbstractInterpolatedVelocityField::FunctionValues(svtkDataSet* dataset, double* x, double* f)
{
  int i, j, numPts, id;
  svtkDataArray* vectors = nullptr;
  double vec[3];

  f[0] = f[1] = f[2] = 0.0;

  // See if a dataset has been specified and if there are input vectors
  if (!dataset)
  {
    svtkErrorMacro(<< "Can't evaluate dataset!");
    vectors = nullptr;
    return 0;
  }

  if (!this->VectorsSelection) // if a selection is not specified,
  {
    // use the first one in the point set (this is a behavior for backward compatibility)
    vectors = dataset->GetPointData()->GetVectors(nullptr);
  }
  else
  {
    vectors =
      dataset->GetAttributesAsFieldData(this->VectorsType)->GetArray(this->VectorsSelection);
  }

  if (!vectors)
  {
    svtkErrorMacro(<< "Can't evaluate dataset!");
    return 0;
  }

  // Make sure a FindCell strategy has been defined and initialized. The
  // potential for composite (svtkPointSet) datasets make the process more complex.
  svtkPointSet* ps = svtkPointSet::SafeDownCast(dataset);
  if (ps != nullptr)
  {
    svtkFindCellStrategy* strategy;
    svtkStrategyMap::iterator sIter = this->StrategyMap->find(ps);
    if (sIter == this->StrategyMap->end())
    {
      if (this->FindCellStrategy != nullptr)
      {
        strategy = this->FindCellStrategy->NewInstance();
      }
      else
      {
        strategy = svtkClosestPointStrategy::New(); // default type if not provided
      }
      this->StrategyMap->insert(std::make_pair(ps, strategy));
    }
    else
    {
      strategy = sIter->second;
    }
    strategy->Initialize(ps);
  }

  // Compute function values
  if (!this->FindAndUpdateCell(dataset, x))
  {
    vectors = nullptr;
    return 0;
  }

  // if the cell is valid
  if (this->LastCellId >= 0)
  {
    numPts = this->GenCell->GetNumberOfPoints();

    // interpolate the vectors
    if (this->VectorsType == svtkDataObject::POINT)
    {
      for (j = 0; j < numPts; j++)
      {
        id = this->GenCell->PointIds->GetId(j);
        vectors->GetTuple(id, vec);
        for (i = 0; i < 3; i++)
        {
          f[i] += vec[i] * this->Weights[j];
        }
      }
    }
    else
    {
      vectors->GetTuple(this->LastCellId, f);
    }

    if (this->ForceSurfaceTangentVector)
    {
      svtkNew<svtkIdList> ptIds;
      dataset->GetCellPoints(this->LastCellId, ptIds);
      if (ptIds->GetNumberOfIds() < 3)
      {
        svtkErrorMacro(<< "Cannot compute normal on cells with less than 3 points");
      }
      else
      {
        double p1[3];
        double p2[3];
        double p3[3];
        double normal[3];
        double v1[3], v2[3];
        double k;

        dataset->GetPoint(ptIds->GetId(0), p1);
        dataset->GetPoint(ptIds->GetId(1), p2);
        dataset->GetPoint(ptIds->GetId(2), p3);

        // Compute othogonal component
        v1[0] = p2[0] - p1[0];
        v1[1] = p2[1] - p1[1];
        v1[2] = p2[2] - p1[2];
        v2[0] = p3[0] - p1[0];
        v2[1] = p3[1] - p1[1];
        v2[2] = p3[2] - p1[2];

        svtkMath::Cross(v1, v2, normal);
        svtkMath::Normalize(normal);
        k = svtkMath::Dot(normal, f);

        // Remove non orthogonal component.
        f[0] = f[0] - (normal[0] * k);
        f[1] = f[1] - (normal[1] * k);
        f[2] = f[2] - (normal[2] * k);
      }
    }

    if (this->NormalizeVector == true)
    {
      svtkMath::Normalize(f);
    }
  }
  // if not, return false
  else
  {
    vectors = nullptr;
    return 0;
  }

  vectors = nullptr;
  return 1;
}

//---------------------------------------------------------------------------
bool svtkAbstractInterpolatedVelocityField::CheckPCoords(double pcoords[3])
{
  for (int i = 0; i < 3; i++)
  {
    if (pcoords[i] < 0 || pcoords[i] > 1)
    {
      return false;
    }
  }
  return true;
}

//---------------------------------------------------------------------------
bool svtkAbstractInterpolatedVelocityField::FindAndUpdateCell(svtkDataSet* dataset, double* x)
{
  double tol2, dist2;
  if (this->SurfaceDataset)
  {
    tol2 = dataset->GetLength() * dataset->GetLength() *
      svtkAbstractInterpolatedVelocityField::SURFACE_TOLERANCE_SCALE;
  }
  else
  {
    tol2 = dataset->GetLength() * dataset->GetLength() *
      svtkAbstractInterpolatedVelocityField::TOLERANCE_SCALE;
  }

  double closest[3];
  bool found = false;
  if (this->Caching)
  {
    bool out = false;

    // See if the point is in the cached cell
    if (this->LastCellId != -1)
    {
      // Use cache cell only if point is inside
      // or , with surface , not far and in pccords
      int ret = this->GenCell->EvaluatePosition(
        x, closest, this->LastSubId, this->LastPCoords, dist2, this->Weights);
      if (ret == -1 || (ret == 0 && !this->SurfaceDataset) ||
        (this->SurfaceDataset && (dist2 > tol2 || !this->CheckPCoords(this->LastPCoords))))
      {
        out = true;
      }

      if (out)
      {
        this->CacheMiss++;

        dataset->GetCell(this->LastCellId, this->Cell);

        // Search around current cached cell to see if there is a cell within tolerance
        svtkFindCellStrategy* strategy = nullptr;
        svtkPointSet* ps;
        if ((ps = svtkPointSet::SafeDownCast(dataset)) != nullptr)
        {
          svtkStrategyMap::iterator sIter = this->StrategyMap->find(ps);
          strategy = (sIter != this->StrategyMap->end() ? sIter->second : nullptr);
        }
        this->LastCellId = ((strategy == nullptr)
            ? dataset->FindCell(x, this->Cell, this->GenCell, this->LastCellId, tol2,
                this->LastSubId, this->LastPCoords, this->Weights)
            : strategy->FindCell(x, this->Cell, this->GenCell, this->LastCellId, tol2,
                this->LastSubId, this->LastPCoords, this->Weights));

        if (this->LastCellId != -1 &&
          (!this->SurfaceDataset || this->CheckPCoords(this->LastPCoords)))
        {
          dataset->GetCell(this->LastCellId, this->GenCell);
          found = true;
        }
      }
      else
      {
        this->CacheHit++;
        found = true;
      }
    }
  } // if caching

  if (!found)
  {
    // if the cell is not found in cache, do a global search (ignore initial
    // cell if there is one)
    svtkFindCellStrategy* strategy = nullptr;
    svtkPointSet* ps;
    if ((ps = svtkPointSet::SafeDownCast(dataset)) != nullptr)
    {
      svtkStrategyMap::iterator sIter = this->StrategyMap->find(ps);
      strategy = (sIter != this->StrategyMap->end() ? sIter->second : nullptr);
    }
    this->LastCellId =
      ((strategy == nullptr) ? dataset->FindCell(x, nullptr, this->GenCell, -1, tol2,
                                 this->LastSubId, this->LastPCoords, this->Weights)
                             : strategy->FindCell(x, nullptr, this->GenCell, -1, tol2,
                                 this->LastSubId, this->LastPCoords, this->Weights));

    if (this->LastCellId != -1 && (!this->SurfaceDataset || this->CheckPCoords(this->LastPCoords)))
    {
      dataset->GetCell(this->LastCellId, this->GenCell);
    }
    else
    {
      if (this->SurfaceDataset)
      {
        // Still cannot find cell, use point locator to find a (arbitrary) cell, for 2D surface
        svtkIdType idPoint = dataset->FindPoint(x);
        if (idPoint < 0)
        {
          this->LastCellId = -1;
          return false;
        }

        svtkNew<svtkIdList> cellList;
        dataset->GetPointCells(idPoint, cellList);
        double minDist2 = dataset->GetLength() * dataset->GetLength();
        svtkIdType minDistId = -1;
        for (svtkIdType idCell = 0; idCell < cellList->GetNumberOfIds(); idCell++)
        {
          this->LastCellId = cellList->GetId(idCell);
          dataset->GetCell(this->LastCellId, this->GenCell);
          int ret = this->GenCell->EvaluatePosition(
            x, closest, this->LastSubId, this->LastPCoords, dist2, this->Weights);
          if (ret != -1 && dist2 < minDist2)
          {
            minDistId = this->LastCellId;
            minDist2 = dist2;
          }
        }

        if (minDistId == -1)
        {
          this->LastCellId = -1;
          return false;
        }

        // Recover closest cell info
        this->LastCellId = minDistId;
        dataset->GetCell(this->LastCellId, this->GenCell);
        int ret = this->GenCell->EvaluatePosition(
          x, closest, this->LastSubId, this->LastPCoords, dist2, this->Weights);

        // Find Point being not perfect to find cell, check for closer cells
        svtkNew<svtkIdList> boundaryPoints;
        svtkNew<svtkIdList> neighCells;
        bool edge = false;
        bool closer;
        while (true)
        {
          this->GenCell->CellBoundary(this->LastSubId, this->LastPCoords, boundaryPoints);
          dataset->GetCellNeighbors(this->LastCellId, boundaryPoints, neighCells);
          if (neighCells->GetNumberOfIds() == 0)
          {
            edge = true;
            break;
          }
          closer = false;
          for (svtkIdType neighCellId = 0; neighCellId < neighCells->GetNumberOfIds(); neighCellId++)
          {
            this->LastCellId = neighCells->GetId(neighCellId);
            dataset->GetCell(this->LastCellId, this->GenCell);
            ret = this->GenCell->EvaluatePosition(
              x, closest, this->LastSubId, this->LastPCoords, dist2, this->Weights);
            if (ret != -1 && dist2 < minDist2)
            {
              minDistId = this->LastCellId;
              minDist2 = dist2;
              closer = true;
            }
          }
          if (!closer)
          {
            break;
          }
        }

        // Recover closest cell info
        if (!edge)
        {
          this->LastCellId = minDistId;
          dataset->GetCell(this->LastCellId, this->GenCell);
          this->GenCell->EvaluatePosition(
            x, closest, this->LastSubId, this->LastPCoords, dist2, this->Weights);
        }
        if (minDist2 > tol2 || (!this->CheckPCoords(this->LastPCoords) && edge))
        {
          this->LastCellId = -1;
          return false;
        }
      }
      else
      {
        this->LastCellId = -1;
        return false;
      }
    }
  }
  return true;
}
//----------------------------------------------------------------------------
int svtkAbstractInterpolatedVelocityField::GetLastWeights(double* w)
{
  if (this->LastCellId < 0)
  {
    return 0;
  }

  int numPts = this->GenCell->GetNumberOfPoints();
  for (int i = 0; i < numPts; i++)
  {
    w[i] = this->Weights[i];
  }

  return 1;
}

//----------------------------------------------------------------------------
int svtkAbstractInterpolatedVelocityField::GetLastLocalCoordinates(double pcoords[3])
{
  if (this->LastCellId < 0)
  {
    return 0;
  }

  pcoords[0] = this->LastPCoords[0];
  pcoords[1] = this->LastPCoords[1];
  pcoords[2] = this->LastPCoords[2];

  return 1;
}

//----------------------------------------------------------------------------
void svtkAbstractInterpolatedVelocityField::FastCompute(svtkDataArray* vectors, double f[3])
{
  int pntIdx;
  int numPts = this->GenCell->GetNumberOfPoints();
  double vector[3];
  f[0] = f[1] = f[2] = 0.0;

  for (int i = 0; i < numPts; i++)
  {
    pntIdx = this->GenCell->PointIds->GetId(i);
    vectors->GetTuple(pntIdx, vector);
    f[0] += vector[0] * this->Weights[i];
    f[1] += vector[1] * this->Weights[i];
    f[2] += vector[2] * this->Weights[i];
  }
}

//----------------------------------------------------------------------------
bool svtkAbstractInterpolatedVelocityField::InterpolatePoint(svtkPointData* outPD, svtkIdType outIndex)
{
  if (!this->LastDataSet)
  {
    return 0;
  }

  outPD->InterpolatePoint(
    this->LastDataSet->GetPointData(), outIndex, this->GenCell->PointIds, this->Weights);
  return 1;
}

//----------------------------------------------------------------------------
void svtkAbstractInterpolatedVelocityField::CopyParameters(
  svtkAbstractInterpolatedVelocityField* from)
{
  this->Caching = from->Caching;
  this->SetFindCellStrategy(from->GetFindCellStrategy());
}

//----------------------------------------------------------------------------
void svtkAbstractInterpolatedVelocityField::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent
     << "VectorsSelection: " << (this->VectorsSelection ? this->VectorsSelection : "(none)")
     << endl;
  os << indent << "NormalizeVector: " << (this->NormalizeVector ? "on." : "off.") << endl;
  os << indent
     << "ForceSurfaceTangentVector: " << (this->ForceSurfaceTangentVector ? "on." : "off.") << endl;
  os << indent << "SurfaceDataset: " << (this->SurfaceDataset ? "on." : "off.") << endl;

  os << indent << "Caching Status: " << (this->Caching ? "on." : "off.") << endl;
  os << indent << "Cache Hit: " << this->CacheHit << endl;
  os << indent << "Cache Miss: " << this->CacheMiss << endl;
  os << indent << "Weights Size: " << this->WeightsSize << endl;

  os << indent << "Last Dataset: " << this->LastDataSet << endl;
  os << indent << "Last Cell Id: " << this->LastCellId << endl;
  os << indent << "Last Cell: " << this->Cell << endl;
  os << indent << "Current Cell: " << this->GenCell << endl;
  os << indent << "Last P-Coords: " << this->LastPCoords[0] << ", " << this->LastPCoords[1] << ", "
     << this->LastPCoords[2] << endl;
  os << indent << "Last Weights: " << this->Weights << endl;

  os << indent << "FindCell Strategy: " << this->FindCellStrategy << endl;
}

//----------------------------------------------------------------------------
void svtkAbstractInterpolatedVelocityField::SelectVectors(int associationType, const char* fieldName)
{
  this->VectorsType = associationType;
  this->SetVectorsSelection(fieldName);
}