//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#include <svtkm/rendering/Color.h>

namespace svtkm
{
namespace rendering
{

svtkm::rendering::Color svtkm::rendering::Color::black(0, 0, 0, 1);
svtkm::rendering::Color svtkm::rendering::Color::white(1, 1, 1, 1);

svtkm::rendering::Color svtkm::rendering::Color::red(1, 0, 0, 1);
svtkm::rendering::Color svtkm::rendering::Color::green(0, 1, 0, 1);
svtkm::rendering::Color svtkm::rendering::Color::blue(0, 0, 1, 1);

svtkm::rendering::Color svtkm::rendering::Color::cyan(0, 1, 1, 1);
svtkm::rendering::Color svtkm::rendering::Color::magenta(1, 0, 1, 1);
svtkm::rendering::Color svtkm::rendering::Color::yellow(1, 1, 0, 1);

svtkm::rendering::Color svtkm::rendering::Color::gray10(.1f, .1f, .1f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray20(.2f, .2f, .2f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray30(.3f, .3f, .3f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray40(.4f, .4f, .4f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray50(.5f, .5f, .5f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray60(.6f, .6f, .6f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray70(.7f, .7f, .7f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray80(.8f, .8f, .8f, 1);
svtkm::rendering::Color svtkm::rendering::Color::gray90(.9f, .9f, .9f, 1);
}
} // namespace svtkm::rendering