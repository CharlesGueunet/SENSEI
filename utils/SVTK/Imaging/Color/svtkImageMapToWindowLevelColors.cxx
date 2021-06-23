/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageMapToWindowLevelColors.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkImageMapToWindowLevelColors.h"

#include "svtkDataArray.h"
#include "svtkImageData.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkScalarsToColors.h"

svtkStandardNewMacro(svtkImageMapToWindowLevelColors);

// Constructor sets default values
svtkImageMapToWindowLevelColors::svtkImageMapToWindowLevelColors()
{
  this->Window = 255;
  this->Level = 127.5;
}

svtkImageMapToWindowLevelColors::~svtkImageMapToWindowLevelColors() = default;

//----------------------------------------------------------------------------
// This method checks to see if we can simply reference the input data
int svtkImageMapToWindowLevelColors::RequestData(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  svtkInformation* outInfo = outputVector->GetInformationObject(0);
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);

  svtkImageData* outData = svtkImageData::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));
  svtkImageData* inData = svtkImageData::SafeDownCast(inInfo->Get(svtkDataObject::DATA_OBJECT()));

  // If LookupTable is null and window / level produces no change,
  // then just pass the data
  if (this->LookupTable == nullptr &&
    (inData->GetScalarType() == SVTK_UNSIGNED_CHAR && this->Window == 255 && this->Level == 127.5))
  {
    svtkDebugMacro("ExecuteData: LookupTable not set, "
                  "Window / Level at default, "
                  "passing input to output.");

    outData->SetExtent(inData->GetExtent());
    outData->GetPointData()->PassData(inData->GetPointData());
    this->DataWasPassed = 1;
  }
  else
  // normal behaviour - skip up a level since we don't want to
  // call the superclasses ExecuteData - it would pass the data if there
  // is no lookup table even if there is a window / level - wrong
  // behavior.
  {
    if (this->DataWasPassed)
    {
      outData->GetPointData()->SetScalars(nullptr);
      this->DataWasPassed = 0;
    }

    return this->svtkThreadedImageAlgorithm::RequestData(request, inputVector, outputVector);
  }

  return 1;
}

//----------------------------------------------------------------------------
int svtkImageMapToWindowLevelColors::RequestInformation(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  svtkInformation* inScalarInfo = svtkDataObject::GetActiveFieldInformation(
    inInfo, svtkDataObject::FIELD_ASSOCIATION_POINTS, svtkDataSetAttributes::SCALARS);
  if (!inScalarInfo)
  {
    svtkErrorMacro("Missing scalar field on input information!");
    return 0;
  }

  // If LookupTable is null and window / level produces no change,
  // then the data will be passed
  if (this->LookupTable == nullptr &&
    (inScalarInfo->Get(svtkDataObject::FIELD_ARRAY_TYPE()) == SVTK_UNSIGNED_CHAR &&
      this->Window == 255 && this->Level == 127.5))
  {
    if (inScalarInfo->Get(svtkDataObject::FIELD_ARRAY_TYPE()) != SVTK_UNSIGNED_CHAR)
    {
      svtkErrorMacro(
        "ExecuteInformation: No LookupTable was set and input data is not SVTK_UNSIGNED_CHAR!");
    }
    else
    {
      // no lookup table, pass the input if it was SVTK_UNSIGNED_CHAR
      svtkDataObject::SetPointDataActiveScalarInfo(
        outInfo, SVTK_UNSIGNED_CHAR, inScalarInfo->Get(svtkDataObject::FIELD_NUMBER_OF_COMPONENTS()));
    }
  }
  else // the lookup table was set or window / level produces a change
  {
    int numComponents = 4;
    switch (this->OutputFormat)
    {
      case SVTK_RGBA:
        numComponents = 4;
        break;
      case SVTK_RGB:
        numComponents = 3;
        break;
      case SVTK_LUMINANCE_ALPHA:
        numComponents = 2;
        break;
      case SVTK_LUMINANCE:
        numComponents = 1;
        break;
      default:
        svtkErrorMacro("ExecuteInformation: Unrecognized color format.");
        break;
    }
    svtkDataObject::SetPointDataActiveScalarInfo(outInfo, SVTK_UNSIGNED_CHAR, numComponents);
  }

  return 1;
}

/*
 * This templated routine calculates effective lower and upper limits
 * for a window of values of type T, lower and upper.
 */
template <class T>
void svtkImageMapToWindowLevelClamps(svtkImageData* data, double w, double l, T& lower, T& upper,
  unsigned char& lower_val, unsigned char& upper_val)
{
  double f_lower, f_upper, f_lower_val, f_upper_val;
  double adjustedLower, adjustedUpper;
  double range[2];

  data->GetPointData()->GetScalars()->GetDataTypeRange(range);

  f_lower = l - fabs(w) / 2.0;
  f_upper = f_lower + fabs(w);

  // Set the correct lower value
  if (f_lower <= range[1])
  {
    if (f_lower >= range[0])
    {
      lower = static_cast<T>(f_lower);
      adjustedLower = f_lower;
    }
    else
    {
      lower = static_cast<T>(range[0]);
      adjustedLower = range[0];
    }
  }
  else
  {
    lower = static_cast<T>(range[1]);
    adjustedLower = range[1];
  }

  // Set the correct upper value
  if (f_upper >= range[0])
  {
    if (f_upper <= range[1])
    {
      upper = static_cast<T>(f_upper);
      adjustedUpper = f_upper;
    }
    else
    {
      upper = static_cast<T>(range[1]);
      adjustedUpper = range[1];
    }
  }
  else
  {
    upper = static_cast<T>(range[0]);
    adjustedUpper = range[0];
  }

  // now compute the lower and upper values
  if (w > 0.0)
  {
    f_lower_val = 255.0 * (adjustedLower - f_lower) / w;
    f_upper_val = 255.0 * (adjustedUpper - f_lower) / w;
  }
  else if (w < 0.0)
  {
    f_lower_val = 255.0 + 255.0 * (adjustedLower - f_lower) / w;
    f_upper_val = 255.0 + 255.0 * (adjustedUpper - f_lower) / w;
  }
  else
  {
    f_lower_val = 0.0;
    f_upper_val = 255.0;
  }

  if (f_upper_val > 255)
  {
    upper_val = 255;
  }
  else if (f_upper_val < 0)
  {
    upper_val = 0;
  }
  else
  {
    upper_val = static_cast<unsigned char>(f_upper_val);
  }

  if (f_lower_val > 255)
  {
    lower_val = 255;
  }
  else if (f_lower_val < 0)
  {
    lower_val = 0;
  }
  else
  {
    lower_val = static_cast<unsigned char>(f_lower_val);
  }
}

//----------------------------------------------------------------------------
// This non-templated function executes the filter for any type of data.
template <class T>
void svtkImageMapToWindowLevelColorsExecute(svtkImageMapToWindowLevelColors* self,
  svtkImageData* inData, T* inPtr, svtkImageData* outData, unsigned char* outPtr, int outExt[6],
  int id)
{
  int idxX, idxY, idxZ;
  int extX, extY, extZ;
  svtkIdType inIncX, inIncY, inIncZ;
  svtkIdType outIncX, outIncY, outIncZ;
  unsigned long count = 0;
  unsigned long target;
  int dataType = inData->GetScalarType();
  int numberOfComponents, numberOfOutputComponents, outputFormat;
  int rowLength;
  svtkScalarsToColors* lookupTable = self->GetLookupTable();
  unsigned char* outPtr1;
  T* inPtr1;
  unsigned char* optr;
  T* iptr;
  double shift = self->GetWindow() / 2.0 - self->GetLevel();
  double scale = 255.0 / self->GetWindow();

  T lower, upper;
  unsigned char lower_val, upper_val, result_val;
  unsigned short ushort_val;
  svtkImageMapToWindowLevelClamps(
    inData, self->GetWindow(), self->GetLevel(), lower, upper, lower_val, upper_val);

  // find the region to loop over
  extX = outExt[1] - outExt[0] + 1;
  extY = outExt[3] - outExt[2] + 1;
  extZ = outExt[5] - outExt[4] + 1;

  target = static_cast<unsigned long>(extZ * extY / 50.0);
  target++;

  // Get increments to march through data
  inData->GetContinuousIncrements(outExt, inIncX, inIncY, inIncZ);

  outData->GetContinuousIncrements(outExt, outIncX, outIncY, outIncZ);
  numberOfComponents = inData->GetNumberOfScalarComponents();
  numberOfOutputComponents = outData->GetNumberOfScalarComponents();
  outputFormat = self->GetOutputFormat();

  rowLength = extX * numberOfComponents;

  // Loop through output pixels
  outPtr1 = outPtr;
  inPtr1 = inPtr;
  for (idxZ = 0; idxZ < extZ; idxZ++)
  {
    for (idxY = 0; !self->AbortExecute && idxY < extY; idxY++)
    {
      if (!id)
      {
        if (!(count % target))
        {
          self->UpdateProgress(count / (50.0 * target));
        }
        count++;
      }

      iptr = inPtr1;
      optr = outPtr1;

      if (lookupTable)
      {
        lookupTable->MapScalarsThroughTable2(inPtr1, static_cast<unsigned char*>(outPtr1), dataType,
          extX, numberOfComponents, outputFormat);

        for (idxX = 0; idxX < extX; idxX++)
        {
          if (*iptr <= lower)
          {
            ushort_val = lower_val;
          }
          else if (*iptr >= upper)
          {
            ushort_val = upper_val;
          }
          else
          {
            ushort_val = static_cast<unsigned char>((*iptr + shift) * scale);
          }
          *optr = static_cast<unsigned char>((*optr * ushort_val) >> 8);
          switch (outputFormat)
          {
            case SVTK_RGBA:
              *(optr + 1) = static_cast<unsigned char>((*(optr + 1) * ushort_val) >> 8);
              *(optr + 2) = static_cast<unsigned char>((*(optr + 2) * ushort_val) >> 8);
              *(optr + 3) = 255;
              break;
            case SVTK_RGB:
              *(optr + 1) = static_cast<unsigned char>((*(optr + 1) * ushort_val) >> 8);
              *(optr + 2) = static_cast<unsigned char>((*(optr + 2) * ushort_val) >> 8);
              break;
            case SVTK_LUMINANCE_ALPHA:
              *(optr + 1) = 255;
              break;
          }
          iptr += numberOfComponents;
          optr += numberOfOutputComponents;
        }
      }
      else
      {
        for (idxX = 0; idxX < extX; idxX++)
        {
          if (*iptr <= lower)
          {
            result_val = lower_val;
          }
          else if (*iptr >= upper)
          {
            result_val = upper_val;
          }
          else
          {
            result_val = static_cast<unsigned char>((*iptr + shift) * scale);
          }
          *optr = result_val;
          switch (outputFormat)
          {
            case SVTK_RGBA:
              *(optr + 1) = result_val;
              *(optr + 2) = result_val;
              *(optr + 3) = 255;
              break;
            case SVTK_RGB:
              *(optr + 1) = result_val;
              *(optr + 2) = result_val;
              break;
            case SVTK_LUMINANCE_ALPHA:
              *(optr + 1) = 255;
              break;
          }
          iptr += numberOfComponents;
          optr += numberOfOutputComponents;
        }
      }
      outPtr1 += outIncY + extX * numberOfOutputComponents;
      inPtr1 += inIncY + rowLength;
    }
    outPtr1 += outIncZ;
    inPtr1 += inIncZ;
  }
}

//----------------------------------------------------------------------------
// This method is passed a input and output data, and executes the filter
// algorithm to fill the output from the input.

void svtkImageMapToWindowLevelColors::ThreadedRequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* svtkNotUsed(outputVector),
  svtkImageData*** inData, svtkImageData** outData, int outExt[6], int id)
{
  void* inPtr = inData[0][0]->GetScalarPointerForExtent(outExt);
  void* outPtr = outData[0]->GetScalarPointerForExtent(outExt);

  switch (inData[0][0]->GetScalarType())
  {
    svtkTemplateMacro(svtkImageMapToWindowLevelColorsExecute(this, inData[0][0],
      static_cast<SVTK_TT*>(inPtr), outData[0], static_cast<unsigned char*>(outPtr), outExt, id));
    default:
      svtkErrorMacro(<< "Execute: Unknown ScalarType");
      return;
  }
}

void svtkImageMapToWindowLevelColors::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Window: " << this->Window << endl;
  os << indent << "Level: " << this->Level << endl;
}