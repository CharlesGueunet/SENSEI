/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageGaussianSmooth.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkImageGaussianSmooth
 * @brief   Performs a gaussian convolution.
 *
 * svtkImageGaussianSmooth implements a convolution of the input image
 * with a gaussian. Supports from one to three dimensional convolutions.
 */

#ifndef svtkImageGaussianSmooth_h
#define svtkImageGaussianSmooth_h

#include "svtkImagingGeneralModule.h" // For export macro
#include "svtkThreadedImageAlgorithm.h"

class SVTKIMAGINGGENERAL_EXPORT svtkImageGaussianSmooth : public svtkThreadedImageAlgorithm
{
public:
  svtkTypeMacro(svtkImageGaussianSmooth, svtkThreadedImageAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  /**
   * Creates an instance of svtkImageGaussianSmooth with the following
   * defaults: Dimensionality 3, StandardDeviations( 2, 2, 2),
   * Radius Factors ( 1.5, 1.5, 1.5)
   */
  static svtkImageGaussianSmooth* New();

  //@{
  /**
   * Sets/Gets the Standard deviation of the gaussian in pixel units.
   */
  svtkSetVector3Macro(StandardDeviations, double);
  void SetStandardDeviation(double std) { this->SetStandardDeviations(std, std, std); }
  void SetStandardDeviations(double a, double b) { this->SetStandardDeviations(a, b, 0.0); }
  svtkGetVector3Macro(StandardDeviations, double);
  //@}

  /**
   * Sets/Gets the Standard deviation of the gaussian in pixel units.
   * These methods are provided for compatibility with old scripts
   */
  void SetStandardDeviation(double a, double b) { this->SetStandardDeviations(a, b, 0.0); }
  void SetStandardDeviation(double a, double b, double c) { this->SetStandardDeviations(a, b, c); }

  //@{
  /**
   * Sets/Gets the Radius Factors of the gaussian (no unit).
   * The radius factors determine how far out the gaussian kernel will
   * go before being clamped to zero.
   */
  svtkSetVector3Macro(RadiusFactors, double);
  void SetRadiusFactors(double f, double f2) { this->SetRadiusFactors(f, f2, 1.5); }
  void SetRadiusFactor(double f) { this->SetRadiusFactors(f, f, f); }
  svtkGetVector3Macro(RadiusFactors, double);
  //@}

  //@{
  /**
   * Set/Get the dimensionality of this filter. This determines whether
   * a one, two, or three dimensional gaussian is performed.
   */
  svtkSetMacro(Dimensionality, int);
  svtkGetMacro(Dimensionality, int);
  //@}

protected:
  svtkImageGaussianSmooth();
  ~svtkImageGaussianSmooth() override;

  int Dimensionality;
  double StandardDeviations[3];
  double RadiusFactors[3];

  void ComputeKernel(double* kernel, int min, int max, double std);
  int RequestUpdateExtent(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;
  void InternalRequestUpdateExtent(int*, int*);
  void ExecuteAxis(int axis, svtkImageData* inData, int inExt[6], svtkImageData* outData,
    int outExt[6], int* pcycle, int target, int* pcount, int total, svtkInformation* inInfo);
  void ThreadedRequestData(svtkInformation* request, svtkInformationVector** inputVector,
    svtkInformationVector* outputVector, svtkImageData*** inData, svtkImageData** outData,
    int outExt[6], int id) override;

private:
  svtkImageGaussianSmooth(const svtkImageGaussianSmooth&) = delete;
  void operator=(const svtkImageGaussianSmooth&) = delete;
};

#endif