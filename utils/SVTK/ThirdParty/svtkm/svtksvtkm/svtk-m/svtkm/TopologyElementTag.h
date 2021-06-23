//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_TopologyElementTag_h
#define svtk_m_TopologyElementTag_h

#include <svtkm/Types.h>

namespace svtkm
{

/// \brief A tag used to identify the cell elements in a topology.
///
/// A topology element refers to some type of substructure of a topology. For
/// example, a 3D mesh has points, edges, faces, and cells. Each of these is an
/// example of a topology element and has its own tag.
///
struct TopologyElementTagCell
{
};

/// \brief A tag used to identify the point elements in a topology.
///
/// A topology element refers to some type of substructure of a topology. For
/// example, a 3D mesh has points, edges, faces, and cells. Each of these is an
/// example of a topology element and has its own tag.
///
struct TopologyElementTagPoint
{
};

/// \brief A tag used to identify the edge elements in a topology.
///
/// A topology element refers to some type of substructure of a topology. For
/// example, a 3D mesh has points, edges, faces, and cells. Each of these is an
/// example of a topology element and has its own tag.
///
struct TopologyElementTagEdge
{
};

/// \brief A tag used to identify the face elements in a topology.
///
/// A topology element refers to some type of substructure of a topology. For
/// example, a 3D mesh has points, edges, faces, and cells. Each of these is an
/// example of a topology element and has its own tag.
///
struct TopologyElementTagFace
{
};

namespace internal
{

/// Checks to see if the given object is a topology element tag.This check is
/// compatible with C++11 type_traits.
/// It contains a typedef named \c type that is either std::true_type or
/// std::false_type. Both of these have a typedef named value with the
/// respective boolean value.
///
template <typename T>
struct TopologyElementTagCheck : std::false_type
{
};

template <>
struct TopologyElementTagCheck<svtkm::TopologyElementTagCell> : std::true_type
{
};

template <>
struct TopologyElementTagCheck<svtkm::TopologyElementTagPoint> : std::true_type
{
};

template <>
struct TopologyElementTagCheck<svtkm::TopologyElementTagEdge> : std::true_type
{
};

template <>
struct TopologyElementTagCheck<svtkm::TopologyElementTagFace> : std::true_type
{
};

#define SVTKM_IS_TOPOLOGY_ELEMENT_TAG(type)                                                         \
  static_assert(::svtkm::internal::TopologyElementTagCheck<type>::value,                            \
                "Invalid Topology Element Tag being used")

} // namespace internal

} // namespace svtkm

#endif //svtk_m_TopologyElementTag_h