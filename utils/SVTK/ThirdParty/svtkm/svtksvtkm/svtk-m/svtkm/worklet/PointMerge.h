//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_worklet_PointMerge_h
#define svtk_m_worklet_PointMerge_h

#include <svtkm/worklet/AverageByKey.h>
#include <svtkm/worklet/DispatcherMapField.h>
#include <svtkm/worklet/DispatcherReduceByKey.h>
#include <svtkm/worklet/Keys.h>
#include <svtkm/worklet/RemoveUnusedPoints.h>
#include <svtkm/worklet/WorkletMapField.h>
#include <svtkm/worklet/WorkletReduceByKey.h>

#include <svtkm/cont/ArrayCopy.h>
#include <svtkm/cont/ArrayHandleIndex.h>
#include <svtkm/cont/ArrayHandlePermutation.h>
#include <svtkm/cont/ArrayHandleVirtual.h>
#include <svtkm/cont/CellSetExplicit.h>
#include <svtkm/cont/ExecutionAndControlObjectBase.h>
#include <svtkm/cont/Invoker.h>

#include <svtkm/Bounds.h>
#include <svtkm/Hash.h>
#include <svtkm/Math.h>
#include <svtkm/VectorAnalysis.h>

namespace svtkm
{
namespace worklet
{

class PointMerge
{
public:
  // This class can take point worldCoords as inputs and return the bin index of the enclosing bin.
  class BinLocator : public svtkm::cont::ExecutionAndControlObjectBase
  {
    svtkm::Vec3f_64 Offset;
    svtkm::Vec3f_64 Scale;

#ifdef SVTKM_USE_64BIT_IDS
    // IEEE double precision floating point as 53 bits for the significand, so it would not be
    // possible to represent a number with more precision than that. We also back off a few bits to
    // avoid potential issues with numerical imprecision in the scaling.
    static constexpr svtkm::IdComponent BitsPerDimension = 50;
#else
    static constexpr svtkm::IdComponent BitsPerDimension = 31;
#endif
    static constexpr svtkm::Id MaxBinsPerDimension =
      static_cast<svtkm::Id>((1LL << BitsPerDimension) - 1);

  public:
    SVTKM_CONT BinLocator()
      : Offset(0.0)
      , Scale(0.0)
    {
    }

    SVTKM_CONT
    static svtkm::Vec3f_64 ComputeBinWidths(const svtkm::Bounds& bounds, svtkm::Float64 delta)
    {
      const svtkm::Vec3f_64 boundLengths(
        bounds.X.Length() + delta, bounds.Y.Length() + delta, bounds.Z.Length() + delta);
      svtkm::Vec3f_64 binWidths;
      for (svtkm::IdComponent dimIndex = 0; dimIndex < 3; ++dimIndex)
      {
        if (boundLengths[dimIndex] > svtkm::Epsilon64())
        {
          svtkm::Float64 minBinWidth = boundLengths[dimIndex] / (MaxBinsPerDimension - 1);
          if (minBinWidth < (2 * delta))
          {
            // We can accurately represent delta with the precision of the bin indices. The bin
            // size is 2*delta, which means we scale the (offset) point coordinates by 1/delta to
            // get the bin index.
            binWidths[dimIndex] = 2.0 * delta;
          }
          else
          {
            // Scale the (offset) point coordinates by 1/minBinWidth, which will give us bin
            // indices between 0 and MaxBinsPerDimension - 1.
            binWidths[dimIndex] = minBinWidth;
          }
        }
        else
        {
          // Bounds are essentially 0 in this dimension. The scale does not matter so much.
          binWidths[dimIndex] = 1.0;
        }
      }
      return binWidths;
    }

    // Constructs a BinLocator such that all bins are at least 2*delta large. The bins might be
    // made larger than that if there would be too many bins for the precision of svtkm::Id.
    SVTKM_CONT
    BinLocator(const svtkm::Bounds& bounds, svtkm::Float64 delta = 0.0)
      : Offset(bounds.X.Min, bounds.Y.Min, bounds.Z.Min)
    {
      const svtkm::Vec3f_64 binWidths = ComputeBinWidths(bounds, delta);
      this->Scale = svtkm::Vec3f_64(1.0) / binWidths;
    }

    // Shifts the grid by delta in the specified directions. This will allow the bins to cover
    // neighbors that straddled the boundaries of the original.
    SVTKM_CONT
    BinLocator ShiftBins(const svtkm::Bounds& bounds,
                         svtkm::Float64 delta,
                         const svtkm::Vec<bool, 3>& directions)
    {
      const svtkm::Vec3f_64 binWidths = ComputeBinWidths(bounds, delta);
      BinLocator shiftedLocator(*this);
      for (svtkm::IdComponent dimIndex = 0; dimIndex < 3; ++dimIndex)
      {
        if (directions[dimIndex])
        {
          shiftedLocator.Offset[dimIndex] -= (0.5 * binWidths[dimIndex]);
        }
      }
      return shiftedLocator;
    }

    template <typename T>
    SVTKM_EXEC_CONT svtkm::Id3 FindBin(const svtkm::Vec<T, 3>& worldCoords) const
    {
      svtkm::Vec3f_64 relativeCoords = (worldCoords - this->Offset) * this->Scale;

      return svtkm::Id3(svtkm::Floor(relativeCoords));
    }

    // Because this class is a POD, we can reuse it in both control and execution environments.

    template <typename Device>
    BinLocator PrepareForExecution(Device) const
    {
      return *this;
    }

    BinLocator PrepareForControl() const { return *this; }
  };

  // Converts point coordinates to a hash that represents the bin.
  struct CoordsToHash : public svtkm::worklet::WorkletMapField
  {
    using ControlSignature = void(FieldIn pointCoordinates,
                                  ExecObject binLocator,
                                  FieldOut hashesOut);
    using ExecutionSignature = void(_1, _2, _3);

    template <typename T>
    SVTKM_EXEC void operator()(const svtkm::Vec<T, 3>& coordiantes,
                              const BinLocator binLocator,
                              svtkm::HashType& hashOut) const
    {
      svtkm::Id3 binId = binLocator.FindBin(coordiantes);
      hashOut = svtkm::Hash(binId);
    }
  };

  class FindNeighbors : public svtkm::worklet::WorkletReduceByKey
  {
    svtkm::Float64 DeltaSquared;
    bool FastCheck;

  public:
    SVTKM_CONT
    FindNeighbors(bool fastCheck = true, svtkm::Float64 delta = svtkm::Epsilon64())
      : DeltaSquared(delta * delta)
      , FastCheck(fastCheck)
    {
    }

    using ControlSignature = void(KeysIn keys,
                                  ValuesInOut pointIndices,
                                  ValuesInOut pointCoordinates,
                                  ExecObject binLocator,
                                  ValuesOut neighborIndices);
    using ExecutionSignature = void(_2, _3, _4, _5);

    template <typename IndexVecInType, typename CoordinateVecInType, typename IndexVecOutType>
    SVTKM_EXEC void operator()(IndexVecInType& pointIndices,
                              CoordinateVecInType& pointCoordinates,
                              const BinLocator& binLocator,
                              IndexVecOutType& neighborIndices) const
    {
      // For each point we are going to find all points close enough to be considered neighbors. We
      // record the neighbors by filling in the same index into neighborIndices. That is, if two
      // items in neighborIndices have the same value, they should be considered neighbors.
      // Otherwise, they should not. We will use the "local" index, which refers to index in the
      // vec-like objects passed into this worklet. This allows us to quickly identify the local
      // point without sorting through the global indices.

      using CoordType = typename CoordinateVecInType::ComponentType;

      svtkm::IdComponent numPoints = pointIndices.GetNumberOfComponents();
      SVTKM_ASSERT(numPoints == pointCoordinates.GetNumberOfComponents());
      SVTKM_ASSERT(numPoints == neighborIndices.GetNumberOfComponents());

      // Initially, set every point to be its own neighbor.
      for (svtkm::IdComponent i = 0; i < numPoints; ++i)
      {
        neighborIndices[i] = i;
      }

      // Iterate over every point and look for neighbors. Only need to look to numPoints-1 since we
      // only need to check points after the current index (earlier points are already checked).
      for (svtkm::IdComponent i = 0; i < (numPoints - 1); ++i)
      {
        CoordType p0 = pointCoordinates[i];
        svtkm::Id3 bin0 = binLocator.FindBin(p0);

        // Check all points after this one. (All those before already checked themselves to this.)
        for (svtkm::IdComponent j = i + 1; j < numPoints; ++j)
        {
          if (neighborIndices[i] == neighborIndices[j])
          {
            // We have already identified these points as neighbors. Can skip the check.
            continue;
          }
          CoordType p1 = pointCoordinates[j];
          svtkm::Id3 bin1 = binLocator.FindBin(p1);

          // Check to see if these points should be considered neighbors. First, check to make sure
          // that they are in the same bin. If they are not, then they cannot be neighbors. Next,
          // check the FastCheck flag. If fast checking is on, then all points in the same bin are
          // considered neighbors. Otherwise, check that the distance is within the specified
          // delta. If so, mark them as neighbors.
          if ((bin0 == bin1) &&
              (this->FastCheck || (this->DeltaSquared >= svtkm::MagnitudeSquared(p0 - p1))))
          {
            // The two points should be merged. But we also might need to merge larger
            // neighborhoods.
            if (neighborIndices[j] == j)
            {
              // Second point not yet merged into another neighborhood. We can just take it.
              neighborIndices[j] = neighborIndices[i];
            }
            else
            {
              // The second point is already part of a neighborhood. Merge the neighborhood with
              // the largest index into the neighborhood with the smaller index.
              svtkm::IdComponent neighborhoodToGrow;
              svtkm::IdComponent neighborhoodToAbsorb;
              if (neighborIndices[i] < neighborIndices[j])
              {
                neighborhoodToGrow = neighborIndices[i];
                neighborhoodToAbsorb = neighborIndices[j];
              }
              else
              {
                neighborhoodToGrow = neighborIndices[j];
                neighborhoodToAbsorb = neighborIndices[i];
              }

              // Change all neighborhoodToAbsorb indices to neighborhoodToGrow.
              for (svtkm::IdComponent k = neighborhoodToAbsorb; k < numPoints; ++k)
              {
                if (neighborIndices[k] == neighborhoodToAbsorb)
                {
                  neighborIndices[k] = neighborhoodToGrow;
                }
              }
            }
          } // if merge points
        }   // for each p1
      }     // for each p0

      // We have finished grouping neighbors. neighborIndices contains a unique local index for
      // each neighbor group. Now find the average (centroid) point coordinates for each group and
      // write those coordinates back into the coordinates array. Also modify the point indices
      // so that all indices of a group are the same. (This forms a map from old point indices to
      // merged point indices.)
      for (svtkm::IdComponent i = 0; i < numPoints; ++i)
      {
        svtkm::IdComponent neighborhood = neighborIndices[i];
        if (i == neighborhood)
        {
          // Found a new group. Find the centroid.
          CoordType centroid = pointCoordinates[i];
          svtkm::IdComponent numInGroup = 1;
          for (svtkm::IdComponent j = i + 1; j < numPoints; ++j)
          {
            if (neighborhood == neighborIndices[j])
            {
              centroid = centroid + pointCoordinates[j];
              ++numInGroup;
            }
          }
          centroid = centroid / numInGroup;

          // Now that we have the centroid, write new point coordinates and index.
          svtkm::Id groupIndex = pointIndices[i];
          pointCoordinates[i] = centroid;
          for (svtkm::IdComponent j = i + 1; j < numPoints; ++j)
          {
            if (neighborhood == neighborIndices[j])
            {
              pointCoordinates[j] = centroid;
              pointIndices[j] = groupIndex;
            }
          }
        }
      }
    }
  };

  struct BuildPointInputToOutputMap : svtkm::worklet::WorkletReduceByKey
  {
    using ControlSignature = void(KeysIn, ValuesOut PointInputToOutputMap);
    using ExecutionSignature = void(InputIndex, _2);

    template <typename MapPortalType>
    SVTKM_EXEC void operator()(svtkm::Id newIndex, MapPortalType outputIndices) const
    {
      const svtkm::IdComponent numIndices = outputIndices.GetNumberOfComponents();
      for (svtkm::IdComponent i = 0; i < numIndices; ++i)
      {
        outputIndices[i] = newIndex;
      }
    }
  };

private:
  template <typename T>
  SVTKM_CONT static void RunOneIteration(
    svtkm::Float64 delta,                              // Distance to consider two points coincident
    bool fastCheck,                                   // If true, approximate distances are used
    const BinLocator& binLocator,                     // Used to find nearby points
    svtkm::cont::ArrayHandle<svtkm::Vec<T, 3>>& points, // coordinates, modified to merge close
    svtkm::cont::ArrayHandle<svtkm::Id> indexNeighborMap) // identifies each neighbor group, updated
  {
    svtkm::cont::Invoker invoker;

    svtkm::cont::ArrayHandle<svtkm::HashType> hashes;
    invoker(CoordsToHash(), points, binLocator, hashes);

    svtkm::worklet::Keys<HashType> keys(hashes);

    // Really just scratch space
    svtkm::cont::ArrayHandle<svtkm::IdComponent> neighborIndices;

    invoker(
      FindNeighbors(fastCheck, delta), keys, indexNeighborMap, points, binLocator, neighborIndices);
  }

public:
  template <typename T>
  SVTKM_CONT void Run(
    svtkm::Float64 delta,                              // Distance to consider two points coincident
    bool fastCheck,                                   // If true, approximate distances are used
    const svtkm::Bounds& bounds,                       // Bounds of points
    svtkm::cont::ArrayHandle<svtkm::Vec<T, 3>>& points) // coordinates, modified to merge close
  {
    svtkm::cont::Invoker invoker;

    BinLocator binLocator(bounds, delta);

    svtkm::cont::ArrayHandle<svtkm::Id> indexNeighborMap;
    svtkm::cont::ArrayCopy(svtkm::cont::ArrayHandleIndex(points.GetNumberOfValues()),
                          indexNeighborMap);

    this->RunOneIteration(delta, fastCheck, binLocator, points, indexNeighborMap);

    if (!fastCheck)
    {
      // Run the algorithm again after shifting the bins to capture nearby points that straddled
      // the previous bins.
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(true, false, false)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(false, true, false)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(false, false, true)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(true, true, false)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(true, false, true)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(false, true, true)),
                            points,
                            indexNeighborMap);
      this->RunOneIteration(delta,
                            fastCheck,
                            binLocator.ShiftBins(bounds, delta, svtkm::make_Vec(true, true, true)),
                            points,
                            indexNeighborMap);
    }

    this->MergeKeys = svtkm::worklet::Keys<svtkm::Id>(indexNeighborMap);

    invoker(BuildPointInputToOutputMap(), this->MergeKeys, this->PointInputToOutputMap);

    // Need to pull out the unique point coordiantes
    svtkm::cont::ArrayHandle<svtkm::Vec<T, 3>> uniquePointCoordinates;
    svtkm::cont::ArrayCopy(
      svtkm::cont::make_ArrayHandlePermutation(this->MergeKeys.GetUniqueKeys(), points),
      uniquePointCoordinates);
    points = uniquePointCoordinates;
  }

  SVTKM_CONT void Run(
    svtkm::Float64 delta,                               // Distance to consider two points coincident
    bool fastCheck,                                    // If true, approximate distances are used
    const svtkm::Bounds& bounds,                        // Bounds of points
    svtkm::cont::ArrayHandleVirtualCoordinates& points) // coordinates, modified to merge close
  {
    // Get a cast to a concrete set of point coordiantes so that it can be modified in place
    svtkm::cont::ArrayHandle<svtkm::Vec3f> concretePoints;
    if (points.IsType<decltype(concretePoints)>())
    {
      concretePoints = points.Cast<decltype(concretePoints)>();
    }
    else
    {
      svtkm::cont::ArrayCopy(points, concretePoints);
    }

    Run(delta, fastCheck, bounds, concretePoints);

    // Make sure that the modified points are reflected back in the virtual array.
    points = svtkm::cont::ArrayHandleVirtualCoordinates(concretePoints);
  }

  template <typename ShapeStorage, typename ConnectivityStorage, typename OffsetsStorage>
  SVTKM_CONT
    svtkm::cont::CellSetExplicit<ShapeStorage, SVTKM_DEFAULT_CONNECTIVITY_STORAGE_TAG, OffsetsStorage>
    MapCellSet(const svtkm::cont::CellSetExplicit<ShapeStorage, ConnectivityStorage, OffsetsStorage>&
                 inCellSet) const
  {
    return svtkm::worklet::RemoveUnusedPoints::MapCellSet(
      inCellSet, this->PointInputToOutputMap, this->MergeKeys.GetInputRange());
  }

  template <typename InArrayHandle, typename OutArrayHandle>
  SVTKM_CONT void MapPointField(const InArrayHandle& inArray, OutArrayHandle& outArray) const
  {
    SVTKM_IS_ARRAY_HANDLE(InArrayHandle);
    SVTKM_IS_ARRAY_HANDLE(OutArrayHandle);

    svtkm::worklet::AverageByKey::Run(this->MergeKeys, inArray, outArray);
  }

  template <typename InArrayHandle>
  SVTKM_CONT svtkm::cont::ArrayHandle<typename InArrayHandle::ValueType> MapPointField(
    const InArrayHandle& inArray) const
  {
    SVTKM_IS_ARRAY_HANDLE(InArrayHandle);

    svtkm::cont::ArrayHandle<typename InArrayHandle::ValueType> outArray;
    this->MapPointField(inArray, outArray);

    return outArray;
  }

private:
  svtkm::worklet::Keys<svtkm::Id> MergeKeys;
  svtkm::cont::ArrayHandle<svtkm::Id> PointInputToOutputMap;
};
}
} // namespace svtkm::worklet

#endif //svtk_m_worklet_PointMerge_h