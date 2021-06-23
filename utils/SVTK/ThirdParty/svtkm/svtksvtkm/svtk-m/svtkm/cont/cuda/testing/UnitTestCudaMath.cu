//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#include <svtkm/cont/RuntimeDeviceTracker.h>
#include <svtkm/cont/cuda/DeviceAdapterCuda.h>
#include <svtkm/cont/testing/Testing.h>
#include <svtkm/testing/TestingMath.h>

#include <svtkm/worklet/DispatcherMapField.h>
#include <svtkm/worklet/WorkletMapField.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "curand_kernel.h"

namespace
{

struct TriggerICE : public svtkm::worklet::WorkletMapField
{
  using ControlSignature = void(FieldIn, FieldIn, FieldOut);
  using ExecutionSignature = _3(_1, _2, WorkIndex);

#ifdef SVTKM_CUDA_DEVICE_PASS
  template <class ValueType>
  __device__ ValueType operator()(const ValueType& bad,
                                  const ValueType& sane,
                                  const svtkm::Id sequenceId) const
  {

    curandState_t state;
    //Each thread uses same seed but different sequence numbers
    curand_init(42, sequenceId, 0, &state);

    int signBad = svtkm::SignBit(bad);
    int signGood = svtkm::SignBit(bad);

    svtkm::Vec<ValueType, 3> coord = { svtkm::Abs(bad * sane),
                                      bad * sane + (ValueType)signBad,
                                      bad * sane + (ValueType)signGood };

    for (int i = 0; i < 10; ++i)
    {
      for (int j = 0; j < 3; ++j)
      {
        if (svtkm::IsNan(coord[j]))
        {
          coord[j] = curand_normal(&state) * 5.0f;
          coord[j] = svtkm::Sqrt(svtkm::Dot(coord, coord));
          if (coord[j] <= 1.0f)
          {
            coord[j] += 1.0f;
          }
        }
        if (svtkm::IsInf(coord[j]))
        {
          coord[j] = curand_normal(&state) * 8.0f;
          coord[j] = svtkm::Tan(svtkm::Cos(svtkm::Dot(coord, coord)));
        }
      }
    }
    return coord[0] * 4.0f + coord[1] * 4.0f + coord[2] * 4.0f;
  }
#else
  template <class ValueType>
  ValueType operator()(const ValueType& bad, const ValueType& sane, const svtkm::Id sequenceId) const
  {
    return bad + sane * static_cast<ValueType>(sequenceId);
  }
#endif
};

//-----------------------------------------------------------------------------
template <typename Device>
void RunEdgeCases()
{
  std::cout << "Testing complicated worklets that can cause NVCC to ICE." << std::endl;
  //When running CUDA on unsupported hardware we find that IsInf, IsNan, and
  //SignBit can cause the CUDA compiler to crash. This test is a consistent
  //way to detect this.
  //
  //The way it works is we generate all kinds of nasty floating point values
  //such as signaling Nan, quiet Nan, other Nans, +Inf, -Inf, -0, +0, a collection of
  //denormal numbers, and the min and max float values
  //and than a random collection of values from normal float space. We combine this
  //array which we will call 'bad' with another input array which we will call 'sane',
  //We than execute a worklet that takes values stored in 'bad' and 'sane' that does
  //some computation that takes into account the results of IsInf, IsNan, and
  //SignBit
  const svtkm::Id desired_size = 2048;
  std::vector<float> sanevalues;
  std::vector<float> badvalues = { std::numeric_limits<float>::signaling_NaN(),
                                   std::numeric_limits<float>::quiet_NaN(),
                                   std::nanf("1"),
                                   std::nanf("4200042"),
                                   std::numeric_limits<float>::infinity(),
                                   std::numeric_limits<float>::infinity() * -1,
                                   0.0f,
                                   -0.0f,
                                   std::numeric_limits<float>::denorm_min(),
                                   std::nextafter(std::numeric_limits<float>::min(), 0.0f),
                                   std::numeric_limits<float>::denorm_min() *
                                     (1 + std::numeric_limits<float>::epsilon()),
                                   std::nextafter(std::numeric_limits<float>::min(), 0.0f) *
                                     (1 + std::numeric_limits<float>::epsilon()),
                                   std::numeric_limits<float>::lowest(),
                                   std::numeric_limits<float>::min(),
                                   std::numeric_limits<float>::max() };
  const std::size_t bad_size = badvalues.size();
  const svtkm::Id bad_size_as_id = static_cast<svtkm::Id>(bad_size);

  badvalues.reserve(desired_size);
  sanevalues.reserve(desired_size);

  //construct a random number generator
  std::mt19937 rng;
  std::uniform_real_distribution<float> range(-1.0f, 1.0f);

  // now add in some random numbers to the bad values
  for (std::size_t i = 0; i < desired_size - bad_size; ++i)
  {
    badvalues.push_back(range(rng));
  }
  for (std::size_t i = 0; i < desired_size; ++i)
  {
    sanevalues.push_back(range(rng));
  }

  auto bad = svtkm::cont::make_ArrayHandle(badvalues);
  auto sane = svtkm::cont::make_ArrayHandle(sanevalues);
  decltype(sane) result;
  svtkm::worklet::DispatcherMapField<TriggerICE> dispatcher;
  dispatcher.SetDevice(Device());
  dispatcher.Invoke(bad, sane, result);

  auto portal = result.GetPortalConstControl();

  //the first 6 values should be nan
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(0)), "Value should be NaN.");
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(1)), "Value should be NaN.");
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(2)), "Value should be NaN.");
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(3)), "Value should be NaN.");
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(4)), "Value should be NaN.");
  SVTKM_TEST_ASSERT(svtkm::IsNan(portal.Get(5)), "Value should be NaN.");

  for (svtkm::Id i = bad_size_as_id; i < desired_size; ++i)
  { //The rest of the values shouldn't be Nan or Inf
    auto v = portal.Get(i);
    const bool valid = !svtkm::IsNan(v) && !svtkm::IsInf(v);
    SVTKM_TEST_ASSERT(valid, "value shouldn't be NaN or INF");
  }
}

} //namespace

int UnitTestCudaMath(int argc, char* argv[])
{
  auto& tracker = svtkm::cont::GetRuntimeDeviceTracker();
  tracker.ForceDevice(svtkm::cont::DeviceAdapterTagCuda{});
  int tests_valid = svtkm::cont::testing::Testing::Run(
    UnitTestMathNamespace::RunMathTests<svtkm::cont::DeviceAdapterTagCuda>, argc, argv);

  tests_valid +=
    svtkm::cont::testing::Testing::Run(RunEdgeCases<svtkm::cont::DeviceAdapterTagCuda>, argc, argv);

  return tests_valid;
}