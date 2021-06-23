/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkGreedyTerrainDecimation.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkGreedyTerrainDecimation
 * @brief   reduce height field (represented as image) to reduced TIN
 *
 * svtkGreedyTerrainDecimation approximates a height field with a triangle
 * mesh (triangulated irregular network - TIN) using a greedy insertion
 * algorithm similar to that described by Garland and Heckbert in their paper
 * "Fast Polygonal Approximations of Terrain and Height Fields" (Technical
 * Report CMU-CS-95-181).  The input to the filter is a height field
 * (represented by a image whose scalar values are height) and the output of
 * the filter is polygonal data consisting of triangles. The number of
 * triangles in the output is reduced in number as compared to a naive
 * tessellation of the input height field. This filter copies point data
 * from the input to the output for those points present in the output.
 *
 * An brief description of the algorithm is as follows. The algorithm uses a
 * top-down decimation approach that initially represents the height field
 * with two triangles (whose vertices are at the four corners of the
 * image). These two triangles form a Delaunay triangulation. In an iterative
 * fashion, the point in the image with the greatest error (as compared to
 * the original height field) is injected into the triangulation. (Note that
 * the single point with the greatest error per triangle is identified and
 * placed into a priority queue. As the triangulation is modified, the errors
 * from the deleted triangles are removed from the queue, error values from
 * the new triangles are added.) The point whose error is at the top of the
 * queue is added to the triangulaion modifying it using the standard
 * incremental Delaunay point insertion (see svtkDelaunay2D) algorithm. Points
 * are repeatedly inserted until the appropriate (user-specified) error
 * criterion is met.
 *
 * To use this filter, set the input and specify the error measure to be
 * used.  The error measure options are 1) the absolute number of triangles
 * to be produced; 2) a fractional reduction of the mesh (numTris/maxTris)
 * where maxTris is the largest possible number of triangles
 * 2*(dims[0]-1)*(dims[1]-1); 3) an absolute measure on error (maximum
 * difference in height field to reduced TIN); and 4) relative error (the
 * absolute error is normalized by the diagonal of the bounding box of the
 * height field).
 *
 * @warning
 * This algorithm requires the entire input dataset to be in memory, hence it
 * may not work for extremely large images. Invoking BoundaryVertexDeletionOff
 * will allow you to stitch together images with matching boundaries.
 *
 * @warning
 * The input height image is assumed to be positioned in the x-y plane so the
 * scalar value is the z-coordinate, height value.
 *
 * @sa
 * svtkDecimatePro svtkQuadricDecimation svtkQuadricClustering
 */

#ifndef svtkGreedyTerrainDecimation_h
#define svtkGreedyTerrainDecimation_h

#include "svtkFiltersHybridModule.h" // For export macro
#include "svtkPolyDataAlgorithm.h"

class svtkPriorityQueue;
class svtkDataArray;
class svtkPointData;
class svtkIdList;
class svtkDoubleArray;
class svtkFloatArray;

// PIMPL Encapsulation for STL containers
class svtkGreedyTerrainDecimationTerrainInfoType;
class svtkGreedyTerrainDecimationPointInfoType;

#define SVTK_ERROR_NUMBER_OF_TRIANGLES 0
#define SVTK_ERROR_SPECIFIED_REDUCTION 1
#define SVTK_ERROR_ABSOLUTE 2
#define SVTK_ERROR_RELATIVE 3

class SVTKFILTERSHYBRID_EXPORT svtkGreedyTerrainDecimation : public svtkPolyDataAlgorithm
{
public:
  svtkTypeMacro(svtkGreedyTerrainDecimation, svtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  /**
   * Instantiate the class.
   */
  static svtkGreedyTerrainDecimation* New();

  //@{
  /**
   * Specify how to terminate the algorithm: either as an absolute number of
   * triangles, a relative number of triangles (normalized by the full
   * resolution mesh), an absolute error (in the height field), or relative
   * error (normalized by the length of the diagonal of the image).
   */
  svtkSetClampMacro(ErrorMeasure, int, SVTK_ERROR_NUMBER_OF_TRIANGLES, SVTK_ERROR_RELATIVE);
  svtkGetMacro(ErrorMeasure, int);
  void SetErrorMeasureToNumberOfTriangles()
  {
    this->SetErrorMeasure(SVTK_ERROR_NUMBER_OF_TRIANGLES);
  }
  void SetErrorMeasureToSpecifiedReduction()
  {
    this->SetErrorMeasure(SVTK_ERROR_SPECIFIED_REDUCTION);
  }
  void SetErrorMeasureToAbsoluteError() { this->SetErrorMeasure(SVTK_ERROR_ABSOLUTE); }
  void SetErrorMeasureToRelativeError() { this->SetErrorMeasure(SVTK_ERROR_RELATIVE); }
  //@}

  //@{
  /**
   * Specify the number of triangles to produce on output. (It is a
   * good idea to make sure this is less than a tessellated mesh
   * at full resolution.) You need to set this value only when
   * the error measure is set to NumberOfTriangles.
   */
  svtkSetClampMacro(NumberOfTriangles, svtkIdType, 2, SVTK_ID_MAX);
  svtkGetMacro(NumberOfTriangles, svtkIdType);
  //@}

  //@{
  /**
   * Specify the reduction of the mesh (represented as a fraction).  Note
   * that a value of 0.10 means a 10% reduction.  You need to set this value
   * only when the error measure is set to SpecifiedReduction.
   */
  svtkSetClampMacro(Reduction, double, 0.0, 1.0);
  svtkGetMacro(Reduction, double);
  //@}

  //@{
  /**
   * Specify the absolute error of the mesh; that is, the error in height
   * between the decimated mesh and the original height field.  You need to
   * set this value only when the error measure is set to AbsoluteError.
   */
  svtkSetClampMacro(AbsoluteError, double, 0.0, SVTK_DOUBLE_MAX);
  svtkGetMacro(AbsoluteError, double);
  //@}

  //@{
  /**
   * Specify the relative error of the mesh; that is, the error in height
   * between the decimated mesh and the original height field normalized by
   * the diagonal of the image.  You need to set this value only when the
   * error measure is set to RelativeError.
   */
  svtkSetClampMacro(RelativeError, double, 0.0, SVTK_DOUBLE_MAX);
  svtkGetMacro(RelativeError, double);
  //@}

  //@{
  /**
   * Turn on/off the deletion of vertices on the boundary of a mesh. This
   * may limit the maximum reduction that may be achieved.
   */
  svtkSetMacro(BoundaryVertexDeletion, svtkTypeBool);
  svtkGetMacro(BoundaryVertexDeletion, svtkTypeBool);
  svtkBooleanMacro(BoundaryVertexDeletion, svtkTypeBool);
  //@}

  //@{
  /**
   * Compute normals based on the input image. Off by default.
   */
  svtkSetMacro(ComputeNormals, svtkTypeBool);
  svtkGetMacro(ComputeNormals, svtkTypeBool);
  svtkBooleanMacro(ComputeNormals, svtkTypeBool);
  //@}

protected:
  svtkGreedyTerrainDecimation();
  ~svtkGreedyTerrainDecimation() override;

  int RequestData(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;
  int FillInputPortInformation(int port, svtkInformation* info) override;

  svtkTypeBool ComputeNormals;
  svtkFloatArray* Normals;
  void ComputePointNormal(int i, int j, float n[3]);

  // ivars that the API addresses
  int ErrorMeasure;
  svtkIdType NumberOfTriangles;
  double Reduction;
  double AbsoluteError;
  double RelativeError;
  svtkTypeBool BoundaryVertexDeletion; // Can we delete boundary vertices?

  // Used for convenience
  svtkPolyData* Mesh;
  svtkPointData* InputPD;
  svtkPointData* OutputPD;
  svtkDoubleArray* Points;
  svtkDataArray* Heights;
  svtkIdType CurrentPointId;
  double Tolerance;
  svtkIdList* Neighbors;
  int Dimensions[3];
  double Origin[3];
  double Spacing[3];
  svtkIdType MaximumNumberOfTriangles;
  double Length;

  // Bookkeeping arrays
  svtkPriorityQueue* TerrainError;                         // errors for each pt in height field
  svtkGreedyTerrainDecimationTerrainInfoType* TerrainInfo; // owning triangle for each pt
  svtkGreedyTerrainDecimationPointInfoType* PointInfo;     // map mesh pt id to input pt id

  // Make a guess at initial allocation
  void EstimateOutputSize(const svtkIdType numInputPts, svtkIdType& numPts, svtkIdType& numTris);

  // Returns non-zero if the error measure is satisfied.
  virtual int SatisfiesErrorMeasure(double error);

  // Insert all the boundary vertices into the TIN
  void InsertBoundaryVertices();

  // Insert a point into the triangulation; get a point from the triangulation
  svtkIdType AddPointToTriangulation(svtkIdType inputPtId);
  svtkIdType InsertNextPoint(svtkIdType inputPtId, double x[3]);
  double* GetPoint(svtkIdType id);
  void GetPoint(svtkIdType id, double x[3]);

  // Helper functions
  void GetTerrainPoint(int i, int j, double x[3]);
  void ComputeImageCoordinates(svtkIdType inputPtId, int ij[2]);
  int InCircle(double x[3], double x1[3], double x2[3], double x3[3]);
  svtkIdType FindTriangle(double x[3], svtkIdType ptIds[3], svtkIdType tri, double tol,
    svtkIdType nei[3], svtkIdList* neighbors, int& status);
  void CheckEdge(svtkIdType ptId, double x[3], svtkIdType p1, svtkIdType p2, svtkIdType tri, int depth);

  void UpdateTriangles(svtkIdType meshPtId); // update all points connected to this point
  void UpdateTriangle(svtkIdType triId, svtkIdType p1, svtkIdType p2, svtkIdType p3);
  void UpdateTriangle(svtkIdType triId, int ij1[2], int ij2[2], int ij3[2], double h[4]);

  int CharacterizeTriangle(int ij1[2], int ij2[2], int ij[3], int*& min, int*& max, int*& midL,
    int*& midR, int*& mid, int mid2[2], double h[3], double& hMin, double& hMax, double& hL,
    double& hR);

private:
  svtkGreedyTerrainDecimation(const svtkGreedyTerrainDecimation&) = delete;
  void operator=(const svtkGreedyTerrainDecimation&) = delete;
};

#endif