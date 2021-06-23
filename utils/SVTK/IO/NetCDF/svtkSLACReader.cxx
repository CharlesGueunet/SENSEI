// -*- c++ -*-
/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkSLACReader.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

#include "svtkSLACReader.h"

#include "svtkCallbackCommand.h"
#include "svtkCellArray.h"
#include "svtkCompositeDataIterator.h"
#include "svtkDataArraySelection.h"
#include "svtkDoubleArray.h"
#include "svtkIdTypeArray.h"
#include "svtkInformation.h"
#include "svtkInformationIntegerKey.h"
#include "svtkInformationObjectBaseKey.h"
#include "svtkInformationVector.h"
#include "svtkMath.h"
#include "svtkMultiBlockDataSet.h"
#include "svtkNew.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPoints.h"
#include "svtkStdString.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkUnsignedCharArray.h"
#include "svtkUnstructuredGrid.h"

#include "svtkSmartPointer.h"
#define SVTK_CREATE(type, name) svtkSmartPointer<type> name = svtkSmartPointer<type>::New()

#include "svtk_netcdf.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include <svtksys/RegularExpression.hxx>

//=============================================================================
#define CALL_NETCDF(call)                                                                          \
  {                                                                                                \
    int errorcode = call;                                                                          \
    if (errorcode != NC_NOERR)                                                                     \
    {                                                                                              \
      svtkErrorMacro(<< "netCDF Error: " << nc_strerror(errorcode));                                \
      return 0;                                                                                    \
    }                                                                                              \
  }

#define WRAP_NETCDF(call)                                                                          \
  {                                                                                                \
    int errorcode = call;                                                                          \
    if (errorcode != NC_NOERR)                                                                     \
      return errorcode;                                                                            \
  }

//-----------------------------------------------------------------------------
#ifdef SVTK_USE_64BIT_IDS
//#ifdef NC_INT64
//// This may or may not work with the netCDF 4 library reading in netCDF 3 files.
//#define nc_get_var_svtkIdType nc_get_var_longlong
//#define nc_get_vars_svtkIdType nc_get_vars_longlong
//#else // NC_INT64
static int nc_get_var_svtkIdType(int ncid, int varid, svtkIdType* ip)
{
  // Step 1, figure out how many entries in the given variable.
  int numdims, dimids[NC_MAX_VAR_DIMS];
  WRAP_NETCDF(nc_inq_varndims(ncid, varid, &numdims));
  WRAP_NETCDF(nc_inq_vardimid(ncid, varid, dimids));
  svtkIdType numValues = 1;
  for (int dim = 0; dim < numdims; dim++)
  {
    size_t dimlen;
    WRAP_NETCDF(nc_inq_dimlen(ncid, dimids[dim], &dimlen));
    numValues *= dimlen;
  }

  // Step 2, read the data in as 32 bit integers.  Recast the input buffer
  // so we do not have to create a new one.
  long* smallIp = reinterpret_cast<long*>(ip);
  WRAP_NETCDF(nc_get_var_long(ncid, varid, smallIp));

  // Step 3, recast the data from 32 bit integers to 64 bit integers.  Since we
  // are storing both in the same buffer, we need to be careful to not overwrite
  // uncopied 32 bit numbers with 64 bit numbers.  We can do that by copying
  // backwards.
  for (svtkIdType i = numValues - 1; i >= 0; i--)
  {
    ip[i] = static_cast<svtkIdType>(smallIp[i]);
  }

  return NC_NOERR;
}
static int nc_get_vars_svtkIdType(int ncid, int varid, const size_t start[], const size_t count[],
  const ptrdiff_t stride[], svtkIdType* ip)
{
  // Step 1, figure out how many entries in the given variable.
  int numdims;
  WRAP_NETCDF(nc_inq_varndims(ncid, varid, &numdims));
  svtkIdType numValues = 1;
  for (int dim = 0; dim < numdims; dim++)
  {
    numValues *= count[dim];
  }

  // Step 2, read the data in as 32 bit integers.  Recast the input buffer
  // so we do not have to create a new one.
  long* smallIp = reinterpret_cast<long*>(ip);
  WRAP_NETCDF(nc_get_vars_long(ncid, varid, start, count, stride, smallIp));

  // Step 3, recast the data from 32 bit integers to 64 bit integers.  Since we
  // are storing both in the same buffer, we need to be careful to not overwrite
  // uncopied 32 bit numbers with 64 bit numbers.  We can do that by copying
  // backwards.
  for (svtkIdType i = numValues - 1; i >= 0; i--)
  {
    ip[i] = static_cast<svtkIdType>(smallIp[i]);
  }

  return NC_NOERR;
}
//#endif // NC_INT64
#else // SVTK_USE_64_BIT_IDS
#define nc_get_var_svtkIdType nc_get_var_int
#define nc_get_vars_svtkIdType nc_get_vars_int
#endif // SVTK_USE_64BIT_IDS

//-----------------------------------------------------------------------------
// This convenience function gets a scalar variable as a double, doing the
// appropriate checks.
static int nc_get_scalar_double(int ncid, const char* name, double* dp)
{
  int varid;
  WRAP_NETCDF(nc_inq_varid(ncid, name, &varid));
  int numdims;
  WRAP_NETCDF(nc_inq_varndims(ncid, varid, &numdims));
  if (numdims != 0)
  {
    // Not a great error to return, but better than nothing.
    return NC_EVARSIZE;
  }
  WRAP_NETCDF(nc_get_var_double(ncid, varid, dp));

  return NC_NOERR;
}

//=============================================================================
// Describes how faces are defined in a tetrahedra in the files.
const int tetFaces[4][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 0, 1, 3 }, { 1, 2, 3 } };

// Describes the points on each edge of a SVTK triangle.  The edges are in the
// same order as the midpoints are defined in a SVTK quadratic triangle.
const int triEdges[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };

//=============================================================================
static int NetCDFTypeToSVTKType(nc_type type)
{
  switch (type)
  {
    case NC_BYTE:
      return SVTK_UNSIGNED_CHAR;
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
    default:
      svtkGenericWarningMacro(<< "Unknown netCDF variable type " << type);
      return -1;
  }
}

//=============================================================================
// This class automatically closes a netCDF file descripter when it goes out
// of scope.  This allows us to exit on error without having to close the
// file at every instance.
class svtkSLACReaderAutoCloseNetCDF
{
public:
  svtkSLACReaderAutoCloseNetCDF()
  {
    this->FileDescriptor = -1;
    this->ReferenceCount = new int;
    *this->ReferenceCount = 1;
  }

  svtkSLACReaderAutoCloseNetCDF(const char* filename, int omode, bool quiet = false)
  {
    int errorcode = nc_open(filename, omode, &this->FileDescriptor);
    if (errorcode != NC_NOERR)
    {
      if (!quiet)
      {
        svtkGenericWarningMacro(<< "Could not open " << filename << endl << nc_strerror(errorcode));
      }
      this->FileDescriptor = -1;
    }

    // Reference count is maintained regardless of validity of FileDescriptor.
    this->ReferenceCount = new int;
    *this->ReferenceCount = 1;
  }

  ~svtkSLACReaderAutoCloseNetCDF() { this->UnReference(); }

  svtkSLACReaderAutoCloseNetCDF(const svtkSLACReaderAutoCloseNetCDF& src)
  {
    this->FileDescriptor = src.FileDescriptor;
    this->ReferenceCount = src.ReferenceCount;
    (*this->ReferenceCount)++;
  }

  svtkSLACReaderAutoCloseNetCDF& operator=(const svtkSLACReaderAutoCloseNetCDF& src)
  {
    this->UnReference();
    this->FileDescriptor = src.FileDescriptor;
    this->ReferenceCount = src.ReferenceCount;
    (*this->ReferenceCount)++;
    return *this;
  }

  operator int() const { return this->FileDescriptor; }

  bool Valid() const { return this->FileDescriptor != -1; }

protected:
  int FileDescriptor;
  int* ReferenceCount;

  void UnReference()
  {
    assert(*this->ReferenceCount > 0);
    (*this->ReferenceCount)--;
    if (*this->ReferenceCount < 1)
    {
      if (this->FileDescriptor != -1)
      {
        nc_close(this->FileDescriptor);
      }
      delete this->ReferenceCount;
      this->ReferenceCount = nullptr;
    }
  }
};

//=============================================================================
// A convenience function that gets a block from a multiblock data set,
// performing allocation if necessary.
static svtkUnstructuredGrid* AllocateGetBlock(
  svtkMultiBlockDataSet* blocks, unsigned int blockno, svtkInformationIntegerKey* typeKey)
{
  if (blockno > 1000)
  {
    svtkGenericWarningMacro(<< "Unexpected block number: " << blockno);
    blockno = 0;
  }

  if (blocks->GetNumberOfBlocks() <= blockno)
  {
    blocks->SetNumberOfBlocks(blockno + 1);
  }

  svtkUnstructuredGrid* grid = svtkUnstructuredGrid::SafeDownCast(blocks->GetBlock(blockno));
  if (!grid)
  {
    grid = svtkUnstructuredGrid::New();
    blocks->SetBlock(blockno, grid);
    blocks->GetMetaData(blockno)->Set(typeKey, 1);
    grid->Delete(); // Not really deleted.
  }

  return grid;
}

//=============================================================================
// Classes for storing midpoint maps.  These are basically wrappers around STL
// maps.

// I originally had this placed inside of the svtkSLACReader declaration, where
// it makes much more sense.  However, MSVC6 seems to have a problem with this.
struct svtkSLACReaderEdgeEndpointsHash
{
public:
  size_t operator()(const svtkSLACReader::EdgeEndpoints& edge) const
  {
    return static_cast<size_t>(edge.GetMinEndPoint() + edge.GetMaxEndPoint());
  }
};

//-----------------------------------------------------------------------------
class svtkSLACReader::MidpointCoordinateMap::svtkInternal
{
public:
  typedef std::unordered_map<svtkSLACReader::EdgeEndpoints, svtkSLACReader::MidpointCoordinates,
    svtkSLACReaderEdgeEndpointsHash>
    MapType;
  MapType Map;
};

svtkSLACReader::MidpointCoordinateMap::MidpointCoordinateMap()
{
  this->Internal = new svtkSLACReader::MidpointCoordinateMap::svtkInternal;
}

svtkSLACReader::MidpointCoordinateMap::~MidpointCoordinateMap()
{
  delete this->Internal;
}

void svtkSLACReader::MidpointCoordinateMap::AddMidpoint(
  const EdgeEndpoints& edge, const MidpointCoordinates& midpoint)
{
  this->Internal->Map[edge] = midpoint;
}

void svtkSLACReader::MidpointCoordinateMap::RemoveMidpoint(const EdgeEndpoints& edge)
{
  svtkInternal::MapType::iterator iter = this->Internal->Map.find(edge);
  if (iter != this->Internal->Map.end())
  {
    this->Internal->Map.erase(iter);
  }
}

void svtkSLACReader::MidpointCoordinateMap::RemoveAllMidpoints()
{
  this->Internal->Map.clear();
}

svtkIdType svtkSLACReader::MidpointCoordinateMap::GetNumberOfMidpoints() const
{
  return static_cast<svtkIdType>(this->Internal->Map.size());
}

svtkSLACReader::MidpointCoordinates* svtkSLACReader::MidpointCoordinateMap::FindMidpoint(
  const EdgeEndpoints& edge)
{
  svtkInternal::MapType::iterator iter = this->Internal->Map.find(edge);
  if (iter != this->Internal->Map.end())
  {
    return &iter->second;
  }
  else
  {
    return nullptr;
  }
}

//-----------------------------------------------------------------------------
class svtkSLACReader::MidpointIdMap::svtkInternal
{
public:
  typedef std::unordered_map<svtkSLACReader::EdgeEndpoints, svtkIdType,
    svtkSLACReaderEdgeEndpointsHash>
    MapType;
  MapType Map;
  MapType::iterator Iterator;
};

svtkSLACReader::MidpointIdMap::MidpointIdMap()
{
  this->Internal = new svtkSLACReader::MidpointIdMap::svtkInternal;
}

svtkSLACReader::MidpointIdMap::~MidpointIdMap()
{
  delete this->Internal;
}

void svtkSLACReader::MidpointIdMap::AddMidpoint(const EdgeEndpoints& edge, svtkIdType midpoint)
{
  this->Internal->Map[edge] = midpoint;
}

void svtkSLACReader::MidpointIdMap::RemoveMidpoint(const EdgeEndpoints& edge)
{
  svtkInternal::MapType::iterator iter = this->Internal->Map.find(edge);
  if (iter != this->Internal->Map.end())
  {
    this->Internal->Map.erase(iter);
  }
}

void svtkSLACReader::MidpointIdMap::RemoveAllMidpoints()
{
  this->Internal->Map.clear();
}

svtkIdType svtkSLACReader::MidpointIdMap::GetNumberOfMidpoints() const
{
  return static_cast<svtkIdType>(this->Internal->Map.size());
}

svtkIdType* svtkSLACReader::MidpointIdMap::FindMidpoint(const EdgeEndpoints& edge)
{
  svtkInternal::MapType::iterator iter = this->Internal->Map.find(edge);
  if (iter != this->Internal->Map.end())
  {
    return &iter->second;
  }
  else
  {
    return nullptr;
  }
}

void svtkSLACReader::MidpointIdMap::InitTraversal()
{
  this->Internal->Iterator = this->Internal->Map.begin();
}

bool svtkSLACReader::MidpointIdMap::GetNextMidpoint(EdgeEndpoints& edge, svtkIdType& midpoint)
{
  if (this->Internal->Iterator == this->Internal->Map.end())
    return false;

  edge = this->Internal->Iterator->first;
  midpoint = this->Internal->Iterator->second;

  ++this->Internal->Iterator;
  return true;
}

//=============================================================================
svtkObjectFactoryNewMacro(svtkSLACReader);

svtkInformationKeyMacro(svtkSLACReader, IS_INTERNAL_VOLUME, Integer);
svtkInformationKeyMacro(svtkSLACReader, IS_EXTERNAL_SURFACE, Integer);
svtkInformationKeyMacro(svtkSLACReader, POINTS, ObjectBase);
svtkInformationKeyMacro(svtkSLACReader, POINT_DATA, ObjectBase);

//-----------------------------------------------------------------------------
// The internals class mostly holds templated ivars that we don't want to
// expose in the header file.
class svtkSLACReader::svtkInternal
{
public:
  std::vector<svtkStdString> ModeFileNames;

  svtkSmartPointer<svtkDataArraySelection> VariableArraySelection;

  // Description:
  // A quick lookup to find the correct mode file name given a time value.
  // Only valid when TimeStepModes is true.
  std::map<double, svtkStdString> TimeStepToFile;

  // Description:
  // The rates at which the mode fields repeat.
  // Only valid when FrequencyModes is true.
  std::vector<double> Frequencies;

  // Description:
  // The phases of the modes at the current time step. Set at the beginning of
  // RequestData. Only valid when FreqencyModes is true.
  std::vector<double> Phases;

  // Description:
  // Scale/offset for each of themodes. Only valid when FrequencyModes is true.
  std::vector<double> FrequencyScales;
  std::vector<double> PhaseShifts;

  // Description:
  // References and shallow copies to the last output data.  We keep this
  // around in case we do not have to read everything in again.
  svtkSmartPointer<svtkPoints> PointCache;
  svtkSmartPointer<svtkMultiBlockDataSet> MeshCache;
  MidpointIdMap MidpointIdCache;

  // Description:
  // These are use by GetFrequencyScales() and GetPhaseShifts() methods to
  // returns the values of this->FrequencyScales and this->PhaseShifts as
  // svtkDoubleArray. Don't use these otherwise since these are only populated in
  // the corresponding methods.
  svtkNew<svtkDoubleArray> FrequencyScalesArray;
  svtkNew<svtkDoubleArray> PhaseShiftsArray;
};

//-----------------------------------------------------------------------------
svtkSLACReader::svtkSLACReader()
{
  this->Internal = new svtkSLACReader::svtkInternal;

  this->SetNumberOfInputPorts(0);

  this->MeshFileName = nullptr;

  this->ReadInternalVolume = 0;
  this->ReadExternalSurface = 1;
  this->ReadMidpoints = 1;

  this->Internal->VariableArraySelection = svtkSmartPointer<svtkDataArraySelection>::New();
  SVTK_CREATE(svtkCallbackCommand, cbc);
  cbc->SetCallback(&svtkSLACReader::SelectionModifiedCallback);
  cbc->SetClientData(this);
  this->Internal->VariableArraySelection->AddObserver(svtkCommand::ModifiedEvent, cbc);

  this->ReadModeData = false;
  this->TimeStepModes = false;
  this->FrequencyModes = false;

  this->SetNumberOfOutputPorts(NUM_OUTPUTS);
}

svtkSLACReader::~svtkSLACReader()
{
  this->SetMeshFileName(nullptr);

  delete this->Internal;
}

void svtkSLACReader::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  if (this->MeshFileName)
  {
    os << indent << "MeshFileName: " << this->MeshFileName << endl;
  }
  else
  {
    os << indent << "MeshFileName: (null)\n";
  }

  for (unsigned int i = 0; i < this->Internal->ModeFileNames.size(); i++)
  {
    os << indent << "ModeFileName[" << i << "]: " << this->Internal->ModeFileNames[i] << endl;
  }

  os << indent << "ReadInternalVolume: " << this->ReadInternalVolume << endl;
  os << indent << "ReadExternalSurface: " << this->ReadExternalSurface << endl;
  os << indent << "ReadMidpoints: " << this->ReadMidpoints << endl;

  os << indent << "VariableArraySelection:" << endl;
  this->Internal->VariableArraySelection->PrintSelf(os, indent.GetNextIndent());
}

//-----------------------------------------------------------------------------
int svtkSLACReader::CanReadFile(const char* filename)
{
  svtkSLACReaderAutoCloseNetCDF ncFD(filename, NC_NOWRITE, true);
  if (!ncFD.Valid())
    return 0;

  // Check for the existence of several arrays we know should be in the file.
  int dummy;
  if (nc_inq_varid(ncFD, "coords", &dummy) != NC_NOERR)
    return 0;
  if (nc_inq_varid(ncFD, "tetrahedron_interior", &dummy) != NC_NOERR)
    return 0;
  if (nc_inq_varid(ncFD, "tetrahedron_exterior", &dummy) != NC_NOERR)
    return 0;

  return 1;
}

//-----------------------------------------------------------------------------
void svtkSLACReader::AddModeFileName(const char* fname)
{
  this->Internal->ModeFileNames.push_back(fname);
  this->Modified();
}

void svtkSLACReader::RemoveAllModeFileNames()
{
  this->Internal->ModeFileNames.clear();
  this->Modified();
}

unsigned int svtkSLACReader::GetNumberOfModeFileNames()
{
  return static_cast<unsigned int>(this->Internal->ModeFileNames.size());
}

const char* svtkSLACReader::GetModeFileName(unsigned int idx)
{
  return this->Internal->ModeFileNames[idx].c_str();
}

//-----------------------------------------------------------------------------
svtkIdType svtkSLACReader::GetNumTuplesInVariable(int ncFD, int varId, int expectedNumComponents)
{
  int numDims;
  CALL_NETCDF(nc_inq_varndims(ncFD, varId, &numDims));
  if (numDims != 2)
  {
    char name[NC_MAX_NAME + 1];
    CALL_NETCDF(nc_inq_varname(ncFD, varId, name));
    svtkErrorMacro(<< "Wrong dimensions on " << name);
    return 0;
  }

  int dimIds[2];
  CALL_NETCDF(nc_inq_vardimid(ncFD, varId, dimIds));

  size_t dimLength;
  CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[1], &dimLength));
  if (static_cast<int>(dimLength) != expectedNumComponents)
  {
    char name[NC_MAX_NAME + 1];
    CALL_NETCDF(nc_inq_varname(ncFD, varId, name));
    svtkErrorMacro(<< "Unexpected tuple size on " << name);
    return 0;
  }

  CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[0], &dimLength));
  return static_cast<svtkIdType>(dimLength);
}

//-----------------------------------------------------------------------------
int svtkSLACReader::RequestInformation(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* outputVector)
{
  svtkInformation* surfaceOutInfo = outputVector->GetInformationObject(SURFACE_OUTPUT);
  surfaceOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_STEPS());
  surfaceOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_RANGE());

  svtkInformation* volumeOutInfo = outputVector->GetInformationObject(VOLUME_OUTPUT);
  volumeOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_STEPS());
  volumeOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_RANGE());

  if (!this->MeshFileName)
  {
    svtkErrorMacro("No filename specified.");
    return 0;
  }

  this->Internal->VariableArraySelection->RemoveAllArrays();

  svtkSLACReaderAutoCloseNetCDF meshFD(this->MeshFileName, NC_NOWRITE);
  if (!meshFD.Valid())
    return 0;

  this->ReadModeData = false; // Assume false until everything checks out.
  this->TimeStepModes = false;
  this->Internal->TimeStepToFile.clear();
  this->FrequencyModes = false;
  this->Internal->Frequencies.clear();
  if (!this->Internal->ModeFileNames.empty())
  {
    // Check the first mode file, assume that the rest follow.
    svtkSLACReaderAutoCloseNetCDF modeFD(this->Internal->ModeFileNames[0], NC_NOWRITE);
    if (!modeFD.Valid())
      return 0;

    int meshCoordsVarId, modeCoordsVarId;
    CALL_NETCDF(nc_inq_varid(meshFD, "coords", &meshCoordsVarId));
    CALL_NETCDF(nc_inq_varid(modeFD, "coords", &modeCoordsVarId));

    if (this->GetNumTuplesInVariable(meshFD, meshCoordsVarId, 3) !=
      this->GetNumTuplesInVariable(modeFD, modeCoordsVarId, 3))
    {
      svtkWarningMacro(<< "Mode file " << this->Internal->ModeFileNames[0].c_str()
                      << " invalid for mesh file " << this->MeshFileName
                      << "; the number of coordinates do not match.");
    }
    else
    {
      this->ReadModeData = true;

      // Read the "frequency".  When a time series is written, the frequency
      // variable is overloaded to mean time.  There is no direct way to tell
      // the difference, but things happen very quickly (less than nanoseconds)
      // in simulations that write out this data.  Thus, we expect large numbers
      // to be frequency (in Hz) and small numbers to be time (in seconds).
      double frequency;
      if ((nc_get_scalar_double(modeFD, "frequency", &frequency) != NC_NOERR) &&
        (nc_get_scalar_double(modeFD, "frequencyreal", &frequency) != NC_NOERR))
      {
        svtkWarningMacro(<< "Could not find frequency in mode data.");
        return 0;
      }
      if (frequency < 100)
      {
        this->TimeStepModes = true;
        this->Internal->TimeStepToFile[frequency] = this->Internal->ModeFileNames[0];
      }
      else
      {
        this->FrequencyModes = true;
        this->Internal->Frequencies.resize(this->GetNumberOfModeFileNames());
        this->Internal->Frequencies[0] = frequency;
      }

      // svtksys::RegularExpression imaginaryVar("_imag$");

      int ncoordDim;
      CALL_NETCDF(nc_inq_dimid(modeFD, "ncoord", &ncoordDim));

      int numVariables;
      CALL_NETCDF(nc_inq_nvars(modeFD, &numVariables));

      for (int i = 0; i < numVariables; i++)
      {
        int numDims;
        CALL_NETCDF(nc_inq_varndims(modeFD, i, &numDims));
        if ((numDims < 1) || (numDims > 2))
          continue;

        int dimIds[2];
        CALL_NETCDF(nc_inq_vardimid(modeFD, i, dimIds));
        if (dimIds[0] != ncoordDim)
          continue;

        char name[NC_MAX_NAME + 1];
        CALL_NETCDF(nc_inq_varname(modeFD, i, name));
        if (strcmp(name, "coords") == 0)
          continue;
        // if (this->FrequencyModes && imaginaryVar.find(name)) continue;

        this->Internal->VariableArraySelection->AddArray(name);
      }
    }
  }

  if (this->TimeStepModes)
  {
    // If we are in time steps modes, we need to read in the time values from
    // all the files (and we have already read the first one).  We then report
    // the time steps we have.
    std::vector<svtkStdString>::iterator fileitr = this->Internal->ModeFileNames.begin();
    ++fileitr;
    for (; fileitr != this->Internal->ModeFileNames.end(); ++fileitr)
    {
      svtkSLACReaderAutoCloseNetCDF modeFD(*fileitr, NC_NOWRITE);
      if (!modeFD.Valid())
        return 0;

      double frequency;
      if ((nc_get_scalar_double(modeFD, "frequency", &frequency) != NC_NOERR) &&
        (nc_get_scalar_double(modeFD, "frequencyreal", &frequency) != NC_NOERR))
      {
        svtkWarningMacro(<< "Could not find frequency in mode data.");
        return 0;
      }
      this->Internal->TimeStepToFile[frequency] = *fileitr;
    }

    double range[2];
    surfaceOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_STEPS());
    volumeOutInfo->Remove(svtkStreamingDemandDrivenPipeline::TIME_STEPS());
    std::map<double, svtkStdString>::iterator timeitr = this->Internal->TimeStepToFile.begin();
    range[0] = timeitr->first;
    for (; timeitr != this->Internal->TimeStepToFile.end(); ++timeitr)
    {
      range[1] = timeitr->first; // Eventually set to last value.
      surfaceOutInfo->Append(svtkStreamingDemandDrivenPipeline::TIME_STEPS(), timeitr->first);
      volumeOutInfo->Append(svtkStreamingDemandDrivenPipeline::TIME_STEPS(), timeitr->first);
    }
    surfaceOutInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_RANGE(), range, 2);
    volumeOutInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_RANGE(), range, 2);
  }
  else if (this->FrequencyModes)
  {
    // If we are in time steps modes, we need to read in the frequencies from
    // all the files (and we have already read the first one) and record them.
    std::vector<svtkStdString>::iterator fileitr = this->Internal->ModeFileNames.begin();
    ++fileitr;
    std::vector<double>::iterator frequencyiter = this->Internal->Frequencies.begin();
    ++frequencyiter;
    for (; fileitr != this->Internal->ModeFileNames.end(); ++fileitr, ++frequencyiter)
    {
      assert(frequencyiter != this->Internal->Frequencies.end());

      svtkSLACReaderAutoCloseNetCDF modeFD(*fileitr, NC_NOWRITE);
      if (!modeFD.Valid())
        return 0;

      double frequency;
      if ((nc_get_scalar_double(modeFD, "frequency", &frequency) != NC_NOERR) &&
        (nc_get_scalar_double(modeFD, "frequencyreal", &frequency) != NC_NOERR))
      {
        svtkWarningMacro(<< "Could not find frequency in mode data.");
        return 0;
      }
      *frequencyiter = frequency;
    }
    assert(frequencyiter == this->Internal->Frequencies.end());

    this->Internal->FrequencyScales.resize(this->Internal->Frequencies.size(), 1.0);
    this->Internal->PhaseShifts.resize(this->Internal->Frequencies.size(), 0.0);

    // When there is more than one frequency (defined in multiple mode files),
    // the appropriate range is ill defined. Arbitrarily pick the smallest
    // frequency (the largest range) so that all modes will cycle at least once
    // within the range.
    double minFrequency =
      *std::min_element(this->Internal->Frequencies.begin(), this->Internal->Frequencies.end());
    double range[2];
    range[0] = 0;
    range[1] = 1.0 / minFrequency;
    surfaceOutInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_RANGE(), range, 2);
    volumeOutInfo->Set(svtkStreamingDemandDrivenPipeline::TIME_RANGE(), range, 2);
  }

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::RequestData(svtkInformation* request,
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* outputVector)
{
  svtkInformation* outInfo[NUM_OUTPUTS];
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    outInfo[i] = outputVector->GetInformationObject(i);
  }

  svtkMultiBlockDataSet* surfaceOutput = svtkMultiBlockDataSet::GetData(outInfo[SURFACE_OUTPUT]);
  svtkMultiBlockDataSet* volumeOutput = svtkMultiBlockDataSet::GetData(outInfo[VOLUME_OUTPUT]);

  if (!this->MeshFileName)
  {
    svtkErrorMacro("No filename specified.");
    return 0;
  }

  double time = 0.0;
  bool timeValid = false;
  int fromPort = request->Get(svtkExecutive::FROM_OUTPUT_PORT());
  if (outInfo[fromPort]->Has(svtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP()))
  {
    time = outInfo[fromPort]->Get(svtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP());
    timeValid = true;
  }

  if (this->FrequencyModes)
  {
    this->Internal->Phases.resize(this->Internal->Frequencies.size());
    for (size_t modeIndex = 0; modeIndex < this->Internal->Frequencies.size(); modeIndex++)
    {
      this->Internal->Phases[modeIndex] =
        2.0 * svtkMath::Pi() * (time * this->Internal->Frequencies[modeIndex]);
    }
  }
  else
  {
    this->Internal->Phases.clear();
  }

  int readMesh = !this->MeshUpToDate();

  // This convenience object holds the composite of the surface and volume
  // outputs.  Since each of these outputs is multiblock (and needs iterators)
  // anyway, then subroutines can just iterate over everything once.
  SVTK_CREATE(svtkMultiBlockDataSet, compositeOutput);

  if (readMesh)
  {
    this->Internal->MidpointIdCache.RemoveAllMidpoints();
    this->Internal->MeshCache = svtkSmartPointer<svtkMultiBlockDataSet>::New();

    svtkSLACReaderAutoCloseNetCDF meshFD(this->MeshFileName, NC_NOWRITE);
    if (!meshFD.Valid())
      return 0;

    if (!this->ReadInternalVolume && !this->ReadExternalSurface)
      return 1;

    if (!this->ReadConnectivity(meshFD, surfaceOutput, volumeOutput))
      return 0;

    this->UpdateProgress(0.25);

    // Shove two outputs in composite output.
    compositeOutput->SetNumberOfBlocks(2);
    compositeOutput->SetBlock(SURFACE_OUTPUT, surfaceOutput);
    compositeOutput->SetBlock(VOLUME_OUTPUT, volumeOutput);
    compositeOutput->GetMetaData(static_cast<unsigned int>(SURFACE_OUTPUT))
      ->Set(svtkCompositeDataSet::NAME(), "Internal Volume");
    compositeOutput->GetMetaData(static_cast<unsigned int>(VOLUME_OUTPUT))
      ->Set(svtkCompositeDataSet::NAME(), "External Surface");

    // Set up point data.
    SVTK_CREATE(svtkPoints, points);
    SVTK_CREATE(svtkPointData, pd);
    compositeOutput->GetInformation()->Set(svtkSLACReader::POINTS(), points);
    compositeOutput->GetInformation()->Set(svtkSLACReader::POINT_DATA(), pd);

    if (!this->ReadCoordinates(meshFD, compositeOutput))
      return 0;

    this->UpdateProgress(0.5);

    // if surface_midpoint requested
    if (this->ReadMidpoints)
    {
      // if midpoints present in file
      int dummy;
      if (nc_inq_varid(meshFD, "surface_midpoint", &dummy) == NC_NOERR)
      {
        if (!this->ReadMidpointData(meshFD, compositeOutput, this->Internal->MidpointIdCache))
        {
          return 0;
        }
      }
      else // midpoints requested, but not in file
      {
        //   spit out warning and ignore the midpoint read request.
        svtkWarningMacro(
          << "Midpoints requested, but not present in the mesh file.  Ignoring the request.");
      }
    }

    this->Internal->MeshCache->ShallowCopy(compositeOutput);
    this->Internal->PointCache = points;
    this->MeshReadTime.Modified();
  }
  else
  {
    if (!this->RestoreMeshCache(surfaceOutput, volumeOutput, compositeOutput))
    {
      return 0;
    }
  }

  this->UpdateProgress(0.75);

  if (this->ReadModeData)
  {
    std::vector<svtkStdString> modeFileNames;
    if (this->TimeStepModes)
    {
      modeFileNames.resize(1);
      if (timeValid)
      {
        modeFileNames[0] = this->Internal->TimeStepToFile.lower_bound(time)->second;
      }
      else
      {
        modeFileNames[0] = this->Internal->ModeFileNames[0];
      }
    }
    else
    {
      modeFileNames = this->Internal->ModeFileNames;
    }

    std::vector<svtkSLACReaderAutoCloseNetCDF> modeFDVector;
    modeFDVector.reserve(modeFileNames.size());
    for (std::vector<svtkStdString>::iterator nameIter = modeFileNames.begin();
         nameIter != modeFileNames.end(); ++nameIter)
    {
      svtkSLACReaderAutoCloseNetCDF modeFD(*nameIter, NC_NOWRITE);
      if (modeFD.Valid())
      {
        modeFDVector.push_back(modeFD);
      }
    }
    if (modeFDVector.empty())
    {
      // Warning should already have been emitted.
      return 0;
    }

    // Copy file descriptors to a structure ReadFieldData can accept. The
    // ReadFieldData interface was designed to not use implementation of
    // private or templated objects.
    int* modeFDCopy = new int[modeFDVector.size()];
    std::copy(modeFDVector.begin(), modeFDVector.end(), modeFDCopy);
    if (!this->ReadFieldData(modeFDCopy, static_cast<int>(modeFDVector.size()), compositeOutput))
    {
      return 0;
    }
    delete[] modeFDCopy;

    this->UpdateProgress(0.875);

    if (!this->InterpolateMidpointData(compositeOutput, this->Internal->MidpointIdCache))
    {
      return 0;
    }

    if (timeValid)
    {
      surfaceOutput->GetInformation()->Set(svtkDataObject::DATA_TIME_STEP(), time);
      volumeOutput->GetInformation()->Set(svtkDataObject::DATA_TIME_STEP(), time);
    }
  }

  // Push points to output.
  svtkPoints* points =
    svtkPoints::SafeDownCast(compositeOutput->GetInformation()->Get(svtkSLACReader::POINTS()));
  svtkSmartPointer<svtkCompositeDataIterator> outputIter;
  for (outputIter.TakeReference(compositeOutput->NewIterator()); !outputIter->IsDoneWithTraversal();
       outputIter->GoToNextItem())
  {
    svtkUnstructuredGrid* ugrid =
      svtkUnstructuredGrid::SafeDownCast(compositeOutput->GetDataSet(outputIter));
    ugrid->SetPoints(points);
  }

  // Push point field data to output.
  svtkPointData* pd =
    svtkPointData::SafeDownCast(compositeOutput->GetInformation()->Get(svtkSLACReader::POINT_DATA()));
  for (outputIter.TakeReference(compositeOutput->NewIterator()); !outputIter->IsDoneWithTraversal();
       outputIter->GoToNextItem())
  {
    svtkUnstructuredGrid* ugrid =
      svtkUnstructuredGrid::SafeDownCast(compositeOutput->GetDataSet(outputIter));
    ugrid->GetPointData()->ShallowCopy(pd);
  }

  return 1;
}

//----------------------------------------------------------------------------
void svtkSLACReader::SelectionModifiedCallback(svtkObject*, unsigned long, void* clientdata, void*)
{
  static_cast<svtkSLACReader*>(clientdata)->Modified();
}

//-----------------------------------------------------------------------------
int svtkSLACReader::GetNumberOfVariableArrays()
{
  return this->Internal->VariableArraySelection->GetNumberOfArrays();
}

//-----------------------------------------------------------------------------
const char* svtkSLACReader::GetVariableArrayName(int index)
{
  return this->Internal->VariableArraySelection->GetArrayName(index);
}

//-----------------------------------------------------------------------------
int svtkSLACReader::GetVariableArrayStatus(const char* name)
{
  return this->Internal->VariableArraySelection->ArrayIsEnabled(name);
}

//-----------------------------------------------------------------------------
void svtkSLACReader::SetVariableArrayStatus(const char* name, int status)
{
  svtkDebugMacro("Set cell array \"" << name << "\" status to: " << status);
  if (status)
  {
    this->Internal->VariableArraySelection->EnableArray(name);
  }
  else
  {
    this->Internal->VariableArraySelection->DisableArray(name);
  }
}

//-----------------------------------------------------------------------------
void svtkSLACReader::ResetFrequencyScales()
{
  std::fill(this->Internal->FrequencyScales.begin(), this->Internal->FrequencyScales.end(), 1.0);
}

//-----------------------------------------------------------------------------
void svtkSLACReader::SetFrequencyScale(int index, double scale)
{
  if ((index < 0) || (static_cast<size_t>(index) >= this->Internal->FrequencyScales.size()))
  {
    svtkErrorMacro(<< "Bad mode index: " << index);
  }

  this->Internal->FrequencyScales[index] = scale;
}

//-----------------------------------------------------------------------------
svtkDoubleArray* svtkSLACReader::GetFrequencyScales()
{
  this->Internal->FrequencyScalesArray->SetNumberOfTuples(
    static_cast<svtkIdType>(this->Internal->FrequencyScales.size()));

  // don't copy to nullptr pointer
  if (this->Internal->FrequencyScalesArray->GetPointer(0) != nullptr)
  {
    std::copy(this->Internal->FrequencyScales.begin(), this->Internal->FrequencyScales.end(),
      this->Internal->FrequencyScalesArray->GetPointer(0));
  }
  return this->Internal->FrequencyScalesArray;
}

//-----------------------------------------------------------------------------
void svtkSLACReader::ResetPhaseShifts()
{
  std::fill(this->Internal->PhaseShifts.begin(), this->Internal->PhaseShifts.end(), 0.0);
}

//-----------------------------------------------------------------------------
void svtkSLACReader::SetPhaseShift(int index, double scale)
{
  if ((index < 0) || (static_cast<size_t>(index) >= this->Internal->PhaseShifts.size()))
  {
    svtkErrorMacro(<< "Bad mode index: " << index);
  }

  this->Internal->PhaseShifts[index] = scale;
}

//-----------------------------------------------------------------------------
svtkDoubleArray* svtkSLACReader::GetPhaseShifts()
{
  this->Internal->PhaseShiftsArray->SetNumberOfTuples(
    static_cast<svtkIdType>(this->Internal->PhaseShifts.size()));

  // don't copy to nullptr pointer
  if (this->Internal->PhaseShiftsArray->GetPointer(0) != nullptr)
  {
    std::copy(this->Internal->PhaseShifts.begin(), this->Internal->PhaseShifts.end(),
      this->Internal->PhaseShiftsArray->GetPointer(0));
  }
  return this->Internal->PhaseShiftsArray;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadTetrahedronInteriorArray(int meshFD, svtkIdTypeArray* connectivity)
{
  int tetInteriorVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "tetrahedron_interior", &tetInteriorVarId));
  svtkIdType numTetsInterior = this->GetNumTuplesInVariable(meshFD, tetInteriorVarId, NumPerTetInt);

  connectivity->Initialize();
  connectivity->SetNumberOfComponents(NumPerTetInt);
  connectivity->SetNumberOfTuples(numTetsInterior);
  CALL_NETCDF(nc_get_var_svtkIdType(meshFD, tetInteriorVarId, connectivity->GetPointer(0)));

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadTetrahedronExteriorArray(int meshFD, svtkIdTypeArray* connectivity)
{
  int tetExteriorVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "tetrahedron_exterior", &tetExteriorVarId));
  svtkIdType numTetsExterior = this->GetNumTuplesInVariable(meshFD, tetExteriorVarId, NumPerTetExt);

  connectivity->Initialize();
  connectivity->SetNumberOfComponents(NumPerTetExt);
  connectivity->SetNumberOfTuples(numTetsExterior);
  CALL_NETCDF(nc_get_var_svtkIdType(meshFD, tetExteriorVarId, connectivity->GetPointer(0)));

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::CheckTetrahedraWinding(int meshFD)
{
  int i;

  // Read in the first interior tetrahedron topology.
  int tetInteriorVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "tetrahedron_interior", &tetInteriorVarId));

  size_t start[2], count[2];
  start[0] = 0;
  count[0] = 1;
  start[1] = 0;
  count[1] = NumPerTetInt;

  svtkIdType tetTopology[NumPerTetInt];
  CALL_NETCDF(nc_get_vars_svtkIdType(meshFD, tetInteriorVarId, start, count, nullptr, tetTopology));

  // Read in the point coordinates for the tetrahedron.  The indices for the
  // points are stored in values 1-4 of tetTopology.
  int coordsVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "coords", &coordsVarId));

  double pts[4][3];
  for (i = 0; i < 4; i++)
  {
    start[0] = tetTopology[i + 1];
    count[0] = 1;
    start[1] = 0;
    count[1] = 3;
    CALL_NETCDF(nc_get_vars_double(meshFD, coordsVarId, start, count, nullptr, pts[i]));
  }

  // Given the coordinates of the tetrahedron points, determine the direction of
  // the winding.  Note that this test will fail if the tetrahedron is
  // degenerate.  The first step is finding the normal of the triangle (0,1,2).
  double v1[3], v2[3], n[3];
  for (i = 0; i < 3; i++)
  {
    v1[i] = pts[1][i] - pts[0][i];
    v2[i] = pts[2][i] - pts[0][i];
  }
  svtkMath::Cross(v1, v2, n);

  // For the SVTK winding, the normal, n, should point toward the fourth point
  // of the tetrahedron.
  double v3[3];
  for (i = 0; i < 3; i++)
    v3[i] = pts[3][i] - pts[0][i];
  double dir = svtkMath::Dot(v3, n);
  return (dir >= 0.0);
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadConnectivity(
  int meshFD, svtkMultiBlockDataSet* surfaceOutput, svtkMultiBlockDataSet* volumeOutput)
{
  // Decide if we need to invert the tetrahedra to make them compatible
  // with SVTK winding.
  int invertTets = !this->CheckTetrahedraWinding(meshFD);

  // Read in interior tetrahedra.
  SVTK_CREATE(svtkIdTypeArray, connectivity);
  if (this->ReadInternalVolume)
  {
    if (!this->ReadTetrahedronInteriorArray(meshFD, connectivity))
      return 0;
    svtkIdType numTetsInterior = connectivity->GetNumberOfTuples();
    for (svtkIdType i = 0; i < numTetsInterior; i++)
    {
      // Interior tetrahedra are defined with 5 integers.  The first is an
      // element attribute (which we will use to separate into multiple blocks)
      // and the other four are ids for the 4 points of the tetrahedra.  The
      // faces of the tetrahedra are the following:
      // Face 0:  0,  2,  1
      // Face 1:  0,  3,  2
      // Face 2:  0,  1,  3
      // Face 3:  1,  2,  3
      // There are two possible "windings," the direction in which the normals
      // face, for any given tetrahedra.  SLAC files might support either
      // winding, but it should be consistent through the mesh.  The invertTets
      // flag set earlier indicates whether we need to invert the tetrahedra.
      svtkIdType tetInfo[NumPerTetInt];
      connectivity->GetTypedTuple(i, tetInfo);
      if (invertTets)
        std::swap(tetInfo[1], tetInfo[2]);
      svtkUnstructuredGrid* ugrid = AllocateGetBlock(volumeOutput, tetInfo[0], IS_INTERNAL_VOLUME());
      ugrid->InsertNextCell(SVTK_TETRA, 4, tetInfo + 1);
    }
  }

  // Read in exterior tetrahedra.
  if (!this->ReadTetrahedronExteriorArray(meshFD, connectivity))
    return 0;
  svtkIdType numTetsExterior = connectivity->GetNumberOfTuples();
  for (svtkIdType i = 0; i < numTetsExterior; i++)
  {
    // Exterior tetrahedra are defined with 9 integers.  The first is an element
    // attribute and the next 4 are point ids, which is the same as interior
    // tetrahedra (see above).  The last 4 define the boundary condition of
    // each face (see above for the order of faces).  A flag of -1 is used
    // when the face is internal.  Other flags separate faces in a multiblock
    // data set.
    svtkIdType tetInfo[NumPerTetExt];
    connectivity->GetTypedTuple(i, tetInfo);
    if (invertTets)
    {
      std::swap(tetInfo[1], tetInfo[2]); // Invert point indices
      std::swap(tetInfo[6], tetInfo[8]); // Correct faces for inversion
    }
    if (this->ReadInternalVolume)
    {
      svtkUnstructuredGrid* ugrid = AllocateGetBlock(volumeOutput, tetInfo[0], IS_INTERNAL_VOLUME());
      ugrid->InsertNextCell(SVTK_TETRA, 4, tetInfo + 1);
    }

    if (this->ReadExternalSurface)
    {
      for (int face = 0; face < 4; face++)
      {
        int boundaryCondition = tetInfo[5 + face];
        if (boundaryCondition >= 0)
        {
          svtkUnstructuredGrid* ugrid =
            AllocateGetBlock(surfaceOutput, boundaryCondition, IS_EXTERNAL_SURFACE());
          svtkIdType ptids[3];
          ptids[0] = tetInfo[1 + tetFaces[face][0]];
          ptids[1] = tetInfo[1 + tetFaces[face][1]];
          ptids[2] = tetInfo[1 + tetFaces[face][2]];
          ugrid->InsertNextCell(SVTK_TRIANGLE, 3, ptids);
        }
      }
    }
  }

  return 1;
}

//-----------------------------------------------------------------------------
svtkSmartPointer<svtkDataArray> svtkSLACReader::ReadPointDataArray(int ncFD, int varId)
{
  // Get the dimension info.  We should only need to worry about 1 or 2D arrays.
  int numDims;
  CALL_NETCDF(nc_inq_varndims(ncFD, varId, &numDims));
  if (numDims > 2) // don't support 3d or higher arrays
  {
    svtkErrorMacro(<< "Sanity check failed.  "
                  << "Encountered array with too many dimensions.");
    return nullptr;
  }
  if (numDims < 1) // don't support 0d arrays
  {
    svtkErrorMacro(<< "Sanity check failed.  "
                  << "Encountered array no dimensions.");
    return nullptr;
  }
  int dimIds[2];
  CALL_NETCDF(nc_inq_vardimid(ncFD, varId, dimIds));
  size_t numCoords;
  CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[0], &numCoords));
  size_t numComponents = 1;
  if (numDims > 1)
  {
    CALL_NETCDF(nc_inq_dimlen(ncFD, dimIds[1], &numComponents));
  }

  // Allocate an array of the right type.
  nc_type ncType;
  CALL_NETCDF(nc_inq_vartype(ncFD, varId, &ncType));
  int svtkType = NetCDFTypeToSVTKType(ncType);
  if (svtkType < 1)
    return nullptr;
  svtkSmartPointer<svtkDataArray> dataArray;
  dataArray.TakeReference(svtkDataArray::CreateDataArray(svtkType));
  dataArray->SetNumberOfComponents(static_cast<int>(numComponents));
  dataArray->SetNumberOfTuples(static_cast<svtkIdType>(numCoords));

  // Read the data from the file.
  size_t start[2], count[2];
  start[0] = start[1] = 0;
  count[0] = numCoords;
  count[1] = numComponents;
  CALL_NETCDF(nc_get_vars(ncFD, varId, start, count, nullptr, dataArray->GetVoidPointer(0)));

  return dataArray;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadCoordinates(int meshFD, svtkMultiBlockDataSet* output)
{
  // Read in the point coordinates.  The coordinates are 3-tuples in an array
  // named "coords".
  int coordsVarId;
  CALL_NETCDF(nc_inq_varid(meshFD, "coords", &coordsVarId));

  svtkSmartPointer<svtkDataArray> coordData = this->ReadPointDataArray(meshFD, coordsVarId);
  if (!coordData)
    return 0;
  if (coordData->GetNumberOfComponents() != 3)
  {
    svtkErrorMacro(<< "Failed sanity check!  Coords have wrong dimensions.");
    return 0;
  }
  coordData->SetName("coords");

  svtkPoints* points =
    svtkPoints::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINTS()));
  points->SetData(coordData);

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadFieldData(
  const int* modeFDArray, int numModeFDs, svtkMultiBlockDataSet* output)
{
  assert(numModeFDs > 0);
  assert(!this->FrequencyModes ||
    (static_cast<size_t>(numModeFDs) <= this->Internal->Frequencies.size()));
  assert(
    !this->FrequencyModes || (static_cast<size_t>(numModeFDs) <= this->Internal->Phases.size()));

  svtkPointData* pd =
    svtkPointData::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINT_DATA()));

  // Get the number of coordinates (which determines how many items are read
  // per variable).
  int ncoordDim;
  CALL_NETCDF(nc_inq_dimid(modeFDArray[0], "ncoord", &ncoordDim));
  size_t numCoords;
  CALL_NETCDF(nc_inq_dimlen(modeFDArray[0], ncoordDim, &numCoords));

  int numArrays = this->Internal->VariableArraySelection->GetNumberOfArrays();
  for (int arrayIndex = 0; arrayIndex < numArrays; arrayIndex++)
  {
    // skip array if not enabled
    if (!this->Internal->VariableArraySelection->GetArraySetting(arrayIndex))
    {
      continue;
    }

    // from the variable name, get the variable id
    const char* cname = this->Internal->VariableArraySelection->GetArrayName(arrayIndex);
    int varId;
    CALL_NETCDF(nc_inq_varid(modeFDArray[0], cname, &varId));

    svtkStdString name(cname);

    // if this variable isn't 1d or 2d array, skip it.
    int numDims;
    CALL_NETCDF(nc_inq_varndims(modeFDArray[0], varId, &numDims));
    if (numDims < 1 || numDims > 2)
    {
      svtkWarningMacro(<< "Encountered invalid variable dimensions.");
      continue;
    }

    // Handle the imaginary component of mode data:
    // If simulation is purely real, all imaginary components would be zero.
    // Saving all the zeroes would waste space, so they aren't saved.  So
    // missing imaginary components in the file means we should know to use
    // zeroes.
    //
    // Because we can't know whether a fieldname (without a corresponding
    // fieldname_image) is complex or not, we only do this for "efield" and
    // "bfield.
    //
    // (TLDR: for efield and bfield, load imaginary components if provided,
    // otherwise use zeroes.)
    if (this->FrequencyModes && (name == "efield" || name == "bfield"))
    {
      // An array to accumulate the data.
      svtkSmartPointer<svtkDoubleArray> dataArray = svtkSmartPointer<svtkDoubleArray>::New();

      svtkSmartPointer<svtkDoubleArray> cplxMagArray = svtkSmartPointer<svtkDoubleArray>::New();

      svtkSmartPointer<svtkDoubleArray> phaseArray = svtkSmartPointer<svtkDoubleArray>::New();

      for (int modeIndex = 0; modeIndex < numModeFDs; modeIndex++)
      {
        int modeFD = modeFDArray[modeIndex];

        // Read in the real array data.
        svtkSmartPointer<svtkDataArray> realDataArray = this->ReadPointDataArray(modeFD, varId);
        if (!dataArray)
        {
          return 0;
        }

        svtkIdType numTuples = realDataArray->GetNumberOfTuples();
        int numComponents = realDataArray->GetNumberOfComponents();

        if (modeIndex == 0)
        {
          dataArray->SetNumberOfComponents(numComponents);
          dataArray->SetNumberOfTuples(numTuples);

          cplxMagArray->SetNumberOfComponents(1);
          cplxMagArray->SetNumberOfTuples(numTuples);

          phaseArray->SetNumberOfComponents(numComponents);
          phaseArray->SetNumberOfTuples(numTuples);
        }

        // I am assuming here that the imaginary data has the same dimensions as
        // the real data.

        // if this variable name has a correstponding name_imag, use that,
        // otherwise assume zeroes.
        svtkSmartPointer<svtkDataArray> imagDataArray = nullptr;
        if (nc_inq_varid(modeFD, (name + "_imag").c_str(), &varId) == NC_NOERR)
        {
          imagDataArray = this->ReadPointDataArray(modeFD, varId);
        }

        for (svtkIdType tupleIndex = 0; tupleIndex < numTuples; tupleIndex++)
        {
          double accumulatedMag = 0.0;
          for (int componentIndex = 0; componentIndex < numComponents; componentIndex++)
          {
            double real = realDataArray->GetComponent(tupleIndex, componentIndex);

            // when values are purely real, no imaginary component is saved in the
            // data file, because all those zeroes would waste space.  So if
            // imaginary values are provided, use them, otherwise use 0.0.
            double imag;
            if (imagDataArray)
            {
              imag = imagDataArray->GetComponent(tupleIndex, componentIndex);
            }
            else
            {
              imag = 0.0;
            }

            double mag2 = real * real + imag * imag;
            accumulatedMag += mag2;
            double mag = sqrt(mag2);

            double startphase = atan2(imag, real);

            double accumulatedMode;
            if (modeIndex == 0)
            {
              accumulatedMode = 0.0;
            }
            else
            {
              accumulatedMode = dataArray->GetComponent(tupleIndex, componentIndex);
            }
            double modeMag = mag * this->Internal->FrequencyScales[modeIndex];
            double modePhase = startphase + this->Internal->Phases[modeIndex] +
              this->Internal->PhaseShifts[modeIndex];
            accumulatedMode += modeMag * cos(modePhase);
            dataArray->SetComponent(tupleIndex, componentIndex, accumulatedMode);
            if (modeIndex == 0)
            {
              phaseArray->SetComponent(tupleIndex, componentIndex, startphase);
            }
          }
          if (modeIndex == 0)
          {
            cplxMagArray->SetComponent(tupleIndex, 0, sqrt(accumulatedMag));
          }
        }
      }

      // Add the data to the point data.
      dataArray->SetName(name);
      pd->AddArray(dataArray);

      // add complex magnitude data to the point data
      svtkStdString cplxMagName = name + "_cplx_mag";
      cplxMagArray->SetName(cplxMagName);
      pd->AddArray(cplxMagArray);

      svtkStdString phaseName = name + "_phase";
      phaseArray->SetName(phaseName);
      pd->AddArray(phaseArray);
    }
    else
    {
      // Must be a real-only field.  No animation/blending of modes.
      svtkSmartPointer<svtkDataArray> dataArray = this->ReadPointDataArray(modeFDArray[0], varId);
      if (!dataArray)
        continue;

      // Add the data to the point data.
      dataArray->SetName(name);
      pd->AddArray(dataArray);
    }
  }

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadMidpointCoordinates(
  int meshFD, svtkMultiBlockDataSet* output, svtkSLACReader::MidpointCoordinateMap& map)
{
  // Get the number of midpoints.
  int midpointsVar;
  CALL_NETCDF(nc_inq_varid(meshFD, "surface_midpoint", &midpointsVar));
  svtkIdType numMidpoints = this->GetNumTuplesInVariable(meshFD, midpointsVar, 5);
  if (numMidpoints < 1)
    return 0;

  // Read in the raw data.
  SVTK_CREATE(svtkDoubleArray, midpointData);
  midpointData->SetNumberOfComponents(5);
  midpointData->SetNumberOfTuples(numMidpoints);
  CALL_NETCDF(nc_get_var_double(meshFD, midpointsVar, midpointData->GetPointer(0)));

  svtkPoints* points =
    svtkPoints::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINTS()));
  svtkIdType pointTotal = points->GetNumberOfPoints();
  // Create a searchable structure.
  for (svtkIdType i = 0; i < numMidpoints; i++)
  {
    double* mp = midpointData->GetPointer(i * 5);

    EdgeEndpoints edge(static_cast<svtkIdType>(mp[0]), static_cast<svtkIdType>(mp[1]));
    MidpointCoordinates midpoint(mp + 2, i + pointTotal);
    map.AddMidpoint(edge, midpoint);
  }

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::ReadMidpointData(
  int meshFD, svtkMultiBlockDataSet* output, MidpointIdMap& midpointIds)
{
  // Get the point information from the data.
  svtkPoints* points =
    svtkPoints::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINTS()));

  // Read in the midpoint coordinates.
  MidpointCoordinateMap midpointCoords;
  if (!this->ReadMidpointCoordinates(meshFD, output, midpointCoords))
    return 0;

  svtkIdType newPointTotal = points->GetNumberOfPoints() + midpointCoords.GetNumberOfMidpoints();

  // Iterate over all of the parts in the output and visit the ones for the
  // external surface.
  svtkSmartPointer<svtkCompositeDataIterator> outputIter;
  for (outputIter.TakeReference(output->NewIterator()); !outputIter->IsDoneWithTraversal();
       outputIter->GoToNextItem())
  {
    if (!output->GetMetaData(outputIter)->Get(IS_EXTERNAL_SURFACE()))
      continue;

    // Create a new cell array so that we can convert all the cells from
    // triangles to quadratic triangles.
    svtkUnstructuredGrid* ugrid = svtkUnstructuredGrid::SafeDownCast(output->GetDataSet(outputIter));
    svtkCellArray* oldCells = ugrid->GetCells();
    SVTK_CREATE(svtkCellArray, newCells);
    newCells->AllocateEstimate(oldCells->GetNumberOfCells(), 6);

    // Iterate over all of the cells.
    svtkIdType npts;
    const svtkIdType* pts;
    for (oldCells->InitTraversal(); oldCells->GetNextCell(npts, pts);)
    {
      newCells->InsertNextCell(6);

      // Copy corner points.
      newCells->InsertCellPoint(pts[0]);
      newCells->InsertCellPoint(pts[1]);
      newCells->InsertCellPoint(pts[2]);

      // Add edge midpoints.
      for (int edgeInc = 0; edgeInc < 3; edgeInc++)
      {
        // Get the points defining the edge.
        svtkIdType p0 = pts[triEdges[edgeInc][0]];
        svtkIdType p1 = pts[triEdges[edgeInc][1]];
        EdgeEndpoints edge(p0, p1);

        // See if we have already copied this midpoint.
        svtkIdType midId;
        svtkIdType* midIdPointer = midpointIds.FindMidpoint(edge);
        if (midIdPointer != nullptr)
        {
          midId = *midIdPointer;
        }
        else
        {
          // Check to see if the midpoint was read from the file.  If not,
          // then interpolate linearly between the two edge points.
          MidpointCoordinates midpoint;
          MidpointCoordinates* midpointPointer = midpointCoords.FindMidpoint(edge);
          if (midpointPointer == nullptr)
          {
            double coord0[3], coord1[3], coordMid[3];
            points->GetPoint(p0, coord0);
            points->GetPoint(p1, coord1);
            coordMid[0] = 0.5 * (coord0[0] + coord1[0]);
            coordMid[1] = 0.5 * (coord0[1] + coord1[1]);
            coordMid[2] = 0.5 * (coord0[2] + coord1[2]);
            midpoint = MidpointCoordinates(coordMid, newPointTotal);
            newPointTotal++;
          }
          else
          {
            midpoint = *midpointPointer;
            // Erase the midpoint from the map.  We don't need it anymore since
            // we will insert a point id in the midpointIds map (see below).
            midpointCoords.RemoveMidpoint(edge);
          }

          // Add the new point to the point data.
          points->InsertPoint(midpoint.ID, midpoint.Coordinate);

          // Add the new point to the id map.
          midpointIds.AddMidpoint(edge, midpoint.ID);
          midId = midpoint.ID;
        }

        // Record the midpoint in the quadratic cell.
        newCells->InsertCellPoint(midId);
      }
    }

    // Save the new cells in the data.
    ugrid->SetCells(SVTK_QUADRATIC_TRIANGLE, newCells);
  }

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::InterpolateMidpointData(
  svtkMultiBlockDataSet* output, svtkSLACReader::MidpointIdMap& map)
{
  // Get the point information from the output data (where it was placed
  // earlier).
  svtkPoints* points =
    svtkPoints::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINTS()));
  svtkPointData* pd =
    svtkPointData::SafeDownCast(output->GetInformation()->Get(svtkSLACReader::POINT_DATA()));
  if (!pd)
  {
    svtkWarningMacro(<< "Missing point data.");
    return 0;
  }

  // Set up the point data for adding new points and interpolating their values.
  pd->InterpolateAllocate(pd, points->GetNumberOfPoints());

  EdgeEndpoints edge;
  svtkIdType midpoint;
  for (map.InitTraversal(); map.GetNextMidpoint(edge, midpoint);)
  {
    pd->InterpolateEdge(pd, midpoint, edge.GetMinEndPoint(), edge.GetMaxEndPoint(), 0.5);
  }

  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::MeshUpToDate()
{
  if (this->MeshReadTime < this->GetMTime())
  {
    return 0;
  }
  if (this->MeshReadTime < this->Internal->VariableArraySelection->GetMTime())
  {
    return 0;
  }
  return 1;
}

//-----------------------------------------------------------------------------
int svtkSLACReader::RestoreMeshCache(svtkMultiBlockDataSet* surfaceOutput,
  svtkMultiBlockDataSet* volumeOutput, svtkMultiBlockDataSet* compositeOutput)
{
  surfaceOutput->ShallowCopy(this->Internal->MeshCache->GetBlock(SURFACE_OUTPUT));
  volumeOutput->ShallowCopy(this->Internal->MeshCache->GetBlock(VOLUME_OUTPUT));

  // Shove two outputs in composite output.
  compositeOutput->SetNumberOfBlocks(2);
  compositeOutput->SetBlock(SURFACE_OUTPUT, surfaceOutput);
  compositeOutput->SetBlock(VOLUME_OUTPUT, volumeOutput);
  compositeOutput->GetMetaData(static_cast<unsigned int>(SURFACE_OUTPUT))
    ->Set(svtkCompositeDataSet::NAME(), "Internal Volume");
  compositeOutput->GetMetaData(static_cast<unsigned int>(VOLUME_OUTPUT))
    ->Set(svtkCompositeDataSet::NAME(), "External Surface");

  compositeOutput->GetInformation()->Set(svtkSLACReader::POINTS(), this->Internal->PointCache);

  SVTK_CREATE(svtkPointData, pd);
  compositeOutput->GetInformation()->Set(svtkSLACReader::POINT_DATA(), pd);

  return 1;
}