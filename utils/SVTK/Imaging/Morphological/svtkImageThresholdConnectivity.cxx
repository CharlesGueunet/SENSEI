/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageThresholdConnectivity.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "svtkImageThresholdConnectivity.h"

#include "svtkImageData.h"
#include "svtkImageIterator.h"
#include "svtkImageStencilData.h"
#include "svtkImageStencilIterator.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMath.h"
#include "svtkObjectFactory.h"
#include "svtkPoints.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkTemplateAliasMacro.h"

#include <stack>

svtkStandardNewMacro(svtkImageThresholdConnectivity);
svtkCxxSetObjectMacro(svtkImageThresholdConnectivity, SeedPoints, svtkPoints);

//----------------------------------------------------------------------------
// Constructor sets default values
svtkImageThresholdConnectivity::svtkImageThresholdConnectivity()
{
  this->UpperThreshold = SVTK_FLOAT_MAX;
  this->LowerThreshold = -SVTK_FLOAT_MAX;
  this->SeedPoints = nullptr;
  this->ReplaceIn = 0;
  this->InValue = 0.0;
  this->ReplaceOut = 0;
  this->OutValue = 0.0;

  this->NeighborhoodRadius[0] = 0.0;
  this->NeighborhoodRadius[1] = 0.0;
  this->NeighborhoodRadius[2] = 0.0;
  this->NeighborhoodFraction = 0.5;

  this->SliceRangeX[0] = -SVTK_INT_MAX;
  this->SliceRangeX[1] = SVTK_INT_MAX;
  this->SliceRangeY[0] = -SVTK_INT_MAX;
  this->SliceRangeY[1] = SVTK_INT_MAX;
  this->SliceRangeZ[0] = -SVTK_INT_MAX;
  this->SliceRangeZ[1] = SVTK_INT_MAX;

  this->ActiveComponent = -1;

  this->ImageMask = svtkImageData::New();

  this->NumberOfInVoxels = 0;

  this->SetNumberOfInputPorts(2);
}

//----------------------------------------------------------------------------
svtkImageThresholdConnectivity::~svtkImageThresholdConnectivity()
{
  if (this->SeedPoints)
  {
    this->SeedPoints->Delete();
  }
  this->ImageMask->Delete();
}

//----------------------------------------------------------------------------
void svtkImageThresholdConnectivity::SetInValue(double val)
{
  if (val != this->InValue || this->ReplaceIn != 1)
  {
    this->InValue = val;
    this->ReplaceIn = 1;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
void svtkImageThresholdConnectivity::SetOutValue(double val)
{
  if (val != this->OutValue || this->ReplaceOut != 1)
  {
    this->OutValue = val;
    this->ReplaceOut = 1;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
// The values greater than or equal to the value match.
void svtkImageThresholdConnectivity::ThresholdByUpper(double thresh)
{
  if (this->LowerThreshold != thresh || this->UpperThreshold < SVTK_FLOAT_MAX)
  {
    this->LowerThreshold = thresh;
    this->UpperThreshold = SVTK_FLOAT_MAX;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
// The values less than or equal to the value match.
void svtkImageThresholdConnectivity::ThresholdByLower(double thresh)
{
  if (this->UpperThreshold != thresh || this->LowerThreshold > -SVTK_FLOAT_MAX)
  {
    this->UpperThreshold = thresh;
    this->LowerThreshold = -SVTK_FLOAT_MAX;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
// The values in a range (inclusive) match
void svtkImageThresholdConnectivity::ThresholdBetween(double lower, double upper)
{
  if (this->LowerThreshold != lower || this->UpperThreshold != upper)
  {
    this->LowerThreshold = lower;
    this->UpperThreshold = upper;
    this->Modified();
  }
}

//----------------------------------------------------------------------------
int svtkImageThresholdConnectivity::FillInputPortInformation(int port, svtkInformation* info)
{
  if (port == 1)
  {
    info->Set(svtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "svtkImageStencilData");
    info->Set(svtkAlgorithm::INPUT_IS_OPTIONAL(), 1);
  }
  else
  {
    info->Set(svtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "svtkImageData");
  }
  return 1;
}

//----------------------------------------------------------------------------
void svtkImageThresholdConnectivity::SetStencilData(svtkImageStencilData* stencil)
{
  this->SetInputData(1, stencil);
}

//----------------------------------------------------------------------------
svtkImageStencilData* svtkImageThresholdConnectivity::GetStencil()
{
  if (this->GetNumberOfInputConnections(1) < 1)
  {
    return nullptr;
  }

  return svtkImageStencilData::SafeDownCast(this->GetExecutive()->GetInputData(1, 0));
}

//----------------------------------------------------------------------------
svtkMTimeType svtkImageThresholdConnectivity::GetMTime()
{
  svtkMTimeType mTime = this->MTime.GetMTime();
  svtkMTimeType pointsMTime;

  if (this->SeedPoints)
  {
    pointsMTime = this->SeedPoints->GetMTime();
    mTime = (pointsMTime > mTime ? pointsMTime : mTime);
  }

  return mTime;
}

//----------------------------------------------------------------------------
// seed struct: just a set of indices
class svtkFloodFillSeed
{
public:
  svtkFloodFillSeed()
  {
    store[0] = 0;
    store[1] = 0;
    store[2] = 0;
  }
  svtkFloodFillSeed(int i, int j, int k)
  {
    store[0] = i;
    store[1] = j;
    store[2] = k;
  }
  svtkFloodFillSeed(const svtkFloodFillSeed& seed)
  {
    store[0] = seed.store[0];
    store[1] = seed.store[1];
    store[2] = seed.store[2];
  }
  const int& operator[](int i) const { return store[i]; }
  svtkFloodFillSeed& operator=(const svtkFloodFillSeed& seed)
  {
    store[0] = seed.store[0];
    store[1] = seed.store[1];
    store[2] = seed.store[2];
    return *this;
  }

private:
  int store[3];
};

//----------------------------------------------------------------------------
// Make sure the thresholds are valid for the input scalar range
template <class IT>
void svtkImageThresholdConnectivityThresholds(
  svtkImageThresholdConnectivity* self, svtkImageData* inData, IT& lowerThreshold, IT& upperThreshold)
{
  if (self->GetLowerThreshold() < inData->GetScalarTypeMin())
  {
    lowerThreshold = static_cast<IT>(inData->GetScalarTypeMin());
  }
  else
  {
    if (self->GetLowerThreshold() > inData->GetScalarTypeMax())
    {
      lowerThreshold = static_cast<IT>(inData->GetScalarTypeMax());
    }
    else
    {
      lowerThreshold = static_cast<IT>(self->GetLowerThreshold());
    }
  }
  if (self->GetUpperThreshold() > inData->GetScalarTypeMax())
  {
    upperThreshold = static_cast<IT>(inData->GetScalarTypeMax());
  }
  else
  {
    if (self->GetUpperThreshold() < inData->GetScalarTypeMin())
    {
      upperThreshold = static_cast<IT>(inData->GetScalarTypeMin());
    }
    else
    {
      upperThreshold = static_cast<IT>(self->GetUpperThreshold());
    }
  }
}

//----------------------------------------------------------------------------
// Make sure the replacement values are within the output scalar range
template <class OT>
void svtkImageThresholdConnectivityValues(
  svtkImageThresholdConnectivity* self, svtkImageData* outData, OT& inValue, OT& outValue)
{
  if (self->GetInValue() < outData->GetScalarTypeMin())
  {
    inValue = static_cast<OT>(outData->GetScalarTypeMin());
  }
  else
  {
    if (self->GetInValue() > outData->GetScalarTypeMax())
    {
      inValue = static_cast<OT>(outData->GetScalarTypeMax());
    }
    else
    {
      inValue = static_cast<OT>(self->GetInValue());
    }
  }
  if (self->GetOutValue() > outData->GetScalarTypeMax())
  {
    outValue = static_cast<OT>(outData->GetScalarTypeMax());
  }
  else
  {
    if (self->GetOutValue() < outData->GetScalarTypeMin())
    {
      outValue = static_cast<OT>(outData->GetScalarTypeMin());
    }
    else
    {
      outValue = static_cast<OT>(self->GetOutValue());
    }
  }
}

//----------------------------------------------------------------------------
static void svtkImageThresholdConnectivityApplyStencil(
  svtkImageData* maskData, svtkImageStencilData* stencil, int extent[6])
{
  svtkImageStencilIterator<unsigned char> iter(maskData, stencil, extent);
  while (!iter.IsAtEnd())
  {
    unsigned char* beginptr = iter.BeginSpan();
    unsigned char* endptr = iter.EndSpan();
    unsigned char val = (iter.IsInStencil() ? 0 : 1);

    for (unsigned char* ptr = beginptr; ptr < endptr; ptr++)
    {
      *ptr = val;
    }

    iter.NextSpan();
  }
}

//----------------------------------------------------------------------------
// This templated function executes the filter for any type of data.
template <class IT, class OT>
void svtkImageThresholdConnectivityExecute(svtkImageThresholdConnectivity* self, svtkImageData* inData,
  svtkImageData* outData, svtkImageStencilData* stencil, svtkImageData* maskData, int outExt[6],
  int id, IT* inPtr, OT* outPtr, int& voxelCount)
{
  // Get active component (only one component is thresholded)
  int nComponents = outData->GetNumberOfScalarComponents();
  int activeComponent = self->GetActiveComponent();
  if (activeComponent < 0)
  {
    activeComponent = 0;
  }
  activeComponent = activeComponent % nComponents;

  // Get thresholds as input data type
  IT lowerThreshold, upperThreshold;
  svtkImageThresholdConnectivityThresholds(self, inData, lowerThreshold, upperThreshold);

  // Get replace values as output data type
  bool replaceIn = (self->GetReplaceIn() != 0);
  bool replaceOut = (self->GetReplaceOut() != 0);
  OT inValue, outValue;
  svtkImageThresholdConnectivityValues(self, outData, inValue, outValue);

  // Set the "outside" with either the input or the OutValue
  svtkImageIterator<IT> inIt(inData, outExt);
  svtkImageIterator<OT> outIt(outData, outExt);
  while (!outIt.IsAtEnd())
  {
    IT* inSI = inIt.BeginSpan();
    OT* outSI = outIt.BeginSpan();
    OT* outSIEnd = outIt.EndSpan();

    if (replaceOut)
    {
      if (nComponents == 1)
      {
        while (outSI < outSIEnd)
        {
          *outSI++ = outValue;
        }
      }
      else
      {
        // only color the active component, copy the rest
        while (outSI < outSIEnd)
        {
          int jj = 0;
          while (jj < activeComponent)
          {
            *outSI++ = static_cast<OT>(*inSI++);
            jj++;
          }
          *outSI++ = outValue;
          inSI++;
          jj++;
          while (jj < nComponents)
          {
            *outSI++ = static_cast<OT>(*inSI++);
            jj++;
          }
        }
      }
    }
    else
    {
      while (outSI < outSIEnd)
      {
        *outSI++ = static_cast<OT>(*inSI++);
      }
    }
    inIt.NextSpan();
    outIt.NextSpan();
  }

  // Get the extent for the flood fill, and clip with the input extent
  int extent[6];
  int inExt[6];
  self->GetSliceRangeX(&extent[0]);
  self->GetSliceRangeY(&extent[2]);
  self->GetSliceRangeZ(&extent[4]);
  inData->GetExtent(inExt);
  int outCheck = 0;
  for (int ii = 0; ii < 3; ii++)
  {
    if (extent[2 * ii] > inExt[2 * ii + 1] || extent[2 * ii + 1] < inExt[2 * ii])
    { // extents don't intersect, we're done
      return;
    }
    if (extent[2 * ii] < inExt[2 * ii])
    {
      extent[2 * ii] = inExt[2 * ii];
    }
    if (extent[2 * ii + 1] > inExt[2 * ii + 1])
    {
      extent[2 * ii + 1] = inExt[2 * ii + 1];
    }
    // check against output extent
    if (extent[2 * ii] < outExt[2 * ii] || extent[2 * ii + 1] > outExt[2 * ii + 1])
    {
      outCheck = 1;
    }
  }

  // Indexing goes from 0 to maxIdX
  int maxIdX = extent[1] - extent[0];
  int maxIdY = extent[3] - extent[2];
  int maxIdZ = extent[5] - extent[4];

  // Convert output limits
  int minOutIdX = outExt[0] - extent[0];
  int maxOutIdX = outExt[1] - extent[0];
  int minOutIdY = outExt[2] - extent[2];
  int maxOutIdY = outExt[3] - extent[2];
  int minOutIdZ = outExt[4] - extent[4];
  int maxOutIdZ = outExt[5] - extent[4];

  // Total number of voxels
  svtkIdType fullsize = (static_cast<svtkIdType>(extent[1] - extent[0] + 1) *
    static_cast<svtkIdType>(extent[3] - extent[2] + 1) *
    static_cast<svtkIdType>(extent[5] - extent[4] + 1));

  // for the progress meter
  double progress = 0.0;
  svtkIdType target = static_cast<svtkIdType>(fullsize / 50.0);
  target++;

  // Setup the mask
  maskData->SetOrigin(inData->GetOrigin());
  maskData->SetSpacing(inData->GetSpacing());
  maskData->SetExtent(extent);
  maskData->AllocateScalars(SVTK_UNSIGNED_CHAR, 1);

  unsigned char* maskPtr = static_cast<unsigned char*>(maskData->GetScalarPointerForExtent(extent));
  svtkIdType maskInc[3];
  maskInc[0] = 1;
  maskInc[1] = (extent[1] - extent[0] + 1);
  maskInc[2] = maskInc[1] * (extent[3] - extent[2] + 1);

  // Get input pointer for the extent used by the maskData
  inPtr = static_cast<IT*>(inData->GetScalarPointerForExtent(extent));
  svtkIdType inInc[3];
  inData->GetIncrements(inInc);

  // Get output pointer for the whole output extent
  outPtr = static_cast<OT*>(outData->GetScalarPointerForExtent(outExt));
  svtkIdType outInc[3];
  outData->GetIncrements(outInc);
  // Adjust it so that it corresponds to the maskData extent
  outPtr -= minOutIdX * outInc[0] + minOutIdY * outInc[1] + minOutIdZ * outInc[2];

  // Adjust pointers to active component
  inPtr += activeComponent;
  outPtr += activeComponent;

  if (stencil == nullptr)
  {
    memset(maskPtr, 0, fullsize);
  }
  else
  {
    // pre-set all mask voxels that are outside the stencil
    svtkImageThresholdConnectivityApplyStencil(maskData, stencil, extent);
  }

  // Check whether neighborhood will be used
  double f = self->GetNeighborhoodFraction();
  double radius[3];
  self->GetNeighborhoodRadius(radius);
  int xradius = static_cast<int>(radius[0] + 0.5);
  int yradius = static_cast<int>(radius[1] + 0.5);
  int zradius = static_cast<int>(radius[2] + 0.5);
  double fx = 0.0, fy = 0.0, fz = 0.0;
  bool useNeighborhood = ((xradius > 0) & (yradius > 0) & (zradius > 0));
  if (useNeighborhood)
  {
    fx = 1.0 / radius[0];
    fy = 1.0 / radius[1];
    fz = 1.0 / radius[2];
  }

  // Perform the flood fill within the extent
  double spacing[3];
  double origin[3];
  outData->GetSpacing(spacing);
  outData->GetOrigin(origin);

  // create the seed stack:
  // stack has methods empty(), top(), push(), and pop()
  std::stack<svtkFloodFillSeed> seedStack;

  // initialize with the seeds provided by the user
  svtkPoints* points = self->GetSeedPoints();
  if (points == nullptr)
  { // no seeds!
    return;
  }

  double point[3];
  svtkIdType nPoints = points->GetNumberOfPoints();
  for (svtkIdType p = 0; p < nPoints; p++)
  {
    points->GetPoint(p, point);
    svtkFloodFillSeed seed =
      svtkFloodFillSeed(svtkMath::Floor((point[0] - origin[0]) / spacing[0] + 0.5) - extent[0],
        svtkMath::Floor((point[1] - origin[1]) / spacing[1] + 0.5) - extent[2],
        svtkMath::Floor((point[2] - origin[2]) / spacing[2] + 0.5) - extent[4]);

    if (seed[0] >= 0 && seed[0] <= maxIdX && seed[1] >= 0 && seed[1] <= maxIdY && seed[2] >= 0 &&
      seed[2] <= maxIdZ)
    {
      seedStack.push(seed);
    }
  }

  svtkIdType counter = 0;
  svtkIdType fullcount = 0;

  while (!seedStack.empty())
  {
    svtkFloodFillSeed seed = seedStack.top();
    seedStack.pop();

    unsigned char* maskPtr1 =
      maskPtr + (seed[0] * maskInc[0] + seed[1] * maskInc[1] + seed[2] * maskInc[2]);

    if (*maskPtr1)
    {
      continue;
    }
    *maskPtr1 = 255;

    fullcount++;
    if (id == 0 && (fullcount % target) == 0)
    {
      double v = counter / (10.0 * fullcount);
      double p = fullcount / (v * fullsize + (1.0 - v) * fullcount);
      if (p > progress)
      {
        progress = p;
        self->UpdateProgress(progress);
      }
    }

    IT* inPtr1 = inPtr + (seed[0] * inInc[0] + seed[1] * inInc[1] + seed[2] * inInc[2]);
    IT temp = *inPtr1;

    bool inside = ((lowerThreshold <= temp) & (temp <= upperThreshold));

    // use a spherical neighborhood
    if (useNeighborhood)
    {
      int xmin = seed[0] - xradius;
      xmin = (xmin >= 0 ? xmin : 0);
      int xmax = seed[0] + xradius;
      xmax = (xmax <= maxIdX ? xmax : maxIdX);

      int ymin = seed[1] - yradius;
      ymin = (ymin >= 0 ? ymin : 0);
      int ymax = seed[1] + yradius;
      ymax = (ymax <= maxIdY ? ymax : maxIdY);

      int zmin = seed[2] - zradius;
      zmin = (zmin >= 0 ? zmin : 0);
      int zmax = seed[2] + zradius;
      zmax = (zmax <= maxIdZ ? zmax : maxIdZ);

      inPtr1 = inPtr + (xmin * inInc[0] + ymin * inInc[1] + zmin * inInc[2]);

      int totalcount = 0;
      int threshcount = 0;
      int iz = zmin;
      do
      {
        IT* inPtr2 = inPtr1;
        double rz = (iz - seed[2]) * fz;
        rz *= rz;
        int iy = ymin;
        do
        {
          IT* inPtr3 = inPtr2;
          double ry = (iy - seed[1]) * fy;
          ry *= ry;
          double rzy = rz + ry;
          int ix = xmin;
          do
          {
            double rx = (ix - seed[0]) * fx;
            rx *= rx;
            double rzyx = rzy + rx;
            // include a tolerance in radius check
            bool isgood = (rzyx < (1.0 + 7.62939453125e-06));
            totalcount += isgood;
            isgood &= ((lowerThreshold <= *inPtr3) & (*inPtr3 <= upperThreshold));
            threshcount += isgood;
            inPtr3 += inInc[0];
          } while (++ix <= xmax);
          inPtr2 += inInc[1];
        } while (++iy <= ymax);
        inPtr1 += inInc[2];
      } while (++iz <= zmax);

      // what fraction of the sphere is within threshold?
      inside &= !(static_cast<double>(threshcount) < totalcount * f);
    }

    if (inside)
    {
      // match
      OT* outPtr1 = outPtr + (seed[0] * outInc[0] + seed[1] * outInc[1] + seed[2] * outInc[2]);

      if (outCheck == 0 ||
        (seed[0] >= minOutIdX && seed[0] <= maxOutIdX && seed[1] >= minOutIdY &&
          seed[1] <= maxOutIdY && seed[2] >= minOutIdZ && seed[2] <= maxOutIdZ))
      {
        *outPtr1 = (replaceIn ? inValue : static_cast<OT>(temp));
      }

      // count the seed
      counter += 1;

      // push the new seeds
      if (seed[2] > 0 && *(maskPtr1 - maskInc[2]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0], seed[1], seed[2] - 1));
      }
      if (seed[2] < maxIdZ && *(maskPtr1 + maskInc[2]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0], seed[1], seed[2] + 1));
      }
      if (seed[1] > 0 && *(maskPtr1 - maskInc[1]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0], seed[1] - 1, seed[2]));
      }
      if (seed[1] < maxIdY && *(maskPtr1 + maskInc[1]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0], seed[1] + 1, seed[2]));
      }
      if (seed[0] > 0 && *(maskPtr1 - maskInc[0]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0] - 1, seed[1], seed[2]));
      }
      if (seed[0] < maxIdX && *(maskPtr1 + maskInc[0]) == 0)
      {
        seedStack.push(svtkFloodFillSeed(seed[0] + 1, seed[1], seed[2]));
      }
    }
  }

  if (id == 0)
  {
    self->UpdateProgress(1.0);
  }

  voxelCount = counter;
}

//----------------------------------------------------------------------------
int svtkImageThresholdConnectivity::RequestUpdateExtent(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** inputVector, svtkInformationVector* svtkNotUsed(outputVector))
{
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* stencilInfo = inputVector[1]->GetInformationObject(0);

  int inExt[6], extent[6];
  inInfo->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), inExt);
  extent[0] = this->SliceRangeX[0];
  extent[1] = this->SliceRangeX[1];
  extent[2] = this->SliceRangeY[0];
  extent[3] = this->SliceRangeY[1];
  extent[4] = this->SliceRangeZ[0];
  extent[5] = this->SliceRangeZ[1];

  // Clip the extent to the inExt
  for (int i = 0; i < 3; i++)
  {
    if (extent[2 * i] < inExt[2 * i])
    {
      extent[2 * i] = inExt[2 * i];
    }
    if (extent[2 * i + 1] > inExt[2 * i + 1])
    {
      extent[2 * i + 1] = inExt[2 * i + 1];
    }
  }

  inInfo->Set(svtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), extent, 6);
  if (stencilInfo)
  {
    stencilInfo->Set(svtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), extent, 6);
  }

  return 1;
}

//----------------------------------------------------------------------------
int svtkImageThresholdConnectivity::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  svtkInformation* outInfo = outputVector->GetInformationObject(0);
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* stencilInfo = inputVector[1]->GetInformationObject(0);

  svtkImageData* outData = static_cast<svtkImageData*>(outInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkImageData* inData = static_cast<svtkImageData*>(inInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkImageData* maskData = this->ImageMask;

  svtkImageStencilData* stencil = nullptr;
  if (stencilInfo)
  {
    stencil = static_cast<svtkImageStencilData*>(stencilInfo->Get(svtkDataObject::DATA_OBJECT()));
  }

  int outExt[6];
  outInfo->Get(svtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), outExt);
  this->AllocateOutputData(outData, outInfo, outExt);

  // get scalar pointers
  void* inPtr = inData->GetScalarPointerForExtent(outExt);
  void* outPtr = outData->GetScalarPointerForExtent(outExt);

  int id = 0; // not multi-threaded

  if (inData->GetScalarType() != outData->GetScalarType())
  {
    svtkErrorMacro("Execute: Output ScalarType "
      << outData->GetScalarType() << ", must Input ScalarType " << inData->GetScalarType());
    return 0;
  }

  switch (inData->GetScalarType())
  {
    svtkTemplateAliasMacro(
      svtkImageThresholdConnectivityExecute(this, inData, outData, stencil, maskData, outExt, id,
        static_cast<SVTK_TT*>(inPtr), static_cast<SVTK_TT*>(outPtr), this->NumberOfInVoxels));

    default:
      svtkErrorMacro(<< "Execute: Unknown input ScalarType");
      return 0;
  }

  return 1;
}

//----------------------------------------------------------------------------
void svtkImageThresholdConnectivity::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "InValue: " << this->InValue << "\n";
  os << indent << "OutValue: " << this->OutValue << "\n";
  os << indent << "LowerThreshold: " << this->LowerThreshold << "\n";
  os << indent << "UpperThreshold: " << this->UpperThreshold << "\n";
  os << indent << "ReplaceIn: " << this->ReplaceIn << "\n";
  os << indent << "ReplaceOut: " << this->ReplaceOut << "\n";
  os << indent << "NeighborhoodRadius: " << this->NeighborhoodRadius[0] << " "
     << this->NeighborhoodRadius[1] << " " << this->NeighborhoodRadius[2] << "\n";
  os << indent << "NeighborhoodFraction: " << this->NeighborhoodFraction << "\n";
  os << indent << "NumberOfInVoxels: " << this->NumberOfInVoxels << "\n";
  os << indent << "SliceRangeX: " << this->SliceRangeX[0] << " " << this->SliceRangeX[1] << "\n";
  os << indent << "SliceRangeY: " << this->SliceRangeY[0] << " " << this->SliceRangeY[1] << "\n";
  os << indent << "SliceRangeZ: " << this->SliceRangeZ[0] << " " << this->SliceRangeZ[1] << "\n";
  os << indent << "SeedPoints: " << this->SeedPoints << "\n";
  if (this->SeedPoints)
  {
    this->SeedPoints->PrintSelf(os, indent.GetNextIndent());
  }
  os << indent << "Stencil: " << this->GetStencil() << "\n";
  os << indent << "ActiveComponent: " << this->ActiveComponent << "\n";
}