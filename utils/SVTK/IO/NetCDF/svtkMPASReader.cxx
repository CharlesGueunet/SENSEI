/*=========================================================================

Program:   Visualization Toolkit
Module:    svtkMPASReader.h

Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*=========================================================================

  Copyright (c) 2002-2005 Los Alamos National Laboratory

  This software and ancillary information known as svtk_ext (and herein
  called "SOFTWARE") is made available under the terms described below.
  The SOFTWARE has been approved for release with associated LA_CC
  Number 99-44, granted by Los Alamos National Laboratory in July 1999.

  Unless otherwise indicated, this SOFTWARE has been authored by an
  employee or employees of the University of California, operator of the
  Los Alamos National Laboratory under Contract No. W-7405-ENG-36 with
  the United States Department of Energy.

  The United States Government has rights to use, reproduce, and
  distribute this SOFTWARE.  The public may copy, distribute, prepare
  derivative works and publicly display this SOFTWARE without charge,
  provided that this Notice and any statement of authorship are
  reproduced on all copies.

  Neither the U. S. Government, the University of California, nor the
  Advanced Computing Laboratory makes any warranty, either express or
  implied, nor assumes any liability or responsibility for the use of
  this SOFTWARE.

  If SOFTWARE is modified to produce derivative works, such modified
  SOFTWARE should be clearly marked, so as not to confuse it with the
  version available from Los Alamos National Laboratory.

  =========================================================================*/

// Christine Ahrens (cahrens@lanl.gov)
// Version 1.3

/*=========================================================================
  NOTES
  When using this reader, it is important that you remember to do the following:
  1.  When changing a selected variable, remember to select it also in the drop
  down box to "color by".  It doesn't color by that variable automatically.
  2.  When selecting multilayer sphere view, make layer thickness around
  100,000.
  3.  When selecting multilayer lat/lon view, make layer thickness around 10.
  4.  Always click the -Z orientation after making a switch from lat/lon to
  sphere, from single to multilayer or changing thickness.
  5.  Be conservative on the number of changes you make before hitting Apply,
  since there may be bugs in this reader.  Just make one change and then hit
  Apply.

  =========================================================================*/

#include "svtkMPASReader.h"

#include "svtkCallbackCommand.h"
#include "svtkCellArray.h"
#include "svtkCellData.h"
#include "svtkCellType.h"
#include "svtkDataArraySelection.h"
#include "svtkDataObject.h"
#include "svtkDoubleArray.h"
#include "svtkInformation.h"
#include "svtkInformationDoubleVectorKey.h"
#include "svtkInformationVector.h"
#include "svtkIntArray.h"
#include "svtkMath.h"
#include "svtkNew.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkSmartPointer.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkStringArray.h"
#include "svtkToolkits.h"
#include "svtkUnstructuredGrid.h"

#include "svtk_netcdf.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Restricted to the supported NcType-convertible types.
#define svtkNcTemplateMacro(call)                                                                   \
  svtkTemplateMacroCase(SVTK_DOUBLE, double, call);                                                  \
  svtkTemplateMacroCase(SVTK_FLOAT, float, call);                                                    \
  svtkTemplateMacroCase(SVTK_INT, int, call);                                                        \
  svtkTemplateMacroCase(SVTK_SHORT, short, call);                                                    \
  svtkTemplateMacroCase(SVTK_CHAR, char, call);                                                      \
  svtkTemplateMacroCase(SVTK_SIGNED_CHAR, signed char, call) /* ncbyte */

#define svtkNcDispatch(type, call)                                                                  \
  switch (type)                                                                                    \
  {                                                                                                \
    svtkNcTemplateMacro(call);                                                                      \
    default:                                                                                       \
      svtkErrorMacro(<< "Unsupported data type: " << (type));                                       \
      abort();                                                                                     \
  }

namespace
{

struct DimMetaData
{
  long curIdx;
  size_t dimSize;
};

//------------------------------------------------------------------------------
inline int NcTypeToVtkType(nc_type type)
{
  switch (type)
  {
    case NC_BYTE:
      return SVTK_SIGNED_CHAR;
    case NC_CHAR:
      return SVTK_CHAR;
    case NC_SHORT:
      return SVTK_SHORT;
    case NC_INT:
      return SVTK_INT;
    case NC_FLOAT:
      return SVTK_FLOAT;
    case NC_DOUBLE:
      return SVTK_DOUBLE;
    case NC_NAT:
    default: // Shouldn't happen...
      svtkGenericWarningMacro(<< "Invalid NcType: " << type);
      return SVTK_VOID;
  }
}

template <typename T>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], T* data);

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], double* data)
{
  return nc_get_vara_double(ncid, varid, start, count, data);
}

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], float* data)
{
  return nc_get_vara_float(ncid, varid, start, count, data);
}

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], int* data)
{
  return nc_get_vara_int(ncid, varid, start, count, data);
}

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], short* data)
{
  return nc_get_vara_short(ncid, varid, start, count, data);
}

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], char* data)
{
  return nc_get_vara_text(ncid, varid, start, count, data);
}

template <>
int nc_get_vara(int ncid, int varid, size_t start[], size_t count[], signed char* data)
{
  return nc_get_vara_schar(ncid, varid, start, count, data);
}

} // end anon namespace

//----------------------------------------------------------------------------
// Internal class to avoid name pollution
//----------------------------------------------------------------------------

class svtkMPASReader::Internal
{
public:
  // variableIndex --> svtkDataArray
  typedef std::map<int, svtkSmartPointer<svtkDataArray> > ArrayMap;
  typedef std::map<std::string, DimMetaData> DimMetaDataMap;
  Internal(svtkMPASReader* r)
    : ncFile(-1)
    , reader(r)
  {
  }
  ~Internal() { close(); }

  bool open(const char* file)
  {
    int mode = NC_NOWRITE | NC_NETCDF4 | NC_CLASSIC_MODEL;
    int ncid;
    if (nc_err(nc_open(file, mode, &ncid)))
    {
      return false;
    }
    ncFile = ncid;
    return true;
  }
  void close()
  {
    if (ncFile != -1)
    {
      nc_err(nc_close(ncFile));
      ncFile = -1;
    }
  }

  bool nc_err(int nc_ret, bool msg_on_err = true) const;

  std::string dimensionedArrayName(int nc_var) const;
  bool ValidateDimensions(int nc_var, bool silent, int ndims, ...) const;
  size_t GetCursorForDimension(int nc_dim);
  size_t GetCountForDimension(int nc_dim) const;
  long InitializeDimension(int nc_dim);
  svtkIdType ComputeNumberOfTuples(int nc_var) const;

  template <typename ValueType>
  bool LoadDataArray(int nc_var, svtkDataArray* array, bool resize = true);

  template <typename ValueType>
  int LoadPointVarDataImpl(int nc_var, svtkDataArray* array);

  template <typename ValueType>
  int LoadCellVarDataImpl(int nc_var, svtkDataArray* array);

  int nc_var_id(const char* name, bool msg_on_err = true) const;
  int nc_dim_id(const char* name, bool msg_on_err = true) const;
  int nc_att_id(const char* name, bool msg_on_err = true) const;

  int ncFile;
  svtkMPASReader* reader;
  std::vector<int> pointVars;
  std::vector<int> cellVars;
  ArrayMap pointArrays;
  ArrayMap cellArrays;

  // Returns true if the dimension name is not nCells, nVertices, or Time.
  bool isExtraDim(const std::string& name);

  // Indices at which arbitrary trailing dimensions are fixed:
  DimMetaDataMap dimMetaDataMap;
  svtkTimeStamp dimMetaDataTime;
  // Set of dimensions currently used by the selected arrays:
  svtkNew<svtkStringArray> extraDims;
  svtkTimeStamp extraDimTime;
};

bool svtkMPASReader::Internal::isExtraDim(const std::string& name)
{
  return name != "nCells" && name != "nVertices" && name != "Time";
}

bool svtkMPASReader::Internal::nc_err(int nc_ret, bool msg_on_err) const
{
  if (nc_ret == NC_NOERR)
  {
    return false;
  }

  if (msg_on_err)
  {
    svtkErrorWithObjectMacro(reader, << "NetCDF error: " << nc_strerror(nc_ret));
  }
  return true;
}

std::string svtkMPASReader::Internal::dimensionedArrayName(int nc_var) const
{
  char name[NC_MAX_NAME + 1];
  if (nc_err(nc_inq_varname(ncFile, nc_var, name)))
  {
    return "";
  }

  int ndims;
  if (nc_err(nc_inq_varndims(ncFile, nc_var, &ndims)))
  {
    return "";
  }
  int dims[NC_MAX_VAR_DIMS];
  if (nc_err(nc_inq_vardimid(ncFile, nc_var, dims)))
  {
    return "";
  }

  std::ostringstream out;
  out << name << "(";

  for (int dim = 0; dim < ndims; ++dim)
  {
    if (dim != 0)
    {
      out << ", ";
    }

    if (nc_err(nc_inq_dimname(ncFile, dims[dim], name)))
    {
      return "";
    }
    out << name;
  }
  out << ")";
  return out.str();
}

//------------------------------------------------------------------------------
// Returns true if the dimensions in var match the expected args, or prints a
// warning and returns false if any are incorrect.
// ndims is the number of dimensions, and the variatic args must be
// C-strings identifying the expected dimensions.
// If silent is true, no warnings are printed.
//------------------------------------------------------------------------------
bool svtkMPASReader::Internal::ValidateDimensions(int nc_var, bool silent, int ndims, ...) const
{
  int nc_ndims;
  if (nc_err(nc_inq_varndims(ncFile, nc_var, &nc_ndims)))
  {
    return false;
  }

  if (nc_ndims != ndims)
  {
    if (!silent)
    {
      char name[NC_MAX_NAME + 1];
      if (nc_err(nc_inq_varname(ncFile, nc_var, name)))
      {
        return false;
      }
      svtkWarningWithObjectMacro(reader, << "Expected variable '" << name << "' to have " << ndims
                                        << " dimension(s), but it has " << nc_ndims << ".");
    }
    return false;
  }

  int dims[NC_MAX_VAR_DIMS];
  if (nc_err(nc_inq_vardimid(ncFile, nc_var, dims)))
  {
    return false;
  }

  va_list args;
  va_start(args, ndims);

  for (int i = 0; i < ndims; ++i)
  {
    char nc_name[NC_MAX_NAME + 1];
    if (nc_err(nc_inq_dimname(ncFile, dims[i], nc_name)))
    {
      va_end(args);
      return false;
    }
    std::string dimName(va_arg(args, const char*));
    if (dimName != nc_name)
    {
      if (!silent)
      {
        char name[NC_MAX_NAME + 1];
        if (nc_err(nc_inq_varname(ncFile, nc_var, name)))
        {
          return false;
        }
        svtkWarningWithObjectMacro(reader, << "Expected variable '" << name << "' to have '"
                                          << dimName << "' at dimension index " << i << ", not '"
                                          << nc_name << "'.");
      }
      va_end(args);
      return false;
    }
  }

  va_end(args);

  return true;
}

//------------------------------------------------------------------------------
// Return the cursor position for the specified dimension.
//------------------------------------------------------------------------------
size_t svtkMPASReader::Internal::GetCursorForDimension(int nc_dim)
{
  char name[NC_MAX_NAME + 1];
  if (nc_err(nc_inq_dimname(ncFile, nc_dim, name)))
  {
    return static_cast<size_t>(-1);
  }
  std::string dimName = name;
  if (dimName == "nCells" || dimName == "nVertices")
  {
    return 0;
  }
  else if (dimName == "Time")
  {
    return std::min(static_cast<long>(std::floor(reader->DTime)),
      static_cast<long>(reader->NumberOfTimeSteps - 1));
  }
  else if (reader->ShowMultilayerView && dimName == reader->VerticalDimension)
  {
    return 0;
  }
  else
  {
    return InitializeDimension(nc_dim);
  }
}

//------------------------------------------------------------------------------
// Return the number of values to read for the specified dimension.
//------------------------------------------------------------------------------
size_t svtkMPASReader::Internal::GetCountForDimension(int nc_dim) const
{
  char name[NC_MAX_NAME + 1];
  if (nc_err(nc_inq_dimname(ncFile, nc_dim, name)))
  {
    return static_cast<size_t>(-1);
  }
  std::string dimName = name;
  if (dimName == "nCells")
  {
    return reader->NumberOfPoints;
  }
  else if (dimName == "nVertices")
  {
    return reader->NumberOfCells;
  }
  else if (reader->ShowMultilayerView && dimName == reader->VerticalDimension)
  {
    return reader->MaximumNVertLevels;
  }
  else
  {
    return 1;
  }
}

//------------------------------------------------------------------------------
// For an arbitrary (i.e. not nCells, nVertices, or Time) dimension, extract
// the dimension's metadata into memory (if needed) and return the last used
// index into the dimension values, or 0 if the dimension is new.
// For an arbitrary (i.e. not nCells, nVertices, or Time) dimension, extract
//------------------------------------------------------------------------------
long svtkMPASReader::Internal::InitializeDimension(int nc_dim)
{
  char name[NC_MAX_NAME + 1];
  if (nc_err(nc_inq_dimname(ncFile, nc_dim, name)))
  {
    return false;
  }
  Internal::DimMetaDataMap::const_iterator match = dimMetaDataMap.find(name);

  long result = 0;
  if (match == dimMetaDataMap.end())
  {
    DimMetaData metaData;
    metaData.curIdx = result;
    if (nc_err(nc_inq_dimlen(ncFile, nc_dim, &metaData.dimSize)))
    {
      return -1;
    }

    dimMetaDataMap.insert(std::make_pair(std::string(name), metaData));
    dimMetaDataTime.Modified();
  }
  else
  {
    result = match->second.curIdx;
  }

  return result;
}

//------------------------------------------------------------------------------
svtkIdType svtkMPASReader::Internal::ComputeNumberOfTuples(int nc_var) const
{
  int numDims;
  if (nc_err(nc_inq_varndims(ncFile, nc_var, &numDims)))
  {
    return 0;
  }
  int dims[NC_MAX_VAR_DIMS];
  if (nc_err(nc_inq_vardimid(ncFile, nc_var, dims)))
  {
    return 0;
  }
  svtkIdType size = 0;
  for (int dim = 0; dim < numDims; ++dim)
  {
    svtkIdType count = static_cast<svtkIdType>(GetCountForDimension(dims[dim]));
    if (size == 0)
    {
      size = count;
    }
    else
    {
      size *= count;
    }
  }
  return size;
}

//------------------------------------------------------------------------------
template <typename ValueType>
bool svtkMPASReader::Internal::LoadDataArray(int nc_var, svtkDataArray* array, bool resize)
{
  nc_type var_type;
  if (nc_err(nc_inq_vartype(ncFile, nc_var, &var_type)))
  {
    return false;
  }
  if (array->GetDataType() != NcTypeToVtkType(var_type))
  {
    svtkWarningWithObjectMacro(reader, "Invalid array type.");
    return false;
  }

  int numDims;
  if (nc_err(nc_inq_varndims(ncFile, nc_var, &numDims)))
  {
    return false;
  }
  int dims[NC_MAX_VAR_DIMS];
  if (nc_err(nc_inq_vardimid(ncFile, nc_var, dims)))
  {
    return false;
  }
  std::vector<size_t> cursor;
  std::vector<size_t> counts;
  svtkIdType size = 0;

  for (int dim = 0; dim < numDims; ++dim)
  {
    cursor.push_back(GetCursorForDimension(dims[dim]));
    counts.push_back(GetCountForDimension(dims[dim]));
    if (size == 0)
    {
      size = counts.back();
    }
    else
    {
      size *= counts.back();
    }
  }

  if (resize)
  {
    array->SetNumberOfComponents(1);
    array->SetNumberOfTuples(size);
  }
  else
  {
    if (array->GetNumberOfComponents() != 1)
    {
      svtkWarningWithObjectMacro(
        reader, "Invalid number of components: " << array->GetNumberOfComponents() << ".");
      return false;
    }
    else if (array->GetNumberOfTuples() < size)
    {
      svtkWarningWithObjectMacro(reader,
        "Array only has " << array->GetNumberOfTuples() << " allocated, but we need " << size
                          << ".");
      return false;
    }
  }

  ValueType* dataBlock = static_cast<ValueType*>(array->GetVoidPointer(0));
  if (!dataBlock)
  {
    svtkWarningWithObjectMacro(reader, "GetVoidPointer returned nullptr.");
    return false;
  }

  if (nc_err(nc_get_vara<ValueType>(ncFile, nc_var, &cursor[0], &counts[0], dataBlock)))
  {
    svtkWarningWithObjectMacro(reader, "Reading " << size << " elements failed.");
    return false;
  }

  return true;
}

//------------------------------------------------------------------------------
template <typename ValueType>
int svtkMPASReader::Internal::LoadPointVarDataImpl(int nc_var, svtkDataArray* array)
{
  // Don't resize, we've preallocated extra room for multilayer (if needed):
  if (!LoadDataArray<ValueType>(nc_var, array, /*resize=*/false))
  {
    return 0;
  }

  // Check if this variable contains the vertical dimension:
  bool hasVerticalDimension = false;
  int numDims;
  if (nc_err(nc_inq_varndims(ncFile, nc_var, &numDims)))
  {
    return 0;
  }
  if (reader->ShowMultilayerView)
  {
    char name[NC_MAX_NAME + 1];
    int dims[NC_MAX_VAR_DIMS];
    if (nc_err(nc_inq_vardimid(ncFile, nc_var, dims)))
    {
      return 0;
    }
    for (int d = 0; d < numDims; ++d)
    {
      if (nc_err(nc_inq_dimname(ncFile, dims[d], name)))
      {
        return 0;
      }
      if (reader->VerticalDimension == name)
      {
        hasVerticalDimension = true;
        break;
      }
    }
  }

  svtkIdType varSize = ComputeNumberOfTuples(nc_var);
  ValueType* dataBlock = static_cast<ValueType*>(array->GetVoidPointer(0));
  std::vector<ValueType> tempData; // Used for Multilayer

  // singlelayer
  if (!reader->ShowMultilayerView)
  {
    // Account for point offset:
    if (reader->PointOffset != 0)
    {
      assert(reader->NumberOfPoints <= static_cast<size_t>(array->GetNumberOfTuples()) &&
        "Source array too small.");
      assert(reader->PointOffset + reader->NumberOfPoints <=
          static_cast<size_t>(array->GetNumberOfTuples()) &&
        "Destination array too small.");
      if (reader->PointOffset < reader->NumberOfPoints)
      {
        std::copy_backward(dataBlock, dataBlock + reader->NumberOfPoints,
          dataBlock + reader->PointOffset + reader->NumberOfPoints);
      }
      else
      {
        std::copy(dataBlock, dataBlock + reader->NumberOfPoints, dataBlock + reader->PointOffset);
      }
    }
    dataBlock[0] = dataBlock[1];
    // data is all in place, don't need to do next step
  }
  else
  { // multilayer
    if (reader->MaximumPoints == 0)
    {
      return 0; // No points
    }

    tempData.resize(reader->MaximumPoints);
    size_t vertPointOffset = reader->MaximumNVertLevels * reader->PointOffset;
    ValueType* dataPtr = &tempData[0] + vertPointOffset;

    assert(varSize < array->GetNumberOfTuples());
    assert(varSize < static_cast<svtkIdType>(reader->MaximumPoints - vertPointOffset));
    std::copy(dataBlock, dataBlock + varSize, dataPtr);

    if (!hasVerticalDimension)
    {
      // need to replicate data over all vertical layers
      // layout in memory needs to be:
      // pt1, pt1, ..., (VertLevels times), pt2, pt2, ..., (VertLevels times),
      // need to go backwards through the points in order to not overwrite
      // anything.
      for (size_t i = reader->NumberOfPoints; i > 0; i--)
      {
        // point to copy
        ValueType pt = *(dataPtr + i - 1);

        // where to start copying
        ValueType* copyPtr = dataPtr + (i - 1) * reader->MaximumNVertLevels;

        std::fill(copyPtr, copyPtr + reader->MaximumNVertLevels, pt);
      }
    }
  }

  svtkDebugWithObjectMacro(reader, << "Got point data.");

  size_t i = 0;
  size_t k = 0;

  if (reader->ShowMultilayerView)
  {
    // put in dummy points
    assert(reader->MaximumNVertLevels * 2 <= static_cast<size_t>(reader->MaximumPoints));
    assert(reader->MaximumNVertLevels <= static_cast<size_t>(array->GetNumberOfTuples()));
    std::copy(tempData.begin() + reader->MaximumNVertLevels,
      tempData.begin() + (2 * reader->MaximumNVertLevels), dataBlock);

    // write highest level dummy point (duplicate of last level)
    assert(reader->MaximumNVertLevels < static_cast<size_t>(array->GetNumberOfTuples()));
    assert(2 * reader->MaximumNVertLevels - 1 < static_cast<size_t>(reader->MaximumPoints));
    dataBlock[reader->MaximumNVertLevels] = tempData[2 * reader->MaximumNVertLevels - 1];

    svtkDebugWithObjectMacro(reader, << "Wrote dummy point data.");

    // put in other points
    for (size_t j = reader->PointOffset; j < reader->NumberOfPoints + reader->PointOffset; j++)
    {

      i = j * (reader->MaximumNVertLevels + 1);
      k = j * (reader->MaximumNVertLevels);

      // write data for one point -- lowest level to highest
      assert(k + reader->MaximumNVertLevels <= static_cast<size_t>(reader->MaximumPoints));
      assert(i + reader->MaximumNVertLevels <= static_cast<size_t>(array->GetNumberOfTuples()));
      std::copy(
        tempData.begin() + k, tempData.begin() + k + reader->MaximumNVertLevels, dataBlock + i);

      // for last layer of points, repeat last level's values
      // Need Mark's input on reader one
      dataBlock[i++] = tempData[--k];
      // svtkDebugWithObjectMacro(reader, << "Wrote j:" << j << endl);
    }
  }

  svtkDebugWithObjectMacro(reader, << "Wrote next points.");

  svtkDebugWithObjectMacro(reader, << "NumberOfPoints: " << reader->NumberOfPoints << " "
                                  << "CurrentExtraPoint: " << reader->CurrentExtraPoint);

  // put out data for extra points
  for (size_t j = reader->PointOffset + reader->NumberOfPoints; j < reader->CurrentExtraPoint; j++)
  {
    // use map to find out what point data we are using
    if (!reader->ShowMultilayerView)
    {
      k = reader->PointMap[j - reader->NumberOfPoints - reader->PointOffset];
      assert(j < static_cast<size_t>(array->GetNumberOfTuples()));
      assert(k < static_cast<size_t>(array->GetNumberOfTuples()));
      dataBlock[j] = dataBlock[k];
    }
    else
    {
      k = reader->PointMap[j - reader->NumberOfPoints - reader->PointOffset] *
        reader->MaximumNVertLevels;
      // write data for one point -- lowest level to highest
      assert(k + reader->MaximumNVertLevels <= static_cast<size_t>(reader->MaximumPoints));
      assert(i + reader->MaximumNVertLevels <= static_cast<size_t>(array->GetNumberOfTuples()));
      std::copy(
        tempData.begin() + k, tempData.begin() + k + reader->MaximumNVertLevels, dataBlock + i);

      // for last layer of points, repeat last level's values
      // Need Mark's input on this one
      dataBlock[i++] = tempData[--k];
    }
  }

  svtkDebugWithObjectMacro(reader, << "wrote extra point data.");
  return 1;
}

//------------------------------------------------------------------------------
template <typename ValueType>
int svtkMPASReader::Internal::LoadCellVarDataImpl(int nc_var, svtkDataArray* array)
{
  // Don't resize, we've preallocated extra room for multilayer (if needed):
  if (!LoadDataArray<ValueType>(nc_var, array, /*resize=*/false))
  {
    return 0;
  }

  ValueType* dataBlock = static_cast<ValueType*>(array->GetVoidPointer(0));

  // put out data for extra cells
  for (size_t j = reader->CellOffset + reader->NumberOfCells; j < reader->CurrentExtraCell; j++)
  {
    // use map to find out what cell data we are using
    if (!reader->ShowMultilayerView)
    {
      size_t k = reader->CellMap[j - reader->NumberOfCells - reader->CellOffset];
      assert(j < static_cast<size_t>(array->GetNumberOfTuples()));
      assert(k < static_cast<size_t>(array->GetNumberOfTuples()));
      dataBlock[j] = dataBlock[k];
    }
    else
    {
      size_t i = j * reader->MaximumNVertLevels;
      size_t k = reader->CellMap[j - reader->NumberOfCells - reader->CellOffset] *
        reader->MaximumNVertLevels;

      // write data for one cell -- lowest level to highest
      assert(i < static_cast<size_t>(array->GetNumberOfTuples()));
      assert(k + reader->MaximumNVertLevels <= static_cast<size_t>(array->GetNumberOfTuples()));
      std::copy(dataBlock + k, dataBlock + k + reader->MaximumNVertLevels, dataBlock + i);
    }
  }

  svtkDebugWithObjectMacro(reader, << "Stored data.");

  return 1;
}

//----------------------------------------------------------------------------
// Function to check if there is a NetCDF variable by that name
//-----------------------------------------------------------------------------

int svtkMPASReader::Internal::nc_var_id(const char* name, bool msg_on_err) const
{
  int varid;
  if (nc_err(nc_inq_varid(ncFile, name, &varid), msg_on_err))
  {
    return -1;
  }
  return varid;
}

//----------------------------------------------------------------------------
// Check if there is a NetCDF dimension by that name
//----------------------------------------------------------------------------

int svtkMPASReader::Internal::nc_dim_id(const char* name, bool msg_on_err) const
{
  int dimid;
  if (nc_err(nc_inq_dimid(ncFile, name, &dimid), msg_on_err))
  {
    return -1;
  }
  return dimid;
}

//----------------------------------------------------------------------------
// Check if there is a NetCDF attribute by that name
//----------------------------------------------------------------------------

int svtkMPASReader::Internal::nc_att_id(const char* name, bool msg_on_err) const
{
  int attid;
  if (nc_err(nc_inq_attid(ncFile, NC_GLOBAL, name, &attid), msg_on_err))
  {
    return -1;
  }
  return attid;
}

//----------------------------------------------------------------------------
//  Macro to check if the named NetCDF dimension exists
//----------------------------------------------------------------------------

#define CHECK_DIM(name, out)                                                                       \
  if ((out = this->Internals->nc_dim_id(name)) == -1)                                              \
  {                                                                                                \
    svtkErrorMacro(<< "Cannot find dimension: " << name << endl);                                   \
    return 0;                                                                                      \
  }

//----------------------------------------------------------------------------
// Macro to check if the named NetCDF variable exists
//----------------------------------------------------------------------------

#define CHECK_VAR(name, out)                                                                       \
  if ((out = this->Internals->nc_var_id(name)) == -1)                                              \
  {                                                                                                \
    svtkErrorMacro(<< "Cannot find variable: " << name << endl);                                    \
    return 0;                                                                                      \
  }

//-----------------------------------------------------------------------------
//  Function to convert cartesian coordinates to spherical, for use in
//  computing points in different layers of multilayer spherical view
//----------------------------------------------------------------------------

static int CartesianToSpherical(
  double x, double y, double z, double* rho, double* phi, double* theta)
{
  double trho, ttheta, tphi;

  trho = sqrt((x * x) + (y * y) + (z * z));
  ttheta = atan2(y, x);
  tphi = acos(z / (trho));
  if (svtkMath::IsNan(trho) || svtkMath::IsNan(ttheta) || svtkMath::IsNan(tphi))
  {
    return -1;
  }
  *rho = trho;
  *theta = ttheta;
  *phi = tphi;
  return 0;
}

//----------------------------------------------------------------------------
//  Function to convert spherical coordinates to cartesian, for use in
//  computing points in different layers of multilayer spherical view
//----------------------------------------------------------------------------

static int SphericalToCartesian(
  double rho, double phi, double theta, double* x, double* y, double* z)
{
  double tx, ty, tz;

  tx = rho * sin(phi) * cos(theta);
  ty = rho * sin(phi) * sin(theta);
  tz = rho * cos(phi);
  if (svtkMath::IsNan(tx) || svtkMath::IsNan(ty) || svtkMath::IsNan(tz))
  {
    return -1;
  }

  *x = tx;
  *y = ty;
  *z = tz;

  return 0;
}

svtkStandardNewMacro(svtkMPASReader);

//----------------------------------------------------------------------------
// Constructor for svtkMPASReader
//----------------------------------------------------------------------------

svtkMPASReader::svtkMPASReader()
{
  this->Internals = new svtkMPASReader::Internal(this);

  // Debugging
  // this->DebugOn();
  svtkDebugMacro(<< "Starting to create svtkMPASReader..." << endl);

  this->SetNumberOfInputPorts(0);
  this->SetNumberOfOutputPorts(1);

  this->SetDefaults();

  // Setup selection callback to modify this object when array selection changes
  this->PointDataArraySelection = svtkDataArraySelection::New();
  this->CellDataArraySelection = svtkDataArraySelection::New();
  this->SelectionObserver = svtkCallbackCommand::New();
  this->SelectionObserver->SetCallback(&svtkMPASReader::SelectionCallback);
  this->SelectionObserver->SetClientData(this);
  this->CellDataArraySelection->AddObserver(svtkCommand::ModifiedEvent, this->SelectionObserver);
  this->PointDataArraySelection->AddObserver(svtkCommand::ModifiedEvent, this->SelectionObserver);

  svtkDebugMacro(<< "Created svtkMPASReader" << endl);
}

//----------------------------------------------------------------------------
//  Destroys data stored for variables, points, and cells, but
//  doesn't destroy the list of variables or toplevel cell/pointVarDataArray.
//----------------------------------------------------------------------------

void svtkMPASReader::DestroyData()

{
  svtkDebugMacro(<< "DestroyData...");

  this->Internals->cellArrays.clear();
  this->Internals->pointArrays.clear();

  delete[] this->CellMap;
  this->CellMap = nullptr;

  delete[] this->PointMap;
  this->PointMap = nullptr;

  delete[] this->MaximumLevelPoint;
  this->MaximumLevelPoint = nullptr;
}

//----------------------------------------------------------------------------
// Destructor for MPAS Reader
//----------------------------------------------------------------------------

svtkMPASReader::~svtkMPASReader()
{
  svtkDebugMacro(<< "Destructing svtkMPASReader..." << endl);

  this->SetFileName(nullptr);

  this->Internals->close();

  this->DestroyData();

  svtkDebugMacro(<< "Destructing other stuff..." << endl);
  if (this->PointDataArraySelection)
  {
    this->PointDataArraySelection->Delete();
    this->PointDataArraySelection = nullptr;
  }
  if (this->CellDataArraySelection)
  {
    this->CellDataArraySelection->Delete();
    this->CellDataArraySelection = nullptr;
  }
  if (this->SelectionObserver)
  {
    this->SelectionObserver->Delete();
    this->SelectionObserver = nullptr;
  }

  delete this->Internals;

  svtkDebugMacro(<< "Destructed svtkMPASReader" << endl);
}

//----------------------------------------------------------------------------
void svtkMPASReader::ReleaseNcData()
{
  this->Internals->pointVars.clear();
  this->Internals->pointArrays.clear();
  this->Internals->cellVars.clear();
  this->Internals->cellArrays.clear();

  this->PointDataArraySelection->RemoveAllArrays();
  this->CellDataArraySelection->RemoveAllArrays();
  this->UpdateDimensions(true); // Reset extra dimension list.

  delete[] this->PointX;
  this->PointX = nullptr;
  delete[] this->PointY;
  this->PointY = nullptr;
  delete[] this->PointZ;
  this->PointZ = nullptr;

  delete[] this->OrigConnections;
  this->OrigConnections = nullptr;
  delete[] this->ModConnections;
  this->ModConnections = nullptr;
  delete[] this->CellMap;
  this->CellMap = nullptr;
  delete[] this->PointMap;
  this->PointMap = nullptr;
  delete[] this->MaximumLevelPoint;
  this->MaximumLevelPoint = nullptr;

  this->Internals->close();
}

//----------------------------------------------------------------------------
// Verify that the file exists, get dimension sizes and variables
//----------------------------------------------------------------------------

int svtkMPASReader::RequestInformation(
  svtkInformation* reqInfo, svtkInformationVector** inVector, svtkInformationVector* outVector)
{
  svtkDebugMacro(<< "In svtkMPASReader::RequestInformation" << endl);

  this->ReleaseNcData();

  if (!this->Superclass::RequestInformation(reqInfo, inVector, outVector))
  {
    return 0;
  }

  // Verify that file exists
  if (!this->FileName)
  {
    svtkErrorMacro("No filename specified");
    return 0;
  }

  // Get ParaView information pointer
  svtkInformation* outInfo = outVector->GetInformationObject(0);

  if (!this->Internals->open(this->FileName))
  {
    svtkErrorMacro(<< "Couldn't open file: " << this->FileName << endl);
    this->ReleaseNcData();
    return 0;
  }

  if (!this->GetNcDims())
  {
    this->ReleaseNcData();
    return 0;
  }

  if (!this->GetNcAtts())
  {
    this->ReleaseNcData();
    return 0;
  }

  if (!this->CheckParams())
  {
    this->ReleaseNcData();
    return 0;
  }

  if (!this->BuildVarArrays())
  {
    this->ReleaseNcData();
    return 0;
  }

  // Collect temporal information

  // At this time, MPAS doesn't have fine-grained time value, just
  // the number of the step, so that is what I store here for TimeSteps.
  if (this->NumberOfTimeSteps > 0)
  {
    // Tell the pipeline what steps are available
    std::vector<double> timeSteps;
    timeSteps.reserve(this->NumberOfTimeSteps);
    for (size_t i = 0; i < this->NumberOfTimeSteps; ++i)
    {
      timeSteps.push_back(static_cast<double>(i));
    }
    outInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_STEPS(), &timeSteps[0],
      static_cast<int>(timeSteps.size()));

    double tRange[2];
    tRange[0] = 0.;
    tRange[1] = static_cast<double>(this->NumberOfTimeSteps - 1);
    outInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_RANGE(), tRange, 2);
  }
  else
  {
    outInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_STEPS());
    outInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_RANGE());
  }

  return 1;
}

//----------------------------------------------------------------------------
// Data is read into a svtkUnstructuredGrid
//----------------------------------------------------------------------------

int svtkMPASReader::RequestData(svtkInformation* svtkNotUsed(reqInfo),
  svtkInformationVector** svtkNotUsed(inVector), svtkInformationVector* outVector)
{
  svtkDebugMacro(<< "In svtkMPASReader::RequestData" << endl);

  // get the info object
  svtkInformation* outInfo = outVector->GetInformationObject(0);

  // Output will be an ImageData
  svtkUnstructuredGrid* output =
    svtkUnstructuredGrid::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  this->DestroyData();
  if (!this->ReadAndOutputGrid())
  {
    this->DestroyData();
    return 0;
  }

  // Collect the time step requested
  this->DTime = 0;
  if (outInfo->Has(svtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP()))
  {
    this->DTime = outInfo->Get(svtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP());
  }
  output->GetInformation()->Set(svtkDataObject::DATA_TIME_STEP(), this->DTime);

  // Examine each variable to see if it is selected
  int numPointVars = static_cast<int>(this->Internals->pointVars.size());
  for (int var = 0; var < numPointVars; var++)
  {
    // Is this variable requested
    if (this->PointDataArraySelection->GetArraySetting(var))
    {
      svtkDataArray* array = this->LoadPointVarData(var);
      if (!array)
      {
        char name[NC_MAX_NAME + 1];
        if (!this->Internals->nc_err(
              nc_inq_varname(this->Internals->ncFile, this->Internals->pointVars[var], name)))
        {
          svtkWarningMacro(<< "Error loading point variable '" << name << "'.");
        }
        continue;
      }
      output->GetPointData()->AddArray(array);
    }
  }

  int numCellVars = static_cast<int>(this->Internals->cellVars.size());
  for (int var = 0; var < numCellVars; var++)
  {
    if (this->CellDataArraySelection->GetArraySetting(var))
    {
      svtkDataArray* array = this->LoadCellVarData(var);
      if (!array)
      {
        char name[NC_MAX_NAME + 1];
        if (!this->Internals->nc_err(
              nc_inq_varname(this->Internals->ncFile, this->Internals->pointVars[var], name)))
        {
          svtkWarningMacro(<< "Error loading point variable '" << name << "'.");
        }
        continue;
      }
      output->GetCellData()->AddArray(array);
    }
  }

  this->LoadTimeFieldData(output);

  svtkDebugMacro(<< "Returning from RequestData" << endl);
  return 1;
}

//----------------------------------------------------------------------------
// Set defaults for various parameters and initialize some variables
//----------------------------------------------------------------------------

void svtkMPASReader::SetDefaults()
{

  // put in defaults
  this->VerticalDimension = "nVertLevels";
  this->VerticalLevelRange[0] = 0;
  this->VerticalLevelRange[1] = 1;

  this->LayerThicknessRange[0] = 0;
  this->LayerThicknessRange[1] = 200000;
  this->LayerThickness = 10000;
  svtkDebugMacro(<< "SetDefaults: LayerThickness set to " << LayerThickness << endl);

  this->CenterLonRange[0] = 0;
  this->CenterLonRange[1] = 360;
  this->CenterLon = 180;

  this->Geometry = Spherical;

  this->IsAtmosphere = false;
  this->ProjectLatLon = false;
  this->OnASphere = false;
  this->ShowMultilayerView = false;
  this->IsZeroCentered = false;

  this->IncludeTopography = false;
  this->DoBugFix = false;
  this->CenterRad = CenterLon * svtkMath::Pi() / 180.0;

  this->UseDimensionedArrayNames = false;

  this->PointX = nullptr;
  this->PointY = nullptr;
  this->PointZ = nullptr;
  this->OrigConnections = nullptr;
  this->ModConnections = nullptr;
  this->CellMap = nullptr;
  this->PointMap = nullptr;
  this->MaximumLevelPoint = nullptr;

  this->FileName = nullptr;
  this->DTime = 0;

  this->MaximumPoints = 0;
  this->MaximumCells = 0;
}

//----------------------------------------------------------------------------
// Get dimensions of key NetCDF variables
//----------------------------------------------------------------------------

int svtkMPASReader::GetNcDims()
{
  int dimid;

  CHECK_DIM("nCells", dimid);
  if (this->Internals->nc_err(nc_inq_dimlen(this->Internals->ncFile, dimid, &this->NumberOfPoints)))
  {
    return 0;
  }
  this->PointOffset = 1;

  CHECK_DIM("nVertices", dimid);
  if (this->Internals->nc_err(nc_inq_dimlen(this->Internals->ncFile, dimid, &this->NumberOfCells)))
  {
    return 0;
  }
  this->CellOffset = 0;

  CHECK_DIM("vertexDegree", dimid);
  if (this->Internals->nc_err(nc_inq_dimlen(this->Internals->ncFile, dimid, &this->PointsPerCell)))
  {
    return 0;
  }

  CHECK_DIM("Time", dimid);
  if (this->Internals->nc_err(
        nc_inq_dimlen(this->Internals->ncFile, dimid, &this->NumberOfTimeSteps)))
  {
    return 0;
  }

  if ((dimid = this->Internals->nc_dim_id(this->VerticalDimension.c_str())) != -1)
  {
    if (this->Internals->nc_err(
          nc_inq_dimlen(this->Internals->ncFile, dimid, &this->MaximumNVertLevels)))
    {
      return 0;
    }
  }
  else
  {
    this->MaximumNVertLevels = 0;
  }

  return 1;
}

//----------------------------------------------------------------------------
int svtkMPASReader::GetNcAtts()
{
  int attid = -1;
  nc_inq_attid(this->Internals->ncFile, NC_GLOBAL, "on_a_sphere", &attid);
  if (attid == -1)
  {
    svtkWarningMacro(
      "Attribute 'on_a_sphere' missing in file " << this->FileName << ". Assuming \"YES\".");
    this->OnASphere = true;
  }
  else
  {
    size_t attlen;
    if (this->Internals->nc_err(
          nc_inq_attlen(this->Internals->ncFile, NC_GLOBAL, "on_a_sphere", &attlen)))
    {
      return 0;
    }
    char* val = new char[attlen + 1];
    val[attlen] = '\0';
    if (this->Internals->nc_err(
          nc_get_att_text(this->Internals->ncFile, NC_GLOBAL, "on_a_sphere", val)))
    {
      delete[] val;
      return 0;
    }
    this->OnASphere = (strcmp(val, "YES") == 0);
    delete[] val;
  }

  return 1;
}

//----------------------------------------------------------------------------
//  Check parameters are valid
//----------------------------------------------------------------------------

int svtkMPASReader::CheckParams()
{

  if ((this->PointsPerCell != 3) && (this->PointsPerCell != 4))
  {
    svtkErrorMacro("This code is only for hexagonal or quad primal grids" << endl);
    return (0);
  }

  this->VerticalLevelRange[0] = 0;
  this->VerticalLevelRange[1] = static_cast<int>(this->MaximumNVertLevels - 1);

  if (this->OnASphere)
  {
    if (this->ProjectLatLon)
    {
      this->Geometry = Projected;
    }
    else
    {
      this->Geometry = Spherical;
    }
  }
  else
  {
    this->Geometry = Planar;
    if (this->ProjectLatLon)
    {
      svtkWarningMacro("Ignoring ProjectLatLong -- Data is not on_a_sphere.");
    }
  }

  return (1);
}

//----------------------------------------------------------------------------
// Get the NetCDF variables on cell or vertex
//----------------------------------------------------------------------------

int svtkMPASReader::GetNcVars(const char* cellDimName, const char* pointDimName)
{
  this->Internals->pointArrays.clear();
  this->Internals->pointVars.clear();
  this->Internals->cellArrays.clear();
  this->Internals->cellVars.clear();

  int numVars;
  int vars[NC_MAX_VARS];
  if (this->Internals->nc_err(nc_inq_varids(this->Internals->ncFile, &numVars, vars)))
  {
    return 0;
  }
  for (int i = 0; i < numVars; i++)
  {
    // Variables must have the following dimension specification:
    // [Time, ] (nCells | nVertices), [arbitraryDim1, [arbitraryDim2, [...]]]

    bool isPointData = false;
    bool isCellData = false;
    int numDims;
    if (this->Internals->nc_err(nc_inq_varndims(this->Internals->ncFile, vars[i], &numDims)))
    {
      continue;
    }

    if (numDims < 1)
    {
      char name[NC_MAX_NAME + 1];
      if (this->Internals->nc_err(nc_inq_varname(this->Internals->ncFile, vars[i], name)))
      {
        continue;
      }
      svtkWarningMacro(<< "Variable '" << name << "' has invalid number of dimensions: " << numDims);
      continue;
    }
    int dims[NC_MAX_VAR_DIMS];
    if (this->Internals->nc_err(nc_inq_vardimid(this->Internals->ncFile, vars[i], dims)))
    {
      continue;
    }

    std::vector<std::string> dimNames;
    bool ok = true;
    for (int dim = 0; dim < std::min(numDims, 2); ++dim)
    {
      char name[NC_MAX_NAME + 1];
      if (this->Internals->nc_err(nc_inq_dimname(this->Internals->ncFile, dims[dim], name)))
      {
        ok = false;
        break;
      }
      dimNames.push_back(name);
    }
    if (!ok)
    {
      continue;
    }

    if (dimNames[0] == "Time" && dimNames.size() >= 2)
    {
      if (dimNames[1] == pointDimName)
      {
        isPointData = true;
      }
      else if (dimNames[1] == cellDimName)
      {
        isCellData = true;
      }
    }
    else if (dimNames[0] == pointDimName)
    {
      isPointData = true;
    }
    else if (dimNames[0] == cellDimName)
    {
      isCellData = true;
    }

    // Add to cell or point var array
    if (isCellData)
    {
      this->Internals->cellVars.push_back(vars[i]);
    }
    else if (isPointData)
    {
      this->Internals->pointVars.push_back(vars[i]);
    }
  }

  return (1);
}

//----------------------------------------------------------------------------
// Build the selection Arrays for points and cells in the GUI.
//----------------------------------------------------------------------------

int svtkMPASReader::BuildVarArrays()
{
  // figure out what variables to visualize -
  if (!GetNcVars("nVertices", "nCells"))
  {
    return 0;
  }

  for (size_t v = 0; v < this->Internals->pointVars.size(); v++)
  {
    int varid = this->Internals->pointVars[v];
    std::string name;
    if (this->UseDimensionedArrayNames)
    {
      name = this->Internals->dimensionedArrayName(varid);
    }
    else
    {
      char varname[NC_MAX_NAME + 1];
      if (this->Internals->nc_err(nc_inq_varname(this->Internals->ncFile, varid, varname)))
      {
        continue;
      }
      name = varname;
    }
    this->PointDataArraySelection->EnableArray(name.c_str());
    // Register the dimensions:
    int ndims;
    if (this->Internals->nc_err(nc_inq_varndims(this->Internals->ncFile, varid, &ndims)))
    {
      continue;
    }
    int dims[NC_MAX_VAR_DIMS];
    if (this->Internals->nc_err(nc_inq_vardimid(this->Internals->ncFile, varid, dims)))
    {
      continue;
    }
    for (int d = 0; d < ndims; ++d)
    {
      this->Internals->InitializeDimension(dims[d]);
    }
    svtkDebugMacro(<< "Adding point var: " << name);
  }

  for (size_t v = 0; v < this->Internals->cellVars.size(); v++)
  {
    int varid = this->Internals->cellVars[v];
    std::string name;
    if (this->UseDimensionedArrayNames)
    {
      name = this->Internals->dimensionedArrayName(varid);
    }
    else
    {
      char varname[NC_MAX_NAME + 1];
      if (this->Internals->nc_err(nc_inq_varname(this->Internals->ncFile, varid, varname)))
      {
        continue;
      }
      name = varname;
    }
    this->CellDataArraySelection->EnableArray(name.c_str());
    // Register the dimensions:
    int ndims;
    if (this->Internals->nc_err(nc_inq_varndims(this->Internals->ncFile, varid, &ndims)))
    {
      continue;
    }
    int dims[NC_MAX_VAR_DIMS];
    if (this->Internals->nc_err(nc_inq_vardimid(this->Internals->ncFile, varid, dims)))
    {
      continue;
    }
    for (int d = 0; d < ndims; ++d)
    {
      this->Internals->InitializeDimension(dims[d]);
    }
    svtkDebugMacro(<< "Adding cell var: " << name);
  }

  return 1;
}

//----------------------------------------------------------------------------
//  Read the data from the ncfile, allocate the geometry and create the
//  svtk data structures for points and cells.
//----------------------------------------------------------------------------

int svtkMPASReader::ReadAndOutputGrid()
{
  switch (this->Geometry)
  {
    case svtkMPASReader::Spherical:
      if (!this->AllocSphericalGeometry())
      {
        return 0;
      }
      this->FixPoints();
      break;

    case svtkMPASReader::Projected:
      if (!this->AllocProjectedGeometry())
      {
        return 0;
      }
      this->ShiftLonData();
      this->FixPoints();
      if (!this->EliminateXWrap())
      {
        return 0;
      }
      break;

    case svtkMPASReader::Planar:
      if (!this->AllocPlanarGeometry())
      {
        return 0;
      }
      this->FixPoints();
      break;

    default:
      svtkErrorMacro("Invalid geometry type (" << this->Geometry << ").");
      return 0;
  }

  this->OutputPoints();
  this->OutputCells();

  return 1;
}

//----------------------------------------------------------------------------
// Allocate into sphere view of dual geometry
//----------------------------------------------------------------------------

int svtkMPASReader::AllocSphericalGeometry()
{
  int varid;
  CHECK_VAR("xCell", varid);
  this->PointX = new double[this->NumberOfPoints + this->PointOffset];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  size_t start_pt[] = { 0 };
  size_t count_pt[] = { this->NumberOfPoints };
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointX + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointX[0] = 0.0;

  CHECK_VAR("yCell", varid);
  this->PointY = new double[this->NumberOfPoints + this->PointOffset];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointY + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointY[0] = 0.0;

  CHECK_VAR("zCell", varid);
  this->PointZ = new double[this->NumberOfPoints + this->PointOffset];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointZ + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointZ[0] = 0.0;

  CHECK_VAR("cellsOnVertex", varid);
  this->OrigConnections = new int[this->NumberOfCells * this->PointsPerCell];
  // TODO Spec says dims should be '3', 'nVertices', but my example files
  // use nVertices, vertexDegree...
  if (!this->Internals->ValidateDimensions(varid, false, 2, "nVertices", "vertexDegree"))
  {
    return 0;
  }
  size_t start_conn[] = { 0, 0 };
  size_t count_conn[] = { this->NumberOfCells, this->PointsPerCell };
  if (this->Internals->nc_err(nc_get_vara_int(
        this->Internals->ncFile, varid, start_conn, count_conn, this->OrigConnections)))
  {
    return 0;
  }

  if ((varid = this->Internals->nc_var_id("maxLevelCell", false)) != -1)
  {
    this->IncludeTopography = true;
    this->MaximumLevelPoint = new int[this->NumberOfPoints + this->PointOffset];
    if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
    {
      return 0;
    }
    if (this->Internals->nc_err(nc_get_vara_int(this->Internals->ncFile, varid, start_pt, count_pt,
          this->MaximumLevelPoint + this->PointOffset)))
    {
      return 0;
    }
  }

  this->CurrentExtraPoint = this->NumberOfPoints + this->PointOffset;
  this->CurrentExtraCell = this->NumberOfCells + this->CellOffset;

  if (this->ShowMultilayerView)
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell * this->MaximumNVertLevels);
    svtkDebugMacro(<< "alloc sphere: multilayer: setting MaximumCells to " << this->MaximumCells);
    this->MaximumPoints =
      static_cast<int>(this->CurrentExtraPoint * (this->MaximumNVertLevels + 1));
    svtkDebugMacro(<< "alloc sphere: multilayer: setting MaximumPoints to " << this->MaximumPoints);
  }
  else
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell);
    this->MaximumPoints = static_cast<int>(this->CurrentExtraPoint);
    svtkDebugMacro(<< "alloc sphere: singlelayer: setting MaximumPoints to " << this->MaximumPoints);
  }

  return 1;
}

//----------------------------------------------------------------------------
// Allocate the lat/lon projection of dual geometry.
//----------------------------------------------------------------------------

int svtkMPASReader::AllocProjectedGeometry()
{
  const float BLOATFACTOR = .5;
  this->ModNumPoints = (int)floor(this->NumberOfPoints * (1.0 + BLOATFACTOR));
  this->ModNumCells = (int)floor(this->NumberOfCells * (1.0 + BLOATFACTOR)) + 1;

  int varid;
  CHECK_VAR("lonCell", varid);
  this->PointX = new double[this->ModNumPoints];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  size_t start_pt[] = { 0 };
  size_t count_pt[] = { this->NumberOfPoints };
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointX + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointX[0] = 0.0;

  CHECK_VAR("latCell", varid);
  this->PointY = new double[this->ModNumPoints];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointY + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointY[0] = 0.0;

  CHECK_VAR("cellsOnVertex", varid);
  this->OrigConnections = new int[this->NumberOfCells * this->PointsPerCell];
  // TODO Spec says dims should be '3', 'nVertices', but my example files
  // use nVertices, vertexDegree...
  if (!this->Internals->ValidateDimensions(varid, false, 2, "nVertices", "vertexDegree"))
  {
    return 0;
  }
  size_t start_conn[] = { 0, 0 };
  size_t count_conn[] = { this->NumberOfCells, this->PointsPerCell };
  if (this->Internals->nc_err(nc_get_vara_int(
        this->Internals->ncFile, varid, start_conn, count_conn, this->OrigConnections)))
  {
    return 0;
  }

  // create my own list to include modified origConnections (due to
  // eliminating wraparound in the lat/lon projection) plus additional
  // cells added when mirroring cells that had previously wrapped around

  this->ModConnections = new int[this->ModNumCells * this->PointsPerCell];

  // allocate an array to map the extra points and cells to the original
  // so that when obtaining data, we know where to get it
  this->PointMap = new size_t[(size_t)floor(this->NumberOfPoints * BLOATFACTOR)];
  this->CellMap = new size_t[(size_t)floor(this->NumberOfCells * BLOATFACTOR)];

  if ((varid = this->Internals->nc_var_id("maxLevelCell", false)) != -1)
  {
    this->IncludeTopography = true;
    this->MaximumLevelPoint = new int[this->NumberOfPoints + this->NumberOfPoints];
    if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
    {
      return 0;
    }
    if (this->Internals->nc_err(nc_get_vara_int(this->Internals->ncFile, varid, start_pt, count_pt,
          this->MaximumLevelPoint + this->PointOffset)))
    {
      return 0;
    }
  }

  this->CurrentExtraPoint = this->NumberOfPoints + this->PointOffset;
  this->CurrentExtraCell = this->NumberOfCells + this->CellOffset;

  if (ShowMultilayerView)
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell * this->MaximumNVertLevels);
    this->MaximumPoints =
      static_cast<int>(this->CurrentExtraPoint * (this->MaximumNVertLevels + 1));
    svtkDebugMacro(<< "alloc latlon: multilayer: setting this->MaximumPoints to "
                  << this->MaximumPoints << endl);
  }
  else
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell);
    this->MaximumPoints = static_cast<int>(this->CurrentExtraPoint);
    svtkDebugMacro(<< "alloc latlon: singlelayer: setting this->MaximumPoints to "
                  << this->MaximumPoints << endl);
  }

  return 1;
}

int svtkMPASReader::AllocPlanarGeometry()
{
  int varid;
  CHECK_VAR("xCell", varid);
  this->PointX = new double[this->NumberOfPoints];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  size_t start_pt[] = { 0 };
  size_t count_pt[] = { this->NumberOfPoints };
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointX + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointX[0] = 0.0;

  CHECK_VAR("yCell", varid);
  this->PointY = new double[this->NumberOfPoints];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointY + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointY[0] = 0.0;

  CHECK_VAR("zCell", varid);
  this->PointZ = new double[this->NumberOfPoints];
  if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
  {
    return 0;
  }
  if (this->Internals->nc_err(nc_get_vara_double(
        this->Internals->ncFile, varid, start_pt, count_pt, this->PointZ + this->PointOffset)))
  {
    return 0;
  }
  // point 0 is 0.0
  this->PointZ[0] = 0.0;

  CHECK_VAR("cellsOnVertex", varid);
  this->OrigConnections = new int[this->NumberOfCells * this->PointsPerCell];
  // TODO Spec says dims should be '3', 'nVertices', but my example files
  // use nVertices, vertexDegree...
  if (!this->Internals->ValidateDimensions(varid, false, 2, "nVertices", "vertexDegree"))
  {
    return 0;
  }
  size_t start_conn[] = { 0, 0 };
  size_t count_conn[] = { this->NumberOfCells, this->PointsPerCell };
  if (this->Internals->nc_err(nc_get_vara_int(
        this->Internals->ncFile, varid, start_conn, count_conn, this->OrigConnections)))
  {
    return 0;
  }

  if ((varid = this->Internals->nc_var_id("maxLevelCell", false)) != -1)
  {
    this->IncludeTopography = true;
    this->MaximumLevelPoint = new int[2 * this->NumberOfPoints];
    if (!this->Internals->ValidateDimensions(varid, false, 1, "nCells"))
    {
      return 0;
    }
    size_t start[] = { 0 };
    size_t count[] = { this->NumberOfPoints };
    if (this->Internals->nc_err(nc_get_vara_int(this->Internals->ncFile, varid, start, count,
          this->MaximumLevelPoint + this->PointOffset)))
    {
      return 0;
    }
  }

  this->CurrentExtraPoint = this->NumberOfPoints + this->PointOffset;
  this->CurrentExtraCell = this->NumberOfCells + this->CellOffset;

  if (this->ShowMultilayerView)
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell * this->MaximumNVertLevels);
    this->MaximumPoints =
      static_cast<int>(this->CurrentExtraPoint * (this->MaximumNVertLevels + 1));
  }
  else
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell);
    this->MaximumPoints = static_cast<int>(this->CurrentExtraPoint);
  }

  return 1;
}

//----------------------------------------------------------------------------
//  Shift data if center longitude needs to change.
//----------------------------------------------------------------------------

void svtkMPASReader::ShiftLonData()
{
  svtkDebugMacro(<< "In ShiftLonData..." << endl);
  // if atmospheric data, or zero centered, set center to 180 instead of 0
  if (IsAtmosphere || IsZeroCentered)
  {
    for (size_t j = this->PointOffset; j < this->NumberOfPoints + this->PointOffset; j++)
    {
      // need to shift over the point so center is at PI
      if (this->PointX[j] < 0)
      {
        this->PointX[j] += 2 * svtkMath::Pi();
      }
    }
  }

  if (CenterLon != 180)
  {
    for (size_t j = this->PointOffset; j < this->NumberOfPoints + this->PointOffset; j++)
    {
      // need to shift over the point if centerLon dictates
      if (this->CenterRad < svtkMath::Pi())
      {
        if (this->PointX[j] > (this->CenterRad + svtkMath::Pi()))
        {
          this->PointX[j] = -((2 * svtkMath::Pi()) - this->PointX[j]);
        }
      }
      else if (this->CenterRad > svtkMath::Pi())
      {
        if (this->PointX[j] < (this->CenterRad - svtkMath::Pi()))
        {
          this->PointX[j] += 2 * svtkMath::Pi();
        }
      }
    }
  }
  svtkDebugMacro(<< "Leaving ShiftLonData..." << endl);
}

//----------------------------------------------------------------------------
//  Add a "mirror point" -- a point on the opposite side of the lat/lon
// projection.
//----------------------------------------------------------------------------

int svtkMPASReader::AddMirrorPoint(int index, double dividerX, double offset)
{
  double X = this->PointX[index];
  double Y = this->PointY[index];

  // add on east
  if (X < dividerX)
  {
    X += offset;
  }
  else
  {
    // add on west
    X -= offset;
  }

  assert(this->CurrentExtraPoint < this->ModNumPoints);
  this->PointX[this->CurrentExtraPoint] = X;
  this->PointY[this->CurrentExtraPoint] = Y;

  size_t mirrorPoint = this->CurrentExtraPoint;

  // record mapping
  *(this->PointMap + (this->CurrentExtraPoint - this->NumberOfPoints - this->PointOffset)) = index;
  this->CurrentExtraPoint++;

  return static_cast<int>(mirrorPoint);
}

//----------------------------------------------------------------------------
// Check for out-of-range values and do bugfix
//----------------------------------------------------------------------------

void svtkMPASReader::FixPoints()
{
  svtkDebugMacro(<< "In FixPoints..." << endl);

  for (size_t j = this->CellOffset; j < this->NumberOfCells + this->CellOffset; j++)
  {
    int* conns = this->OrigConnections + (j * this->PointsPerCell);

    // go through and make sure none of the referenced points are
    // out of range
    // if so, set all to point 0
    for (size_t k = 0; k < this->PointsPerCell; k++)
    {
      if ((conns[k] <= 0) || (static_cast<size_t>(conns[k]) > this->NumberOfPoints))
      {
        for (size_t m = 0; m < this->PointsPerCell; m++)
        {
          conns[m] = 0;
        }
        break;
      }
    }

    if (this->DoBugFix)
    {
      // BUG FIX for problem where cells are stretching to a faraway point
      size_t lastk = this->PointsPerCell - 1;
      const double thresh = .06981317007977; // 4 degrees
      for (size_t k = 0; k < this->PointsPerCell; k++)
      {
        double ydiff = std::abs(this->PointY[conns[k]] - this->PointY[conns[lastk]]);
        // Don't look at cells at map border
        if (ydiff > thresh)
        {
          for (size_t m = 0; m < this->PointsPerCell; m++)
          {
            conns[m] = 0;
          }
          break;
        }
      }
    }
  }
  svtkDebugMacro(<< "Leaving FixPoints..." << endl);
}

//----------------------------------------------------------------------------
// Eliminate wraparound at east/west edges of lat/lon projection
//----------------------------------------------------------------------------

int svtkMPASReader::EliminateXWrap()
{
  if (this->NumberOfPoints == 0)
  {
    return 1;
  }

  double xLength;
  double xCenter;
  switch (this->Geometry)
  {
    case svtkMPASReader::Spherical:
      svtkErrorMacro("EliminateXWrap called for spherical geometry.");
      return 0;

    case svtkMPASReader::Projected:
      xLength = 2 * svtkMath::Pi();
      xCenter = this->CenterRad;
      break;

    case svtkMPASReader::Planar:
    {
      // Determine the bounds in the x-dimension
      double xRange[2] = { this->PointX[this->PointOffset], this->PointX[this->PointOffset] };
      for (size_t i = 1; i < this->NumberOfPoints; ++i)
      {
        double x = this->PointX[this->PointOffset + i];
        xRange[0] = std::min(xRange[0], x);
        xRange[1] = std::max(xRange[1], x);
      }

      xLength = xRange[1] - xRange[0];
      xCenter = (xRange[0] + xRange[1]) * 0.5;
    }
    break;

    default:
      svtkErrorMacro("Unrecognized geometry type (" << this->Geometry << ").");
      return 0;
  }

  const double tolerance = 5.5;

  // For each cell, examine vertices
  // Add new points and cells where needed to account for wraparound.
  for (size_t j = this->CellOffset; j < this->NumberOfCells + this->CellOffset; j++)
  {
    int* conns = this->OrigConnections + (j * this->PointsPerCell);
    int* modConns = this->ModConnections + (j * this->PointsPerCell);

    // Determine if we are wrapping in X direction
    size_t lastk = this->PointsPerCell - 1;
    bool xWrap = false;
    for (size_t k = 0; k < this->PointsPerCell; k++)
    {
      if (std::abs(this->PointX[conns[k]] - this->PointX[conns[lastk]]) > tolerance)
      {
        xWrap = true;
        break;
      }
      lastk = k;
    }

    // If we wrapped in X direction, modify cell and add mirror cell
    if (xWrap)
    {
      // first point is anchor it doesn't move
      double anchorX = this->PointX[conns[0]];
      modConns[0] = conns[0];

      // modify existing cell, so it doesn't wrap
      // move points to one side
      for (size_t k = 1; k < this->PointsPerCell; k++)
      {
        int neigh = conns[k];

        // add a new point, figure out east or west
        if (std::abs(this->PointX[neigh] - anchorX) > tolerance)
        {
          modConns[k] = this->AddMirrorPoint(neigh, anchorX, xLength);
        }
        else
        {
          // use existing kth point
          modConns[k] = neigh;
        }
      }

      // move addedConns to this->ModConnections extra cells area
      int* addedConns = this->ModConnections + (this->CurrentExtraCell * this->PointsPerCell);

      // add a mirroring cell to other side

      // add mirrored anchor first
      addedConns[0] = this->AddMirrorPoint(conns[0], xCenter, xLength);
      anchorX = this->PointX[addedConns[0]];

      // add mirror cell points if needed
      for (size_t k = 1; k < this->PointsPerCell; k++)
      {
        int neigh = conns[k];

        // add a new point for neighbor, figure out east or west
        if (std::abs(this->PointX[neigh] - anchorX) > tolerance)
        {
          addedConns[k] = this->AddMirrorPoint(neigh, anchorX, xLength);
        }
        else
        {
          // use existing kth point
          addedConns[k] = neigh;
        }
      }
      *(this->CellMap + (this->CurrentExtraCell - this->NumberOfCells - this->CellOffset)) = j;
      this->CurrentExtraCell++;
    }
    else
    {

      // just add cell "as is" to this->ModConnections
      for (size_t k = 0; k < this->PointsPerCell; k++)
      {
        modConns[k] = conns[k];
      }
    }
    if (this->CurrentExtraCell > this->ModNumCells)
    {
      svtkErrorMacro(<< "Exceeded storage for extra cells!" << endl);
      return (0);
    }
    if (this->CurrentExtraPoint > this->ModNumPoints)
    {
      svtkErrorMacro(<< "Exceeded storage for extra points!" << endl);
      return (0);
    }
  }

  if (!ShowMultilayerView)
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell);
    this->MaximumPoints = static_cast<int>(this->CurrentExtraPoint);
    svtkDebugMacro(<< "elim xwrap: singlelayer: setting this->MaximumPoints to "
                  << this->MaximumPoints << endl);
  }
  else
  {
    this->MaximumCells = static_cast<int>(this->CurrentExtraCell * this->MaximumNVertLevels);
    this->MaximumPoints =
      static_cast<int>(this->CurrentExtraPoint * (this->MaximumNVertLevels + 1));
    svtkDebugMacro(<< "elim xwrap: multilayer: setting this->MaximumPoints to "
                  << this->MaximumPoints << endl);
  }

  return 1;
}

//----------------------------------------------------------------------------
//  Add points to svtk data structures
//----------------------------------------------------------------------------

void svtkMPASReader::OutputPoints()
{
  svtkUnstructuredGrid* output = this->GetOutput();

  double adjustedLayerThickness = this->IsAtmosphere ? static_cast<double>(-this->LayerThickness)
                                                     : static_cast<double>(this->LayerThickness);

  svtkSmartPointer<svtkPoints> points = svtkSmartPointer<svtkPoints>::New();
  points->Allocate(this->MaximumPoints);
  output->SetPoints(points);

  for (size_t j = 0; j < this->CurrentExtraPoint; j++)
  {
    double x, y, z;

    switch (this->Geometry)
    {
      case svtkMPASReader::Planar:
      case svtkMPASReader::Spherical:
        x = this->PointX[j];
        y = this->PointY[j];
        z = this->PointZ[j];
        break;

      case svtkMPASReader::Projected:
        x = this->PointX[j] * 180.0 / svtkMath::Pi();
        y = this->PointY[j] * 180.0 / svtkMath::Pi();
        z = 0.0;
        break;

      default:
        svtkErrorMacro("Unrecognized geometry type (" << this->Geometry << ").");
        return;
    }

    if (!this->ShowMultilayerView)
    {
      points->InsertNextPoint(x, y, z);
    }
    else
    {
      double rho = 0.0, rholevel = 0.0, theta = 0.0, phi = 0.0;
      int retval = -1;

      if (this->Geometry == Spherical)
      {
        if ((x != 0.0) || (y != 0.0) || (z != 0.0))
        {
          retval = CartesianToSpherical(x, y, z, &rho, &phi, &theta);
          if (retval)
          {
            svtkWarningMacro("Can't create point for layered view.");
          }
        }
      }

      for (size_t levelNum = 0; levelNum < this->MaximumNVertLevels + 1; levelNum++)
      {
        if (this->Geometry == Spherical)
        {
          if (!retval && ((x != 0.0) || (y != 0.0) || (z != 0.0)))
          {
            rholevel = rho - (adjustedLayerThickness * levelNum);
            retval = SphericalToCartesian(rholevel, phi, theta, &x, &y, &z);
            if (retval)
            {
              svtkWarningMacro("Can't create point for layered view.");
            }
          }
        }
        else
        {
          z = levelNum * -adjustedLayerThickness;
        }
        points->InsertNextPoint(x, y, z);
      }
    }
  }

  if (this->PointX)
  {
    delete[] this->PointX;
    this->PointX = nullptr;
  }
  if (this->PointY)
  {
    delete[] this->PointY;
    this->PointY = nullptr;
  }
  if (this->PointZ)
  {
    delete[] this->PointZ;
    this->PointZ = nullptr;
  }
}

//----------------------------------------------------------------------------
// Determine if cell is one of SVTK_TRIANGLE, SVTK_WEDGE, SVTK_QUAD or
// SVTK_HEXAHEDRON
//----------------------------------------------------------------------------

unsigned char svtkMPASReader::GetCellType()
{
  // write cell types
  unsigned char cellType = SVTK_TRIANGLE;
  switch (this->PointsPerCell)
  {
    case 3:
      if (!ShowMultilayerView)
      {
        cellType = SVTK_TRIANGLE;
      }
      else
      {
        cellType = SVTK_WEDGE;
      }
      break;
    case 4:
      if (!ShowMultilayerView)
      {
        cellType = SVTK_QUAD;
      }
      else
      {
        cellType = SVTK_HEXAHEDRON;
      }
      break;
    default:
      break;
  }
  return cellType;
}

//----------------------------------------------------------------------------
//  Add cells to svtk data structures
//----------------------------------------------------------------------------

void svtkMPASReader::OutputCells()
{
  svtkDebugMacro(<< "In OutputCells..." << endl);
  svtkUnstructuredGrid* output = GetOutput();

  output->Allocate(this->MaximumCells, this->MaximumCells);

  int cellType = GetCellType();
  size_t val;

  size_t pointsPerPolygon;
  if (this->ShowMultilayerView)
  {
    pointsPerPolygon = 2 * this->PointsPerCell;
  }
  else
  {
    pointsPerPolygon = this->PointsPerCell;
  }

  svtkDebugMacro(<< "OutputCells: this->MaximumCells: " << this->MaximumCells << " cellType: "
                << cellType << " this->MaximumNVertLevels: " << this->MaximumNVertLevels
                << " LayerThickness: " << LayerThickness << " ProjectLatLon: " << ProjectLatLon
                << " ShowMultilayerView: " << ShowMultilayerView);

  std::vector<svtkIdType> polygon(pointsPerPolygon);

  for (size_t j = 0; j < this->CurrentExtraCell; j++)
  {

    int* conns;
    if (this->Geometry == Projected)
    {
      conns = this->ModConnections + (j * this->PointsPerCell);
    }
    else
    {
      conns = this->OrigConnections + (j * this->PointsPerCell);
    }

    int minLevel = 0;

    if (this->IncludeTopography)
    {
      int* connections;

      // check if it is a mirror cell, if so, get original
      if (static_cast<size_t>(j) >= this->NumberOfCells + this->CellOffset)
      {
        size_t origCellNum = *(this->CellMap + (j - this->NumberOfCells - this->CellOffset));
        connections = this->OrigConnections + (origCellNum * this->PointsPerCell);
      }
      else
      {
        connections = this->OrigConnections + (j * this->PointsPerCell);
      }

      minLevel = this->MaximumLevelPoint[connections[0]];

      // Take the min of the this->MaximumLevelPoint of each point
      for (size_t k = 1; k < this->PointsPerCell; k++)
      {
        minLevel = std::min(minLevel, this->MaximumLevelPoint[connections[k]]);
      }
    }

    // singlelayer
    if (!this->ShowMultilayerView)
    {
      // If that min is greater than or equal to this output level,
      // include the cell, otherwise set all points to zero.

      if (this->IncludeTopography && ((minLevel - 1) < this->GetVerticalLevel()))
      {
        // cerr << "Setting all points to zero" << endl;
        val = 0;
        for (size_t k = 0; k < this->PointsPerCell; k++)
        {
          polygon[k] = static_cast<svtkIdType>(val);
        }
      }
      else
      {
        for (size_t k = 0; k < this->PointsPerCell; k++)
        {
          polygon[k] = conns[k];
        }
      }
      output->InsertNextCell(cellType, static_cast<svtkIdType>(pointsPerPolygon), &polygon[0]);
    }
    else
    { // multilayer
      // for each level, write the cell
      for (size_t levelNum = 0; levelNum < this->MaximumNVertLevels; levelNum++)
      {
        if (this->IncludeTopography && (static_cast<size_t>(minLevel - 1) < levelNum))
        {
          // setting all points to zero
          val = 0;
          for (size_t k = 0; k < pointsPerPolygon; k++)
          {
            polygon[k] = static_cast<svtkIdType>(val);
          }
        }
        else
        {
          for (size_t k = 0; k < this->PointsPerCell; k++)
          {
            val = (conns[k] * (this->MaximumNVertLevels + 1)) + levelNum;
            polygon[k] = static_cast<svtkIdType>(val);
          }

          for (size_t k = 0; k < this->PointsPerCell; k++)
          {
            val = (conns[k] * (this->MaximumNVertLevels + 1)) + levelNum + 1;
            polygon[k + this->PointsPerCell] = static_cast<svtkIdType>(val);
          }
        }
        // svtkDebugMacro
        //("InsertingCell j: " << j << " level: " << levelNum << endl);
        output->InsertNextCell(cellType, static_cast<svtkIdType>(pointsPerPolygon), &polygon[0]);
      }
    }
  }

  delete[] this->ModConnections;
  this->ModConnections = nullptr;
  delete[] this->OrigConnections;
  this->OrigConnections = nullptr;

  svtkDebugMacro(<< "Leaving OutputCells..." << endl);
}

//----------------------------------------------------------------------------
//  Load the data for a point variable
//----------------------------------------------------------------------------
svtkDataArray* svtkMPASReader::LoadPointVarData(int variableIndex)
{
  int varid = this->Internals->pointVars[variableIndex];
  char varname[NC_MAX_NAME + 1];
  if (this->Internals->nc_err(nc_inq_varname(this->Internals->ncFile, varid, varname)))
  {
    svtkErrorMacro(<< "No NetCDF data for pointVar @ index " << variableIndex);
    return nullptr;
  }

  svtkDebugMacro(<< "Loading point data array named: " << varname);

  // Get data type:
  nc_type typeNc;
  if (this->Internals->nc_err(nc_inq_vartype(this->Internals->ncFile, varid, &typeNc)))
  {
    return nullptr;
  }
  int typeVtk = NcTypeToVtkType(typeNc);

  // Allocate data array pointer for this variable:
  svtkSmartPointer<svtkDataArray> array = this->LookupPointDataArray(variableIndex);
  if (array == nullptr)
  {
    svtkDebugMacro(<< "Allocating data array.");
    array = svtkSmartPointer<svtkDataArray>::Take(svtkDataArray::CreateDataArray(typeVtk));
  }
  array->SetName(varname);
  array->SetNumberOfComponents(1);
  array->SetNumberOfTuples(this->MaximumPoints);

  int success = false;
  svtkNcDispatch(typeVtk, success = this->Internals->LoadPointVarDataImpl<SVTK_TT>(varid, array););

  if (success)
  {
    this->Internals->pointArrays[variableIndex] = array;
    return array;
  }
  return nullptr;
}

//----------------------------------------------------------------------------
//  Load the data for a cell variable
//----------------------------------------------------------------------------

svtkDataArray* svtkMPASReader::LoadCellVarData(int variableIndex)
{
  int varid = this->Internals->cellVars[variableIndex];
  char varname[NC_MAX_NAME + 1];
  if (this->Internals->nc_err(nc_inq_varname(this->Internals->ncFile, varid, varname)))
  {
    svtkErrorMacro(<< "No NetCDF data for cellVar @ index " << variableIndex);
    return nullptr;
  }

  svtkDebugMacro(<< "Loading cell data array named: " << varname);

  // Get data type:
  nc_type typeNc;
  if (this->Internals->nc_err(nc_inq_vartype(this->Internals->ncFile, varid, &typeNc)))
  {
    return nullptr;
  }
  int typeVtk = NcTypeToVtkType(typeNc);

  // Allocate data array pointer for this variable:
  svtkSmartPointer<svtkDataArray> array = this->LookupCellDataArray(variableIndex);
  if (array == nullptr)
  {
    svtkDebugMacro(<< "Allocating data array.");
    array = svtkSmartPointer<svtkDataArray>::Take(svtkDataArray::CreateDataArray(typeVtk));
  }
  array->SetName(varname);
  array->SetNumberOfComponents(1);
  array->SetNumberOfTuples(this->MaximumCells);

  int success = false;
  svtkNcDispatch(typeVtk, success = this->Internals->LoadCellVarDataImpl<SVTK_TT>(varid, array););
  if (success)
  {
    this->Internals->cellArrays[variableIndex] = array;
    return array;
  }
  return nullptr;
}

//------------------------------------------------------------------------------
svtkDataArray* svtkMPASReader::LookupPointDataArray(int varIdx)
{
  Internal::ArrayMap::iterator it = this->Internals->pointArrays.find(varIdx);
  return it != this->Internals->pointArrays.end() ? it->second : nullptr;
}

//------------------------------------------------------------------------------
svtkDataArray* svtkMPASReader::LookupCellDataArray(int varIdx)
{
  Internal::ArrayMap::iterator it = this->Internals->cellArrays.find(varIdx);
  return it != this->Internals->cellArrays.end() ? it->second : nullptr;
}

//------------------------------------------------------------------------------
void svtkMPASReader::LoadTimeFieldData(svtkUnstructuredGrid* dataset)
{
  svtkStringArray* array = nullptr;
  svtkFieldData* fd = dataset->GetFieldData();
  if (!fd)
  {
    fd = svtkFieldData::New();
    dataset->SetFieldData(fd);
    fd->Delete();
  }

  if (svtkDataArray* da = fd->GetArray("Time"))
  {
    if (!(array = svtkArrayDownCast<svtkStringArray>(da)))
    {
      svtkWarningMacro("Not creating \"Time\" field data array: a data array "
                      "with this name already exists.");
      return;
    }
  }

  if (!array)
  {
    array = svtkStringArray::New();
    array->SetName("Time");
    fd->AddArray(array);
    array->Delete();
  }

  // If the xtime variable exists, use its value at the current timestep:
  std::string time;
  int varid;
  if ((varid = this->Internals->nc_var_id("xtime", false)) != -1)
  {
    if (this->Internals->ValidateDimensions(varid, false, 2, "Time", "StrLen"))
    {
      int dimid = this->Internals->nc_dim_id("StrLen");
      assert(dimid != -1);
      size_t strLen = 0;
      this->Internals->nc_err(nc_inq_dimlen(this->Internals->ncFile, dimid, &strLen));
      if (strLen > 0)
      {
        time.resize(strLen);
        size_t start[] = { this->Internals->GetCursorForDimension(dimid), 0 };
        size_t count[] = { 1, strLen };
        if (this->Internals->nc_err(
              nc_get_vara_text(this->Internals->ncFile, varid, start, count, &time[0])))
        {
          // Trim off trailing whitespace:
          size_t realLength = time.find_last_not_of(' ');
          if (realLength != svtkStdString::npos)
          {
            time.resize(realLength + 1);
          }
        }
        else
        {
          svtkWarningMacro("Error reading xtime variable from file.");
          time.clear();
        }
      }
    }
  }

  // If no string time is available or the read fails, just insert the timestep:
  if (time.empty())
  {
    std::ostringstream timeStr;
    timeStr << "Timestep " << std::floor(this->DTime) << "/" << this->NumberOfTimeSteps;
    time = timeStr.str();
  }

  assert(array != nullptr);
  array->SetNumberOfComponents(1);
  array->SetNumberOfTuples(1);
  array->SetValue(0, time);
}

//----------------------------------------------------------------------------
//  Callback if the user selects a variable.
//----------------------------------------------------------------------------

void svtkMPASReader::SelectionCallback(
  svtkObject*, unsigned long svtkNotUsed(eventid), void* clientdata, void* svtkNotUsed(calldata))
{
  static_cast<svtkMPASReader*>(clientdata)->Modified();
}

//----------------------------------------------------------------------------
void svtkMPASReader::UpdateDimensions(bool force)
{
  if (!force && this->Internals->dimMetaDataTime < this->Internals->extraDimTime)
  {
    return;
  }

  this->Internals->extraDims->Reset();

  if (!this->Internals->ncFile)
  {
    this->Internals->extraDimTime.Modified();
    return;
  }

  std::set<std::string> dimSet;

  typedef Internal::DimMetaDataMap::const_iterator Iter;
  const Internal::DimMetaDataMap& map = this->Internals->dimMetaDataMap;
  for (Iter it = map.begin(), itEnd = map.end(); it != itEnd; ++it)
  {
    if (this->Internals->isExtraDim(it->first))
    {
      dimSet.insert(it->first);
    }
  }

  typedef std::set<std::string>::const_iterator SetIter;
  this->Internals->extraDims->Allocate(static_cast<svtkIdType>(dimSet.size()));
  for (SetIter it = dimSet.begin(), itEnd = dimSet.end(); it != itEnd; ++it)
  {
    this->Internals->extraDims->InsertNextValue(it->c_str());
  }

  this->Internals->extraDimTime.Modified();
}

//----------------------------------------------------------------------------
//  Return the output.
//----------------------------------------------------------------------------

svtkUnstructuredGrid* svtkMPASReader::GetOutput()
{
  return this->GetOutput(0);
}

//----------------------------------------------------------------------------
//  Returns the output given an id.
//----------------------------------------------------------------------------

svtkUnstructuredGrid* svtkMPASReader::GetOutput(int idx)
{
  if (idx)
  {
    return nullptr;
  }
  else
  {
    return svtkUnstructuredGrid::SafeDownCast(this->GetOutputDataObject(idx));
  }
}

//----------------------------------------------------------------------------
//  Get number of point arrays.
//----------------------------------------------------------------------------

int svtkMPASReader::GetNumberOfPointArrays()
{
  return this->PointDataArraySelection->GetNumberOfArrays();
}

//----------------------------------------------------------------------------
// Get number of cell arrays.
//----------------------------------------------------------------------------

int svtkMPASReader::GetNumberOfCellArrays()
{
  return this->CellDataArraySelection->GetNumberOfArrays();
}

//----------------------------------------------------------------------------
// Make all point selections available.
//----------------------------------------------------------------------------
void svtkMPASReader::EnableAllPointArrays()
{
  this->PointDataArraySelection->EnableAllArrays();
}

//----------------------------------------------------------------------------
// Make all point selections unavailable.
//----------------------------------------------------------------------------

void svtkMPASReader::DisableAllPointArrays()
{
  this->PointDataArraySelection->DisableAllArrays();
}

//----------------------------------------------------------------------------
// Make all cell selections available.
//----------------------------------------------------------------------------

void svtkMPASReader::EnableAllCellArrays()
{
  this->CellDataArraySelection->EnableAllArrays();
}

//----------------------------------------------------------------------------
// Make all cell selections unavailable.
//----------------------------------------------------------------------------

void svtkMPASReader::DisableAllCellArrays()
{
  this->CellDataArraySelection->DisableAllArrays();
}

//----------------------------------------------------------------------------
// Get name of indexed point variable
//----------------------------------------------------------------------------

const char* svtkMPASReader::GetPointArrayName(int index)
{
  return this->PointDataArraySelection->GetArrayName(index);
}

//----------------------------------------------------------------------------
// Get status of named point variable selection
//----------------------------------------------------------------------------

int svtkMPASReader::GetPointArrayStatus(const char* name)
{
  return this->PointDataArraySelection->ArrayIsEnabled(name);
}

//----------------------------------------------------------------------------
// Set status of named point variable selection.
//----------------------------------------------------------------------------

void svtkMPASReader::SetPointArrayStatus(const char* name, int status)
{
  if (status)
  {
    this->PointDataArraySelection->EnableArray(name);
  }
  else
  {
    this->PointDataArraySelection->DisableArray(name);
  }
}

//----------------------------------------------------------------------------
// Get name of indexed cell variable
//----------------------------------------------------------------------------

const char* svtkMPASReader::GetCellArrayName(int index)
{
  return this->CellDataArraySelection->GetArrayName(index);
}

//----------------------------------------------------------------------------
// Get status of named cell variable selection.
//----------------------------------------------------------------------------

int svtkMPASReader::GetCellArrayStatus(const char* name)
{
  return this->CellDataArraySelection->ArrayIsEnabled(name);
}

//----------------------------------------------------------------------------
// Set status of named cell variable selection.
//----------------------------------------------------------------------------

void svtkMPASReader::SetCellArrayStatus(const char* name, int status)
{
  if (status)
  {
    this->CellDataArraySelection->EnableArray(name);
  }
  else
  {
    this->CellDataArraySelection->DisableArray(name);
  }
}

//----------------------------------------------------------------------------
svtkIdType svtkMPASReader::GetNumberOfDimensions()
{
  this->UpdateDimensions();
  return this->Internals->extraDims->GetNumberOfTuples();
}

//----------------------------------------------------------------------------
std::string svtkMPASReader::GetDimensionName(int idx)
{
  this->UpdateDimensions();
  return this->Internals->extraDims->GetValue(idx);
}

//----------------------------------------------------------------------------
svtkStringArray* svtkMPASReader::GetAllDimensions()
{
  this->UpdateDimensions();
  return this->Internals->extraDims;
}

//----------------------------------------------------------------------------
int svtkMPASReader::GetDimensionCurrentIndex(const std::string& dim)
{
  this->UpdateDimensions();

  typedef Internal::DimMetaDataMap::const_iterator Iter;
  Iter it = this->Internals->dimMetaDataMap.find(dim);
  if (it == this->Internals->dimMetaDataMap.end())
  {
    return -1;
  }
  return static_cast<int>(it->second.curIdx);
}

//----------------------------------------------------------------------------
void svtkMPASReader::SetDimensionCurrentIndex(const std::string& dim, int idx)
{
  this->UpdateDimensions();

  typedef Internal::DimMetaDataMap::iterator Iter;
  Iter it = this->Internals->dimMetaDataMap.find(dim);
  if (it != this->Internals->dimMetaDataMap.end() && static_cast<size_t>(idx) < it->second.dimSize)
  {
    it->second.curIdx = idx;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
int svtkMPASReader::GetDimensionSize(const std::string& dim)
{
  this->UpdateDimensions();

  typedef Internal::DimMetaDataMap::const_iterator Iter;
  Iter it = this->Internals->dimMetaDataMap.find(dim);
  if (it == this->Internals->dimMetaDataMap.end())
  {
    return -1;
  }
  return static_cast<int>(it->second.dimSize);
}

//----------------------------------------------------------------------------
//  Set vertical level to be viewed.
//----------------------------------------------------------------------------

void svtkMPASReader::SetVerticalLevel(int level)
{
  this->SetDimensionCurrentIndex(this->VerticalDimension, level);
}

//------------------------------------------------------------------------------
int svtkMPASReader::GetVerticalLevel()
{
  return this->GetDimensionCurrentIndex(this->VerticalDimension);
}

//----------------------------------------------------------------------------
//  Set center longitude for lat/lon projection
//----------------------------------------------------------------------------

void svtkMPASReader::SetCenterLon(int val)
{
  svtkDebugMacro(<< "SetCenterLon: is " << this->CenterLon << endl);
  if (CenterLon != val)
  {
    this->CenterLon = val;
    this->CenterRad = val * svtkMath::Pi() / 180.0;
    this->Modified();

    svtkDebugMacro(<< "SetCenterLon: set to " << this->CenterLon << endl);
    svtkDebugMacro(<< "CenterRad set to " << this->CenterRad << endl);
  }
}

//----------------------------------------------------------------------------
//  Determine if this reader can read the given file (if it is an MPAS format)
// NetCDF file
//----------------------------------------------------------------------------

int svtkMPASReader::CanReadFile(const char* filename)
{
  Internal* internals = new Internal(nullptr);
  if (!internals->open(filename))
  {
    delete internals;
    return 0;
  }
  bool ret = true;
  ret &= (internals->nc_dim_id("nCells") != -1);
  ret &= (internals->nc_dim_id("nVertices") != -1);
  ret &= (internals->nc_dim_id("vertexDegree") != -1);
  ret &= (internals->nc_dim_id("Time") != -1);
  delete internals;
  return ret;
}

//------------------------------------------------------------------------------
svtkMTimeType svtkMPASReader::GetMTime()
{
  svtkMTimeType result = this->Superclass::GetMTime();
  result = std::max(result, this->CellDataArraySelection->GetMTime());
  result = std::max(result, this->PointDataArraySelection->GetMTime());
  // Excluded, as this just manages a cache:
  //  result = std::max(result, this->Internals->extraDimTime.GetMTime());
  result = std::max(result, this->Internals->dimMetaDataTime.GetMTime());
  return result;
}

//----------------------------------------------------------------------------
//  Print self.
//----------------------------------------------------------------------------

void svtkMPASReader::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FileName: " << (this->FileName ? this->FileName : "nullptr") << "\n";
  os << indent << "VerticalLevelRange: " << this->VerticalLevelRange[0] << ","
     << this->VerticalLevelRange[1] << "\n";
  os << indent << "this->MaximumPoints: " << this->MaximumPoints << "\n";
  os << indent << "this->MaximumCells: " << this->MaximumCells << "\n";
  os << indent << "ProjectLatLon: " << (this->ProjectLatLon ? "ON" : "OFF") << endl;
  os << indent << "OnASphere: " << (this->OnASphere ? "ON" : "OFF") << endl;
  os << indent << "ShowMultilayerView: " << (this->ShowMultilayerView ? "ON" : "OFF") << endl;
  os << indent << "CenterLonRange: " << this->CenterLonRange[0] << "," << this->CenterLonRange[1]
     << endl;
  os << indent << "IsAtmosphere: " << (this->IsAtmosphere ? "ON" : "OFF") << endl;
  os << indent << "IsZeroCentered: " << (this->IsZeroCentered ? "ON" : "OFF") << endl;
  os << indent << "LayerThicknessRange: " << this->LayerThicknessRange[0] << ","
     << this->LayerThicknessRange[1] << endl;
}

//------------------------------------------------------------------------------
int svtkMPASReader::GetNumberOfCellVars()
{
  return static_cast<int>(this->Internals->cellVars.size());
}

//------------------------------------------------------------------------------
int svtkMPASReader::GetNumberOfPointVars()
{
  return static_cast<int>(this->Internals->pointVars.size());
}
