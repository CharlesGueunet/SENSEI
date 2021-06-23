/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPExtractRectilinearGrid.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkPExtractRectilinearGrid.h"

// SVTK includes
#include "svtkExtractStructuredGridHelper.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMPIController.h"
#include "svtkMPIUtilities.h"
#include "svtkMultiProcessController.h"
#include "svtkObjectFactory.h"
#include "svtkRectilinearGrid.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkStructuredExtent.h"
#include "svtkStructuredImplicitConnectivity.h"

#include <cassert>
#include <sstream>

// Some useful extent macros
#define EMIN(ext, dim) (ext[2 * dim])
#define EMAX(ext, dim) (ext[2 * dim + 1])

// #define DEBUG

#ifdef DEBUG
#define DEBUG_EXTENT(label, extent)                                                                \
  if (this->Controller)                                                                            \
  {                                                                                                \
    svtkMPIUtilities::SynchronizedPrintf(this->Controller, #label "=[%d,%d,%d,%d,%d,%d]\n",         \
      extent[0], extent[1], extent[2], extent[3], extent[4], extent[5]);                           \
  }                                                                                                \
  else                                                                                             \
  {                                                                                                \
    std::cout << label << "=[" << extent[0] << "," << extent[1] << "," << extent[2] << ","         \
              << extent[3] << "," << extent[4] << "," << extent[5] << "]\n";                       \
  }

#define DEBUG_OUT(out)                                                                             \
  if (this->Controller)                                                                            \
  {                                                                                                \
    std::ostringstream tmpStreamOut;                                                               \
    tmpStreamOut << out;                                                                           \
    svtkMPIUtilities::SynchronizedPrintf(this->Controller, tmpStreamOut.str().c_str());             \
  }                                                                                                \
  else                                                                                             \
  {                                                                                                \
    std::cout << out;                                                                              \
  }
#else // DEBUG
#define DEBUG_EXTENT(label, extent)
#define DEBUG_OUT(out)
#endif // DEBUG

svtkStandardNewMacro(svtkPExtractRectilinearGrid);

//------------------------------------------------------------------------------
svtkPExtractRectilinearGrid::svtkPExtractRectilinearGrid()
{
  this->Controller =
    svtkMPIController::SafeDownCast(svtkMultiProcessController::GetGlobalController());
}

//------------------------------------------------------------------------------
svtkPExtractRectilinearGrid::~svtkPExtractRectilinearGrid() {}

//------------------------------------------------------------------------------
void svtkPExtractRectilinearGrid::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int svtkPExtractRectilinearGrid::RequestData(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  DEBUG_OUT("########### RequestData\n");

  bool isSubSampling =
    this->SampleRate[0] != 1 || this->SampleRate[1] != 1 || this->SampleRate[2] != 1;

  // No MPI, or no subsampling? Just run the serial implementation.
  if (!this->Controller || !isSubSampling)
  {
    return this->Superclass::RequestData(request, inputVector, outputVector);
  }

  if (!this->Internal->IsValid())
  {
    return 0;
  }

  // Collect information:
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  int inputWholeExtent[6];
  inInfo->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), inputWholeExtent);
  int outputWholeExtent[6];
  outInfo->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), outputWholeExtent);

  svtkRectilinearGrid* input =
    svtkRectilinearGrid::SafeDownCast(inInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkRectilinearGrid* output =
    svtkRectilinearGrid::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  int inputExtent[6];
  input->GetExtent(inputExtent);

  // Clamp the global VOI to the whole extent:
  int globalVOI[6];
  std::copy(this->VOI, this->VOI + 6, globalVOI);
  svtkStructuredExtent::Clamp(globalVOI, inputWholeExtent);

  // 1D Example:
  //   InputWholeExtent = [0, 20]
  //   GlobalVOI = [3, 17]
  //   SampleRate = 2
  //   OutputWholeExtent = [0, 7]
  //   Processes = 2
  //
  // Process 0:
  //   PartitionedInputExtent = [0, 10]
  //   PartitionedVOI = [3, 9] (due to sampling)
  //   OutputExtent = [0, 3]
  //   SerialOutputExtent = [0, 3]
  //   FinalOutputExtent = [0, 4] (after gap closing)
  //
  // Process 1:
  //   PartitionedInputExtent = [10, 20]
  //   PartitionedVOI = [11, 17] (offset due to sampling)
  //   OutputExtent = [4, 7]
  //   SerialOutputExtent = [0, 3]
  //   FinalOutputExtent = [4, 7]
  //
  // This filter should:
  // 1) Compute ParititonedVOI that will allow the base class to produce as much
  //    of the output data set as possible from the partitioned piece.
  //
  // 2) Update the output dataset's extents to match PartitionedOutputExtent (it
  //    will be [0, L] in each dimension by default).
  //
  // 3) Extract PartitionedVOI using the base class's implementation.
  //
  // 4) Close gaps using svtkStructuredImplicitConnectivity (e.g. [3, 4] in the
  //    above example).

  bool partitionContainsVOI = true;
  for (int dim = 0; partitionContainsVOI && dim < 3; ++dim)
  {
    partitionContainsVOI = EMAX(inputExtent, dim) >= EMIN(globalVOI, dim) &&
      EMIN(inputExtent, dim) <= EMAX(globalVOI, dim);
  }

  DEBUG_EXTENT("InputWholeExtent", inputWholeExtent);
  DEBUG_EXTENT("OutputWholeExtent", outputWholeExtent);
  DEBUG_EXTENT("GlobalVOI", globalVOI);
  DEBUG_EXTENT("InputPartitionedExtent", inputExtent);

  int partitionedVOI[6] = { 0, -1, 0, -1, 0, -1 };
  int partitionedOutputExtent[6] = { 0, -1, 0, -1, 0, -1 };

  if (partitionContainsVOI)
  {
    ////////////////////////////////////////////////////////////////
    // 1) Compute actual VOI for aligning the partitions outputs: //
    ////////////////////////////////////////////////////////////////
    svtkExtractStructuredGridHelper::GetPartitionedVOI(
      globalVOI, inputExtent, this->SampleRate, this->IncludeBoundary != 0, partitionedVOI);
  }
  DEBUG_EXTENT("PartitionedVOI", partitionedVOI);

  if (partitionContainsVOI)
  {
    ////////////////////////////////////////////////////////////////
    // 2) Compute and update the output dataset's actual extents. //
    ////////////////////////////////////////////////////////////////
    svtkExtractStructuredGridHelper::GetPartitionedOutputExtent(globalVOI, partitionedVOI,
      outputWholeExtent, this->SampleRate, this->IncludeBoundary != 0, partitionedOutputExtent);
    output->SetExtent(partitionedOutputExtent);
  }
  DEBUG_EXTENT("PartitionedOutputExtent", partitionedOutputExtent);

  if (partitionContainsVOI)
  {
    ////////////////////////////////////////////////////////////
    // 3) Extract actual VOI using superclass implementation: //
    ////////////////////////////////////////////////////////////
    if (!this->Superclass::RequestDataImpl(inputVector, outputVector))
    {
      return 0;
    }
  }

  //////////////////////////////
  // 4: Detect & resolve gaps //
  //////////////////////////////
  svtkStructuredImplicitConnectivity* gridConnectivity = svtkStructuredImplicitConnectivity::New();
  gridConnectivity->SetWholeExtent(outputWholeExtent);

  // Register the grid, grid ID is the same as the process ID
  gridConnectivity->RegisterRectilinearGrid(this->Controller->GetLocalProcessId(),
    output->GetExtent(), output->GetXCoordinates(), output->GetYCoordinates(),
    output->GetZCoordinates(), output->GetPointData());

  // Establish neighbor connectivity & detect any gaps
  gridConnectivity->EstablishConnectivity();

  // Check if there are any gaps, if any close them now
  if (gridConnectivity->HasImplicitConnectivity())
  {
    DEBUG_OUT("Closing gaps...\n");
    // there are gaps, grow the grid to the right
    gridConnectivity->ExchangeData();

    gridConnectivity->GetOutputRectilinearGrid(this->Controller->GetLocalProcessId(), output);
  }

  gridConnectivity->Delete();

#ifdef DEBUG
  int finalOutputExtent[6];
  output->GetExtent(finalOutputExtent);
  DEBUG_EXTENT("FinalOutputExtent", finalOutputExtent);
#endif

  return 1;
}

//------------------------------------------------------------------------------
int svtkPExtractRectilinearGrid::RequestInformation(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  DEBUG_OUT("########### RequestInformation\n");

  return this->Superclass::RequestInformation(request, inputVector, outputVector);
}

//------------------------------------------------------------------------------
int svtkPExtractRectilinearGrid::RequestUpdateExtent(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  DEBUG_OUT("########### RequestUpdateExtent\n");

  return this->Superclass::RequestUpdateExtent(request, inputVector, outputVector);
}