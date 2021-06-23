//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
//  Copyright (c) 2016, Los Alamos National Security, LLC
//  All rights reserved.
//
//  Copyright 2016. Los Alamos National Security, LLC.
//  This software was produced under U.S. Government contract DE-AC52-06NA25396
//  for Los Alamos National Laboratory (LANL), which is operated by
//  Los Alamos National Security, LLC for the U.S. Department of Energy.
//  The U.S. Government has rights to use, reproduce, and distribute this
//  software.  NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL SECURITY, LLC
//  MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY FOR THE
//  USE OF THIS SOFTWARE.  If software is modified to produce derivative works,
//  such modified software should be clearly marked, so as not to confuse it
//  with the version available from LANL.
//
//  Additionally, redistribution and use in source and binary forms, with or
//  without modification, are permitted provided that the following conditions
//  are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//  3. Neither the name of Los Alamos National Security, LLC, Los Alamos
//     National Laboratory, LANL, the U.S. Government, nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND
//  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
//  BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS
//  NATIONAL SECURITY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
//  USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
//  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//============================================================================

#ifndef svtk_m_worklet_CosmoTools_h
#define svtk_m_worklet_CosmoTools_h

#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/DeviceAdapterAlgorithm.h>
#include <svtkm/cont/Field.h>

#include <svtkm/worklet/cosmotools/CosmoTools.h>
#include <svtkm/worklet/cosmotools/CosmoToolsCenterFinder.h>
#include <svtkm/worklet/cosmotools/CosmoToolsHaloFinder.h>

namespace svtkm
{
namespace worklet
{

class CosmoTools
{
public:
  // Run the halo finder and then the NxN MBP center finder
  template <typename FieldType, typename StorageType>
  void RunHaloFinder(svtkm::cont::ArrayHandle<FieldType, StorageType>& xLocation,
                     svtkm::cont::ArrayHandle<FieldType, StorageType>& yLocation,
                     svtkm::cont::ArrayHandle<FieldType, StorageType>& zLocation,
                     const svtkm::Id nParticles,
                     const FieldType particleMass,
                     const svtkm::Id minHaloSize,
                     const FieldType linkingLen,
                     svtkm::cont::ArrayHandle<svtkm::Id>& resultHaloId,
                     svtkm::cont::ArrayHandle<svtkm::Id>& resultMBP,
                     svtkm::cont::ArrayHandle<FieldType>& resultPot)
  {
    // Constructor gets particle locations, particle mass and min halo size
    cosmotools::CosmoTools<FieldType, StorageType> cosmo(
      nParticles, particleMass, minHaloSize, linkingLen, xLocation, yLocation, zLocation);

    // Find the halos within the particles and the MBP center of each halo
    cosmo.HaloFinder(resultHaloId, resultMBP, resultPot);
  }

  // Run MBP on a single halo of particles using the N^2 algorithm
  template <typename FieldType, typename StorageType>
  void RunMBPCenterFinderNxN(svtkm::cont::ArrayHandle<FieldType, StorageType> xLocation,
                             svtkm::cont::ArrayHandle<FieldType, StorageType> yLocation,
                             svtkm::cont::ArrayHandle<FieldType, StorageType> zLocation,
                             const svtkm::Id nParticles,
                             const FieldType particleMass,
                             svtkm::Pair<svtkm::Id, FieldType>& nxnResult)
  {
    // Constructor gets particle locations and particle mass
    cosmotools::CosmoTools<FieldType, StorageType> cosmo(
      nParticles, particleMass, xLocation, yLocation, zLocation);

    // Most Bound Particle N x N algorithm
    FieldType nxnPotential;
    svtkm::Id nxnMBP = cosmo.MBPCenterFinderNxN(&nxnPotential);

    nxnResult.first = nxnMBP;
    nxnResult.second = nxnPotential;
  }

  // Run MBP on a single halo of particles using MxN estimation algorithm
  template <typename FieldType, typename StorageType>
  void RunMBPCenterFinderMxN(svtkm::cont::ArrayHandle<FieldType, StorageType> xLocation,
                             svtkm::cont::ArrayHandle<FieldType, StorageType> yLocation,
                             svtkm::cont::ArrayHandle<FieldType, StorageType> zLocation,
                             const svtkm::Id nParticles,
                             const FieldType particleMass,
                             svtkm::Pair<svtkm::Id, FieldType>& mxnResult)
  {
    // Constructor gets particle locations and particle mass
    cosmotools::CosmoTools<FieldType, StorageType> cosmo(
      nParticles, particleMass, xLocation, yLocation, zLocation);

    // Most Bound Particle M x N algorithm with binning estimates
    FieldType mxnPotential;
    svtkm::Id mxnMBP = cosmo.MBPCenterFinderMxN(&mxnPotential);

    mxnResult.first = mxnMBP;
    mxnResult.second = mxnPotential;
  }
};
}
} // namespace svtkm::worklet

#endif // svtk_m_worklet_CosmoTools_h