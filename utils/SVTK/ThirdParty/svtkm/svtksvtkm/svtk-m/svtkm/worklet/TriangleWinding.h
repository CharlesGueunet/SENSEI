//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2019 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//  Copyright 2019 UT-Battelle, LLC.
//  Copyright 2019 Los Alamos National Security.
//
//  Under the terms of Contract DE-NA0003525 with NTESS,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================

#ifndef svtkm_m_worklet_TriangleWinding_h
#define svtkm_m_worklet_TriangleWinding_h

#include <svtkm/cont/Algorithm.h>
#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/ArrayHandleCast.h>
#include <svtkm/cont/ArrayHandleConstant.h>
#include <svtkm/cont/ArrayHandleCounting.h>
#include <svtkm/cont/ArrayHandleGroupVec.h>
#include <svtkm/cont/ArrayHandleGroupVecVariable.h>
#include <svtkm/cont/ArrayHandleView.h>
#include <svtkm/cont/ArrayRangeCompute.h>
#include <svtkm/cont/CellSetExplicit.h>
#include <svtkm/cont/CellSetSingleType.h>
#include <svtkm/cont/DynamicCellSet.h>
#include <svtkm/cont/Invoker.h>

#include <svtkm/worklet/DispatcherMapField.h>
#include <svtkm/worklet/MaskIndices.h>
#include <svtkm/worklet/WorkletMapField.h>

#include <svtkm/Types.h>
#include <svtkm/VectorAnalysis.h>

namespace svtkm
{
namespace worklet
{

/**
 * This worklet ensures that triangle windings are consistent with provided
 * cell normals. The triangles are wound CCW around the cell normals, and
 * all other cells are ignored.
 *
 * The input cellset must be unstructured.
 */
class TriangleWinding
{
public:
  // Used by Explicit and SingleType specializations
  struct WorkletWindToCellNormals : public WorkletMapField
  {
    using ControlSignature = void(FieldIn cellNormals, FieldInOut cellPoints, WholeArrayIn coords);
    using ExecutionSignature = void(_1 cellNormal, _2 cellPoints, _3 coords);

    template <typename NormalCompType, typename CellPointsType, typename CoordsPortal>
    SVTKM_EXEC void operator()(const svtkm::Vec<NormalCompType, 3>& cellNormal,
                              CellPointsType& cellPoints,
                              const CoordsPortal& coords) const
    {
      // We only care about triangles:
      if (cellPoints.GetNumberOfComponents() != 3)
      {
        return;
      }

      using NormalType = svtkm::Vec<NormalCompType, 3>;

      const NormalType p0 = coords.Get(cellPoints[0]);
      const NormalType p1 = coords.Get(cellPoints[1]);
      const NormalType p2 = coords.Get(cellPoints[2]);
      const NormalType v01 = p1 - p0;
      const NormalType v02 = p2 - p0;
      const NormalType triangleNormal = svtkm::Cross(v01, v02);
      if (svtkm::Dot(cellNormal, triangleNormal) < 0)
      {
        // Can't just use std::swap from exec function:
        const svtkm::Id tmp = cellPoints[1];
        cellPoints[1] = cellPoints[2];
        cellPoints[2] = tmp;
      }
    }
  };

  // Used by generic implementations:
  struct WorkletGetCellShapesAndSizes : public WorkletVisitCellsWithPoints
  {
    using ControlSignature = void(CellSetIn cells, FieldOutCell shapes, FieldOutCell sizes);
    using ExecutionSignature = void(CellShape, PointCount, _2, _3);

    template <typename CellShapeTag>
    SVTKM_EXEC void operator()(const CellShapeTag cellShapeIn,
                              const svtkm::IdComponent cellSizeIn,
                              svtkm::UInt8& cellShapeOut,
                              svtkm::IdComponent& cellSizeOut) const
    {
      cellSizeOut = cellSizeIn;
      cellShapeOut = cellShapeIn.Id;
    }
  };

  struct WorkletWindToCellNormalsGeneric : public WorkletVisitCellsWithPoints
  {
    using ControlSignature = void(CellSetIn cellsIn,
                                  WholeArrayIn coords,
                                  FieldInCell cellNormals,
                                  FieldOutCell cellsOut);
    using ExecutionSignature = void(PointIndices, _2, _3, _4);

    template <typename InputIds, typename Coords, typename Normal, typename OutputIds>
    SVTKM_EXEC void operator()(const InputIds& inputIds,
                              const Coords& coords,
                              const Normal& normal,
                              OutputIds& outputIds) const
    {
      SVTKM_ASSERT(inputIds.GetNumberOfComponents() == outputIds.GetNumberOfComponents());

      // We only care about triangles:
      if (inputIds.GetNumberOfComponents() != 3)
      {
        // Just passthrough non-triangles
        // Cannot just assign here, must do a manual component-wise copy to
        // support VecFromPortal:
        for (svtkm::IdComponent i = 0; i < inputIds.GetNumberOfComponents(); ++i)
        {
          outputIds[i] = inputIds[i];
        }
        return;
      }

      const Normal p0 = coords.Get(inputIds[0]);
      const Normal p1 = coords.Get(inputIds[1]);
      const Normal p2 = coords.Get(inputIds[2]);
      const Normal v01 = p1 - p0;
      const Normal v02 = p2 - p0;
      const Normal triangleNormal = svtkm::Cross(v01, v02);
      if (svtkm::Dot(normal, triangleNormal) < 0)
      { // Reorder triangle:
        outputIds[0] = inputIds[0];
        outputIds[1] = inputIds[2];
        outputIds[2] = inputIds[1];
      }
      else
      { // passthrough:
        outputIds[0] = inputIds[0];
        outputIds[1] = inputIds[1];
        outputIds[2] = inputIds[2];
      }
    }
  };

  struct Launcher
  {
    svtkm::cont::DynamicCellSet Result;

    // Generic handler:
    template <typename CellSetType,
              typename PointComponentType,
              typename PointStorageType,
              typename CellNormalComponentType,
              typename CellNormalStorageType>
    SVTKM_CONT void operator()(
      const CellSetType& cellSet,
      const svtkm::cont::ArrayHandle<svtkm::Vec<PointComponentType, 3>, PointStorageType>& coords,
      const svtkm::cont::ArrayHandle<svtkm::Vec<CellNormalComponentType, 3>, CellNormalStorageType>&
        cellNormals,
      ...)
    {
      const auto numCells = cellSet.GetNumberOfCells();
      if (numCells == 0)
      {
        this->Result = cellSet;
        return;
      }

      svtkm::cont::Invoker invoker;

      // Get each cell's size:
      svtkm::cont::ArrayHandle<svtkm::IdComponent> numIndices;
      svtkm::cont::ArrayHandle<svtkm::UInt8> cellShapes;
      {
        WorkletGetCellShapesAndSizes worklet;
        invoker(worklet, cellSet, cellShapes, numIndices);
      }

      // Check to see if we can use CellSetSingleType:
      svtkm::IdComponent cellSize = 0; // 0 if heterogeneous, >0 if homogeneous
      svtkm::UInt8 cellShape = 0;      // only valid if homogeneous
      {
        auto rangeHandleSizes = svtkm::cont::ArrayRangeCompute(numIndices);
        auto rangeHandleShapes = svtkm::cont::ArrayRangeCompute(cellShapes);

        cellShapes.ReleaseResourcesExecution();

        auto rangeSizes = rangeHandleSizes.GetPortalConstControl().Get(0);
        auto rangeShapes = rangeHandleShapes.GetPortalConstControl().Get(0);

        const bool sameSize = svtkm::Abs(rangeSizes.Max - rangeSizes.Min) < 0.5;
        const bool sameShape = svtkm::Abs(rangeShapes.Max - rangeShapes.Min) < 0.5;

        if (sameSize && sameShape)
        {
          cellSize = static_cast<svtkm::IdComponent>(rangeSizes.Min + 0.5);
          cellShape = static_cast<svtkm::UInt8>(rangeShapes.Min + 0.5);
        }
      }

      if (cellSize > 0)
      { // Single cell type:
        // don't need these anymore:
        numIndices.ReleaseResources();
        cellShapes.ReleaseResources();

        svtkm::cont::ArrayHandle<svtkm::Id> conn;
        conn.Allocate(cellSize * numCells);

        auto offsets = svtkm::cont::make_ArrayHandleCounting<svtkm::Id>(0, cellSize, numCells);
        auto connGroupVec = svtkm::cont::make_ArrayHandleGroupVecVariable(conn, offsets);

        WorkletWindToCellNormalsGeneric worklet;
        invoker(worklet, cellSet, coords, cellNormals, connGroupVec);

        svtkm::cont::CellSetSingleType<> outCells;
        outCells.Fill(cellSet.GetNumberOfPoints(), cellShape, cellSize, conn);
        this->Result = outCells;
      }
      else
      { // Multiple cell types:
        svtkm::cont::ArrayHandle<svtkm::Id> offsets;
        svtkm::Id connSize;
        svtkm::cont::ConvertNumIndicesToOffsets(numIndices, offsets, connSize);
        numIndices.ReleaseResourcesExecution();

        svtkm::cont::ArrayHandle<svtkm::Id> conn;
        conn.Allocate(connSize);

        // Trim the last value off for the group vec array:
        auto offsetsTrim =
          svtkm::cont::make_ArrayHandleView(offsets, 0, offsets.GetNumberOfValues() - 1);
        auto connGroupVec = svtkm::cont::make_ArrayHandleGroupVecVariable(conn, offsetsTrim);

        WorkletWindToCellNormalsGeneric worklet;
        invoker(worklet, cellSet, coords, cellNormals, connGroupVec);

        svtkm::cont::CellSetExplicit<> outCells;
        outCells.Fill(cellSet.GetNumberOfPoints(), cellShapes, conn, offsets);
        this->Result = outCells;
      }
    }

    // Specialization for CellSetExplicit
    template <typename S,
              typename C,
              typename O,
              typename PointComponentType,
              typename PointStorageType,
              typename CellNormalComponentType,
              typename CellNormalStorageType>
    SVTKM_CONT void operator()(
      const svtkm::cont::CellSetExplicit<S, C, O>& cellSet,
      const svtkm::cont::ArrayHandle<svtkm::Vec<PointComponentType, 3>, PointStorageType>& coords,
      const svtkm::cont::ArrayHandle<svtkm::Vec<CellNormalComponentType, 3>, CellNormalStorageType>&
        cellNormals,
      int)
    {
      using WindToCellNormals = svtkm::worklet::DispatcherMapField<WorkletWindToCellNormals>;

      const auto numCells = cellSet.GetNumberOfCells();
      if (numCells == 0)
      {
        this->Result = cellSet;
        return;
      }

      svtkm::cont::ArrayHandle<svtkm::Id> conn;
      {
        const auto& connIn = cellSet.GetConnectivityArray(svtkm::TopologyElementTagCell{},
                                                          svtkm::TopologyElementTagPoint{});
        svtkm::cont::Algorithm::Copy(connIn, conn);
      }

      const auto& offsets =
        cellSet.GetOffsetsArray(svtkm::TopologyElementTagCell{}, svtkm::TopologyElementTagPoint{});
      auto offsetsTrim =
        svtkm::cont::make_ArrayHandleView(offsets, 0, offsets.GetNumberOfValues() - 1);
      auto cells = svtkm::cont::make_ArrayHandleGroupVecVariable(conn, offsetsTrim);

      WindToCellNormals dispatcher;
      dispatcher.Invoke(cellNormals, cells, coords);

      const auto& shapes =
        cellSet.GetShapesArray(svtkm::TopologyElementTagCell{}, svtkm::TopologyElementTagPoint{});
      svtkm::cont::CellSetExplicit<S, svtkm::cont::StorageTagBasic, O> newCells;
      newCells.Fill(cellSet.GetNumberOfPoints(), shapes, conn, offsets);

      this->Result = newCells;
    }

    // Specialization for CellSetSingleType
    template <typename C,
              typename PointComponentType,
              typename PointStorageType,
              typename CellNormalComponentType,
              typename CellNormalStorageType>
    SVTKM_CONT void operator()(
      const svtkm::cont::CellSetSingleType<C>& cellSet,
      const svtkm::cont::ArrayHandle<svtkm::Vec<PointComponentType, 3>, PointStorageType>& coords,
      const svtkm::cont::ArrayHandle<svtkm::Vec<CellNormalComponentType, 3>, CellNormalStorageType>&
        cellNormals,
      int)
    {
      using WindToCellNormals = svtkm::worklet::DispatcherMapField<WorkletWindToCellNormals>;

      const auto numCells = cellSet.GetNumberOfCells();
      if (numCells == 0)
      {
        this->Result = cellSet;
        return;
      }

      svtkm::cont::ArrayHandle<svtkm::Id> conn;
      {
        const auto& connIn = cellSet.GetConnectivityArray(svtkm::TopologyElementTagCell{},
                                                          svtkm::TopologyElementTagPoint{});
        svtkm::cont::Algorithm::Copy(connIn, conn);
      }

      const auto& offsets =
        cellSet.GetOffsetsArray(svtkm::TopologyElementTagCell{}, svtkm::TopologyElementTagPoint{});
      auto offsetsTrim =
        svtkm::cont::make_ArrayHandleView(offsets, 0, offsets.GetNumberOfValues() - 1);
      auto cells = svtkm::cont::make_ArrayHandleGroupVecVariable(conn, offsetsTrim);

      WindToCellNormals dispatcher;
      dispatcher.Invoke(cellNormals, cells, coords);

      svtkm::cont::CellSetSingleType<svtkm::cont::StorageTagBasic> newCells;
      newCells.Fill(cellSet.GetNumberOfPoints(),
                    cellSet.GetCellShape(0),
                    cellSet.GetNumberOfPointsInCell(0),
                    conn);

      this->Result = newCells;
    }
  };

  template <typename CellSetType,
            typename PointComponentType,
            typename PointStorageType,
            typename CellNormalComponentType,
            typename CellNormalStorageType>
  SVTKM_CONT static svtkm::cont::DynamicCellSet Run(
    const CellSetType& cellSet,
    const svtkm::cont::ArrayHandle<svtkm::Vec<PointComponentType, 3>, PointStorageType>& coords,
    const svtkm::cont::ArrayHandle<svtkm::Vec<CellNormalComponentType, 3>, CellNormalStorageType>&
      cellNormals)
  {
    Launcher launcher;
    // The last arg is just to help with overload resolution on the templated
    // Launcher::operator() method, so that the more specialized impls are
    // preferred over the generic one.
    svtkm::cont::CastAndCall(cellSet, launcher, coords, cellNormals, 0);
    return launcher.Result;
  }
};
}
} // end namespace svtkm::worklet

#endif // svtkm_m_worklet_TriangleWinding_h