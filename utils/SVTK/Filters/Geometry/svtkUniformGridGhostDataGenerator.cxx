/*=========================================================================

 Program:   Visualization Toolkit
 Module:    svtkUniformGridGhostDataGenerator.cxx

 Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
 All rights reserved.
 See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

 This software is distributed WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the above copyright notice for more information.

 =========================================================================*/
#include "svtkUniformGridGhostDataGenerator.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMultiBlockDataSet.h"
#include "svtkObjectFactory.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkStructuredGridConnectivity.h"
#include "svtkUniformGrid.h"

svtkStandardNewMacro(svtkUniformGridGhostDataGenerator);

//------------------------------------------------------------------------------
svtkUniformGridGhostDataGenerator::svtkUniformGridGhostDataGenerator()
{
  this->GridConnectivity = svtkStructuredGridConnectivity::New();

  this->GlobalOrigin[0] = this->GlobalOrigin[1] = this->GlobalOrigin[2] = SVTK_DOUBLE_MAX;

  this->GlobalSpacing[0] = this->GlobalSpacing[1] = this->GlobalSpacing[2] = SVTK_DOUBLE_MIN;
}

//------------------------------------------------------------------------------
svtkUniformGridGhostDataGenerator::~svtkUniformGridGhostDataGenerator()
{
  this->GridConnectivity->Delete();
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::ComputeOrigin(svtkMultiBlockDataSet* in)
{
  assert("pre: Multi-block dataset is nullptr" && (in != nullptr));

  for (unsigned int i = 0; i < in->GetNumberOfBlocks(); ++i)
  {
    svtkUniformGrid* grid = svtkUniformGrid::SafeDownCast(in->GetBlock(i));
    assert("pre: grid block is nullptr" && (grid != nullptr));

    double blkOrigin[3];
    grid->GetOrigin(blkOrigin);
    if (blkOrigin[0] < this->GlobalOrigin[0])
    {
      this->GlobalOrigin[0] = blkOrigin[0];
    }
    if (blkOrigin[1] < this->GlobalOrigin[1])
    {
      this->GlobalOrigin[1] = blkOrigin[1];
    }
    if (blkOrigin[2] < this->GlobalOrigin[2])
    {
      this->GlobalOrigin[2] = blkOrigin[2];
    }
  } // END for all blocks
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::ComputeGlobalSpacingVector(svtkMultiBlockDataSet* in)
{
  assert("pre: Multi-block dataset is nullptr" && (in != nullptr));

  // NOTE: we assume that the spacing of the all the blocks is the same.
  svtkUniformGrid* block0 = svtkUniformGrid::SafeDownCast(in->GetBlock(0));
  assert("pre: grid block is nullptr" && (block0 != nullptr));

  block0->GetSpacing(this->GlobalSpacing);
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::RegisterGrids(svtkMultiBlockDataSet* in)
{
  assert("pre: Multi-block dataset is nullptr" && (in != nullptr));

  this->GridConnectivity->SetNumberOfGrids(in->GetNumberOfBlocks());
  this->GridConnectivity->SetNumberOfGhostLayers(0);
  this->GridConnectivity->SetWholeExtent(
    in->GetInformation()->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()));

  for (unsigned int i = 0; i < in->GetNumberOfBlocks(); ++i)
  {
    svtkUniformGrid* grid = svtkUniformGrid::SafeDownCast(in->GetBlock(i));
    assert("pre: grid block is nullptr" && (grid != nullptr));

    svtkInformation* info = in->GetMetaData(i);
    assert("pre: nullptr meta-data" && (info != nullptr));
    assert("pre: No piece meta-data" && info->Has(svtkDataObject::PIECE_EXTENT()));

    this->GridConnectivity->RegisterGrid(static_cast<int>(i),
      info->Get(svtkDataObject::PIECE_EXTENT()), grid->GetPointGhostArray(),
      grid->GetCellGhostArray(), grid->GetPointData(), grid->GetCellData(), nullptr);
  } // END for all blocks
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::CreateGhostedDataSet(
  svtkMultiBlockDataSet* in, svtkMultiBlockDataSet* out)
{
  assert("pre: input multi-block is nullptr" && (in != nullptr));
  assert("pre: output multi-block is nullptr" && (out != nullptr));

  out->SetNumberOfBlocks(in->GetNumberOfBlocks());

  int wholeExt[6];
  in->GetInformation()->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), wholeExt);
  svtkInformation* outInfo = out->GetInformation();
  outInfo->Set(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), wholeExt, 6);

  int ghostedExtent[6];
  double origin[3];
  int dims[3];

  for (unsigned int i = 0; i < out->GetNumberOfBlocks(); ++i)
  {
    // STEP 0: Get the computed ghosted grid extent
    this->GridConnectivity->GetGhostedGridExtent(i, ghostedExtent);

    // STEP 1: Get the ghosted grid dimensions from the ghosted extent
    svtkStructuredData::GetDimensionsFromExtent(ghostedExtent, dims);

    // STEP 2: Construct ghosted grid instance
    svtkUniformGrid* ghostedGrid = svtkUniformGrid::New();
    assert("pre: Cannot create ghosted grid instance" && (ghostedGrid != nullptr));

    // STEP 3: Get ghosted grid origin
    origin[0] = this->GlobalOrigin[0] + ghostedExtent[0] * this->GlobalSpacing[0];
    origin[1] = this->GlobalOrigin[1] + ghostedExtent[2] * this->GlobalSpacing[1];
    origin[2] = this->GlobalOrigin[2] + ghostedExtent[4] * this->GlobalSpacing[2];

    // STEP 4: Set ghosted uniform grid attributes
    ghostedGrid->SetOrigin(origin);
    ghostedGrid->SetDimensions(dims);
    ghostedGrid->SetSpacing(this->GlobalSpacing);

    // STEP 5: Copy the node/cell data
    ghostedGrid->GetPointData()->DeepCopy(this->GridConnectivity->GetGhostedGridPointData(i));
    ghostedGrid->GetCellData()->DeepCopy(this->GridConnectivity->GetGhostedGridCellData(i));

    out->SetBlock(i, ghostedGrid);
    ghostedGrid->Delete();
  } // END for all blocks
}

//------------------------------------------------------------------------------
void svtkUniformGridGhostDataGenerator::GenerateGhostLayers(
  svtkMultiBlockDataSet* in, svtkMultiBlockDataSet* out)
{
  assert("pre: Number of ghost-layers must be greater than 0!" && (this->NumberOfGhostLayers > 0));
  assert("pre: Input dataset is nullptr!" && (in != nullptr));
  assert("pre: Output dataset is nullptr!" && (out != nullptr));
  assert("pre: GridConnectivity is nullptr!" && (this->GridConnectivity != nullptr));

  // STEP 0: Register grids & compute global grid parameters
  this->RegisterGrids(in);
  this->ComputeOrigin(in);
  this->ComputeGlobalSpacingVector(in);

  // STEP 1: Compute Neighbors
  this->GridConnectivity->ComputeNeighbors();

  // STEP 2: Generate ghost layers
  this->GridConnectivity->CreateGhostLayers(this->NumberOfGhostLayers);

  // STEP 3: Get output data-set
  this->CreateGhostedDataSet(in, out);
}