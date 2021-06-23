//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_io_internal_Endian_h
#define svtk_m_io_internal_Endian_h

#include <svtkm/Types.h>

#include <algorithm>
#include <vector>

namespace svtkm
{
namespace io
{
namespace internal
{

inline bool IsLittleEndian()
{
  static constexpr svtkm::Int16 i16 = 0x1;
  const svtkm::Int8* i8p = reinterpret_cast<const svtkm::Int8*>(&i16);
  return (*i8p == 1);
}

template <typename T>
inline void FlipEndianness(std::vector<T>& buffer)
{
  svtkm::UInt8* bytes = reinterpret_cast<svtkm::UInt8*>(&buffer[0]);
  const std::size_t tsize = sizeof(T);
  const std::size_t bsize = buffer.size();
  for (std::size_t i = 0; i < bsize; i++, bytes += tsize)
  {
    std::reverse(bytes, bytes + tsize);
  }
}

template <typename T, svtkm::IdComponent N>
inline void FlipEndianness(std::vector<svtkm::Vec<T, N>>& buffer)
{
  svtkm::UInt8* bytes = reinterpret_cast<svtkm::UInt8*>(&buffer[0]);
  const std::size_t tsize = sizeof(T);
  const std::size_t bsize = buffer.size();
  for (std::size_t i = 0; i < bsize; i++)
  {
    for (svtkm::IdComponent j = 0; j < N; j++, bytes += tsize)
    {
      std::reverse(bytes, bytes + tsize);
    }
  }
}
}
}
} // svtkm::io::internal

#endif //svtk_m_io_internal_Endian_h