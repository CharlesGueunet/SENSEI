/*=========================================================================

  Program:   Visualization Toolkit

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkOpenGLIndexBufferObject.h"
#include "svtkObjectFactory.h"

#include "svtkArrayDispatch.h"
#include "svtkAssume.h"
#include "svtkCellArray.h"
#include "svtkCellArrayIterator.h"
#include "svtkDataArrayRange.h"
#include "svtkPoints.h"
#include "svtkPolygon.h"
#include "svtkProperty.h"
#include "svtkUnsignedCharArray.h"

#include "svtk_glew.h"

#include <set>

svtkStandardNewMacro(svtkOpenGLIndexBufferObject);

svtkOpenGLIndexBufferObject::svtkOpenGLIndexBufferObject()
{
  this->IndexCount = 0;
  this->SetType(svtkOpenGLIndexBufferObject::ElementArrayBuffer);
}

svtkOpenGLIndexBufferObject::~svtkOpenGLIndexBufferObject() = default;

namespace
{
// A worker functor. The calculation is implemented in the function template
// for operator().
struct AppendTrianglesWorker
{
  std::vector<unsigned int>* indexArray;
  svtkCellArray* cells;
  svtkIdType vOffset;

  // AoS fast path
  template <typename ValueType>
  void operator()(svtkAOSDataArrayTemplate<ValueType>* src)
  {
    ValueType* points = src->Begin();

    auto cellIter = svtk::TakeSmartPointer(cells->NewIterator());

    for (cellIter->GoToFirstCell(); !cellIter->IsDoneWithTraversal(); cellIter->GoToNextCell())
    {
      svtkIdType cellSize;
      const svtkIdType* cell;
      cellIter->GetCurrentCell(cellSize, cell);

      if (cellSize >= 3)
      {
        svtkIdType id1 = cell[0];
        ValueType* p1 = points + id1 * 3;
        for (int i = 1; i < cellSize - 1; i++)
        {
          svtkIdType id2 = cell[i];
          svtkIdType id3 = cell[i + 1];
          ValueType* p2 = points + id2 * 3;
          ValueType* p3 = points + id3 * 3;
          if ((p1[0] != p2[0] || p1[1] != p2[1] || p1[2] != p2[2]) &&
            (p3[0] != p2[0] || p3[1] != p2[1] || p3[2] != p2[2]) &&
            (p3[0] != p1[0] || p3[1] != p1[1] || p3[2] != p1[2]))
          {
            indexArray->push_back(static_cast<unsigned int>(id1 + vOffset));
            indexArray->push_back(static_cast<unsigned int>(id2 + vOffset));
            indexArray->push_back(static_cast<unsigned int>(id3 + vOffset));
          }
        }
      }
    }
  }

  // Generic API, on VS13 Rel this is about 80% slower than
  // the AOS template above. (We should retest this now that it uses ranges).
  template <typename PointArray>
  void operator()(PointArray* pointArray)
  {
    const auto points = svtk::DataArrayTupleRange<3>(pointArray);

    auto cellIter = svtk::TakeSmartPointer(cells->NewIterator());

    for (cellIter->GoToFirstCell(); !cellIter->IsDoneWithTraversal(); cellIter->GoToNextCell())
    {
      svtkIdType cellSize;
      const svtkIdType* cell;
      cellIter->GetCurrentCell(cellSize, cell);

      if (cellSize >= 3)
      {
        const svtkIdType id1 = cell[0];
        for (int i = 1; i < cellSize - 1; i++)
        {
          const svtkIdType id2 = cell[i];
          const svtkIdType id3 = cell[i + 1];

          const auto pt1 = points[id1];
          const auto pt2 = points[id2];
          const auto pt3 = points[id3];

          if (pt1 != pt2 && pt1 != pt3 && pt2 != pt3)
          {
            indexArray->push_back(static_cast<unsigned int>(id1 + vOffset));
            indexArray->push_back(static_cast<unsigned int>(id2 + vOffset));
            indexArray->push_back(static_cast<unsigned int>(id3 + vOffset));
          }
        }
      }
    }
  }
};

} // end anon namespace

// used to create an IBO for triangle primitives
void svtkOpenGLIndexBufferObject::AppendTriangleIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray* cells, svtkPoints* points, svtkIdType vOffset)
{
  if (cells->GetNumberOfConnectivityIds() > cells->GetNumberOfCells() * 3)
  {
    size_t targetSize =
      indexArray.size() + (cells->GetNumberOfConnectivityIds() - cells->GetNumberOfCells() * 2) * 3;
    if (targetSize > indexArray.capacity())
    {
      if (targetSize < indexArray.capacity() * 1.5)
      {
        targetSize = indexArray.capacity() * 1.5;
      }
      indexArray.reserve(targetSize);
    }
  }

  // Create our worker functor:
  AppendTrianglesWorker worker;
  worker.indexArray = &indexArray;
  worker.cells = cells;
  worker.vOffset = vOffset;

  // Define our dispatcher on float/double
  typedef svtkArrayDispatch::DispatchByValueType<svtkArrayDispatch::Reals> Dispatcher;

  // Execute the dispatcher:
  if (!Dispatcher::Execute(points->GetData(), worker))
  {
    // If Execute() fails, it means the dispatch failed due to an
    // unsupported array type this falls back to using the
    // svtkDataArray double API:
    worker(points->GetData());
  }
}

// used to create an IBO for triangle primitives
size_t svtkOpenGLIndexBufferObject::CreateTriangleIndexBuffer(svtkCellArray* cells, svtkPoints* points)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendTriangleIndexBuffer(indexArray, cells, points, 0);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

// used to create an IBO for point primitives
void svtkOpenGLIndexBufferObject::AppendPointIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray* cells, svtkIdType vOffset)
{
  const svtkIdType* indices(nullptr);
  svtkIdType npts(0);
  size_t targetSize = indexArray.size() + cells->GetNumberOfConnectivityIds();
  if (targetSize > indexArray.capacity())
  {
    if (targetSize < indexArray.capacity() * 1.5)
    {
      targetSize = indexArray.capacity() * 1.5;
    }
    indexArray.reserve(targetSize);
  }

  for (cells->InitTraversal(); cells->GetNextCell(npts, indices);)
  {
    for (int i = 0; i < npts; ++i)
    {
      indexArray.push_back(static_cast<unsigned int>(*(indices++) + vOffset));
    }
  }
}

// used to create an IBO for triangle primitives
size_t svtkOpenGLIndexBufferObject::CreatePointIndexBuffer(svtkCellArray* cells)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendPointIndexBuffer(indexArray, cells, 0);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

// used to create an IBO for primitives as lines.  This method treats each line segment
// as independent.  So for a triangle mesh you would get 6 verts per triangle
// 3 edges * 2 verts each.  With a line loop you only get 3 verts so half the storage.
// but... line loops are slower than line segments.
void svtkOpenGLIndexBufferObject::AppendTriangleLineIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray* cells, svtkIdType vOffset)
{
  const svtkIdType* indices(nullptr);
  svtkIdType npts(0);
  size_t targetSize = indexArray.size() + 2 * cells->GetNumberOfConnectivityIds();
  if (targetSize > indexArray.capacity())
  {
    if (targetSize < indexArray.capacity() * 1.5)
    {
      targetSize = indexArray.capacity() * 1.5;
    }
    indexArray.reserve(targetSize);
  }

  for (cells->InitTraversal(); cells->GetNextCell(npts, indices);)
  {
    for (int i = 0; i < npts; ++i)
    {
      indexArray.push_back(static_cast<unsigned int>(indices[i] + vOffset));
      indexArray.push_back(static_cast<unsigned int>(indices[i < npts - 1 ? i + 1 : 0] + vOffset));
    }
  }
}

// used to create an IBO for primitives as lines.  This method treats each line segment
// as independent.  So for a triangle mesh you would get 6 verts per triangle
// 3 edges * 2 verts each.  With a line loop you only get 3 verts so half the storage.
// but... line loops are slower than line segments.
size_t svtkOpenGLIndexBufferObject::CreateTriangleLineIndexBuffer(svtkCellArray* cells)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendTriangleLineIndexBuffer(indexArray, cells, 0);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

// used to create an IBO for primitives as lines.  This method treats each
// line segment as independent.  So for a line strip you would get multiple
// line segments out
void svtkOpenGLIndexBufferObject::AppendLineIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray* cells, svtkIdType vOffset)
{
  const svtkIdType* indices(nullptr);
  svtkIdType npts(0);

  // possibly adjust size
  if (cells->GetNumberOfConnectivityIds() > 2 * cells->GetNumberOfCells())
  {
    size_t targetSize =
      indexArray.size() + 2 * (cells->GetNumberOfConnectivityIds() - cells->GetNumberOfCells());
    if (targetSize > indexArray.capacity())
    {
      if (targetSize < indexArray.capacity() * 1.5)
      {
        targetSize = indexArray.capacity() * 1.5;
      }
      indexArray.reserve(targetSize);
    }
  }
  for (cells->InitTraversal(); cells->GetNextCell(npts, indices);)
  {
    for (int i = 0; i < npts - 1; ++i)
    {
      indexArray.push_back(static_cast<unsigned int>(indices[i] + vOffset));
      indexArray.push_back(static_cast<unsigned int>(indices[i + 1] + vOffset));
    }
  }
}

// used to create an IBO for primitives as lines.  This method treats each
// line segment as independent.  So for a line strip you would get multiple
// line segments out
size_t svtkOpenGLIndexBufferObject::CreateLineIndexBuffer(svtkCellArray* cells)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendLineIndexBuffer(indexArray, cells, 0);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

// used to create an IBO for triangle strips
size_t svtkOpenGLIndexBufferObject::CreateStripIndexBuffer(
  svtkCellArray* cells, bool wireframeTriStrips)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendStripIndexBuffer(indexArray, cells, 0, wireframeTriStrips);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

void svtkOpenGLIndexBufferObject::AppendStripIndexBuffer(std::vector<unsigned int>& indexArray,
  svtkCellArray* cells, svtkIdType vOffset, bool wireframeTriStrips)
{
  const svtkIdType* pts = nullptr;
  svtkIdType npts = 0;

  size_t triCount = cells->GetNumberOfConnectivityIds() - 2 * cells->GetNumberOfCells();
  size_t targetSize = wireframeTriStrips ? 2 * (triCount * 2 + 1) : triCount * 3;
  indexArray.reserve(targetSize);

  if (wireframeTriStrips)
  {
    for (cells->InitTraversal(); cells->GetNextCell(npts, pts);)
    {
      indexArray.push_back(static_cast<unsigned int>(pts[0] + vOffset));
      indexArray.push_back(static_cast<unsigned int>(pts[1] + vOffset));
      for (int j = 0; j < npts - 2; ++j)
      {
        indexArray.push_back(static_cast<unsigned int>(pts[j] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(pts[j + 2] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(pts[j + 1] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(pts[j + 2] + vOffset));
      }
    }
  }
  else
  {
    for (cells->InitTraversal(); cells->GetNextCell(npts, pts);)
    {
      for (int j = 0; j < npts - 2; ++j)
      {
        indexArray.push_back(static_cast<unsigned int>(pts[j] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(pts[j + 1 + j % 2] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(pts[j + 1 + (j + 1) % 2] + vOffset));
      }
    }
  }
}

// used to create an IBO for polys in wireframe with edge flags
void svtkOpenGLIndexBufferObject::AppendEdgeFlagIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray* cells, svtkIdType vOffset, svtkDataArray* ef)
{
  const svtkIdType* pts(nullptr);
  svtkIdType npts(0);

  unsigned char* ucef = svtkArrayDownCast<svtkUnsignedCharArray>(ef)->GetPointer(0);

  // possibly adjust size
  if (cells->GetNumberOfConnectivityIds() > 2 * cells->GetNumberOfCells())
  {
    size_t targetSize =
      indexArray.size() + 2 * (cells->GetNumberOfConnectivityIds() - cells->GetNumberOfCells());
    if (targetSize > indexArray.capacity())
    {
      if (targetSize < indexArray.capacity() * 1.5)
      {
        targetSize = indexArray.capacity() * 1.5;
      }
      indexArray.reserve(targetSize);
    }
  }
  for (cells->InitTraversal(); cells->GetNextCell(npts, pts);)
  {
    for (int j = 0; j < npts; ++j)
    {
      if (ucef[pts[j]] && npts > 1) // draw this edge and poly is not degenerate
      {
        // determine the ending vertex
        svtkIdType nextVert = (j == npts - 1) ? pts[0] : pts[j + 1];
        indexArray.push_back(static_cast<unsigned int>(pts[j] + vOffset));
        indexArray.push_back(static_cast<unsigned int>(nextVert + vOffset));
      }
    }
  }
}

// used to create an IBO for polys in wireframe with edge flags
size_t svtkOpenGLIndexBufferObject::CreateEdgeFlagIndexBuffer(svtkCellArray* cells, svtkDataArray* ef)
{
  if (!cells->GetNumberOfCells())
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendEdgeFlagIndexBuffer(indexArray, cells, 0, ef);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

// used to create an IBO for point primitives
void svtkOpenGLIndexBufferObject::AppendVertexIndexBuffer(
  std::vector<unsigned int>& indexArray, svtkCellArray** cells, svtkIdType vOffset)
{
  const svtkIdType* indices(nullptr);
  svtkIdType npts(0);

  // we use a set to make them unique
  std::set<svtkIdType> vertsUsed;
  for (int j = 0; j < 4; j++)
  {
    for (cells[j]->InitTraversal(); cells[j]->GetNextCell(npts, indices);)
    {
      for (int i = 0; i < npts; ++i)
      {
        vertsUsed.insert(static_cast<unsigned int>(*(indices++) + vOffset));
      }
    }
  }

  // now put them into the vector
  size_t targetSize = indexArray.size() + vertsUsed.size();
  if (targetSize > indexArray.capacity())
  {
    if (targetSize < indexArray.capacity() * 1.5)
    {
      targetSize = indexArray.capacity() * 1.5;
    }
    indexArray.reserve(targetSize);
  }

  for (std::set<svtkIdType>::const_iterator i = vertsUsed.begin(); i != vertsUsed.end(); ++i)
  {
    indexArray.push_back(*i);
  }
}

// used to create an IBO for triangle primitives
size_t svtkOpenGLIndexBufferObject::CreateVertexIndexBuffer(svtkCellArray** cells)
{
  unsigned long totalCells = 0;
  for (int i = 0; i < 4; i++)
  {
    totalCells += cells[i]->GetNumberOfCells();
  }

  if (!totalCells)
  {
    this->IndexCount = 0;
    return 0;
  }
  std::vector<unsigned int> indexArray;
  AppendVertexIndexBuffer(indexArray, cells, 0);
  this->Upload(indexArray, svtkOpenGLIndexBufferObject::ElementArrayBuffer);
  this->IndexCount = indexArray.size();
  return indexArray.size();
}

//-----------------------------------------------------------------------------
void svtkOpenGLIndexBufferObject::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}