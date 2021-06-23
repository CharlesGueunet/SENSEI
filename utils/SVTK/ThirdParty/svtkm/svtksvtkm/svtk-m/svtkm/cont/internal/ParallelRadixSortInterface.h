//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#ifndef svtk_m_cont_internal_ParallelRadixSortInterface_h
#define svtk_m_cont_internal_ParallelRadixSortInterface_h

#include <svtkm/BinaryPredicates.h>
#include <svtkm/cont/ArrayHandle.h>

#include <functional>
#include <type_traits>

namespace svtkm
{
namespace cont
{
namespace internal
{
namespace radix
{

const size_t MIN_BYTES_FOR_PARALLEL = 400000;
const size_t BYTES_FOR_MAX_PARALLELISM = 4000000;

struct RadixSortTag
{
};

struct PSortTag
{
};

// Detect supported functors for radix sort:
template <typename T>
struct is_valid_compare_type : std::integral_constant<bool, false>
{
};
template <typename T>
struct is_valid_compare_type<std::less<T>> : std::integral_constant<bool, true>
{
};
template <typename T>
struct is_valid_compare_type<std::greater<T>> : std::integral_constant<bool, true>
{
};
template <>
struct is_valid_compare_type<svtkm::SortLess> : std::integral_constant<bool, true>
{
};
template <>
struct is_valid_compare_type<svtkm::SortGreater> : std::integral_constant<bool, true>
{
};

// Convert svtkm::Sort[Less|Greater] to the std:: equivalents:
template <typename BComp, typename T>
BComp&& get_std_compare(BComp&& b, T&&)
{
  return std::forward<BComp>(b);
}
template <typename T>
std::less<T> get_std_compare(svtkm::SortLess, T&&)
{
  return std::less<T>{};
}
template <typename T>
std::greater<T> get_std_compare(svtkm::SortGreater, T&&)
{
  return std::greater<T>{};
}

// Determine if radix sort can be used for a given ValueType, StorageType, and
// comparison functor.
template <typename T, typename StorageTag, typename BinaryCompare>
struct sort_tag_type
{
  using type = PSortTag;
};
template <typename T, typename BinaryCompare>
struct sort_tag_type<T, svtkm::cont::StorageTagBasic, BinaryCompare>
{
  using PrimT = std::is_arithmetic<T>;
  using LongDT = std::is_same<T, long double>;
  using BComp = is_valid_compare_type<BinaryCompare>;
  using type = typename std::conditional<PrimT::value && BComp::value && !LongDT::value,
                                         RadixSortTag,
                                         PSortTag>::type;
};

template <typename KeyType,
          typename ValueType,
          typename KeyStorageTagType,
          typename ValueStorageTagType,
          class BinaryCompare>
struct sortbykey_tag_type
{
  using type = PSortTag;
};
template <typename KeyType, typename ValueType, class BinaryCompare>
struct sortbykey_tag_type<KeyType,
                          ValueType,
                          svtkm::cont::StorageTagBasic,
                          svtkm::cont::StorageTagBasic,
                          BinaryCompare>
{
  using PrimKey = std::is_arithmetic<KeyType>;
  using PrimValue = std::is_arithmetic<ValueType>;
  using LongDKey = std::is_same<KeyType, long double>;
  using BComp = is_valid_compare_type<BinaryCompare>;
  using type = typename std::conditional<PrimKey::value && PrimValue::value && BComp::value &&
                                           !LongDKey::value,
                                         RadixSortTag,
                                         PSortTag>::type;
};

#define SVTKM_INTERNAL_RADIX_SORT_DECLARE(key_type)                                                 \
  SVTKM_CONT_EXPORT void parallel_radix_sort(                                                       \
    key_type* data, size_t num_elems, const std::greater<key_type>& comp);                         \
  SVTKM_CONT_EXPORT void parallel_radix_sort(                                                       \
    key_type* data, size_t num_elems, const std::less<key_type>& comp);                            \
  SVTKM_CONT_EXPORT void parallel_radix_sort_key_values(                                            \
    key_type* keys, svtkm::Id* vals, size_t num_elems, const std::greater<key_type>& comp);         \
  SVTKM_CONT_EXPORT void parallel_radix_sort_key_values(                                            \
    key_type* keys, svtkm::Id* vals, size_t num_elems, const std::less<key_type>& comp);

// Generate radix sort interfaces for key and key value sorts.
#define SVTKM_DECLARE_RADIX_SORT()                                                                  \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(short int)                                                      \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(unsigned short int)                                             \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(int)                                                            \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(unsigned int)                                                   \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(long int)                                                       \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(unsigned long int)                                              \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(long long int)                                                  \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(unsigned long long int)                                         \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(unsigned char)                                                  \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(signed char)                                                    \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(char)                                                           \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(char16_t)                                                       \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(char32_t)                                                       \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(wchar_t)                                                        \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(float)                                                          \
  SVTKM_INTERNAL_RADIX_SORT_DECLARE(double)
}
}
}
} // end svtkm::cont::internal::radix

#endif // svtk_m_cont_internal_ParallelRadixSortInterface_h