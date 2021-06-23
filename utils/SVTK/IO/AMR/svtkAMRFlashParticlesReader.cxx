/*=========================================================================

 Program:   Visualization Toolkit
 Module:    svtkAMRFlashParticlesReader.cxx

 Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
 All rights reserved.
 See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

 This software is distributed WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the above copyright notice for more information.

 =========================================================================*/
#include "svtkAMRFlashParticlesReader.h"
#include "svtkCellArray.h"
#include "svtkDataArraySelection.h"
#include "svtkDoubleArray.h"
#include "svtkIdList.h"
#include "svtkIntArray.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPolyData.h"

#include "svtkAMRFlashReaderInternal.h"

#define H5_USE_16_API
#include "svtk_hdf5.h" // for the HDF data loading engine

#include <cassert>
#include <vector>

#define FLASH_READER_MAX_DIMS 3
#define FLASH_READER_LEAF_BLOCK 1
#define FLASH_READER_FLASH3_FFV8 8
#define FLASH_READER_FLASH3_FFV9 9

//------------------------------------------------------------------------------
// Description:
// Helper function that reads the particle coordinates
// NOTE: it is assumed that H5DOpen has been called on the
// internal file index this->FileIndex.
static void GetParticleCoordinates(hid_t& dataIdx, std::vector<double>& xcoords,
  std::vector<double>& ycoords, std::vector<double>& zcoords, svtkFlashReaderInternal* iReader,
  int NumParticles)
{

  assert("pre: internal reader should not be nullptr" && (iReader != nullptr));

  hid_t theTypes[3];
  theTypes[0] = theTypes[1] = theTypes[2] = H5I_UNINIT;
  xcoords.resize(NumParticles);
  ycoords.resize(NumParticles);
  zcoords.resize(NumParticles);

  if (iReader->FileFormatVersion < FLASH_READER_FLASH3_FFV8)
  {
    theTypes[0] = H5Tcreate(H5T_COMPOUND, sizeof(double));
    theTypes[1] = H5Tcreate(H5T_COMPOUND, sizeof(double));
    theTypes[2] = H5Tcreate(H5T_COMPOUND, sizeof(double));
    H5Tinsert(theTypes[0], "particle_x", 0, H5T_NATIVE_DOUBLE);
    H5Tinsert(theTypes[1], "particle_y", 0, H5T_NATIVE_DOUBLE);
    H5Tinsert(theTypes[2], "particle_z", 0, H5T_NATIVE_DOUBLE);
  }

  // Read the coordinates from the file
  switch (iReader->NumberOfDimensions)
  {
    case 1:
      if (iReader->FileFormatVersion < FLASH_READER_FLASH3_FFV8)
      {
        H5Dread(dataIdx, theTypes[0], H5S_ALL, H5S_ALL, H5P_DEFAULT, &xcoords[0]);
      }
      else
      {
        iReader->ReadParticlesComponent(dataIdx, "Particles/posx", &xcoords[0]);
      }
      break;
    case 2:
      if (iReader->FileFormatVersion < FLASH_READER_FLASH3_FFV8)
      {
        H5Dread(dataIdx, theTypes[0], H5S_ALL, H5S_ALL, H5P_DEFAULT, &xcoords[0]);
        H5Dread(dataIdx, theTypes[1], H5S_ALL, H5S_ALL, H5P_DEFAULT, &ycoords[0]);
      }
      else
      {
        iReader->ReadParticlesComponent(dataIdx, "Particles/posx", &xcoords[0]);
        iReader->ReadParticlesComponent(dataIdx, "Particles/posy", &ycoords[0]);
      }
      break;
    case 3:
      if (iReader->FileFormatVersion < FLASH_READER_FLASH3_FFV8)
      {
        H5Dread(dataIdx, theTypes[0], H5S_ALL, H5S_ALL, H5P_DEFAULT, &xcoords[0]);
        H5Dread(dataIdx, theTypes[1], H5S_ALL, H5S_ALL, H5P_DEFAULT, &ycoords[0]);
        H5Dread(dataIdx, theTypes[2], H5S_ALL, H5S_ALL, H5P_DEFAULT, &zcoords[0]);
      }
      else
      {
        iReader->ReadParticlesComponent(dataIdx, "Particles/posx", &xcoords[0]);
        iReader->ReadParticlesComponent(dataIdx, "Particles/posy", &ycoords[0]);
        iReader->ReadParticlesComponent(dataIdx, "Particles/posz", &zcoords[0]);
      }
      break;
    default:
      std::cerr << "ERROR: Undefined dimension!\n" << std::endl;
      std::cerr.flush();
      return;
  }
}

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

svtkStandardNewMacro(svtkAMRFlashParticlesReader);

svtkAMRFlashParticlesReader::svtkAMRFlashParticlesReader()
{
  this->Internal = new svtkFlashReaderInternal();
  this->Initialized = false;
  this->Initialize();
}

//------------------------------------------------------------------------------
svtkAMRFlashParticlesReader::~svtkAMRFlashParticlesReader()
{
  delete this->Internal;
}

//------------------------------------------------------------------------------
void svtkAMRFlashParticlesReader::PrintSelf(std::ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void svtkAMRFlashParticlesReader::ReadMetaData()
{
  if (this->Initialized)
  {
    return;
  }

  this->Internal->SetFileName(this->FileName);
  this->Internal->ReadMetaData();

  // In some cases, the FLASH file format may have no blocks but store
  // just particles in a single block. However, the AMRBaseParticles reader
  // would expect that in this case, the number of blocks is set to 1. The
  // following lines of code provide a simple workaround for that.
  this->NumberOfBlocks = this->Internal->NumberOfBlocks;
  if (this->NumberOfBlocks == 0 && this->Internal->NumberOfParticles > 0)
  {
    this->NumberOfBlocks = 1;
  }
  this->Initialized = true;
  this->SetupParticleDataSelections();
}

//------------------------------------------------------------------------------
int svtkAMRFlashParticlesReader::GetTotalNumberOfParticles()
{
  assert("Internal reader is null" && (this->Internal != nullptr));
  return (this->Internal->NumberOfParticles);
}

//------------------------------------------------------------------------------
svtkPolyData* svtkAMRFlashParticlesReader::GetParticles(
  const char* file, const int svtkNotUsed(blkidx))
{
  hid_t dataIdx = H5Dopen(this->Internal->FileIndex, file);
  if (dataIdx < 0)
  {
    svtkErrorMacro("Could not open particles file!");
    return nullptr;
  }

  svtkPolyData* particles = svtkPolyData::New();
  svtkPoints* positions = svtkPoints::New();
  positions->SetDataTypeToDouble();
  positions->SetNumberOfPoints(this->Internal->NumberOfParticles);

  svtkPointData* pdata = particles->GetPointData();
  assert("pre: PointData is nullptr" && (pdata != nullptr));

  // Load the particle position arrays by name
  std::vector<double> xcoords;
  std::vector<double> ycoords;
  std::vector<double> zcoords;
  GetParticleCoordinates(
    dataIdx, xcoords, ycoords, zcoords, this->Internal, this->Internal->NumberOfParticles);

  // Sub-sample particles
  int TotalNumberOfParticles = static_cast<int>(xcoords.size());
  svtkIdList* ids = svtkIdList::New();
  ids->SetNumberOfIds(TotalNumberOfParticles);

  svtkIdType NumberOfParticlesLoaded = 0;
  for (int i = 0; i < TotalNumberOfParticles; ++i)
  {
    if (i % this->Frequency == 0)
    {
      if (this->CheckLocation(xcoords[i], ycoords[i], zcoords[i]))
      {
        int pidx = NumberOfParticlesLoaded;
        ids->InsertId(pidx, i);
        positions->SetPoint(pidx, xcoords[i], ycoords[i], zcoords[i]);
        ++NumberOfParticlesLoaded;
      } // END if within requested region
    }   // END if within requested interval
  }     // END for all particles

  xcoords.clear();
  ycoords.clear();
  zcoords.clear();

  ids->SetNumberOfIds(NumberOfParticlesLoaded);
  ids->Squeeze();

  positions->SetNumberOfPoints(NumberOfParticlesLoaded);
  positions->Squeeze();

  particles->SetPoints(positions);
  positions->Squeeze();

  // Create CellArray consisting of a single polyvertex
  svtkCellArray* polyVertex = svtkCellArray::New();
  polyVertex->InsertNextCell(NumberOfParticlesLoaded);
  for (svtkIdType idx = 0; idx < NumberOfParticlesLoaded; ++idx)
  {
    polyVertex->InsertCellPoint(idx);
  }
  particles->SetVerts(polyVertex);
  polyVertex->Delete();

  // Load particle data arrays
  int numArrays = this->ParticleDataArraySelection->GetNumberOfArrays();
  for (int i = 0; i < numArrays; ++i)
  {
    const char* name = this->ParticleDataArraySelection->GetArrayName(i);
    if (this->ParticleDataArraySelection->ArrayIsEnabled(name))
    {
      int attrIdx = this->Internal->ParticleAttributeNamesToIds[name];
      hid_t attrType = this->Internal->ParticleAttributeTypes[attrIdx];

      if (attrType == H5T_NATIVE_DOUBLE)
      {
        std::vector<double> data(this->Internal->NumberOfParticles);

        if (this->Internal->FileFormatVersion < FLASH_READER_FLASH3_FFV8)
        {
          hid_t dataType = H5Tcreate(H5T_COMPOUND, sizeof(double));
          H5Tinsert(dataType, name, 0, H5T_NATIVE_DOUBLE);
          H5Dread(dataIdx, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());
          H5Tclose(dataType);
        }
        else
        {
          this->Internal->ReadParticlesComponent(dataIdx, name, data.data());
        }

        svtkDataArray* array = svtkDoubleArray::New();
        array->SetName(name);
        array->SetNumberOfTuples(ids->GetNumberOfIds());
        array->SetNumberOfComponents(1);

        svtkIdType numIds = ids->GetNumberOfIds();
        for (svtkIdType pidx = 0; pidx < numIds; ++pidx)
        {
          svtkIdType particleIdx = ids->GetId(pidx);
          array->SetComponent(pidx, 0, data[particleIdx]);
        } // END for all ids of loaded particles
        pdata->AddArray(array);
      }
      else if (attrType == H5T_NATIVE_INT)
      {
        hid_t dataType = H5Tcreate(H5T_COMPOUND, sizeof(int));
        H5Tinsert(dataType, name, 0, H5T_NATIVE_INT);

        std::vector<int> data(this->Internal->NumberOfParticles);
        H5Dread(dataIdx, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

        svtkDataArray* array = svtkIntArray::New();
        array->SetName(name);
        array->SetNumberOfTuples(ids->GetNumberOfIds());
        array->SetNumberOfComponents(1);

        svtkIdType numIds = ids->GetNumberOfIds();
        for (svtkIdType pidx = 0; pidx < numIds; ++pidx)
        {
          svtkIdType particleIdx = ids->GetId(pidx);
          array->SetComponent(pidx, 0, data[particleIdx]);
        } // END for all ids of loaded particles
        pdata->AddArray(array);
      }
      else
      {
        svtkErrorMacro("Unsupported array type in HDF5 file!");
        return nullptr;
      }
    } // END if the array is supposed to be loaded
  }   // END for all arrays

  H5Dclose(dataIdx);

  return (particles);
}

//------------------------------------------------------------------------------
svtkPolyData* svtkAMRFlashParticlesReader::ReadParticles(const int blkidx)
{
  assert("pre: Internal reader is nullptr" && (this->Internal != nullptr));
  assert("pre: Not initialized " && (this->Initialized));

  int NumberOfParticles = this->Internal->NumberOfParticles;
  if (NumberOfParticles <= 0)
  {
    svtkPolyData* emptyParticles = svtkPolyData::New();
    assert("Cannot create particle dataset" && (emptyParticles != nullptr));
    return (emptyParticles);
  }

  svtkPolyData* particles = this->GetParticles(this->Internal->ParticleName.c_str(), blkidx);
  assert("partciles should not be nullptr " && (particles != nullptr));

  return (particles);
}

//------------------------------------------------------------------------------
void svtkAMRFlashParticlesReader::SetupParticleDataSelections()
{
  assert("pre: Internal reader is nullptr" && (this->Internal != nullptr));

  unsigned int N = static_cast<unsigned int>(this->Internal->ParticleAttributeNames.size());
  for (unsigned int i = 0; i < N; ++i)
  {
    this->ParticleDataArraySelection->AddArray(this->Internal->ParticleAttributeNames[i].c_str());
  } // END for all particles attributes

  this->InitializeParticleDataSelections();
}