/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkTIFFReader.cxx,v

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkTIFFReader.h"
#include "svtkTIFFReaderInternal.h"

#include "svtkDataArray.h"
#include "svtkErrorCode.h"
#include "svtkImageData.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtksys/SystemTools.hxx"

#include <algorithm>
#include <cassert>
#include <string>

namespace
{
struct FlipTrue
{
};
struct FlipFalse
{
};

int GetFileRow(int row, int height, FlipTrue)
{
  return height - row - 1;
}

int GetFileRow(int row, int, FlipFalse)
{
  return row;
}

// This is inverse of GetFileRow(), which is same as calling GetFileRow()
// again.
template <class Flip>
int GetImageRow(int file_row, int height, Flip flip)
{
  return GetFileRow(file_row, height, flip);
}

bool SupportsRandomAccess(TIFF* image)
{
  unsigned int rowsPerStrip;
  unsigned short compression;
  TIFFGetFieldDefaulted(image, TIFFTAG_COMPRESSION, &compression);
  TIFFGetFieldDefaulted(image, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
  return (compression == COMPRESSION_NONE || rowsPerStrip == 1);
}

bool PurgeInitialScanLinesIfNeeded(int fileStartRow, TIFF* image)
{
  if (fileStartRow == 0 || SupportsRandomAccess(image))
  {
    return true;
  }

  // File doesn't support random access and we want to start at a non-0 row. We
  // read (and discard) initial scanlines.
  unsigned int isize = TIFFScanlineSize(image);
  tdata_t buf = _TIFFmalloc(isize);
  for (int i = 0; i < fileStartRow; ++i)
  {
    if (TIFFReadScanline(image, buf, i, 0) <= 0)
    {
      _TIFFfree(buf);
      return false;
    }
  }
  _TIFFfree(buf);
  return true;
}

// Simple scan line copy of a slice in a volume with tightly packed memory.
template <typename T, typename Flip>
bool ReadTemplatedImage(T* out, Flip flip, int startCol, int endCol, int startRow, int endRow,
  int yIncrements, unsigned int height, TIFF* image)
{
  int fileStartRow = GetFileRow(startRow, height, flip);
  int fileEndRow = GetFileRow(endRow, height, flip);
  int minFileRow = std::min(fileStartRow, fileEndRow);
  int maxFileRow = std::max(fileStartRow, fileEndRow);

  if (!PurgeInitialScanLinesIfNeeded(minFileRow, image))
  {
    return false;
  }

  unsigned int isize = TIFFScanlineSize(image);
  size_t scanLineSize = endCol - startCol + 1;
  if (scanLineSize * sizeof(T) == isize)
  {
    // We can copy straight into the image data output.
    for (int fi = minFileRow; fi <= maxFileRow; ++fi)
    {
      int i = GetImageRow(fi, height, flip);
      T* tmp = out + (i - startRow) * yIncrements;
      if (TIFFReadScanline(image, tmp, fi, 0) <= 0)
      {
        return false;
      }
    }
  }
  else
  {
    // Copy into a buffer of the appropriate size, then subset into the output.
    tdata_t buf = _TIFFmalloc(isize);
    for (int fi = minFileRow; fi <= maxFileRow; ++fi)
    {
      int i = GetImageRow(fi, height, flip);
      T* tmp = out + (i - startRow) * yIncrements;
      if (TIFFReadScanline(image, buf, fi, 0) <= 0)
      {
        _TIFFfree(buf);
        return false;
      }
      memcpy(tmp, static_cast<T*>(buf) + startCol, sizeof(T) * scanLineSize);
    }
    _TIFFfree(buf);
  }
  return true;
}
}

//-------------------------------------------------------------------------
svtkStandardNewMacro(svtkTIFFReader);
extern "C"
{
  static void svtkTIFFReaderInternalErrorHandler(
    const char* svtkNotUsed(module), const char* svtkNotUsed(fmt), va_list svtkNotUsed(ap))
  {
    // Do nothing
    // Ignore errors
  }
}

//-------------------------------------------------------------------------
bool svtkTIFFReader::svtkTIFFReaderInternal::Open(const char* filename)
{
  this->Clean();
  svtksys::SystemTools::Stat_t fs;
  if (svtksys::SystemTools::Stat(filename, &fs))
  {
    return false;
  }
  this->Image = TIFFOpen(filename, "r");
  if (!this->Image)
  {
    this->Clean();
    return false;
  }
  if (!this->Initialize())
  {
    this->Clean();
    return false;
  }

  this->IsOpen = true;
  return true;
}

//-------------------------------------------------------------------------
void svtkTIFFReader::svtkTIFFReaderInternal::Clean()
{
  if (this->Image)
  {
    TIFFClose(this->Image);
    this->Image = nullptr;
  }
  this->Width = 0;
  this->Height = 0;
  this->SamplesPerPixel = 0;
  this->Compression = 0;
  this->BitsPerSample = 0;
  this->Photometrics = 0;
  this->HasValidPhotometricInterpretation = false;
  this->PlanarConfig = 0;
  this->TileDepth = 0;
  this->CurrentPage = 0;
  this->NumberOfPages = 0;
  this->NumberOfTiles = 0;
  this->TileRows = 0;
  this->TileColumns = 0;
  this->TileWidth = 0;
  this->TileHeight = 0;
  this->XResolution = 1;
  this->YResolution = 1;
  this->SubFiles = 0;
  this->SampleFormat = 1;
  this->ResolutionUnit = 1; // none
  this->IsOpen = false;
}

//-------------------------------------------------------------------------
svtkTIFFReader::svtkTIFFReaderInternal::svtkTIFFReaderInternal()
{
  this->Image = nullptr;
  // Note that this suppresses all error/warning output from libtiff!
  TIFFSetErrorHandler(&svtkTIFFReaderInternalErrorHandler);
  TIFFSetWarningHandler(&svtkTIFFReaderInternalErrorHandler);
  this->Clean();
}

//-------------------------------------------------------------------------
bool svtkTIFFReader::svtkTIFFReaderInternal::Initialize()
{
  if (this->Image)
  {
    if (!TIFFGetField(this->Image, TIFFTAG_IMAGEWIDTH, &this->Width) ||
      !TIFFGetField(this->Image, TIFFTAG_IMAGELENGTH, &this->Height))
    {
      return false;
    }

    // Get the resolution in each direction
    TIFFGetField(this->Image, TIFFTAG_XRESOLUTION, &this->XResolution);
    TIFFGetField(this->Image, TIFFTAG_YRESOLUTION, &this->YResolution);
    TIFFGetField(this->Image, TIFFTAG_RESOLUTIONUNIT, &this->ResolutionUnit);

    // Check the number of pages. First by looking at the number of directories.
    this->NumberOfPages = TIFFNumberOfDirectories(this->Image);
    if (this->NumberOfPages == 0)
    {
      if (!TIFFGetField(this->Image, TIFFTAG_PAGENUMBER, &this->CurrentPage, &this->NumberOfPages))
      {
        // Check the Image Description tag to know the number of images
        // This is used by ImageJ
        char** description = new char*[255];
        if (TIFFGetField(this->Image, TIFFTAG_IMAGEDESCRIPTION, description))
        {
          // look for the number of images
          std::string desc = description[0];
          std::string::size_type pos = desc.find("images=");
          std::string::size_type pos2 = desc.find('\n');
          if ((pos != std::string::npos) && (pos2 != std::string::npos))
          {
            this->NumberOfPages = atoi(desc.substr(pos + 7, pos2 - pos - 7).c_str());
          }
        }
      }
    }

    // If the number of pages is still zero we look if the image is tiled.
    if (this->NumberOfPages <= 1 && TIFFIsTiled(this->Image))
    {
      this->NumberOfTiles = TIFFNumberOfTiles(this->Image);

      if (!TIFFGetField(this->Image, TIFFTAG_TILEWIDTH, &this->TileWidth) ||
        !TIFFGetField(this->Image, TIFFTAG_TILELENGTH, &this->TileHeight))
      {
        cerr << "Cannot read tile width and height from file" << endl;
      }
      else
      {
        TileRows = this->Height / this->TileHeight;
        TileColumns = this->Width / this->TileWidth;
      }
    }

    // Checking if the TIFF contains subfiles
    if (this->NumberOfPages > 1)
    {
      this->SubFiles = 0;

      for (unsigned int page = 0; page < this->NumberOfPages; ++page)
      {
        long subfiletype = 6;
        if (TIFFGetField(this->Image, TIFFTAG_SUBFILETYPE, &subfiletype))
        {
          if (subfiletype == 0)
          {
            this->SubFiles += 1;
          }
        }
        TIFFReadDirectory(this->Image);
      }

      // Set the directory to the first image
      TIFFSetDirectory(this->Image, 0);
    }

    // TIFFTAG_ORIENTATION tag from the image data and use it if available.
    // If the tag is not found in the image data, use the ORIENTATION_TOPLEFT by default.
    int status = TIFFGetField(this->Image, TIFFTAG_ORIENTATION, &this->Orientation);
    if (!status)
    {
      this->Orientation = ORIENTATION_TOPLEFT;
    }

    TIFFGetFieldDefaulted(this->Image, TIFFTAG_SAMPLESPERPIXEL, &this->SamplesPerPixel);
    TIFFGetFieldDefaulted(this->Image, TIFFTAG_COMPRESSION, &this->Compression);
    TIFFGetFieldDefaulted(this->Image, TIFFTAG_BITSPERSAMPLE, &this->BitsPerSample);
    TIFFGetFieldDefaulted(this->Image, TIFFTAG_PLANARCONFIG, &this->PlanarConfig);
    TIFFGetFieldDefaulted(this->Image, TIFFTAG_SAMPLEFORMAT, &this->SampleFormat);

    // If SamplesPerPixel is one, then PlanarConfig has no meaning and some
    // files have it set arbitrarily.  Therefore, set it to CONTIG so that
    // the reader will not refuse to read the file on a technicality.
    if (this->SamplesPerPixel == 1)
    {
      this->PlanarConfig = PLANARCONFIG_CONTIG;
    }

    // If TIFFGetField returns false, there's no Photometric Interpretation
    // set for this image, but that's a required field so we set a warning flag.
    // (Because the "Photometrics" field is an enum, we can't rely on setting
    // this->Photometrics to some signal value.)
    if (TIFFGetField(this->Image, TIFFTAG_PHOTOMETRIC, &this->Photometrics))
    {
      this->HasValidPhotometricInterpretation = true;
    }
    else
    {
      this->HasValidPhotometricInterpretation = false;
    }
    if (!TIFFGetField(this->Image, TIFFTAG_TILEDEPTH, &this->TileDepth))
    {
      this->TileDepth = 0;
    }
  }

  return true;
}

//-------------------------------------------------------------------------
bool svtkTIFFReader::svtkTIFFReaderInternal::CanRead()
{
  return (this->Image && (this->Width > 0) && (this->Height > 0) && (this->SamplesPerPixel > 0) &&
    (this->Compression == COMPRESSION_NONE || this->Compression == COMPRESSION_PACKBITS ||
      this->Compression == COMPRESSION_LZW || this->Compression == COMPRESSION_ADOBE_DEFLATE) &&
    (this->HasValidPhotometricInterpretation) &&
    (this->Photometrics == PHOTOMETRIC_RGB || this->Photometrics == PHOTOMETRIC_MINISWHITE ||
      this->Photometrics == PHOTOMETRIC_MINISBLACK || this->Photometrics == PHOTOMETRIC_PALETTE) &&
    (this->PlanarConfig == PLANARCONFIG_CONTIG) && (!this->TileDepth) &&
    (this->BitsPerSample == 8 || this->BitsPerSample == 16 || this->BitsPerSample == 32));
}

//-------------------------------------------------------------------------
svtkTIFFReader::svtkTIFFReader()
{
  this->Initialize();
  this->InternalImage = new svtkTIFFReader::svtkTIFFReaderInternal;
  this->OutputExtent[0] = 0;
  this->OutputExtent[1] = 0;
  this->OutputExtent[2] = 0;
  this->OutputExtent[3] = 0;
  this->OutputExtent[4] = 0;
  this->OutputExtent[5] = 0;
  this->OutputIncrements[0] = 0;
  this->OutputIncrements[1] = 0;
  this->OutputIncrements[2] = 0;

  this->OrientationTypeSpecifiedFlag = false;
  this->OriginSpecifiedFlag = false;
  this->SpacingSpecifiedFlag = false;

  // Make the default orientation type to be ORIENTATION_TOPLEFT
  this->OrientationType = ORIENTATION_TOPLEFT;
  this->IgnoreColorMap = false;
}

//-------------------------------------------------------------------------
svtkTIFFReader::~svtkTIFFReader()
{
  delete this->InternalImage;
}

//-------------------------------------------------------------------------
void svtkTIFFReader::ExecuteInformation()
{
  this->Initialize();
  this->ComputeInternalFileName(this->DataExtent[4]);
  if (this->InternalFileName == nullptr)
  {
    svtkErrorMacro("Need to specify a filename");
    this->SetErrorCode(svtkErrorCode::NoFileNameError);
    return;
  }

  if (!this->InternalImage->Open(this->InternalFileName))
  {
    svtkErrorMacro("Unable to open file "
      << this->InternalFileName << " Reason: " << svtksys::SystemTools::GetLastSystemError());
    this->SetErrorCode(svtkErrorCode::CannotOpenFileError);
    this->DataExtent[0] = 0;
    this->DataExtent[1] = 0;
    this->DataExtent[2] = 0;
    this->DataExtent[3] = 0;
    this->DataExtent[4] = 0;
    this->DataExtent[5] = 0;
    this->SetNumberOfScalarComponents(1);
    this->svtkImageReader2::ExecuteInformation();
    return;
  }

  // If orientation information is provided, overwrite the value
  // read from the tiff image
  if (this->OrientationTypeSpecifiedFlag)
  {
    this->InternalImage->Orientation = OrientationType;
  }

  if (!SpacingSpecifiedFlag)
  {
    this->DataSpacing[0] = 1.0;
    this->DataSpacing[1] = 1.0;

    // If we have some spacing information we use it
    if (this->InternalImage->ResolutionUnit > 0 && this->InternalImage->XResolution > 0 &&
      this->InternalImage->YResolution > 0)
    {
      if (this->InternalImage->ResolutionUnit == 2) // inches
      {
        this->DataSpacing[0] = 25.4 / this->InternalImage->XResolution;
        this->DataSpacing[1] = 25.4 / this->InternalImage->YResolution;
      }
      else if (this->InternalImage->ResolutionUnit == 3) // cm
      {
        this->DataSpacing[0] = 10.0 / this->InternalImage->XResolution;
        this->DataSpacing[1] = 10.0 / this->InternalImage->YResolution;
      }
      // Somewhat arbitrary choice of using the X spacing as also the
      // Z spacing. Used only with image stacks.
      this->DataSpacing[2] = this->DataSpacing[0];
    }
  }

  if (!OriginSpecifiedFlag)
  {
    this->DataOrigin[0] = 0.0;
    this->DataOrigin[1] = 0.0;
    this->DataOrigin[2] = 0.0;
  }

  // Pull out the width/height, etc.
  this->DataExtent[0] = 0;
  this->DataExtent[1] = this->InternalImage->Width - 1;
  this->DataExtent[2] = 0;
  this->DataExtent[3] = this->InternalImage->Height - 1;

  switch (this->GetFormat())
  {
    case svtkTIFFReader::GRAYSCALE:
    case svtkTIFFReader::PALETTE_GRAYSCALE:
      this->SetNumberOfScalarComponents(1);
      break;
    case svtkTIFFReader::RGB:
      this->SetNumberOfScalarComponents(this->InternalImage->SamplesPerPixel);
      break;
    case svtkTIFFReader::PALETTE_RGB:
      this->SetNumberOfScalarComponents(3);
      break;
    default:
      this->SetNumberOfScalarComponents(4);
  }

  if (!this->InternalImage->CanRead())
  {
    this->SetNumberOfScalarComponents(4);
  }

  // Figure out the appropriate scalar type for the data.
  int scalarType = SVTK_SIGNED_CHAR;
  short sampleFormat = this->InternalImage->SampleFormat;
  if (this->InternalImage->BitsPerSample <= 8)
  {
    scalarType = sampleFormat == 2 ? SVTK_SIGNED_CHAR : SVTK_UNSIGNED_CHAR;
  }
  else if (this->InternalImage->BitsPerSample <= 16)
  {
    scalarType = sampleFormat == 2 ? SVTK_SHORT : SVTK_UNSIGNED_SHORT;
  }
  else if (this->InternalImage->BitsPerSample <= 32 && sampleFormat <= 2)
  {
    scalarType = sampleFormat == 2 ? SVTK_INT : SVTK_UNSIGNED_INT;
  }
  else if (this->InternalImage->BitsPerSample <= 32 && sampleFormat == 3)
  {
    scalarType = SVTK_FLOAT;
  }
  else
  {
    svtkErrorMacro("Unhandled Bit Per Sample: " << this->InternalImage->BitsPerSample);
    return;
  }
  this->SetDataScalarType(scalarType);

  // We check if we have a Zeiss image.
  // Meaning that the SamplesPerPixel is 2 but the image should be treated as
  // an RGB image.
  if (this->InternalImage->SamplesPerPixel == 2)
  {
    this->SetNumberOfScalarComponents(3);
  }

  // If the tiff file is multi-pages series of tiff images (3D volume)
  if (this->InternalImage->NumberOfPages > 1)
  {
    if (this->InternalImage->SubFiles > 0)
    {
      this->DataExtent[4] = 0;
      this->DataExtent[5] = this->InternalImage->SubFiles - 1;
    }
    else
    {
      this->DataExtent[4] = 0;
      this->DataExtent[5] = this->InternalImage->NumberOfPages - 1;
    }
  }

  // If the tiff is tiled
  if (this->InternalImage->NumberOfTiles > 1)
  {
    this->DataExtent[0] = 0;
    this->DataExtent[1] = this->InternalImage->Width - 1;
    this->DataExtent[2] = 0;
    this->DataExtent[3] = this->InternalImage->Height - 1;
    this->DataExtent[4] = 0;
    this->DataExtent[5] = 0;
    if (!SpacingSpecifiedFlag)
    {
      this->DataSpacing[2] = 1.0;
    }
    if (!OriginSpecifiedFlag)
    {
      this->DataOrigin[2] = 0.0;
    }
  }

  this->svtkImageReader2::ExecuteInformation();
  // Don't close the file yet, since we need the image internal
  // parameters such as NumberOfPages, NumberOfTiles to decide
  // how to read in the image.
}

// Set orientation type
//
// ORIENTATION_TOPLEFT         1       (row 0 top, col 0 lhs)
// ORIENTATION_TOPRIGHT        2       (row 0 top, col 0 rhs)
// ORIENTATION_BOTRIGHT        3       (row 0 bottom, col 0 rhs)
// ORIENTATION_BOTLEFT         4       (row 0 bottom, col 0 lhs)
// ORIENTATION_LEFTTOP         5       (row 0 lhs, col 0 top)
// ORIENTATION_RIGHTTOP        6       (row 0 rhs, col 0 top)
// ORIENTATION_RIGHTBOT        7       (row 0 rhs, col 0 bottom)
// ORIENTATION_LEFTBOT         8       (row 0 lhs, col 0 bottom) */
void svtkTIFFReader::SetOrientationType(unsigned int orientationType)
{
  if (orientationType < 1 || orientationType > 8)
  {
    svtkErrorMacro("Invalid Orientation type specified");
    return;
  }

  if (this->OrientationType != orientationType)
  {
    this->OrientationType = orientationType;
    this->Modified();
  }
  if (!this->OrientationTypeSpecifiedFlag)
  {
    this->Modified();
  }
  // To preserve backward compatibility OrientationTypeSpecifiedFlag would
  // always be set to true whatever user input...
  this->OrientationTypeSpecifiedFlag = true;
}

//-------------------------------------------------------------------------
template <class OT>
void svtkTIFFReader::Process2(OT* outPtr, int*)
{
  if (!this->InternalImage->Open(this->GetInternalFileName()))
  {
    return;
  }
  // if orientation information is provided, overwrite the value
  // read from the tiff image
  if (this->GetOrientationTypeSpecifiedFlag())
  {
    this->InternalImage->Orientation = this->GetOrientationType();
  }

  this->Initialize();
  this->ReadImageInternal(outPtr);
}

//----------------------------------------------------------------------------
// This function reads in one data of data.
// templated to handle different data types.
template <class OT>
void svtkTIFFReader::Process(OT* outPtr, int outExtent[6], svtkIdType outIncr[3])
{
  // multiple number of pages
  if (this->InternalImage->NumberOfPages > 1)
  {
    this->ReadVolume(outPtr);
    // close the TIFF file
    this->InternalImage->Clean();
    return;
  }

  // tiled image
  if (this->InternalImage->NumberOfTiles > 0)
  {
    this->ReadTiles(outPtr);
    // close the TIFF file
    this->InternalImage->Clean();
    return;
  }

  // The input tiff dataset is neither multiple pages and nor
  // tiled. Hence close the image and start reading each TIFF
  // file
  this->InternalImage->Clean();

  OT* outPtr2 = outPtr;
  for (int idx2 = outExtent[4]; idx2 <= outExtent[5]; ++idx2)
  {
    this->ComputeInternalFileName(idx2);
    // read in a TIFF file
    this->Process2(outPtr2, outExtent);
    // close the TIFF file
    this->InternalImage->Clean();

    this->UpdateProgress((idx2 - outExtent[4]) / (outExtent[5] - outExtent[4] + 1.0));
    outPtr2 += outIncr[2];
  }
}

//----------------------------------------------------------------------------
// This function reads a data from a file.  The datas extent/axes
// are assumed to be the same as the file extent/order.
void svtkTIFFReader::ExecuteDataWithInformation(svtkDataObject* output, svtkInformation* outInfo)
{
  if (this->InternalFileName == nullptr)
  {
    svtkErrorMacro("Either a FileName or FilePrefix must be specified.");
    return;
  }

  this->ComputeDataIncrements();

  // Get the data
  svtkImageData* data = this->AllocateOutputData(output, outInfo);
  data->GetExtent(this->OutputExtent);
  data->GetIncrements(this->OutputIncrements);

  // Call the correct templated function for the input
  void* outPtr = data->GetScalarPointer();

  switch (data->GetScalarType())
  {
    svtkTemplateMacro(this->Process((SVTK_TT*)(outPtr), this->OutputExtent, this->OutputIncrements));
    default:
      svtkErrorMacro("UpdateFromFile: Unknown data type");
  }
  data->GetPointData()->GetScalars()->SetName("Tiff Scalars");
}

//----------------------------------------------------------------------------
unsigned int svtkTIFFReader::GetFormat()
{
  if (this->ImageFormat != svtkTIFFReader::NOFORMAT)
  {
    return this->ImageFormat;
  }

  switch (this->InternalImage->Photometrics)
  {
    case PHOTOMETRIC_RGB:
    case PHOTOMETRIC_YCBCR:
      this->ImageFormat = svtkTIFFReader::RGB;
      return this->ImageFormat;
    case PHOTOMETRIC_MINISWHITE:
    case PHOTOMETRIC_MINISBLACK:
      this->ImageFormat = svtkTIFFReader::GRAYSCALE;
      return this->ImageFormat;
    case PHOTOMETRIC_PALETTE:
      if (this->IgnoreColorMap)
      {
        // ignore color map; setting svtkTIFFReader::PALETTE_GRAYSCALE ensures
        // we'll pick correct number of components.
        this->ImageFormat = svtkTIFFReader::PALETTE_GRAYSCALE;
        return this->ImageFormat;
      }
      for (unsigned int cc = 0; cc < 256; ++cc)
      {
        unsigned short red, green, blue;
        this->GetColor(cc, &red, &green, &blue);
        if (red != green || red != blue)
        {
          this->ImageFormat = svtkTIFFReader::PALETTE_RGB;
          return this->ImageFormat;
        }
      }
      this->ImageFormat = svtkTIFFReader::PALETTE_GRAYSCALE;
      return this->ImageFormat;
  }
  this->ImageFormat = svtkTIFFReader::OTHER;
  return this->ImageFormat;
}

//----------------------------------------------------------------------------
void svtkTIFFReader::GetColor(
  int index, unsigned short* red, unsigned short* green, unsigned short* blue)
{
  *red = 0;
  *green = 0;
  *blue = 0;
  if (index < 0)
  {
    svtkErrorMacro("Color index has to be greater than 0");
    return;
  }
  if (this->TotalColors > 0 && this->ColorRed && this->ColorGreen && this->ColorBlue)
  {
    if (index >= this->TotalColors)
    {
      svtkErrorMacro(
        "Color index has to be less than number of colors (" << this->TotalColors << ")");
      return;
    }
    *red = *(this->ColorRed + index);
    *green = *(this->ColorGreen + index);
    *blue = *(this->ColorBlue + index);
    return;
  }

  unsigned short photometric;

  if (!TIFFGetField(this->InternalImage->Image, TIFFTAG_PHOTOMETRIC, &photometric))
  {
    if (this->InternalImage->Photometrics != PHOTOMETRIC_PALETTE)
    {
      svtkErrorMacro("You can only access colors for palette images");
      return;
    }
  }

  unsigned short *red_orig, *green_orig, *blue_orig;

  switch (this->InternalImage->BitsPerSample)
  {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
      break;
    default:
      svtkErrorMacro(
        "Sorry, can not image with " << this->InternalImage->BitsPerSample << "-bit samples");
      return;
  }
  if (!TIFFGetField(
        this->InternalImage->Image, TIFFTAG_COLORMAP, &red_orig, &green_orig, &blue_orig))
  {
    svtkErrorMacro("Missing required \"Colormap\" tag");
    return;
  }
  this->TotalColors = (1L << this->InternalImage->BitsPerSample);

  if (index >= this->TotalColors)
  {
    svtkErrorMacro("Color index has to be less than number of colors (" << this->TotalColors << ")");
    return;
  }
  this->ColorRed = red_orig;
  this->ColorGreen = green_orig;
  this->ColorBlue = blue_orig;

  *red = *(red_orig + index);
  *green = *(green_orig + index);
  *blue = *(blue_orig + index);
}

//-------------------------------------------------------------------------
void svtkTIFFReader::Initialize()
{
  this->ColorRed = nullptr;
  this->ColorGreen = nullptr;
  this->ColorBlue = nullptr;
  this->TotalColors = -1;
  this->ImageFormat = svtkTIFFReader::NOFORMAT;
}

//-------------------------------------------------------------------------
template <typename T>
void svtkTIFFReader::ReadVolume(T* buffer)
{
  int width = this->InternalImage->Width;
  int height = this->InternalImage->Height;
  int samplesPerPixel = this->InternalImage->SamplesPerPixel;
  unsigned int npages = this->InternalImage->NumberOfPages;

  int outDims[3];
  svtkStructuredData::GetDimensionsFromExtent(this->OutputExtent, outDims);

  // counter for slices (not every page is a slice)
  int slice = 0;
  for (unsigned int page = 0; page < npages; ++page)
  {
    this->UpdateProgress(static_cast<double>(page + 1) / npages);
    if (this->InternalImage->SubFiles > 0)
    {
      long subfiletype = 6;
      if (TIFFGetField(this->InternalImage->Image, TIFFTAG_SUBFILETYPE, &subfiletype))
      {
        if (subfiletype != 0)
        {
          TIFFReadDirectory(this->InternalImage->Image);
          continue;
        }
      }
    }

    if (slice >= this->OutputExtent[4] && slice <= this->OutputExtent[5])
    {
      // if we have a Zeiss image meaning that the SamplesPerPixel is 2
      if (samplesPerPixel == 2)
      {
        if (outDims[0] != width || outDims[1] != height)
        {
          svtkErrorMacro("Case not supported currently! Please report back!");
          return;
        }
        T* volume = buffer;
        volume += width * height * samplesPerPixel * (slice - this->OutputExtent[4]);
        this->ReadTwoSamplesPerPixelImage(volume, width, height);
        break;
      }
      else
      {
        svtkIdType offset =
          outDims[0] * outDims[1] * static_cast<svtkIdType>(slice - this->OutputExtent[4]);
        // I don't think this is correct since if `CanRead==false`, we may not have actually
        // allocated a target buffer with 4 components. For now, I am keeping
        // the logic same as before.
        offset *= this->InternalImage->CanRead() ? samplesPerPixel : 4;
        this->ReadImageInternal(buffer + offset);
      }
    }

    // advance to next slice
    slice++;
    TIFFReadDirectory(this->InternalImage->Image);
  }
}

/** Read a tiled tiff */
void svtkTIFFReader::ReadTiles(void* buffer)
{
  unsigned char* volume = reinterpret_cast<unsigned char*>(buffer);
  unsigned char* tile = new unsigned char[TIFFTileSize(this->InternalImage->Image)];

  const unsigned int width = this->InternalImage->Width;
  const unsigned int height = this->InternalImage->Height;
  const unsigned int tileWidth = this->InternalImage->TileWidth;
  const unsigned int tileHeight = this->InternalImage->TileHeight;
  const unsigned int pixelSize = this->InternalImage->SamplesPerPixel;
  const bool rowMultiple = (height % tileHeight == 0) ? true : false;
  const bool colMultiple = (width % tileWidth == 0) ? true : false;
  const bool flip = this->InternalImage->Orientation != ORIENTATION_TOPLEFT;

  for (unsigned int slice = 0; slice < this->InternalImage->NumberOfPages; ++slice)
  {
    for (unsigned int row = 0; row < (rowMultiple ? height : height - tileHeight);
         row += tileHeight)
    {
      const unsigned int r = flip ? height - row - tileHeight : row;
      for (unsigned int col = 0; col < (colMultiple ? width : width - tileWidth); col += tileWidth)
      {
        if (TIFFReadTile(this->InternalImage->Image, tile, col, r, slice, 0) < 0)
        {
          svtkErrorMacro(<< "Cannot read tile : " << r << "," << col << " from file");
          delete[] tile;
          return;
        }
        // Currently not using tile depth
        unsigned int zz = 0;
        for (unsigned int yy = 0; yy < tileHeight; ++yy)
        {
          const unsigned int y = flip ? tileHeight + height % tileHeight - yy - 1 : yy;
          memcpy(volume + (((slice + zz) * height + row + y) * width + col) * pixelSize,
            tile + (zz * tileHeight + yy) * tileWidth * pixelSize, tileWidth * pixelSize);
        }
      }
    }
  }
  // Fill the boundaries
  if (!colMultiple)
  {
    const unsigned int lenx = width % tileWidth;
    const unsigned int col = width - lenx;
    const unsigned int tilexWidth = lenx;
    for (unsigned int row = 0; row < (rowMultiple ? height : height - tileHeight);
         row += tileHeight)
    {
      const unsigned int r = flip ? height - row - tileHeight - 1 : row;
      if (TIFFReadTile(this->InternalImage->Image, tile, col, r, 0, 0) < 0)
      {
        svtkErrorMacro(<< "Cannot read tile : " << r << "," << col << " from file");
        delete[] tile;
        return;
      }
      const unsigned int zz = 0;
      for (unsigned int yy = 0; yy < tileHeight; ++yy)
      {
        const unsigned int y = flip ? tileHeight + height % tileHeight - yy - 1 : yy;
        memcpy(volume + (((0 + zz) * height + row + y) * width + col) * pixelSize,
          tile + (zz * tileHeight + yy) * tileWidth * pixelSize, tilexWidth * pixelSize);
      }
    }
  }
  if (!rowMultiple)
  {
    const unsigned int leny = height % tileHeight;
    const unsigned int row = height - leny;
    const unsigned int r = flip ? 0 : row;
    for (unsigned int col = 0; col < (colMultiple ? width : width - tileWidth); col += tileWidth)
    {
      if (TIFFReadTile(this->InternalImage->Image, tile, col, row, 0, 0) < 0)
      {
        svtkErrorMacro(<< "Cannot read tile : " << r << "," << col << " from file");
        delete[] tile;
        return;
      }
      const unsigned int zz = 0;
      for (unsigned int yy = 0; yy < leny; ++yy)
      {
        const unsigned int y = flip ? leny - yy - 1 : yy;
        memcpy(volume + (((0 + zz) * height + r + y) * width + col) * pixelSize,
          tile + (zz * tileHeight + yy) * tileWidth * pixelSize, tileWidth * pixelSize);
      }
    }
  }
  if (!colMultiple && !rowMultiple)
  {
    const unsigned int lenx = width % tileWidth;
    const unsigned int col = width - lenx;

    const unsigned int leny = height % tileHeight;
    const unsigned int row = height - leny;
    const unsigned int tilexWidth = lenx;

    const unsigned int r = flip ? 0 : row;
    if (TIFFReadTile(this->InternalImage->Image, tile, col, row, 0, 0) < 0)
    {
      svtkErrorMacro(<< "Cannot read tile : " << r << "," << col << " from file");
      delete[] tile;
      return;
    }
    const unsigned int zz = 0;
    unsigned int y;
    for (unsigned int yy = 0; yy < leny; ++yy)
    {
      y = flip ? leny - yy - 1 : yy;
      memcpy(volume + (((0 + zz) * height + r + y) * width + col) * pixelSize,
        tile + (zz * tileHeight + yy) * tileWidth * pixelSize, tilexWidth * pixelSize);
    }
  }
  delete[] tile;
}

/** To Support Zeiss images that contains only 2 samples per pixel but are actually
 *  RGB images */
void svtkTIFFReader::ReadTwoSamplesPerPixelImage(void* out, unsigned int width, unsigned int height)
{
  unsigned int isize = TIFFScanlineSize(this->InternalImage->Image);
  unsigned int cc;
  int row;
  tdata_t buf = _TIFFmalloc(isize);

  int inc = 1;

  if (this->GetDataScalarType() == SVTK_UNSIGNED_CHAR)
  {
    unsigned char* image;
    if (this->InternalImage->PlanarConfig == PLANARCONFIG_CONTIG)
    {
      for (row = 0; row < (int)height; row++)
      {
        if (TIFFReadScanline(this->InternalImage->Image, buf, row, 0) <= 0)
        {
          svtkErrorMacro(<< "Problem reading the row: " << row);
          break;
        }

        if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
        {
          image = reinterpret_cast<unsigned char*>(out) + row * width * inc;
        }
        else
        {
          image = reinterpret_cast<unsigned char*>(out) + width * inc * (height - (row + 1));
        }

        for (cc = 0; cc < isize; cc += this->InternalImage->SamplesPerPixel)
        {
          inc = this->EvaluateImageAt(image, static_cast<unsigned char*>(buf) + cc);
          image += inc;
        }
      }
    }
    else if (this->InternalImage->PlanarConfig == PLANARCONFIG_SEPARATE)
    {
      unsigned long s;
      unsigned long nsamples = 0;
      TIFFGetField(this->InternalImage->Image, TIFFTAG_SAMPLESPERPIXEL, &nsamples);
      for (s = 0; s < nsamples; s++)
      {
        for (row = 0; row < (int)height; row++)
        {
          if (TIFFReadScanline(this->InternalImage->Image, buf, row, s) <= 0)
          {
            svtkErrorMacro(<< "Problem reading the row: " << row);
            break;
          }

          inc = 3;

          if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
          {
            image = reinterpret_cast<unsigned char*>(out) + row * width * inc;
          }
          else
          {
            image = reinterpret_cast<unsigned char*>(out) + width * inc * (height - (row + 1));
          }

          // We translate the output pixel to be on the right RGB
          image += s;
          for (cc = 0; cc < isize; cc += 1)
          {
            (*image) = *(static_cast<unsigned char*>(buf) + cc);
            inc = 3;
            image += inc;
          }
        }
      }
    }
  }
  else if (this->GetDataScalarType() == SVTK_UNSIGNED_SHORT)
  {
    isize /= 2;
    unsigned short* image;
    if (this->InternalImage->PlanarConfig == PLANARCONFIG_CONTIG)
    {
      for (row = 0; row < (int)height; row++)
      {
        if (TIFFReadScanline(this->InternalImage->Image, buf, row, 0) <= 0)
        {
          svtkErrorMacro(<< "Problem reading the row: " << row);
          break;
        }

        if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
        {
          image = reinterpret_cast<unsigned short*>(out) + row * width * inc;
        }
        else
        {
          image = reinterpret_cast<unsigned short*>(out) + width * inc * (height - (row + 1));
        }

        for (cc = 0; cc < isize; cc += this->InternalImage->SamplesPerPixel)
        {
          inc = this->EvaluateImageAt(image, static_cast<unsigned short*>(buf) + cc);
          image += inc;
        }
      }
    }
    else if (this->InternalImage->PlanarConfig == PLANARCONFIG_SEPARATE)
    {
      unsigned long s, nsamples;
      TIFFGetField(this->InternalImage->Image, TIFFTAG_SAMPLESPERPIXEL, &nsamples);
      for (s = 0; s < nsamples; s++)
      {
        for (row = 0; row < (int)height; row++)
        {
          if (TIFFReadScanline(this->InternalImage->Image, buf, row, s) <= 0)
          {
            svtkErrorMacro(<< "Problem reading the row: " << row);
            break;
          }

          if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
          {
            image = reinterpret_cast<unsigned short*>(out) + row * width * inc;
          }
          else
          {
            image = reinterpret_cast<unsigned short*>(out) + width * inc * (height - (row + 1));
          }
          // We translate the output pixel to be on the right RGB
          image += s;
          for (cc = 0; cc < isize; cc += 1)
          {
            (*image) = *(static_cast<unsigned short*>(buf) + cc);
            inc = 3;
            image += inc;
          }
        }
      }
    }
  }
  _TIFFfree(buf);
}

template <typename T>
void svtkTIFFReader::ReadGenericImage(T* out, unsigned int, unsigned int height)
{
  // Fast path for simple images
  unsigned int format = this->GetFormat();
  if (this->InternalImage->PlanarConfig == PLANARCONFIG_CONTIG && this->OutputIncrements[0] == 1 &&
    (format == svtkTIFFReader::GRAYSCALE &&
      this->InternalImage->Photometrics == PHOTOMETRIC_MINISBLACK &&
      this->InternalImage->SamplesPerPixel == 1))
  {
    if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
    {
      FlipFalse flip;
      if (!ReadTemplatedImage(out, flip, this->OutputExtent[0], this->OutputExtent[1],
            this->OutputExtent[2], this->OutputExtent[3], this->OutputIncrements[1], height,
            this->InternalImage->Image))
      {
        svtkErrorMacro(<< "Problem reading slice of volume in TIFF file.");
      }
    }
    else
    {
      FlipTrue flip;
      if (!ReadTemplatedImage(out, flip, this->OutputExtent[0], this->OutputExtent[1],
            this->OutputExtent[2], this->OutputExtent[3], this->OutputIncrements[1], height,
            this->InternalImage->Image))
      {
        svtkErrorMacro(<< "Problem reading slice of volume in TIFF file.");
      }
    }
    return;
  }

  unsigned int isize = TIFFScanlineSize(this->InternalImage->Image);

  if (this->InternalImage->PlanarConfig != PLANARCONFIG_CONTIG)
  {
    svtkErrorMacro(<< "This reader can only do PLANARCONFIG_CONTIG");
    return;
  }

  tdata_t buf = _TIFFmalloc(isize);

  T* image;
  if (this->InternalImage->PlanarConfig == PLANARCONFIG_CONTIG)
  {
    int fileRow = 0;
    for (int row = this->OutputExtent[2]; row <= this->OutputExtent[3]; ++row)
    {
      // Flip from lower left origin to upper left if necessary.
      if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
      {
        fileRow = row;
      }
      else
      {
        fileRow = height - row - 1;
      }
      if (TIFFReadScanline(this->InternalImage->Image, buf, fileRow, 0) <= 0)
      {
        svtkErrorMacro(<< "Problem reading the row: " << fileRow);
        break;
      }
      image = out + (row - this->OutputExtent[2]) * this->OutputIncrements[1];

      // Copy the pixels into the output buffer
      unsigned int cc = this->OutputExtent[0] * this->InternalImage->SamplesPerPixel;
      for (int ix = this->OutputExtent[0]; ix <= this->OutputExtent[1]; ++ix)
      {
        this->EvaluateImageAt(image, static_cast<T*>(buf) + cc);
        image += this->OutputIncrements[0];
        cc += this->InternalImage->SamplesPerPixel;
      }
    }
  }
  else if (this->InternalImage->PlanarConfig == PLANARCONFIG_SEPARATE)
  {
    int fileRow = 0;
    unsigned long nsamples;
    TIFFGetField(this->InternalImage->Image, TIFFTAG_SAMPLESPERPIXEL, &nsamples);
    for (unsigned long s = 0; s < nsamples; ++s)
    {
      for (int row = this->OutputExtent[2]; row <= this->OutputExtent[3]; ++row)
      {
        if (this->InternalImage->Orientation == ORIENTATION_TOPLEFT)
        {
          fileRow = row;
        }
        else
        {
          fileRow = height - row - 1;
        }
        if (TIFFReadScanline(this->InternalImage->Image, buf, fileRow, s) <= 0)
        {
          svtkErrorMacro(<< "Problem reading the row: " << fileRow);
          break;
        }

        image = out + (row - this->OutputExtent[2]) * this->OutputIncrements[1];
        unsigned int cc = this->OutputExtent[0] * this->InternalImage->SamplesPerPixel;
        for (int ix = this->OutputExtent[0]; ix <= this->OutputExtent[1]; ++ix)
        {
          this->EvaluateImageAt(image, static_cast<T*>(buf) + cc);
          image += this->OutputIncrements[0];
          cc += this->InternalImage->SamplesPerPixel;
        }
      }
    }
  }
  _TIFFfree(buf);

  // release color map ptrs, if any. since color map changes with each IFD,
  // to avoid reading obsolete ptrs in `ReadVolume`, we just clear these
  // references. The overhead to doing that is minimal. The next invocation to
  // `ReadGenericImage` may have to fetch these points again. Note, these are
  // just pointers to memory internally allocated by libtiff and hence we are
  // not actually deep-copying the color map.
  this->ColorRed = this->ColorBlue = this->ColorGreen = nullptr;
  this->TotalColors = -1;
}

//-------------------------------------------------------------------------
template <typename T>
void svtkTIFFReader::ReadImageInternal(T* outPtr)
{
  int width = this->InternalImage->Width;
  int height = this->InternalImage->Height;

  if (!this->InternalImage->CanRead())
  {
    // Why do we read the image for the ! CanRead case?
    uint32* tempImage = reinterpret_cast<uint32*>(outPtr);

    if (this->OutputExtent[0] != 0 || this->OutputExtent[1] != width - 1 ||
      this->OutputExtent[2] != 0 || this->OutputExtent[3] != height - 1)
    {
      tempImage = new uint32[width * height];
    }
    // This should really be fixed to read only the rows necessary.
    if (!TIFFReadRGBAImage(this->InternalImage->Image, width, height, tempImage, 0))
    {
      svtkErrorMacro("Problem reading RGB image");
      if (tempImage != reinterpret_cast<uint32*>(outPtr))
      {
        delete[] tempImage;
      }
      return;
    }
    const bool flip = this->InternalImage->Orientation != ORIENTATION_TOPLEFT;
    T* fimage = outPtr;
    for (int yy = 0; yy < height; ++yy)
    {
      uint32* ssimage = flip ? (tempImage + yy * width) : (tempImage + (height - yy - 1) * width);
      for (int xx = 0; xx < width; ++xx)
      {
        if (xx >= this->OutputExtent[0] && xx <= this->OutputExtent[1] &&
          yy >= this->OutputExtent[2] && yy <= this->OutputExtent[3])
        {
          *(fimage) = static_cast<T>(TIFFGetR(*ssimage));     // Red
          *(fimage + 1) = static_cast<T>(TIFFGetG(*ssimage)); // Green
          *(fimage + 2) = static_cast<T>(TIFFGetB(*ssimage)); // Blue
          *(fimage + 3) = static_cast<T>(TIFFGetA(*ssimage)); // Alpha
          fimage += 4;
        }
        ++ssimage;
      }
    }

    if (tempImage != reinterpret_cast<uint32*>(outPtr))
    {
      delete[] tempImage;
    }
    return;
  }

  switch (this->GetFormat())
  {
    case svtkTIFFReader::GRAYSCALE:
    case svtkTIFFReader::RGB:
    case svtkTIFFReader::PALETTE_RGB:
    case svtkTIFFReader::PALETTE_GRAYSCALE:
      this->ReadGenericImage(outPtr, width, height);
      break;
    default:
      return;
  }
}

//-------------------------------------------------------------------------
template <typename T>
int svtkTIFFReader::EvaluateImageAt(T* out, T* in)
{
  unsigned char* image = reinterpret_cast<unsigned char*>(out);
  unsigned char* source = reinterpret_cast<unsigned char*>(in);
  unsigned short red, green, blue;
  switch (this->GetFormat())
  {
    case svtkTIFFReader::GRAYSCALE:
      if (this->InternalImage->Photometrics == PHOTOMETRIC_MINISBLACK)
      {
        *out = *in;
      }
      else
      {
        *image = ~(*source);
      }
      return 1;
    case svtkTIFFReader::PALETTE_GRAYSCALE:
      if (this->IgnoreColorMap)
      {
        *out = *in;
      }
      else
      {
        this->GetColor(static_cast<int>(*in), &red, &green, &blue);
        *out = static_cast<T>(red); // red
      }
      return 1;
    case svtkTIFFReader::RGB:
      *(image) = *(source);         // red
      *(image + 1) = *(source + 1); // green
      *(image + 2) = *(source + 2); // blue
      if (this->InternalImage->SamplesPerPixel == 4)
      {
        *(image + 3) = 255 - *(source + 3); // alpha
      }
      return this->InternalImage->SamplesPerPixel;
    case svtkTIFFReader::PALETTE_RGB:
      assert(!this->IgnoreColorMap); // if IgnoreColorMap, the format is set to PALETTE_GRAYSCALE.
      this->GetColor(static_cast<int>(*in), &red, &green, &blue);
      *(out) = red << 8;
      *(out + 1) = green << 8;
      *(out + 2) = blue << 8;
      if (this->GetDataScalarType() == SVTK_SHORT || this->GetDataScalarType() == SVTK_UNSIGNED_SHORT)
      {
        this->GetColor(static_cast<int>(*in), &red, &green, &blue);
        *(out) = red << 8;
        *(out + 1) = green << 8;
        *(out + 2) = blue << 8;
      }
      else
      {
        this->GetColor(static_cast<int>(*in), &red, &green, &blue);
        *(out) = red >> 8;
        *(out + 1) = green >> 8;
        *(out + 2) = blue >> 8;
      }
      return 3;
    default:
      return 0;
  }
}

//-------------------------------------------------------------------------
int svtkTIFFReader::CanReadFile(const char* fname)
{
  svtkTIFFReaderInternal tf;
  int res = tf.Open(fname);
  tf.Clean();
  if (res)
  {
    return 3;
  }
  return 0;
}

//----------------------------------------------------------------------------
void svtkTIFFReader::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "OrientationType: " << this->OrientationType << endl;
  os << indent << "OrientationTypeSpecifiedFlag: " << this->OrientationTypeSpecifiedFlag << endl;
  os << indent << "OriginSpecifiedFlag: " << this->OriginSpecifiedFlag << endl;
  os << indent << "SpacingSpecifiedFlag: " << this->SpacingSpecifiedFlag << endl;
  os << indent << "IgnoreColorMap: " << this->IgnoreColorMap << endl;
}