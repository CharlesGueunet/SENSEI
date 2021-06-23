//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_cont_testing_TestingSerialization_h
#define svtk_m_cont_testing_TestingSerialization_h

#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/VariantArrayHandle.h>
#include <svtkm/cont/testing/Testing.h>

#include <svtkm/thirdparty/diy/serialization.h>

#include <random>

namespace svtkm
{
namespace cont
{
namespace testing
{
namespace serialization
{

//-----------------------------------------------------------------------------
static std::default_random_engine generator;

template <typename T>
class UniformRandomValueGenerator
{
private:
  using DistributionType = typename std::conditional<std::is_integral<T>::value,
                                                     std::uniform_int_distribution<svtkm::Id>,
                                                     std::uniform_real_distribution<T>>::type;

public:
  UniformRandomValueGenerator()
    : Distribution(-127, 127)
  {
  }

  UniformRandomValueGenerator(T min, T max)
    : Distribution(static_cast<typename DistributionType::result_type>(min),
                   static_cast<typename DistributionType::result_type>(max))
  {
  }

  T operator()() { return static_cast<T>(this->Distribution(generator)); }

private:
  DistributionType Distribution;
};

template <typename T, typename Tag = typename svtkm::VecTraits<T>::HasMultipleComponents>
struct BaseScalarType;

template <typename T>
struct BaseScalarType<T, svtkm::VecTraitsTagSingleComponent>
{
  using Type = T;
};

template <typename T>
struct BaseScalarType<T, svtkm::VecTraitsTagMultipleComponents>
{
  using Type = typename BaseScalarType<typename svtkm::VecTraits<T>::ComponentType>::Type;
};

template <typename T>
using BaseScalarType_t = typename BaseScalarType<T>::Type;

template <typename T>
struct RandomValue_
{
  static T Make(UniformRandomValueGenerator<T>& rangen) { return static_cast<T>(rangen()); }
};

template <typename T, svtkm::IdComponent NumComponents>
struct RandomValue_<svtkm::Vec<T, NumComponents>>
{
  using VecType = svtkm::Vec<T, NumComponents>;

  static VecType Make(UniformRandomValueGenerator<BaseScalarType_t<T>>& rangen)
  {
    VecType val{};
    for (svtkm::IdComponent i = 0; i < NumComponents; ++i)
    {
      val[i] = RandomValue_<T>::Make(rangen);
    }
    return val;
  }
};

template <typename T>
struct RandomValue : RandomValue_<T>
{
  using RandomValue_<T>::Make;

  static T Make(BaseScalarType_t<T> min, BaseScalarType_t<T> max)
  {
    auto rangen = UniformRandomValueGenerator<BaseScalarType_t<T>>(min, max);
    return Make(rangen);
  }

  static T Make()
  {
    auto rangen = UniformRandomValueGenerator<BaseScalarType_t<T>>();
    return Make(rangen);
  }
};

template <typename T>
struct RandomArrayHandle
{
  static svtkm::cont::ArrayHandle<T> Make(UniformRandomValueGenerator<BaseScalarType_t<T>>& rangen,
                                         svtkm::Id length)
  {
    svtkm::cont::ArrayHandle<T> a;
    a.Allocate(length);

    for (svtkm::Id i = 0; i < length; ++i)
    {
      a.GetPortalControl().Set(i, RandomValue<T>::Make(rangen));
    }

    return a;
  }

  static svtkm::cont::ArrayHandle<T> Make(svtkm::Id length,
                                         BaseScalarType_t<T> min,
                                         BaseScalarType_t<T> max)
  {
    auto rangen = UniformRandomValueGenerator<BaseScalarType_t<T>>(min, max);
    return Make(rangen, length);
  }

  static svtkm::cont::ArrayHandle<T> Make(svtkm::Id length)
  {
    auto rangen = UniformRandomValueGenerator<BaseScalarType_t<T>>();
    return Make(rangen, length);
  }
};

//-----------------------------------------------------------------------------
template <typename T>
struct Block
{
  T send;
  T received;
};

template <typename T, typename TestEqualFunctor>
void TestSerialization(const T& obj, const TestEqualFunctor& test)
{
  auto comm = svtkm::cont::EnvironmentTracker::GetCommunicator();
  svtkmdiy::Master master(comm);

  auto nblocks = comm.size();
  svtkmdiy::RoundRobinAssigner assigner(comm.size(), nblocks);

  std::vector<int> gids;
  assigner.local_gids(comm.rank(), gids);
  SVTKM_ASSERT(gids.size() == 1);
  auto gid = gids[0];

  Block<T> block;
  block.send = obj;

  svtkmdiy::Link* link = new svtkmdiy::Link;
  svtkmdiy::BlockID neighbor;

  // send neighbor
  neighbor.gid = (gid < (nblocks - 1)) ? (gid + 1) : 0;
  neighbor.proc = assigner.rank(neighbor.gid);
  link->add_neighbor(neighbor);

  // recv neighbor
  neighbor.gid = (gid > 0) ? (gid - 1) : (nblocks - 1);
  neighbor.proc = assigner.rank(neighbor.gid);
  link->add_neighbor(neighbor);

  master.add(gid, &block, link);

  // compute, exchange, compute
  master.foreach ([](Block<T>* b, const svtkmdiy::Master::ProxyWithLink& cp) {
    cp.enqueue(cp.link()->target(0), b->send);
  });
  master.exchange();
  master.foreach ([](Block<T>* b, const svtkmdiy::Master::ProxyWithLink& cp) {
    cp.dequeue(cp.link()->target(1).gid, b->received);
  });

  comm.barrier();

  test(block.send, block.received);
}
}
}
}
} // svtkm::cont::testing::serialization

#endif // svtk_m_cont_testing_TestingSerialization_h