//=============================================================================
//
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2012 Sandia Corporation.
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//
//=============================================================================

#define svtkmlib_ArrayConverterExport_cxx
#include "ArrayConverters.hxx"

namespace tosvtkm
{

SVTK_EXPORT_REAL_ARRAY_CONVERSION_TO_SVTKM(svtkAOSDataArrayTemplate)
SVTK_EXPORT_REAL_ARRAY_CONVERSION_TO_SVTKM(svtkSOADataArrayTemplate)

} // tosvtkm