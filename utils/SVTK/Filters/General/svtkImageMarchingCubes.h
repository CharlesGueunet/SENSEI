/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkImageMarchingCubes.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkImageMarchingCubes
 * @brief   generate isosurface(s) from volume/images
 *
 * svtkImageMarchingCubes is a filter that takes as input images (e.g., 3D
 * image region) and generates on output one or more isosurfaces.
 * One or more contour values must be specified to generate the isosurfaces.
 * Alternatively, you can specify a min/max scalar range and the number of
 * contours to generate a series of evenly spaced contour values.
 * This filter can stream, so that the entire volume need not be loaded at
 * once.  Streaming is controlled using the instance variable
 * InputMemoryLimit, which has units KBytes.
 *
 * @warning
 * This filter is specialized to volumes. If you are interested in
 * contouring other types of data, use the general svtkContourFilter. If you
 * want to contour an image (i.e., a volume slice), use svtkMarchingSquares.
 * @sa
 * svtkContourFilter svtkSliceCubes svtkMarchingSquares svtkSynchronizedTemplates3D
 */

#ifndef svtkImageMarchingCubes_h
#define svtkImageMarchingCubes_h

#include "svtkFiltersGeneralModule.h" // For export macro
#include "svtkPolyDataAlgorithm.h"

#include "svtkContourValues.h" // Needed for direct access to ContourValues

class svtkCellArray;
class svtkFloatArray;
class svtkImageData;
class svtkPoints;

class SVTKFILTERSGENERAL_EXPORT svtkImageMarchingCubes : public svtkPolyDataAlgorithm
{
public:
  static svtkImageMarchingCubes* New();
  svtkTypeMacro(svtkImageMarchingCubes, svtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  //@{
  /**
   * Methods to set contour values
   */
  void SetValue(int i, double value);
  double GetValue(int i);
  double* GetValues();
  void GetValues(double* contourValues);
  void SetNumberOfContours(int number);
  svtkIdType GetNumberOfContours();
  void GenerateValues(int numContours, double range[2]);
  void GenerateValues(int numContours, double rangeStart, double rangeEnd);
  //@}

  /**
   * Because we delegate to svtkContourValues & refer to svtkImplicitFunction
   */
  svtkMTimeType GetMTime() override;

  //@{
  /**
   * Set/Get the computation of scalars.
   */
  svtkSetMacro(ComputeScalars, svtkTypeBool);
  svtkGetMacro(ComputeScalars, svtkTypeBool);
  svtkBooleanMacro(ComputeScalars, svtkTypeBool);
  //@}

  //@{
  /**
   * Set/Get the computation of normals. Normal computation is fairly expensive
   * in both time and storage. If the output data will be processed by filters
   * that modify topology or geometry, it may be wise to turn Normals and Gradients off.
   */
  svtkSetMacro(ComputeNormals, svtkTypeBool);
  svtkGetMacro(ComputeNormals, svtkTypeBool);
  svtkBooleanMacro(ComputeNormals, svtkTypeBool);
  //@}

  //@{
  /**
   * Set/Get the computation of gradients. Gradient computation is fairly expensive
   * in both time and storage. Note that if ComputeNormals is on, gradients will
   * have to be calculated, but will not be stored in the output dataset.
   * If the output data will be processed by filters that modify topology or
   * geometry, it may be wise to turn Normals and Gradients off.
   */
  svtkSetMacro(ComputeGradients, svtkTypeBool);
  svtkGetMacro(ComputeGradients, svtkTypeBool);
  svtkBooleanMacro(ComputeGradients, svtkTypeBool);
  //@}

  // Should be protected, but the templated functions need these
  svtkTypeBool ComputeScalars;
  svtkTypeBool ComputeNormals;
  svtkTypeBool ComputeGradients;
  int NeedGradients;

  svtkCellArray* Triangles;
  svtkFloatArray* Scalars;
  svtkPoints* Points;
  svtkFloatArray* Normals;
  svtkFloatArray* Gradients;

  svtkIdType GetLocatorPoint(int cellX, int cellY, int edge);
  void AddLocatorPoint(int cellX, int cellY, int edge, svtkIdType ptId);
  void IncrementLocatorZ();

  //@{
  /**
   * The InputMemoryLimit determines the chunk size (the number of slices
   * requested at each iteration).  The units of this limit is KiloBytes.
   * For now, only the Z axis is split.
   */
  svtkSetMacro(InputMemoryLimit, svtkIdType);
  svtkGetMacro(InputMemoryLimit, svtkIdType);
  //@}

protected:
  svtkImageMarchingCubes();
  ~svtkImageMarchingCubes() override;

  int NumberOfSlicesPerChunk;
  svtkIdType InputMemoryLimit;

  svtkContourValues* ContourValues;

  svtkIdType* LocatorPointIds;
  int LocatorDimX;
  int LocatorDimY;
  int LocatorMinX;
  int LocatorMinY;

  int RequestData(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;
  int RequestUpdateExtent(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;
  int FillInputPortInformation(int port, svtkInformation* info) override;

  void March(svtkImageData* inData, int chunkMin, int chunkMax, int numContours, double* values);
  void InitializeLocator(int min0, int max0, int min1, int max1);
  void DeleteLocator();
  svtkIdType* GetLocatorPointer(int cellX, int cellY, int edge);

private:
  svtkImageMarchingCubes(const svtkImageMarchingCubes&) = delete;
  void operator=(const svtkImageMarchingCubes&) = delete;
};

/**
 * Set a particular contour value at contour number i. The index i ranges
 * between 0<=i<NumberOfContours.
 */
inline void svtkImageMarchingCubes::SetValue(int i, double value)
{
  this->ContourValues->SetValue(i, value);
}

/**
 * Get the ith contour value.
 */
inline double svtkImageMarchingCubes::GetValue(int i)
{
  return this->ContourValues->GetValue(i);
}

/**
 * Get a pointer to an array of contour values. There will be
 * GetNumberOfContours() values in the list.
 */
inline double* svtkImageMarchingCubes::GetValues()
{
  return this->ContourValues->GetValues();
}

/**
 * Fill a supplied list with contour values. There will be
 * GetNumberOfContours() values in the list. Make sure you allocate
 * enough memory to hold the list.
 */
inline void svtkImageMarchingCubes::GetValues(double* contourValues)
{
  this->ContourValues->GetValues(contourValues);
}

/**
 * Set the number of contours to place into the list. You only really
 * need to use this method to reduce list size. The method SetValue()
 * will automatically increase list size as needed.
 */
inline void svtkImageMarchingCubes::SetNumberOfContours(int number)
{
  this->ContourValues->SetNumberOfContours(number);
}

/**
 * Get the number of contours in the list of contour values.
 */
inline svtkIdType svtkImageMarchingCubes::GetNumberOfContours()
{
  return this->ContourValues->GetNumberOfContours();
}

/**
 * Generate numContours equally spaced contour values between specified
 * range. Contour values will include min/max range values.
 */
inline void svtkImageMarchingCubes::GenerateValues(int numContours, double range[2])
{
  this->ContourValues->GenerateValues(numContours, range);
}

/**
 * Generate numContours equally spaced contour values between specified
 * range. Contour values will include min/max range values.
 */
inline void svtkImageMarchingCubes::GenerateValues(
  int numContours, double rangeStart, double rangeEnd)
{
  this->ContourValues->GenerateValues(numContours, rangeStart, rangeEnd);
}

#endif