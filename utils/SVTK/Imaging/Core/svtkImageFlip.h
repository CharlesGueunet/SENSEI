/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageFlip.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkImageFlip
 * @brief   This flips an axis of an image. Right becomes left ...
 *
 * svtkImageFlip will reflect the data along the filtered axis.  This filter is
 * actually a thin wrapper around svtkImageReslice.
 */

#ifndef svtkImageFlip_h
#define svtkImageFlip_h

#include "svtkImageReslice.h"
#include "svtkImagingCoreModule.h" // For export macro

class SVTKIMAGINGCORE_EXPORT svtkImageFlip : public svtkImageReslice
{
public:
  static svtkImageFlip* New();

  svtkTypeMacro(svtkImageFlip, svtkImageReslice);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Specify which axis will be flipped.  This must be an integer
   * between 0 (for x) and 2 (for z). Initial value is 0.
   */
  svtkSetMacro(FilteredAxis, int);
  svtkGetMacro(FilteredAxis, int);
  //@}

  //@{
  /**
   * By default the image will be flipped about its center, and the
   * Origin, Spacing and Extent of the output will be identical to
   * the input.  However, if you have a coordinate system associated
   * with the image and you want to use the flip to convert +ve values
   * along one axis to -ve values (and vice versa) then you actually
   * want to flip the image about coordinate (0,0,0) instead of about
   * the center of the image.  This method will adjust the Origin of
   * the output such that the flip occurs about (0,0,0).  Note that
   * this method only changes the Origin (and hence the coordinate system)
   * the output data: the actual pixel values are the same whether or not
   * this method is used.  Also note that the Origin in this method name
   * refers to (0,0,0) in the coordinate system associated with the image,
   * it does not refer to the Origin ivar that is associated with a
   * svtkImageData.
   */
  svtkSetMacro(FlipAboutOrigin, svtkTypeBool);
  svtkGetMacro(FlipAboutOrigin, svtkTypeBool);
  svtkBooleanMacro(FlipAboutOrigin, svtkTypeBool);
  //@}

  /**
   * Keep the mis-named Axes variations around for compatibility with old
   * scripts. Axis is singular, not plural...
   */
  void SetFilteredAxes(int axis) { this->SetFilteredAxis(axis); }
  int GetFilteredAxes() { return this->GetFilteredAxis(); }

  //@{
  /**
   * PreserveImageExtentOff wasn't covered by test scripts and its
   * implementation was broken.  It is deprecated now and it has
   * no effect (i.e. the ImageExtent is always preserved).
   */
  svtkSetMacro(PreserveImageExtent, svtkTypeBool);
  svtkGetMacro(PreserveImageExtent, svtkTypeBool);
  svtkBooleanMacro(PreserveImageExtent, svtkTypeBool);
  //@}

protected:
  svtkImageFlip();
  ~svtkImageFlip() override {}

  int RequestInformation(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;

  int FilteredAxis;
  svtkTypeBool FlipAboutOrigin;
  svtkTypeBool PreserveImageExtent;

private:
  svtkImageFlip(const svtkImageFlip&) = delete;
  void operator=(const svtkImageFlip&) = delete;
};

#endif