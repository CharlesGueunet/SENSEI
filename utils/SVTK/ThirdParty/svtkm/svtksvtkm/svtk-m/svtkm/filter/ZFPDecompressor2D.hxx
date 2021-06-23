//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_filter_ZFPDecompressor2D_hxx
#define svtk_m_filter_ZFPDecompressor2D_hxx

#include <svtkm/cont/CellSetStructured.h>
#include <svtkm/cont/DynamicCellSet.h>
#include <svtkm/cont/ErrorFilterExecution.h>

namespace svtkm
{
namespace filter
{


//-----------------------------------------------------------------------------
inline SVTKM_CONT ZFPDecompressor2D::ZFPDecompressor2D()
  : svtkm::filter::FilterField<ZFPDecompressor2D>()
{
}
//-----------------------------------------------------------------------------
template <typename T, typename StorageType, typename DerivedPolicy>
inline SVTKM_CONT svtkm::cont::DataSet ZFPDecompressor2D::DoExecute(
  const svtkm::cont::DataSet&,
  const svtkm::cont::ArrayHandle<T, StorageType>&,
  const svtkm::filter::FieldMetadata&,
  const svtkm::filter::PolicyBase<DerivedPolicy>&)
{
  SVTKM_ASSERT(true);

  svtkm::cont::DataSet ds;
  return ds;
}

//-----------------------------------------------------------------------------
template <typename StorageType, typename DerivedPolicy>
inline SVTKM_CONT svtkm::cont::DataSet ZFPDecompressor2D::DoExecute(
  const svtkm::cont::DataSet& input,
  const svtkm::cont::ArrayHandle<svtkm::Int64, StorageType>& field,
  const svtkm::filter::FieldMetadata&,
  const svtkm::filter::PolicyBase<DerivedPolicy>&)
{
  svtkm::cont::CellSetStructured<2> cellSet;
  input.GetCellSet().CopyTo(cellSet);
  svtkm::Id2 pointDimensions = cellSet.GetPointDimensions();

  svtkm::cont::ArrayHandle<svtkm::Float64> decompress;
  decompressor.Decompress(field, decompress, rate, pointDimensions);

  svtkm::cont::DataSet dataset;
  dataset.AddField(svtkm::cont::make_FieldPoint("decompressed", decompress));
  return dataset;
}

//-----------------------------------------------------------------------------
template <typename T, typename StorageType, typename DerivedPolicy>
inline SVTKM_CONT bool ZFPDecompressor2D::DoMapField(svtkm::cont::DataSet&,
                                                    const svtkm::cont::ArrayHandle<T, StorageType>&,
                                                    const svtkm::filter::FieldMetadata&,
                                                    const svtkm::filter::PolicyBase<DerivedPolicy>&)
{
  return false;
}
}
} // namespace svtkm::filter
#endif