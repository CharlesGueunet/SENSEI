//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#include "Benchmarker.h"

#include <svtkm/Math.h>
#include <svtkm/VectorAnalysis.h>

#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/CellSetStructured.h>
#include <svtkm/cont/Invoker.h>
#include <svtkm/cont/Timer.h>

#include <svtkm/worklet/WorkletMapField.h>
#include <svtkm/worklet/WorkletMapTopology.h>

#include <svtkm/cont/testing/Testing.h>

#include <cctype>
#include <random>
#include <string>

namespace
{

#define CUBE_SIZE 256

using ValueTypes = svtkm::List<svtkm::UInt32, svtkm::Int32, svtkm::Int64, svtkm::Float32, svtkm::Float64>;

using ValueVariantHandle = svtkm::cont::VariantArrayHandleBase<ValueTypes>;

// Hold configuration state (e.g. active device)
svtkm::cont::InitializeResult Config;

class AveragePointToCell : public svtkm::worklet::WorkletVisitCellsWithPoints
{
public:
  using ControlSignature = void(FieldInPoint inPoints, CellSetIn cellset, FieldOutCell outCells);
  using ExecutionSignature = void(_1, PointCount, _3);
  using InputDomain = _2;

  template <typename PointValueVecType, typename OutType>
  SVTKM_EXEC void operator()(const PointValueVecType& pointValues,
                            const svtkm::IdComponent& numPoints,
                            OutType& average) const
  {
    OutType sum = static_cast<OutType>(pointValues[0]);
    for (svtkm::IdComponent pointIndex = 1; pointIndex < numPoints; ++pointIndex)
    {
      sum = sum + static_cast<OutType>(pointValues[pointIndex]);
    }

    average = sum / static_cast<OutType>(numPoints);
  }
};

class AverageCellToPoint : public svtkm::worklet::WorkletVisitPointsWithCells
{
public:
  using ControlSignature = void(FieldInCell inCells, CellSetIn topology, FieldOut outPoints);
  using ExecutionSignature = void(_1, _3, CellCount);
  using InputDomain = _2;

  template <typename CellVecType, typename OutType>
  SVTKM_EXEC void operator()(const CellVecType& cellValues,
                            OutType& avgVal,
                            const svtkm::IdComponent& numCellIDs) const
  {
    //simple functor that returns the average cell Value.
    avgVal = svtkm::TypeTraits<OutType>::ZeroInitialization();
    if (numCellIDs != 0)
    {
      for (svtkm::IdComponent cellIndex = 0; cellIndex < numCellIDs; ++cellIndex)
      {
        avgVal += static_cast<OutType>(cellValues[cellIndex]);
      }
      avgVal = avgVal / static_cast<OutType>(numCellIDs);
    }
  }
};

// -----------------------------------------------------------------------------
template <typename T>
class Classification : public svtkm::worklet::WorkletVisitCellsWithPoints
{
public:
  using ControlSignature = void(FieldInPoint inNodes, CellSetIn cellset, FieldOutCell outCaseId);
  using ExecutionSignature = void(_1, _3);
  using InputDomain = _2;

  T IsoValue;

  SVTKM_CONT
  Classification(T isovalue)
    : IsoValue(isovalue)
  {
  }

  template <typename FieldInType>
  SVTKM_EXEC void operator()(const FieldInType& fieldIn, svtkm::IdComponent& caseNumber) const
  {
    using FieldType = typename svtkm::VecTraits<FieldInType>::ComponentType;
    const FieldType iso = static_cast<FieldType>(this->IsoValue);

    caseNumber = ((fieldIn[0] > iso) | (fieldIn[1] > iso) << 1 | (fieldIn[2] > iso) << 2 |
                  (fieldIn[3] > iso) << 3 | (fieldIn[4] > iso) << 4 | (fieldIn[5] > iso) << 5 |
                  (fieldIn[6] > iso) << 6 | (fieldIn[7] > iso) << 7);
  }
};

template <typename T, typename Enable = void>
struct NumberGenerator
{
};

template <typename T>
struct NumberGenerator<T, typename std::enable_if<std::is_floating_point<T>::value>::type>
{
  std::mt19937 rng;
  std::uniform_real_distribution<T> distribution;
  NumberGenerator(T low, T high)
    : rng()
    , distribution(low, high)
  {
  }
  T next() { return distribution(rng); }
};

template <typename T>
struct NumberGenerator<T, typename std::enable_if<!std::is_floating_point<T>::value>::type>
{
  std::mt19937 rng;
  std::uniform_int_distribution<T> distribution;

  NumberGenerator(T low, T high)
    : rng()
    , distribution(low, high)
  {
  }
  T next() { return distribution(rng); }
};

// Returns an extra random value.
// Like, an additional random value.
// Not a random value that's somehow "extra random".
template <typename ArrayT>
SVTKM_CONT typename ArrayT::ValueType FillRandomValues(ArrayT& array,
                                                      svtkm::Id size,
                                                      svtkm::Float64 min,
                                                      svtkm::Float64 max)
{
  using ValueType = typename ArrayT::ValueType;

  NumberGenerator<ValueType> generator{ static_cast<ValueType>(min), static_cast<ValueType>(max) };
  array.Allocate(size);
  auto portal = array.GetPortalControl();
  for (svtkm::Id i = 0; i < size; ++i)
  {
    portal.Set(i, generator.next());
  }
  return generator.next();
}

template <typename Value>
struct BenchCellToPointAvgImpl
{
  svtkm::cont::ArrayHandle<Value> Input;

  ::benchmark::State& State;
  svtkm::Id CubeSize;
  svtkm::Id NumCells;

  svtkm::cont::Timer Timer;
  svtkm::cont::Invoker Invoker;

  SVTKM_CONT
  BenchCellToPointAvgImpl(::benchmark::State& state)
    : State{ state }
    , CubeSize{ CUBE_SIZE }
    , NumCells{ (this->CubeSize - 1) * (this->CubeSize - 1) * (this->CubeSize - 1) }
    , Timer{ Config.Device }
    , Invoker{ Config.Device }
  {
    FillRandomValues(this->Input, this->NumCells, 1., 100.);

    { // Configure label:
      std::ostringstream desc;
      desc << "CubeSize:" << this->CubeSize;
      this->State.SetLabel(desc.str());
    }
  }

  template <typename BenchArrayType>
  SVTKM_CONT void Run(const BenchArrayType& input)
  {
    svtkm::cont::CellSetStructured<3> cellSet;
    cellSet.SetPointDimensions(svtkm::Id3{ this->CubeSize, this->CubeSize, this->CubeSize });
    svtkm::cont::ArrayHandle<Value> result;

    for (auto _ : this->State)
    {
      (void)_;
      this->Timer.Start();
      this->Invoker(AverageCellToPoint{}, input, cellSet, result);
      this->Timer.Stop();

      this->State.SetIterationTime(this->Timer.GetElapsedTime());
    }

    // #items = #points
    const int64_t iterations = static_cast<int64_t>(this->State.iterations());
    this->State.SetItemsProcessed(static_cast<int64_t>(cellSet.GetNumberOfPoints()) * iterations);
  }
};

template <typename ValueType>
void BenchCellToPointAvgStatic(::benchmark::State& state)
{
  BenchCellToPointAvgImpl<ValueType> impl{ state };
  impl.Run(impl.Input);
};
SVTKM_BENCHMARK_TEMPLATES(BenchCellToPointAvgStatic, ValueTypes);

template <typename ValueType>
void BenchCellToPointAvgDynamic(::benchmark::State& state)
{
  BenchCellToPointAvgImpl<ValueType> impl{ state };
  impl.Run(ValueVariantHandle{ impl.Input });
};
SVTKM_BENCHMARK_TEMPLATES(BenchCellToPointAvgDynamic, ValueTypes);

template <typename Value>
struct BenchPointToCellAvgImpl
{
  svtkm::cont::ArrayHandle<Value> Input;

  ::benchmark::State& State;
  svtkm::Id CubeSize;
  svtkm::Id NumPoints;

  svtkm::cont::Timer Timer;
  svtkm::cont::Invoker Invoker;

  SVTKM_CONT
  BenchPointToCellAvgImpl(::benchmark::State& state)
    : State{ state }
    , CubeSize{ CUBE_SIZE }
    , NumPoints{ (this->CubeSize) * (this->CubeSize) * (this->CubeSize) }
    , Timer{ Config.Device }
    , Invoker{ Config.Device }
  {
    FillRandomValues(this->Input, this->NumPoints, 1., 100.);

    { // Configure label:
      std::ostringstream desc;
      desc << "CubeSize:" << this->CubeSize;
      this->State.SetLabel(desc.str());
    }
  }

  template <typename BenchArrayType>
  SVTKM_CONT void Run(const BenchArrayType& input)
  {
    svtkm::cont::CellSetStructured<3> cellSet;
    cellSet.SetPointDimensions(svtkm::Id3{ this->CubeSize, this->CubeSize, this->CubeSize });
    svtkm::cont::ArrayHandle<Value> result;

    for (auto _ : this->State)
    {
      (void)_;
      this->Timer.Start();
      this->Invoker(AveragePointToCell{}, input, cellSet, result);
      this->Timer.Stop();

      this->State.SetIterationTime(this->Timer.GetElapsedTime());
    }

    // #items = #cells
    const int64_t iterations = static_cast<int64_t>(this->State.iterations());
    this->State.SetItemsProcessed(static_cast<int64_t>(cellSet.GetNumberOfCells()) * iterations);
  }
};

template <typename ValueType>
void BenchPointToCellAvgStatic(::benchmark::State& state)
{
  BenchPointToCellAvgImpl<ValueType> impl{ state };
  impl.Run(impl.Input);
};
SVTKM_BENCHMARK_TEMPLATES(BenchPointToCellAvgStatic, ValueTypes);

template <typename ValueType>
void BenchPointToCellAvgDynamic(::benchmark::State& state)
{
  BenchPointToCellAvgImpl<ValueType> impl{ state };
  impl.Run(ValueVariantHandle{ impl.Input });
};
SVTKM_BENCHMARK_TEMPLATES(BenchPointToCellAvgDynamic, ValueTypes);

template <typename Value>
struct BenchClassificationImpl
{
  svtkm::cont::ArrayHandle<Value> Input;

  ::benchmark::State& State;
  svtkm::Id CubeSize;
  svtkm::Id DomainSize;
  Value IsoValue;

  svtkm::cont::Timer Timer;
  svtkm::cont::Invoker Invoker;

  SVTKM_CONT
  BenchClassificationImpl(::benchmark::State& state)
    : State{ state }
    , CubeSize{ CUBE_SIZE }
    , DomainSize{ this->CubeSize * this->CubeSize * this->CubeSize }
    , Timer{ Config.Device }
    , Invoker{ Config.Device }
  {
    this->IsoValue = FillRandomValues(this->Input, this->DomainSize, 1., 100.);

    { // Configure label:
      std::ostringstream desc;
      desc << "CubeSize:" << this->CubeSize;
      this->State.SetLabel(desc.str());
    }
  }

  template <typename BenchArrayType>
  SVTKM_CONT void Run(const BenchArrayType& input)
  {
    svtkm::cont::CellSetStructured<3> cellSet;
    cellSet.SetPointDimensions(svtkm::Id3{ this->CubeSize, this->CubeSize, this->CubeSize });
    svtkm::cont::ArrayHandle<svtkm::IdComponent> result;

    Classification<Value> worklet(this->IsoValue);

    for (auto _ : this->State)
    {
      (void)_;
      this->Timer.Start();
      this->Invoker(worklet, input, cellSet, result);
      this->Timer.Stop();

      this->State.SetIterationTime(this->Timer.GetElapsedTime());
    }

    // #items = #cells
    const int64_t iterations = static_cast<int64_t>(this->State.iterations());
    this->State.SetItemsProcessed(static_cast<int64_t>(cellSet.GetNumberOfCells()) * iterations);
  }
};

template <typename ValueType>
void BenchClassificationStatic(::benchmark::State& state)
{
  BenchClassificationImpl<ValueType> impl{ state };
  impl.Run(impl.Input);
};
SVTKM_BENCHMARK_TEMPLATES(BenchClassificationStatic, ValueTypes);

template <typename ValueType>
void BenchClassificationDynamic(::benchmark::State& state)
{
  BenchClassificationImpl<ValueType> impl{ state };
  impl.Run(ValueVariantHandle{ impl.Input });
};
SVTKM_BENCHMARK_TEMPLATES(BenchClassificationDynamic, ValueTypes);

} // end anon namespace

int main(int argc, char* argv[])
{
  // Parse SVTK-m options:
  auto opts = svtkm::cont::InitializeOptions::RequireDevice | svtkm::cont::InitializeOptions::AddHelp;
  Config = svtkm::cont::Initialize(argc, argv, opts);

  // Setup device:
  svtkm::cont::GetRuntimeDeviceTracker().ForceDevice(Config.Device);

  // handle benchmarking related args and run benchmarks:
  SVTKM_EXECUTE_BENCHMARKS(argc, argv);
}