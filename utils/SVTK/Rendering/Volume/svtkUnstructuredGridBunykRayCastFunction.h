/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkUnstructuredGridBunykRayCastFunction.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/**
 * @class   svtkUnstructuredGridBunykRayCastFunction
 * @brief   a superclass for ray casting functions
 *
 *
 * svtkUnstructuredGridBunykRayCastFunction is a concrete implementation of a
 * ray cast function for unstructured grid data. This class was based on the
 * paper "Simple, Fast, Robust Ray Casting of Irregular Grids" by Paul Bunyk,
 * Arie Kaufmna, and Claudio Silva. This method is quite memory intensive
 * (with extra explicit copies of the data) and therefore should not be used
 * for very large data. This method assumes that the input data is composed
 * entirely of tetras - use svtkDataSetTriangleFilter before setting the input
 * on the mapper.
 *
 * The basic idea of this method is as follows:
 *
 *   1) Enumerate the triangles. At each triangle have space for some
 *      information that will be used during rendering. This includes
 *      which tetra the triangles belong to, the plane equation and the
 *      Barycentric coefficients.
 *
 *   2) Keep a reference to all four triangles for each tetra.
 *
 *   3) At the beginning of each render, do the precomputation. This
 *      includes creating an array of transformed points (in view
 *      coordinates) and computing the view dependent info per triangle
 *      (plane equations and barycentric coords in view space)
 *
 *   4) Find all front facing boundary triangles (a triangle is on the
 *      boundary if it belongs to only one tetra). For each triangle,
 *      find all pixels in the image that intersect the triangle, and
 *      add this to the sorted (by depth) intersection list at each
 *      pixel.
 *
 *   5) For each ray cast, traverse the intersection list. At each
 *      intersection, accumulate opacity and color contribution
 *      per tetra along the ray until you reach an exiting triangle
 *      (on the boundary).
 *
 *
 * @sa
 * svtkUnstructuredGridVolumeRayCastMapper
 */

#ifndef svtkUnstructuredGridBunykRayCastFunction_h
#define svtkUnstructuredGridBunykRayCastFunction_h

#include "svtkRenderingVolumeModule.h" // For export macro
#include "svtkUnstructuredGridVolumeRayCastFunction.h"

class svtkRenderer;
class svtkVolume;
class svtkUnstructuredGridVolumeRayCastMapper;
class svtkMatrix4x4;
class svtkPiecewiseFunction;
class svtkColorTransferFunction;
class svtkUnstructuredGridBase;
class svtkIdList;
class svtkDoubleArray;
class svtkDataArray;

// We manage the memory for the list of intersections ourself - this is the
// storage used. We keep 10,000 elements in each array, and we can have up to
// 1,000 arrays.
#define SVTK_BUNYKRCF_MAX_ARRAYS 10000
#define SVTK_BUNYKRCF_ARRAY_SIZE 10000

class SVTKRENDERINGVOLUME_EXPORT svtkUnstructuredGridBunykRayCastFunction
  : public svtkUnstructuredGridVolumeRayCastFunction
{
public:
  static svtkUnstructuredGridBunykRayCastFunction* New();
  svtkTypeMacro(svtkUnstructuredGridBunykRayCastFunction, svtkUnstructuredGridVolumeRayCastFunction);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  /**
   * Called by the ray cast mapper at the start of rendering
   */
  void Initialize(svtkRenderer* ren, svtkVolume* vol) override;

  /**
   * Called by the ray cast mapper at the end of rendering
   */
  void Finalize() override;

  SVTK_NEWINSTANCE
  svtkUnstructuredGridVolumeRayCastIterator* NewIterator() override;

  // Used to store each triangle - made public because of the
  // templated function
  class Triangle
  {
  public:
    svtkIdType PointIndex[3];
    svtkIdType ReferredByTetra[2];
    double P1X, P1Y;
    double P2X, P2Y;
    double Denominator;
    double A, B, C, D;
    Triangle* Next;
  };

  // Used to store each intersection for the pixel rays - made
  // public because of the templated function
  class Intersection
  {
  public:
    Triangle* TriPtr;
    double Z;
    Intersection* Next;
  };

  /**
   * Is the point x, y, in the given triangle? Public for
   * access from the templated function.
   */
  int InTriangle(double x, double y, Triangle* triPtr);

  /**
   * Access to an internal structure for the templated method.
   */
  double* GetPoints() { return this->Points; }

  //@{
  /**
   * Access to an internal structure for the templated method.
   */
  svtkGetObjectMacro(ViewToWorldMatrix, svtkMatrix4x4);
  //@}

  //@{
  /**
   * Access to an internal structure for the templated method.
   */
  svtkGetVectorMacro(ImageOrigin, int, 2);
  //@}

  //@{
  /**
   * Access to an internal structure for the templated method.
   */
  svtkGetVectorMacro(ImageViewportSize, int, 2);
  //@}

  /**
   * Access to an internal structure for the templated method.
   */
  Triangle** GetTetraTriangles() { return this->TetraTriangles; }

  /**
   * Access to an internal structure for the templated method.
   */
  Intersection* GetIntersectionList(int x, int y)
  {
    return this->Image[y * this->ImageSize[0] + x];
  }

protected:
  svtkUnstructuredGridBunykRayCastFunction();
  ~svtkUnstructuredGridBunykRayCastFunction() override;

  // These are cached during the initialize method so that they do not
  // need to be passed into subsequent CastRay calls.
  svtkRenderer* Renderer;
  svtkVolume* Volume;
  svtkUnstructuredGridVolumeRayCastMapper* Mapper;

  // Computed during the initialize method - if something is
  // wrong (no mapper, no volume, no input, etc.) then no rendering
  // will actually be performed.
  int Valid;

  // These are the transformed points
  int NumberOfPoints;
  double* Points;

  // This is the matrix that will take a transformed point back
  // to world coordinates
  svtkMatrix4x4* ViewToWorldMatrix;

  // This is the intersection list per pixel in the image
  Intersection** Image;

  // This is the size of the image we are computing (which does
  // not need to match the screen size)
  int ImageSize[2];

  // Since we may only be computing a subregion of the "full" image,
  // this is the origin of the region we are computing. We must
  // subtract this origin from any pixel (x,y) locations before
  // accessing the pixel in this->Image (which represents only the
  // subregion)
  int ImageOrigin[2];

  // This is the full size of the image
  int ImageViewportSize[2];

  // These are values saved for the building of the TriangleList. Basically
  // we need to check if the data has changed in some way.
  svtkUnstructuredGridBase* SavedTriangleListInput;
  svtkTimeStamp SavedTriangleListMTime;

  // This is a memory intensive algorithm! For each tetra in the
  // input data we create up to 4 triangles (we don't create duplicates)
  // This is the TriangleList. Then, for each tetra we keep track of
  // the pointer to each of its four triangles - this is the
  // TetraTriangles. We also keep a duplicate list of points
  // (transformed into view space) - these are the Points.
  Triangle** TetraTriangles;
  svtkIdType TetraTrianglesSize;

  Triangle* TriangleList;

  // Compute whether a boundary triangle is front facing by
  // looking at the fourth point in the tetra to see if it is
  // in front (triangle is backfacing) or behind (triangle is
  // front facing) the plane containing the triangle.
  int IsTriangleFrontFacing(Triangle* triPtr, svtkIdType tetraIndex);

  // The image contains lists of intersections per pixel - we
  // need to clear this during the initialization phase for each
  // render.
  void ClearImage();

  // This is the memory buffer used to build the intersection
  // lists. We do our own memory management here because allocating
  // a bunch of small elements during rendering is too slow.
  Intersection* IntersectionBuffer[SVTK_BUNYKRCF_MAX_ARRAYS];
  int IntersectionBufferCount[SVTK_BUNYKRCF_MAX_ARRAYS];

  // This method replaces new for creating a new element - it
  // returns one from the big block already allocated (it
  // allocates another big block if necessary)
  void* NewIntersection();

  // This method is used during the initialization process to
  // check the validity of the objects - missing information
  // such as the volume, renderer, mapper, etc. will be flagged
  // and reported.
  int CheckValidity(svtkRenderer* ren, svtkVolume* vol);

  // This method is used during the initialization process to
  // transform the points to view coordinates
  void TransformPoints();

  // This method is used during the initialization process to
  // create the list of triangles if the data has changed
  void UpdateTriangleList();

  // This method is used during the initialization process to
  // update the view dependent information in the triangle list
  void ComputeViewDependentInfo();

  // This method is used during the initialization process to
  // compute the intersections for each pixel with the boundary
  // triangles.
  void ComputePixelIntersections();

private:
  svtkUnstructuredGridBunykRayCastFunction(const svtkUnstructuredGridBunykRayCastFunction&) = delete;
  void operator=(const svtkUnstructuredGridBunykRayCastFunction&) = delete;
};

#endif