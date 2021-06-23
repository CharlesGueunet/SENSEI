//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_worklet_SplitSharpEdges_h
#define svtk_m_worklet_SplitSharpEdges_h

#include <svtkm/worklet/CellDeepCopy.h>

#include <svtkm/cont/Algorithm.h>
#include <svtkm/cont/ArrayCopy.h>
#include <svtkm/cont/ArrayHandleCounting.h>
#include <svtkm/cont/ArrayHandlePermutation.h>
#include <svtkm/cont/Invoker.h>
#include <svtkm/exec/CellEdge.h>

#include <svtkm/Bitset.h>
#include <svtkm/CellTraits.h>
#include <svtkm/TypeTraits.h>
#include <svtkm/VectorAnalysis.h>

namespace svtkm
{
namespace worklet
{

namespace internal
{
// Given a cell and a point on the cell, find the two edges that are
// associated with this point in canonical index
template <typename PointFromCellSetType>
SVTKM_EXEC void FindRelatedEdges(const svtkm::Id& pointIndex,
                                const svtkm::Id& cellIndexG,
                                const PointFromCellSetType& pFromCellSet,
                                svtkm::Id2& edge0G,
                                svtkm::Id2& edge1G,
                                const svtkm::exec::FunctorBase& worklet)
{
  typename PointFromCellSetType::CellShapeTag cellShape = pFromCellSet.GetCellShape(cellIndexG);
  typename PointFromCellSetType::IndicesType cellConnections = pFromCellSet.GetIndices(cellIndexG);
  svtkm::IdComponent numPointsInCell = pFromCellSet.GetNumberOfIndices(cellIndexG);
  svtkm::IdComponent numEdges =
    svtkm::exec::CellEdgeNumberOfEdges(numPointsInCell, cellShape, worklet);
  svtkm::IdComponent edgeIndex = -1;
  // Find the two edges with the pointIndex
  while (true)
  {
    ++edgeIndex;
    if (edgeIndex >= numEdges)
    {
      worklet.RaiseError("Bad cell. Could not find two incident edges.");
      return;
    }
    svtkm::Id2 canonicalEdgeId(cellConnections[svtkm::exec::CellEdgeLocalIndex(
                                numPointsInCell, 0, edgeIndex, cellShape, worklet)],
                              cellConnections[svtkm::exec::CellEdgeLocalIndex(
                                numPointsInCell, 1, edgeIndex, cellShape, worklet)]);
    if (canonicalEdgeId[0] == pointIndex || canonicalEdgeId[1] == pointIndex)
    { // Assign value to edge0 first
      if ((edge0G[0] == -1) && (edge0G[1] == -1))
      {
        edge0G = canonicalEdgeId;
      }
      else
      {
        edge1G = canonicalEdgeId;
        break;
      }
    }
  }
}

// TODO: We should replace this expensive lookup with a WholeCellSetIn<Edge, Cell> map.
// Given an edge on a cell, it would find the neighboring
// cell of this edge in local index. If it's a non manifold edge, -1 would be returned.
template <typename PointFromCellSetType, typename IncidentCellVecType>
SVTKM_EXEC int FindNeighborCellInLocalIndex(const svtkm::Id2& eOI,
                                           const PointFromCellSetType& pFromCellSet,
                                           const IncidentCellVecType& incidentCells,
                                           const svtkm::Id currentCellLocalIndex,
                                           const svtkm::exec::FunctorBase& worklet)
{
  int neighboringCellIndex = -1;
  svtkm::IdComponent numberOfIncidentCells = incidentCells.GetNumberOfComponents();
  size_t neighboringCellsCount = 0;
  for (svtkm::IdComponent incidentCellIndex = 0; incidentCellIndex < numberOfIncidentCells;
       incidentCellIndex++)
  {
    if (currentCellLocalIndex == incidentCellIndex)
    {
      continue; // No need to check the current interested cell
    }
    svtkm::Id cellIndexG = incidentCells[incidentCellIndex]; // Global cell index
    typename PointFromCellSetType::CellShapeTag cellShape = pFromCellSet.GetCellShape(cellIndexG);
    typename PointFromCellSetType::IndicesType cellConnections =
      pFromCellSet.GetIndices(cellIndexG);
    svtkm::IdComponent numPointsInCell = pFromCellSet.GetNumberOfIndices(cellIndexG);
    svtkm::IdComponent numEdges =
      svtkm::exec::CellEdgeNumberOfEdges(numPointsInCell, cellShape, worklet);
    svtkm::IdComponent edgeIndex = -1;
    // Check if this cell has edge of interest
    while (true)
    {
      ++edgeIndex;
      if (edgeIndex >= numEdges)
      {
        break;
      }
      svtkm::Id2 canonicalEdgeId(cellConnections[svtkm::exec::CellEdgeLocalIndex(
                                  numPointsInCell, 0, edgeIndex, cellShape, worklet)],
                                cellConnections[svtkm::exec::CellEdgeLocalIndex(
                                  numPointsInCell, 1, edgeIndex, cellShape, worklet)]);
      if ((canonicalEdgeId[0] == eOI[0] && canonicalEdgeId[1] == eOI[1]) ||
          (canonicalEdgeId[0] == eOI[1] && canonicalEdgeId[1] == eOI[0]))
      {
        neighboringCellIndex = incidentCellIndex;
        neighboringCellsCount++;
        break;
      }
    }
  }
  return neighboringCellIndex;
}

// Generalized logic for finding what 'regions' own the connected cells.
template <typename IncidentCellVecType, typename PointFromCellSetType, typename FaceNormalVecType>
SVTKM_EXEC bool FindConnectedCellOwnerships(svtkm::FloatDefault cosFeatureAngle,
                                           const IncidentCellVecType& incidentCells,
                                           svtkm::Id pointIndex,
                                           const PointFromCellSetType& pFromCellSet,
                                           const FaceNormalVecType& faceNormals,
                                           svtkm::Id visitedCellsRegionIndex[64],
                                           svtkm::Id& regionIndex,
                                           const svtkm::exec::FunctorBase& worklet)
{
  const svtkm::IdComponent numberOfIncidentCells = incidentCells.GetNumberOfComponents();
  SVTKM_ASSERT(numberOfIncidentCells < 64);
  if (numberOfIncidentCells <= 1)
  {
    return false; // Not enough cells to compare
  }
  // Initialize a global cell mask to avoid confusion. globalCellIndex->status
  // 0 means not visited yet 1 means visited.
  svtkm::Bitset<svtkm::UInt64> visitedCells;
  // Reallocate memory for visitedCellsGroup if needed

  // Loop through each cell
  for (svtkm::IdComponent incidentCellIndex = 0; incidentCellIndex < numberOfIncidentCells;
       incidentCellIndex++)
  {
    svtkm::Id cellIndexG = incidentCells[incidentCellIndex]; // cell index in global order
    // If not visited
    if (!visitedCells.test(incidentCellIndex))
    {
      // Mark the cell and track the region
      visitedCells.set(incidentCellIndex);
      visitedCellsRegionIndex[incidentCellIndex] = regionIndex;

      // Find two edges containing the current point in canonial index
      svtkm::Id2 edge0G(-1, -1), edge1G(-1, -1);
      internal::FindRelatedEdges(pointIndex, cellIndexG, pFromCellSet, edge0G, edge1G, worklet);
      // Grow the area along each edge
      for (size_t i = 0; i < 2; i++)
      { // Reset these two values for each grow operation
        svtkm::Id2 currentEdgeG = i == 0 ? edge0G : edge1G;
        svtkm::IdComponent currentTestingCellIndex = incidentCellIndex;
        while (currentTestingCellIndex >= 0)
        {
          // Find the neighbor cell of the current cell edge in local index
          int neighboringCellIndexQuery = internal::FindNeighborCellInLocalIndex(
            currentEdgeG, pFromCellSet, incidentCells, currentTestingCellIndex, worklet);
          // The edge should be manifold and the neighboring cell should
          // have not been visited
          if (neighboringCellIndexQuery != -1 && !visitedCells.test(neighboringCellIndexQuery))
          {
            svtkm::IdComponent neighborCellIndex =
              static_cast<svtkm::IdComponent>(neighboringCellIndexQuery);
            // Try to grow the area if the feature angle between current neighbor
            auto thisNormal = faceNormals[currentTestingCellIndex];
            //neighborNormal
            auto neighborNormal = faceNormals[neighborCellIndex];
            // Try to grow the area
            if (svtkm::dot(thisNormal, neighborNormal) > cosFeatureAngle)
            { // No need to split.
              visitedCells.set(neighborCellIndex);

              // Mark the region visited
              visitedCellsRegionIndex[neighborCellIndex] = regionIndex;

              // Move to examine next cell
              currentTestingCellIndex = neighborCellIndex;
              svtkm::Id2 neighborCellEdge0G(-1, -1), neighborCellEdge1G(-1, -1);
              internal::FindRelatedEdges(pointIndex,
                                         incidentCells[currentTestingCellIndex],
                                         pFromCellSet,
                                         neighborCellEdge0G,
                                         neighborCellEdge1G,
                                         worklet);
              // Update currentEdgeG
              if ((currentEdgeG == neighborCellEdge0G) ||
                  currentEdgeG == svtkm::Id2(neighborCellEdge0G[1], neighborCellEdge0G[0]))
              {
                currentEdgeG = neighborCellEdge1G;
              }
              else
              {
                currentEdgeG = neighborCellEdge0G;
              }
            }
            else
            {
              currentTestingCellIndex = -1;
            }
          }
          else
          {
            currentTestingCellIndex =
              -1; // Either seperated by previous visit, boundary or non-manifold
          }
          // cells is smaller than the thresold and the nighboring cell has not been visited
        }
      }
      regionIndex++;
    }
  }
  return true;
}

} // internal namespace

// Split sharp manifold edges where the feature angle between the
// adjacent surfaces are larger than the threshold value
class SplitSharpEdges
{
public:
  // This worklet would calculate the needed space for splitting sharp edges.
  // For each point, it would have two values as numberOfNewPoint(how many
  // times this point needs to be duplicated) and numberOfCellsNeedsUpdate
  // (how many neighboring cells need to update connectivity).
  // For example, Given a unit cube and feature angle
  // as 89 degree, each point would be duplicated twice and there are two cells
  // need connectivity update. There is no guarantee on which cell would get which
  // new point.
  class ClassifyPoint : public svtkm::worklet::WorkletVisitPointsWithCells
  {
  public:
    ClassifyPoint(svtkm::FloatDefault cosfeatureAngle)
      : CosFeatureAngle(cosfeatureAngle)
    {
    }
    using ControlSignature = void(CellSetIn intputCells,
                                  WholeCellSetIn<Cell, Point>, // Query points from cell
                                  FieldInCell faceNormals,
                                  FieldOutPoint newPointNum,
                                  FieldOutPoint cellNum);
    using ExecutionSignature = void(CellIndices incidentCells,
                                    InputIndex pointIndex,
                                    _2 pFromCellSet,
                                    _3 faceNormals,
                                    _4 newPointNum,
                                    _5 cellNum);
    using InputDomain = _1;

    template <typename IncidentCellVecType,
              typename PointFromCellSetType,
              typename FaceNormalVecType>
    SVTKM_EXEC void operator()(const IncidentCellVecType& incidentCells,
                              svtkm::Id pointIndex,
                              const PointFromCellSetType& pFromCellSet,
                              const FaceNormalVecType& faceNormals,
                              svtkm::Id& newPointNum,
                              svtkm::Id& cellNum) const
    {
      svtkm::Id regionIndex = 0;
      svtkm::Id visitedCellsRegionIndex[64] = { 0 };
      const bool foundConnections = internal::FindConnectedCellOwnerships(this->CosFeatureAngle,
                                                                          incidentCells,
                                                                          pointIndex,
                                                                          pFromCellSet,
                                                                          faceNormals,
                                                                          visitedCellsRegionIndex,
                                                                          regionIndex,
                                                                          *this);
      if (!foundConnections)
      {
        newPointNum = 0;
        cellNum = 0;
      }
      else
      {
        // For each new region you need a new point
        svtkm::Id numberOfCellsNeedUpdate = 0;
        const svtkm::IdComponent size = incidentCells.GetNumberOfComponents();
        for (svtkm::IdComponent i = 0; i < size; i++)
        {
          if (visitedCellsRegionIndex[i] > 0)
          {
            numberOfCellsNeedUpdate++;
          }
        }
        newPointNum = regionIndex - 1;
        cellNum = numberOfCellsNeedUpdate;
      }
    }

  private:
    svtkm::FloatDefault CosFeatureAngle; // Cos value of the feature angle
  };

  // This worklet split the sharp edges and populate the
  // cellTopologyUpdateTuples as (cellGlobalId, oldPointId, newPointId).
  class SplitSharpEdge : public svtkm::worklet::WorkletVisitPointsWithCells
  {
  public:
    SplitSharpEdge(svtkm::FloatDefault cosfeatureAngle, svtkm::Id numberOfOldPoints)
      : CosFeatureAngle(cosfeatureAngle)
      , NumberOfOldPoints(numberOfOldPoints)
    {
    }
    using ControlSignature = void(CellSetIn intputCells,
                                  WholeCellSetIn<Cell, Point>, // Query points from cell
                                  FieldInCell faceNormals,
                                  FieldInPoint newPointStartingIndex,
                                  FieldInPoint pointCellsStartingIndex,
                                  WholeArrayOut cellTopologyUpdateTuples);
    using ExecutionSignature = void(CellIndices incidentCells,
                                    InputIndex pointIndex,
                                    _2 pFromCellSet,
                                    _3 faceNormals,
                                    _4 newPointStartingIndex,
                                    _5 pointCellsStartingIndex,
                                    _6 cellTopologyUpdateTuples);
    using InputDomain = _1;

    template <typename IncidentCellVecType,
              typename PointFromCellSetType,
              typename FaceNormalVecType,
              typename CellTopologyUpdateTuples>
    SVTKM_EXEC void operator()(const IncidentCellVecType& incidentCells,
                              svtkm::Id pointIndex,
                              const PointFromCellSetType& pFromCellSet,
                              const FaceNormalVecType& faceNormals,
                              const svtkm::Id& newPointStartingIndex,
                              const svtkm::Id& pointCellsStartingIndex,
                              CellTopologyUpdateTuples& cellTopologyUpdateTuples) const
    {
      svtkm::Id regionIndex = 0;
      svtkm::Id visitedCellsRegionIndex[64] = { 0 };
      const bool foundConnections = internal::FindConnectedCellOwnerships(this->CosFeatureAngle,
                                                                          incidentCells,
                                                                          pointIndex,
                                                                          pFromCellSet,
                                                                          faceNormals,
                                                                          visitedCellsRegionIndex,
                                                                          regionIndex,
                                                                          *this);
      if (foundConnections)
      {
        // For each new region you need a new point
        // Initialize the offset in the global cellTopologyUpdateTuples;
        svtkm::Id cellTopologyUpdateTuplesIndex = pointCellsStartingIndex;
        const svtkm::IdComponent size = incidentCells.GetNumberOfComponents();
        for (svtkm::Id i = 0; i < size; i++)
        {
          if (visitedCellsRegionIndex[i])
          { // New region generated. Need to update the topology
            svtkm::Id replacementPointId =
              NumberOfOldPoints + newPointStartingIndex + visitedCellsRegionIndex[i] - 1;
            svtkm::Id globalCellId = incidentCells[static_cast<svtkm::IdComponent>(i)];
            // (cellGlobalIndex, oldPointId, replacementPointId)
            svtkm::Id3 tuple = svtkm::make_Vec(globalCellId, pointIndex, replacementPointId);
            cellTopologyUpdateTuples.Set(cellTopologyUpdateTuplesIndex, tuple);
            cellTopologyUpdateTuplesIndex++;
          }
        }
      }
    }

  private:
    svtkm::FloatDefault CosFeatureAngle; // Cos value of the feature angle
    svtkm::Id NumberOfOldPoints;
  };

  template <typename CellSetType,
            typename FaceNormalsType,
            typename CoordsComType,
            typename CoordsInStorageType,
            typename CoordsOutStorageType,
            typename NewCellSetType>
  void Run(
    const CellSetType& oldCellset,
    const svtkm::FloatDefault featureAngle,
    const FaceNormalsType& faceNormals,
    const svtkm::cont::ArrayHandle<svtkm::Vec<CoordsComType, 3>, CoordsInStorageType>& oldCoords,
    svtkm::cont::ArrayHandle<svtkm::Vec<CoordsComType, 3>, CoordsOutStorageType>& newCoords,
    NewCellSetType& newCellset)
  {
    svtkm::cont::Invoker invoke;

    const svtkm::FloatDefault featureAngleR =
      featureAngle / static_cast<svtkm::FloatDefault>(180.0) * svtkm::Pi<svtkm::FloatDefault>();

    //Launch the first kernel that computes which points need to be split
    svtkm::cont::ArrayHandle<svtkm::Id> newPointNums, cellNeedUpdateNums;
    ClassifyPoint classifyPoint(svtkm::Cos(featureAngleR));
    invoke(classifyPoint, oldCellset, oldCellset, faceNormals, newPointNums, cellNeedUpdateNums);
    SVTKM_ASSERT(newPointNums.GetNumberOfValues() == oldCoords.GetNumberOfValues());

    //Compute relevant information from cellNeedUpdateNums so we can release
    //that memory asap
    svtkm::cont::ArrayHandle<svtkm::Id> pointCellsStartingIndexs;
    svtkm::cont::Algorithm::ScanExclusive(cellNeedUpdateNums, pointCellsStartingIndexs);

    const svtkm::Id cellsNeedUpdateNum =
      svtkm::cont::Algorithm::Reduce(cellNeedUpdateNums, svtkm::Id(0));
    cellNeedUpdateNums.ReleaseResources();


    //Compute the mapping of new points to old points. This is required for
    //processing additional point fields
    const svtkm::Id totalNewPointsNum = svtkm::cont::Algorithm::Reduce(newPointNums, svtkm::Id(0));
    this->NewPointsIdArray.Allocate(oldCoords.GetNumberOfValues() + totalNewPointsNum);
    svtkm::cont::Algorithm::CopySubRange(
      svtkm::cont::make_ArrayHandleCounting(svtkm::Id(0), svtkm::Id(1), oldCoords.GetNumberOfValues()),
      0,
      oldCoords.GetNumberOfValues(),
      this->NewPointsIdArray,
      0);
    auto newPointsIdArrayPortal = this->NewPointsIdArray.GetPortalControl();

    // Fill the new point coordinate system with all the existing values
    newCoords.Allocate(oldCoords.GetNumberOfValues() + totalNewPointsNum);
    svtkm::cont::Algorithm::CopySubRange(oldCoords, 0, oldCoords.GetNumberOfValues(), newCoords);

    if (totalNewPointsNum > 0)
    { //only if we have new points do we need add any of the new
      //coordinate locations
      svtkm::Id newCoordsIndex = oldCoords.GetNumberOfValues();
      auto oldCoordsPortal = oldCoords.GetPortalConstControl();
      auto newCoordsPortal = newCoords.GetPortalControl();
      auto newPointNumsPortal = newPointNums.GetPortalControl();
      for (svtkm::Id i = 0; i < oldCoords.GetNumberOfValues(); i++)
      { // Find out for each new point, how many times it should be added
        for (svtkm::Id j = 0; j < newPointNumsPortal.Get(i); j++)
        {
          newPointsIdArrayPortal.Set(newCoordsIndex, i);
          newCoordsPortal.Set(newCoordsIndex++, oldCoordsPortal.Get(i));
        }
      }
    }

    // Allocate the size for the updateCellTopologyArray
    svtkm::cont::ArrayHandle<svtkm::Id3> cellTopologyUpdateTuples;
    cellTopologyUpdateTuples.Allocate(cellsNeedUpdateNum);

    svtkm::cont::ArrayHandle<svtkm::Id> newpointStartingIndexs;
    svtkm::cont::Algorithm::ScanExclusive(newPointNums, newpointStartingIndexs);
    newPointNums.ReleaseResources();


    SplitSharpEdge splitSharpEdge(svtkm::Cos(featureAngleR), oldCoords.GetNumberOfValues());
    invoke(splitSharpEdge,
           oldCellset,
           oldCellset,
           faceNormals,
           newpointStartingIndexs,
           pointCellsStartingIndexs,
           cellTopologyUpdateTuples);
    auto ctutPortal = cellTopologyUpdateTuples.GetPortalConstControl();
    svtkm::cont::printSummary_ArrayHandle(cellTopologyUpdateTuples, std::cout);


    // Create the new cellset
    CellDeepCopy::Run(oldCellset, newCellset);
    // FIXME: Since the non const get array function is not in CellSetExplict.h,
    // here I just get a non-const copy of the array handle.
    auto connectivityArrayHandle = newCellset.GetConnectivityArray(svtkm::TopologyElementTagCell(),
                                                                   svtkm::TopologyElementTagPoint());
    auto connectivityArrayHandleP = connectivityArrayHandle.GetPortalControl();
    auto offsetArrayHandle =
      newCellset.GetOffsetsArray(svtkm::TopologyElementTagCell(), svtkm::TopologyElementTagPoint());
    auto offsetArrayHandleP = offsetArrayHandle.GetPortalControl();
    for (svtkm::Id i = 0; i < cellTopologyUpdateTuples.GetNumberOfValues(); i++)
    {
      svtkm::Id cellId(ctutPortal.Get(i)[0]), oldPointId(ctutPortal.Get(i)[1]),
        newPointId(ctutPortal.Get(i)[2]);
      svtkm::Id bound = (cellId + 1 == offsetArrayHandle.GetNumberOfValues())
        ? connectivityArrayHandle.GetNumberOfValues()
        : offsetArrayHandleP.Get(cellId + 1);
      svtkm::Id k = 0;
      for (svtkm::Id j = offsetArrayHandleP.Get(cellId); j < bound; j++, k++)
      {
        if (connectivityArrayHandleP.Get(j) == oldPointId)
        {
          connectivityArrayHandleP.Set(j, newPointId);
        }
      }
    }
  }

  template <typename ValueType, typename StorageTag>
  svtkm::cont::ArrayHandle<ValueType> ProcessPointField(
    const svtkm::cont::ArrayHandle<ValueType, StorageTag> in) const
  {
    // Use a temporary permutation array to simplify the mapping:
    auto tmp = svtkm::cont::make_ArrayHandlePermutation(this->NewPointsIdArray, in);

    // Copy into an array with default storage:
    svtkm::cont::ArrayHandle<ValueType> result;
    svtkm::cont::ArrayCopy(tmp, result);

    return result;
  }

private:
  svtkm::cont::ArrayHandle<svtkm::Id> NewPointsIdArray;
};
}
} // svtkm::worklet

#endif // svtk_m_worklet_SplitSharpEdges_h