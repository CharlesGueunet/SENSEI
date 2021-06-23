//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2014 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//  Copyright 2014 UT-Battelle, LLC.
//  Copyright 2014 Los Alamos National Security.
//
//  Under the terms of Contract DE-NA0003525 with NTESS,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
// Copyright (c) 2018, The Regents of the University of California, through
// Lawrence Berkeley National Laboratory (subject to receipt of any required approvals
// from the U.S. Dept. of Energy).  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National
//     Laboratory, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=============================================================================
//
//  This code is an extension of the algorithm presented in the paper:
//  Parallel Peak Pruning for Scalable SMP Contour Tree Computation.
//  Hamish Carr, Gunther Weber, Christopher Sewell, and James Ahrens.
//  Proceedings of the IEEE Symposium on Large Data Analysis and Visualization
//  (LDAV), October 2016, Baltimore, Maryland.
//
//  The PPP2 algorithm and software were jointly developed by
//  Hamish Carr (University of Leeds), Gunther H. Weber (LBNL), and
//  Oliver Ruebel (LBNL)
//==============================================================================

#ifndef svtkm_worklet_contourtree_augmented_mesh_dem_triangulation_contourtree_mesh_execution_obect_mesh_structure_h
#define svtkm_worklet_contourtree_augmented_mesh_dem_triangulation_contourtree_mesh_execution_obect_mesh_structure_h

#include <svtkm/Pair.h>
#include <svtkm/Types.h>
#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/worklet/contourtree_augmented/Types.h>



//Define namespace alias for the freudenthal types to make the code a bit more readable
namespace cpp2_ns = svtkm::worklet::contourtree_augmented;

namespace svtkm
{
namespace worklet
{
namespace contourtree_augmented
{
namespace mesh_dem_contourtree_mesh_inc
{

// Worklet for computing the sort indices from the sort order
template <typename DeviceAdapter>
class MeshStructureContourTreeMesh
{
public:
  typedef typename cpp2_ns::IdArrayType::template ExecutionTypes<DeviceAdapter>::PortalConst
    IdArrayPortalType;

  // Default constucture. Needed for the CUDA built to work
  SVTKM_EXEC_CONT
  MeshStructureContourTreeMesh()
    : getMax(false)
  {
  }

  // Main constructure used in the code
  SVTKM_CONT
  MeshStructureContourTreeMesh(const cpp2_ns::IdArrayType neighbours,
                               const cpp2_ns::IdArrayType firstNeighbour,
                               const svtkm::Id maxneighbours,
                               bool getmax)
    : maxNeighbours(maxneighbours)
    , getMax(getmax)
  {
    neighboursPortal = neighbours.PrepareForInput(DeviceAdapter());
    firstNeighbourPortal = firstNeighbour.PrepareForInput(DeviceAdapter());
  }

  SVTKM_EXEC
  svtkm::Id GetNumberOfVertices() const { return this->firstNeighbourPortal.GetNumberOfValues(); }

  SVTKM_EXEC
  constexpr svtkm::Id GetMaxNumberOfNeighbours() const { return this->maxNeighbours; }

  SVTKM_EXEC
  inline svtkm::Id GetNeighbourIndex(svtkm::Id sortIndex, svtkm::Id neighbourNo) const
  { // GetNeighbourIndex
    return neighboursPortal.Get(firstNeighbourPortal.Get(sortIndex) + neighbourNo);
  } // GetNeighbourIndex

  // sets outgoing paths for saddles
  SVTKM_EXEC
  inline svtkm::Id GetExtremalNeighbour(svtkm::Id sortIndex) const
  { // GetExtremalNeighbour()
    svtkm::Id neighboursBeginIndex = firstNeighbourPortal.Get(sortIndex);
    svtkm::Id neighboursEndIndex = (sortIndex < this->GetNumberOfVertices() - 1)
      ? (firstNeighbourPortal.Get(sortIndex + 1) - 1)
      : (neighboursPortal.GetNumberOfValues() - 1);
    svtkm::Id neighboursBegin = neighboursPortal.Get(neighboursBeginIndex);
    svtkm::Id neighboursEnd = neighboursPortal.Get(neighboursEndIndex);

    if (neighboursBeginIndex == neighboursEndIndex + 1)
    { // Empty list of neighbours, this should never happen
      return sortIndex | TERMINAL_ELEMENT;
    }

    svtkm::Id ret;
    if (this->getMax)
    {
      ret = neighboursEnd;
      if (ret < sortIndex)
        ret = sortIndex | TERMINAL_ELEMENT;
    }
    else
    {
      ret = neighboursBegin;
      if (ret > sortIndex)
        ret = sortIndex | TERMINAL_ELEMENT;
    }
    return ret;
  } // GetExtremalNeighbour()


  // NOTE/FIXME: The following also iterates over all values and could be combined with GetExtremalNeighbour(). However, the
  // results are needed at different places and splitting the two functions leads to a cleaner design
  SVTKM_EXEC
  inline svtkm::Pair<svtkm::Id, svtkm::Id> GetNeighbourComponentsMaskAndDegree(
    svtkm::Id sortIndex,
    bool getMaxComponents) const
  { // GetNeighbourComponentsMaskAndDegree()
    svtkm::Id neighboursBeginIndex = firstNeighbourPortal.Get(sortIndex);
    svtkm::Id neighboursEndIndex = (sortIndex < this->GetNumberOfVertices() - 1)
      ? firstNeighbourPortal.Get(sortIndex + 1)
      : neighboursPortal.GetNumberOfValues();
    svtkm::Id numNeighbours = neighboursEndIndex - neighboursBeginIndex;
    svtkm::Id outDegree = 0;
    svtkm::Id neighbourComponentMask = 0;
    svtkm::Id currNeighbour = 0;
    for (svtkm::Id nbrNo = 0; nbrNo < numNeighbours; ++nbrNo)
    {
      currNeighbour = neighboursPortal.Get(neighboursBeginIndex + nbrNo);
      if ((getMaxComponents && (currNeighbour > sortIndex)) ||
          (!getMaxComponents && (currNeighbour < sortIndex)))
      {
        ++outDegree;
        neighbourComponentMask |= svtkm::Id{ 1 } << nbrNo;
      }
    }
    svtkm::Pair<svtkm::Id, svtkm::Id> re(neighbourComponentMask, outDegree);
    return re;
  } // GetNeighbourComponentsMaskAndDegree()

private:
  IdArrayPortalType neighboursPortal;
  IdArrayPortalType firstNeighbourPortal;
  svtkm::Id maxNeighbours;
  bool getMax;

}; // ExecutionObjec_MeshStructure_3Dt

} // namespace mesh_dem_2d_freudenthal_inc
} // namespace contourtree_augmented
} // namespace worklet
} // namespace svtkm

#endif