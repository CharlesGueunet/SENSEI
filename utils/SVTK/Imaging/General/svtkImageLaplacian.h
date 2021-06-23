/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageLaplacian.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkImageLaplacian
 * @brief   Computes divergence of gradient.
 *
 * svtkImageLaplacian computes the Laplacian (like a second derivative)
 * of a scalar image.  The operation is the same as taking the
 * divergence after a gradient.  Boundaries are handled, so the input
 * is the same as the output.
 * Dimensionality determines how the input regions are interpreted.
 * (images, or volumes). The Dimensionality defaults to two.
 */

#ifndef svtkImageLaplacian_h
#define svtkImageLaplacian_h

#include "svtkImagingGeneralModule.h" // For export macro
#include "svtkThreadedImageAlgorithm.h"

class SVTKIMAGINGGENERAL_EXPORT svtkImageLaplacian : public svtkThreadedImageAlgorithm
{
public:
  static svtkImageLaplacian* New();
  svtkTypeMacro(svtkImageLaplacian, svtkThreadedImageAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Determines how the input is interpreted (set of 2d slices ...)
   */
  svtkSetClampMacro(Dimensionality, int, 2, 3);
  svtkGetMacro(Dimensionality, int);
  //@}

protected:
  svtkImageLaplacian();
  ~svtkImageLaplacian() override {}

  int Dimensionality;

  int RequestUpdateExtent(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;
  void ThreadedRequestData(svtkInformation* request, svtkInformationVector** inputVector,
    svtkInformationVector* outputVector, svtkImageData*** inData, svtkImageData** outData,
    int outExt[6], int id) override;

private:
  svtkImageLaplacian(const svtkImageLaplacian&) = delete;
  void operator=(const svtkImageLaplacian&) = delete;
};

#endif