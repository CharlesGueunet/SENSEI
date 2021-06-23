//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_rendering_raytracing_PartialComposite_h
#define svtk_m_rendering_raytracing_PartialComposite_h

#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/rendering/raytracing/ChannelBuffer.h>

namespace svtkm
{
namespace rendering
{
namespace raytracing
{

template <typename FloatType>
struct PartialComposite
{
  svtkm::cont::ArrayHandle<svtkm::Id> PixelIds;   // pixel that owns composite
  svtkm::cont::ArrayHandle<FloatType> Distances; // distance of composite end
  ChannelBuffer<FloatType> Buffer;              // holds either color or absorption
  // (optional fields)
  ChannelBuffer<FloatType> Intensities;           // holds the intensity emerging from each ray
  svtkm::cont::ArrayHandle<FloatType> PathLengths; // Total distance traversed through the mesh
};
}
}
} // namespace svtkm::rendering::raytracing
#endif