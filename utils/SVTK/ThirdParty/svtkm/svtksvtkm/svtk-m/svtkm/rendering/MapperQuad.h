//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_rendering_MapperQuad_h
#define svtk_m_rendering_MapperQuad_h

#include <svtkm/cont/ColorTable.h>
#include <svtkm/rendering/Camera.h>
#include <svtkm/rendering/Mapper.h>

#include <memory>

namespace svtkm
{
namespace rendering
{

/**
 * \brief MapperQuad renderers quad facess from a cell set via ray tracing.
 *        As opposed to breaking quads into two trianges, scalars are
 *        interpolated using all 4 points of the quad resulting in more
 *        accurate interpolation.
 */
class SVTKM_RENDERING_EXPORT MapperQuad : public Mapper
{
public:
  MapperQuad();

  ~MapperQuad();

  void SetCanvas(svtkm::rendering::Canvas* canvas) override;
  virtual svtkm::rendering::Canvas* GetCanvas() const override;

  void RenderCells(const svtkm::cont::DynamicCellSet& cellset,
                   const svtkm::cont::CoordinateSystem& coords,
                   const svtkm::cont::Field& scalarField,
                   const svtkm::cont::ColorTable& colorTable,
                   const svtkm::rendering::Camera& camera,
                   const svtkm::Range& scalarRange) override;

  virtual void StartScene() override;
  virtual void EndScene() override;
  void SetCompositeBackground(bool on);
  svtkm::rendering::Mapper* NewCopy() const override;

private:
  struct InternalsType;
  std::shared_ptr<InternalsType> Internals;

  struct RenderFunctor;
};
}
} //namespace svtkm::rendering

#endif //svtk_m_rendering_MapperQuad_h