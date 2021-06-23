/*=========================================================================

Program:   Visualization Toolkit
Module:    svtkTessellatorFilter.h
Language:  C++

Copyright 2003 Sandia Corporation.
Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
license for use of this work by or on behalf of the
U.S. Government. Redistribution and use in source and binary forms, with
or without modification, are permitted provided that this Notice and any
statement of authorship are reproduced on all copies.

=========================================================================*/
#ifndef svtkTessellatorFilter_h
#define svtkTessellatorFilter_h

/**
 * @class   svtkTessellatorFilter
 * @brief   approximate nonlinear FEM elements with simplices
 *
 * This class approximates nonlinear FEM elements with linear simplices.
 *
 * <b>Warning</b>: This class is temporary and will go away at some point
 * after ParaView 1.4.0.
 *
 * This filter rifles through all the cells in an input svtkDataSet. It
 * tesselates each cell and uses the svtkStreamingTessellator and
 * svtkDataSetEdgeSubdivisionCriterion classes to generate simplices that
 * approximate the nonlinear mesh using some approximation metric (encoded
 * in the particular svtkDataSetEdgeSubdivisionCriterion::EvaluateLocationAndFields
 * implementation). The simplices are placed into the filter's output
 * svtkDataSet object by the callback routines AddATetrahedron,
 * AddATriangle, and AddALine, which are registered with the triangulator.
 *
 * The output mesh will have geometry and any fields specified as
 * attributes in the input mesh's point data.  The attribute's copy flags
 * are honored, except for normals.
 *
 *
 * @par Internals:
 * The filter's main member function is RequestData(). This function first
 * calls SetupOutput() which allocates arrays and some temporary variables
 * for the primitive callbacks (OutputTriangle and OutputLine which are
 * called by AddATriangle and AddALine, respectively).  Each cell is given
 * an initial tessellation, which results in one or more calls to
 * OutputTetrahedron, OutputTriangle or OutputLine to add elements to the
 * OutputMesh. Finally, Teardown() is called to free the filter's working
 * space.
 *
 * @sa
 * svtkDataSetToUnstructuredGridFilter svtkDataSet svtkStreamingTessellator
 * svtkDataSetEdgeSubdivisionCriterion
 */

#include "svtkFiltersGeneralModule.h" // For export macro
#include "svtkUnstructuredGridAlgorithm.h"

class svtkDataArray;
class svtkDataSet;
class svtkDataSetEdgeSubdivisionCriterion;
class svtkPointLocator;
class svtkPoints;
class svtkStreamingTessellator;
class svtkEdgeSubdivisionCriterion;
class svtkUnstructuredGrid;

class SVTKFILTERSGENERAL_EXPORT svtkTessellatorFilter : public svtkUnstructuredGridAlgorithm
{
public:
  svtkTypeMacro(svtkTessellatorFilter, svtkUnstructuredGridAlgorithm);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  static svtkTessellatorFilter* New();

  virtual void SetTessellator(svtkStreamingTessellator*);
  svtkGetObjectMacro(Tessellator, svtkStreamingTessellator);

  virtual void SetSubdivider(svtkDataSetEdgeSubdivisionCriterion*);
  svtkGetObjectMacro(Subdivider, svtkDataSetEdgeSubdivisionCriterion);

  svtkMTimeType GetMTime() override;

  //@{
  /**
   * Set the dimension of the output tessellation.
   * Cells in dimensions higher than the given value will have
   * their boundaries of dimension \a OutputDimension tessellated.
   * For example, if \a OutputDimension is 2, a hexahedron's
   * quadrilateral faces would be tessellated rather than its
   * interior.
   */
  svtkSetClampMacro(OutputDimension, int, 1, 3);
  svtkGetMacro(OutputDimension, int);
  //@}

  int GetOutputDimension() const;

  //@{
  /**
   * These are convenience routines for setting properties maintained by the
   * tessellator and subdivider. They are implemented here for ParaView's
   * sake.
   */
  virtual void SetMaximumNumberOfSubdivisions(int num_subdiv_in);
  int GetMaximumNumberOfSubdivisions();
  virtual void SetChordError(double ce);
  double GetChordError();
  //@}

  //@{
  /**
   * These methods are for the ParaView client.
   */
  virtual void ResetFieldCriteria();
  virtual void SetFieldCriterion(int field, double chord);
  //@}

  //@{
  /**
   * The adaptive tessellation will output vertices that are not shared
   * among cells, even where they should be. This can be corrected to
   * some extents with a svtkMergeFilter.
   * By default, the filter is off and vertices will not be shared.
   */
  svtkGetMacro(MergePoints, svtkTypeBool);
  svtkSetMacro(MergePoints, svtkTypeBool);
  svtkBooleanMacro(MergePoints, svtkTypeBool);
  //@}

protected:
  svtkTessellatorFilter();
  ~svtkTessellatorFilter() override;

  int FillInputPortInformation(int port, svtkInformation* info) override;

  /**
   * Called by RequestData to set up a multitude of member variables used by
   * the per-primitive output functions (OutputLine, OutputTriangle, and
   * maybe one day... OutputTetrahedron).
   */
  void SetupOutput(svtkDataSet* input, svtkUnstructuredGrid* output);

  /**
   * Called by RequestData to merge output points.
   */
  void MergeOutputPoints(svtkUnstructuredGrid* input, svtkUnstructuredGrid* output);

  /**
   * Reset the temporary variables used during the filter's RequestData() method.
   */
  void Teardown();

  /**
   * Run the filter; produce a polygonal approximation to the grid.
   */
  int RequestData(svtkInformation* request, svtkInformationVector** inputVector,
    svtkInformationVector* outputVector) override;

  svtkStreamingTessellator* Tessellator;
  svtkDataSetEdgeSubdivisionCriterion* Subdivider;
  int OutputDimension;
  svtkTypeBool MergePoints;
  svtkPointLocator* Locator;

  //@{
  /**
   * These member variables are set by SetupOutput for use inside the
   * callback members OutputLine and OutputTriangle.
   */
  svtkUnstructuredGrid* OutputMesh;
  svtkPoints* OutputPoints;
  svtkDataArray** OutputAttributes;
  int* OutputAttributeIndices;
  //@}

  static void AddAPoint(const double*, svtkEdgeSubdivisionCriterion*, void*, const void*);
  static void AddALine(
    const double*, const double*, svtkEdgeSubdivisionCriterion*, void*, const void*);
  static void AddATriangle(
    const double*, const double*, const double*, svtkEdgeSubdivisionCriterion*, void*, const void*);
  static void AddATetrahedron(const double*, const double*, const double*, const double*,
    svtkEdgeSubdivisionCriterion*, void*, const void*);
  void OutputPoint(const double*);
  void OutputLine(const double*, const double*);
  void OutputTriangle(const double*, const double*, const double*);
  void OutputTetrahedron(const double*, const double*, const double*, const double*);

private:
  svtkTessellatorFilter(const svtkTessellatorFilter&) = delete;
  void operator=(const svtkTessellatorFilter&) = delete;
};

inline int svtkTessellatorFilter::GetOutputDimension() const
{
  return this->OutputDimension;
}

#endif // svtkTessellatorFilter_h