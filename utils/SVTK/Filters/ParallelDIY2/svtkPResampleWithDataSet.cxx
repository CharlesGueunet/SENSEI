/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPResampleWithDataSet.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkPResampleWithDataSet.h"

#include "svtkArrayDispatch.h"
#include "svtkBoundingBox.h"
#include "svtkCharArray.h"
#include "svtkCompositeDataIterator.h"
#include "svtkCompositeDataProbeFilter.h"
#include "svtkCompositeDataSet.h"
#include "svtkDIYUtilities.h"
#include "svtkDataArrayRange.h"
#include "svtkDataObject.h"
#include "svtkDataSet.h"
#include "svtkIdTypeArray.h"
#include "svtkImageData.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMultiProcessController.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPoints.h"
#include "svtkStreamingDemandDrivenPipeline.h"
#include "svtkUnstructuredGrid.h"

// clang-format off
#include "svtk_diy2.h" // must include this before any diy header
#include SVTK_DIY2(diy/assigner.hpp)
#include SVTK_DIY2(diy/link.hpp)
#include SVTK_DIY2(diy/master.hpp)
#include SVTK_DIY2(diy/mpi.hpp)
// clang-format on

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

//---------------------------------------------------------------------------
// Algorithm of this filter:
// 1) Compute the bounds of all the blocks of Source.
// 2) Do an all_gather so that all the nodes know all the bounds.
// 3) Using Input blocks' bounds and Source bounds, find the communication
//    neighbors of each node.
// 4) Find and send the Input points that lie inside a neighbor's Source bounds.
//    The search is made faster by using a point lookup structure
//    (RegularPartition or BalancedPartition below).
// 5) Perform resampling on local Input blocks.
// 6) Perform resampling on points received from neighbors.
// 7) Send the resampled points back to the neighbors they were received from.
// 8) Receive resampled points from neighbors and update local blocks of output.
//    Since points of a single Input block can overlap multiple Source blocks
//    and since different Source blocks can have different arrays (Partial Arrays),
//    it is possible that the points of an output block will have different arrays.
//    Remove arrays from a block that are not valid for all its points.
//---------------------------------------------------------------------------

svtkStandardNewMacro(svtkPResampleWithDataSet);

svtkCxxSetObjectMacro(svtkPResampleWithDataSet, Controller, svtkMultiProcessController);

//---------------------------------------------------------------------------
svtkPResampleWithDataSet::svtkPResampleWithDataSet()
  : Controller(nullptr)
  , UseBalancedPartitionForPointsLookup(false)
{
  this->SetController(svtkMultiProcessController::GetGlobalController());
}

//----------------------------------------------------------------------------
svtkPResampleWithDataSet::~svtkPResampleWithDataSet()
{
  this->SetController(nullptr);
}

//----------------------------------------------------------------------------
void svtkPResampleWithDataSet::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  if (this->Controller)
  {
    this->Controller->PrintSelf(os, indent);
  }
  os << indent << "Points lookup partitioning: "
     << (this->UseBalancedPartitionForPointsLookup ? "Balanced" : "Regular") << endl;
}

//-----------------------------------------------------------------------------
int svtkPResampleWithDataSet::RequestUpdateExtent(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  if (!this->Controller || this->Controller->GetNumberOfProcesses() == 1)
  {
    return this->Superclass::RequestUpdateExtent(request, inputVector, outputVector);
  }

  svtkInformation* sourceInfo = inputVector[1]->GetInformationObject(0);

  sourceInfo->Remove(svtkStreamingDemandDrivenPipeline::UPDATE_EXTENT());
  if (sourceInfo->Has(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()))
  {
    sourceInfo->Set(svtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(),
      sourceInfo->Get(svtkStreamingDemandDrivenPipeline::WHOLE_EXTENT()), 6);
  }

  return 1;
}

namespace
{

//----------------------------------------------------------------------------
struct Point
{
  double Position[3];
  svtkIdType PointId;
  int BlockId;
};

//----------------------------------------------------------------------------
class Partition
{
public:
  virtual ~Partition() {}
  virtual void CreatePartition(const std::vector<svtkDataSet*>& blocks) = 0;
  virtual void FindPointsInBounds(const double bounds[6], std::vector<Point>& points) const = 0;
};

// Partitions the points into spatially regular sized bins. The bins may contain
// widely varying number of points.
class RegularPartition : public Partition
{
public:
  void CreatePartition(const std::vector<svtkDataSet*>& blocks) override
  {
    // compute the bounds of the composite dataset
    size_t totalNumberOfPoints = 0;
    this->Bounds[0] = this->Bounds[2] = this->Bounds[4] = SVTK_DOUBLE_MAX;
    this->Bounds[1] = this->Bounds[3] = this->Bounds[5] = SVTK_DOUBLE_MIN;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
      svtkDataSet* ds = blocks[i];
      if (!ds)
      {
        continue;
      }

      totalNumberOfPoints += ds->GetNumberOfPoints();
      double bounds[6];
      ds->GetBounds(bounds);

      for (int j = 0; j < 3; ++j)
      {
        this->Bounds[2 * j] = std::min(this->Bounds[2 * j], bounds[2 * j]);
        this->Bounds[2 * j + 1] = std::max(this->Bounds[2 * j + 1], bounds[2 * j + 1]);
      }
    }

    if (totalNumberOfPoints == 0)
    {
      return;
    }

    // compute a regular partitioning of the space
    int nbins = 1;
    double dim = 0; // the dimensionality of the dataset
    for (int i = 0; i < 3; ++i)
    {
      if ((this->Bounds[2 * i + 1] - this->Bounds[2 * i]) > 0.0)
      {
        ++dim;
      }
    }
    if (dim != 0.0)
    {
      nbins =
        static_cast<int>(std::ceil(std::pow(static_cast<double>(totalNumberOfPoints), (1.0 / dim)) /
          std::pow(static_cast<double>(NUM_POINTS_PER_BIN), (1.0 / dim))));
    }
    for (int i = 0; i < 3; ++i)
    {
      this->NumBins[i] = ((this->Bounds[2 * i + 1] - this->Bounds[2 * i]) > 0.0) ? nbins : 1;
      this->BinSize[i] =
        (this->Bounds[2 * i + 1] - this->Bounds[2 * i]) / static_cast<double>(NumBins[i]);

      // slightly increase bin size to include points on this->Bounds[2*i]
      double e = 1.0 / std::max(1000.0, static_cast<double>(nbins + 1));
      if (this->BinSize[i] > 0.0)
      {
        e *= this->BinSize[i]; // make e relative to binsize
      }
      this->BinSize[i] += e;
    }

    // compute the bin id of each point
    this->Nodes.reserve(totalNumberOfPoints);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
      svtkDataSet* ds = blocks[i];
      if (!ds)
      {
        continue;
      }

      svtkIdType numPts = ds->GetNumberOfPoints();
      for (svtkIdType j = 0; j < numPts; ++j)
      {
        double pos[3];
        ds->GetPoint(j, pos);

        int bin[3];
        bin[0] = static_cast<int>((pos[0] - this->Bounds[0]) / (this->BinSize[0]));
        bin[1] = static_cast<int>((pos[1] - this->Bounds[2]) / (this->BinSize[1]));
        bin[2] = static_cast<int>((pos[2] - this->Bounds[4]) / (this->BinSize[2]));

        Node n;
        n.BinId = bin[0] + this->NumBins[0] * bin[1] + this->NumBins[0] * this->NumBins[1] * bin[2];
        n.Pt.BlockId = static_cast<int>(i);
        n.Pt.PointId = j;
        std::copy(pos, pos + 3, n.Pt.Position);
        this->Nodes.push_back(n);
      }
    }
    // sort by BinId
    std::sort(this->Nodes.begin(), this->Nodes.end());

    // map from bin id to first node of the bin
    size_t totalBins = this->NumBins[0] * this->NumBins[1] * this->NumBins[2];
    this->Bins.resize(totalBins + 1);
    for (size_t i = 0, j = 0; i <= totalBins; ++i)
    {
      this->Bins[i] = j;
      while (j < totalNumberOfPoints && this->Nodes[j].BinId == i)
      {
        ++j;
      }
    }
  }

  void FindPointsInBounds(const double bounds[6], std::vector<Point>& points) const override
  {
    if (this->Nodes.empty())
    {
      return;
    }

    double searchBds[6];
    for (int i = 0; i < 3; ++i)
    {
      searchBds[2 * i] = std::max(bounds[2 * i], this->Bounds[2 * i]);
      searchBds[2 * i + 1] = std::min(bounds[2 * i + 1], this->Bounds[2 * i + 1]);
    }

    int minBin[3], maxBin[3];
    for (int i = 0; i < 3; ++i)
    {
      minBin[i] = static_cast<int>((searchBds[2 * i] - this->Bounds[2 * i]) / (this->BinSize[i]));
      maxBin[i] =
        static_cast<int>((searchBds[2 * i + 1] - this->Bounds[2 * i]) / (this->BinSize[i]));
    }

    for (int k = minBin[2]; k <= maxBin[2]; ++k)
    {
      bool passAllZ = (k > minBin[2] && k < maxBin[2]);
      for (int j = minBin[1]; j <= maxBin[1]; ++j)
      {
        bool passAllY = (j > minBin[1] && j < maxBin[1]);
        for (int i = minBin[0]; i <= maxBin[0]; ++i)
        {
          bool passAllX = (i > minBin[0] && i < maxBin[0]);

          svtkIdType bid = i + j * this->NumBins[0] + k * this->NumBins[0] * this->NumBins[1];
          size_t binBegin = this->Bins[bid];
          size_t binEnd = this->Bins[bid + 1];
          if (binBegin == binEnd) // empty bin
          {
            continue;
          }
          if (passAllX && passAllY && passAllZ)
          {
            for (size_t p = binBegin; p < binEnd; ++p)
            {
              points.push_back(this->Nodes[p].Pt);
            }
          }
          else
          {
            for (size_t p = binBegin; p < binEnd; ++p)
            {
              const double* pos = this->Nodes[p].Pt.Position;
              if (pos[0] >= searchBds[0] && pos[0] <= searchBds[1] && pos[1] >= searchBds[2] &&
                pos[1] <= searchBds[3] && pos[2] >= searchBds[4] && pos[2] <= searchBds[5])
              {
                points.push_back(this->Nodes[p].Pt);
              }
            }
          }
        }
      }
    }
  }

private:
  enum
  {
    NUM_POINTS_PER_BIN = 512
  };

  struct Node
  {
    Point Pt;
    size_t BinId;

    bool operator<(const Node& n) const { return this->BinId < n.BinId; }
  };

  std::vector<Node> Nodes;
  std::vector<size_t> Bins;
  double Bounds[6];
  int NumBins[3];
  double BinSize[3];
};

// Partitions the points into balanced bins. Each bin contains similar number
// of points
class BalancedPartition : public Partition
{
public:
  void CreatePartition(const std::vector<svtkDataSet*>& blocks) override
  {
    // count total number of points
    svtkIdType totalNumberOfPoints = 0;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
      totalNumberOfPoints += blocks[i] ? blocks[i]->GetNumberOfPoints() : 0;
    }

    // copy points and compute dataset bounds
    this->Nodes.reserve(totalNumberOfPoints);
    this->Bounds[0] = this->Bounds[2] = this->Bounds[4] = SVTK_DOUBLE_MAX;
    this->Bounds[1] = this->Bounds[3] = this->Bounds[5] = SVTK_DOUBLE_MIN;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
      svtkDataSet* ds = blocks[i];
      if (!ds)
      {
        continue;
      }

      svtkIdType numPts = ds->GetNumberOfPoints();
      for (svtkIdType j = 0; j < numPts; ++j)
      {
        double pos[3];
        ds->GetPoint(j, pos);

        Point pt;
        pt.PointId = j;
        pt.BlockId = static_cast<int>(i);
        std::copy(pos, pos + 3, pt.Position);
        this->Nodes.push_back(pt);

        for (int k = 0; k < 3; ++k)
        {
          this->Bounds[2 * k] = std::min(this->Bounds[2 * k], pos[k]);
          this->Bounds[2 * k + 1] = std::max(this->Bounds[2 * k + 1], pos[k]);
        }
      }
    }

    // approximate number of nodes in the tree
    svtkIdType splitsSize = totalNumberOfPoints / (NUM_POINTS_PER_BIN / 2);
    this->Splits.resize(splitsSize);
    this->RecursiveSplit(&this->Nodes[0], &this->Nodes[totalNumberOfPoints], &this->Splits[0],
      &this->Splits[splitsSize], 0);
  }

  void FindPointsInBounds(const double bounds[6], std::vector<Point>& points) const override
  {
    int tag = 0;
    for (int i = 0; i < 3; ++i)
    {
      if (this->Bounds[2 * i] > bounds[2 * i + 1] || this->Bounds[2 * i + 1] < bounds[2 * i])
      {
        return;
      }
      tag |= (this->Bounds[2 * i] >= bounds[2 * i]) ? (1 << (2 * i)) : 0;
      tag |= (this->Bounds[2 * i + 1] <= bounds[2 * i + 1]) ? (1 << (2 * i + 1)) : 0;
    }

    svtkIdType numPoints = static_cast<svtkIdType>(this->Nodes.size());
    svtkIdType splitSize = static_cast<svtkIdType>(this->Splits.size());
    this->RecursiveSearch(bounds, &this->Nodes[0], &this->Nodes[numPoints], &this->Splits[0],
      &this->Splits[splitSize], 0, tag, points);
  }

private:
  enum
  {
    NUM_POINTS_PER_BIN = 512
  };

  struct PointComp
  {
    PointComp(int axis)
      : Axis(axis)
    {
    }

    bool operator()(const Point& p1, const Point& p2) const
    {
      return p1.Position[this->Axis] < p2.Position[this->Axis];
    }

    int Axis;
  };

  void RecursiveSplit(Point* begin, Point* end, double* sbegin, double* send, int level)
  {
    if ((end - begin) <= NUM_POINTS_PER_BIN)
    {
      return;
    }

    int axis = level % 3;
    Point* mid = begin + (end - begin) / 2;
    std::nth_element(begin, mid, end, PointComp(axis));
    *(sbegin++) = mid->Position[axis];

    double* smid = sbegin + ((send - sbegin) / 2);
    this->RecursiveSplit(begin, mid, sbegin, smid, level + 1);
    this->RecursiveSplit(mid, end, smid, send, level + 1);
  }

  void RecursiveSearch(const double bounds[6], const Point* begin, const Point* end,
    const double* sbegin, const double* send, int level, int tag, std::vector<Point>& points) const
  {
    if (tag == 63)
    {
      points.insert(points.end(), begin, end);
      return;
    }
    if ((end - begin) <= NUM_POINTS_PER_BIN)
    {
      for (; begin != end; ++begin)
      {
        const double* pos = begin->Position;
        if (pos[0] >= bounds[0] && pos[0] <= bounds[1] && pos[1] >= bounds[2] &&
          pos[1] <= bounds[3] && pos[2] >= bounds[4] && pos[2] <= bounds[5])
        {
          points.push_back(*begin);
        }
      }
      return;
    }

    int axis = level % 3;
    const Point* mid = begin + (end - begin) / 2;
    const double split = *(sbegin++);
    const double* smid = sbegin + ((send - sbegin) / 2);
    if (split >= bounds[2 * axis])
    {
      int ltag = tag | ((split <= bounds[2 * axis + 1]) ? (1 << (2 * axis + 1)) : 0);
      this->RecursiveSearch(bounds, begin, mid, sbegin, smid, level + 1, ltag, points);
    }
    if (split <= bounds[2 * axis + 1])
    {
      int rtag = tag | ((split >= bounds[2 * axis]) ? (1 << (2 * axis)) : 0);
      this->RecursiveSearch(bounds, mid, end, smid, send, level + 1, rtag, points);
    }
  }

  std::vector<double> Splits;
  std::vector<Point> Nodes;
  double Bounds[6];
};

//----------------------------------------------------------------------------
// Iterate over each dataset in a composite dataset and execute func
template <typename Functor>
void ForEachDataSetBlock(svtkDataObject* data, const Functor& func)
{
  if (data->IsA("svtkDataSet"))
  {
    func(static_cast<svtkDataSet*>(data));
  }
  else if (data->IsA("svtkCompositeDataSet"))
  {
    svtkCompositeDataSet* composite = static_cast<svtkCompositeDataSet*>(data);

    svtkSmartPointer<svtkCompositeDataIterator> iter;
    iter.TakeReference(composite->NewIterator());
    for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
    {
      func(static_cast<svtkDataSet*>(iter->GetCurrentDataObject()));
    }
  }
}

// For each valid block add its bounds to boundsArray
struct GetBlockBounds
{
  GetBlockBounds(std::vector<double>& boundsArray)
    : BoundsArray(&boundsArray)
  {
  }

  void operator()(svtkDataSet* block) const
  {
    if (block)
    {
      double bounds[6];
      block->GetBounds(bounds);
      this->BoundsArray->insert(this->BoundsArray->end(), bounds, bounds + 6);
    }
  }

  std::vector<double>* BoundsArray;
};

struct FlattenCompositeDataset
{
  FlattenCompositeDataset(std::vector<svtkDataSet*>& blocks)
    : Blocks(&blocks)
  {
  }

  void operator()(svtkDataSet* block) const { this->Blocks->push_back(block); }

  std::vector<svtkDataSet*>* Blocks;
};

//----------------------------------------------------------------------------
void CopyDataSetStructure(svtkDataObject* input, svtkDataObject* output)
{
  if (input->IsA("svtkDataSet"))
  {
    static_cast<svtkDataSet*>(output)->CopyStructure(static_cast<svtkDataSet*>(input));
  }
  else if (input->IsA("svtkCompositeDataSet"))
  {
    svtkCompositeDataSet* compositeIn = static_cast<svtkCompositeDataSet*>(input);
    svtkCompositeDataSet* compositeOut = static_cast<svtkCompositeDataSet*>(output);
    compositeOut->CopyStructure(compositeIn);

    svtkSmartPointer<svtkCompositeDataIterator> iter;
    iter.TakeReference(compositeIn->NewIterator());
    for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
    {
      svtkDataSet* in = static_cast<svtkDataSet*>(iter->GetCurrentDataObject());
      if (in)
      {
        svtkDataSet* out = in->NewInstance();
        out->CopyStructure(in);
        compositeOut->SetDataSet(iter, out);
        out->Delete();
      }
    }
  }
}

// Find all the neighbors that this rank will need to send to and recv from.
// Based on the intersection of this rank's input bounds with remote's source
// bounds.
void FindNeighbors(diy::mpi::communicator comm, std::vector<std::vector<double> >& sourceBounds,
  std::vector<svtkDataSet*>& inputBlocks, std::vector<int>& neighbors)
{
  for (int gid = 0; gid < comm.size(); ++gid)
  {
    if (gid == comm.rank())
    {
      continue;
    }

    std::vector<double>& boundsArray = sourceBounds[gid];
    for (size_t next = 0; next < boundsArray.size(); next += 6)
    {
      double* sbounds = &boundsArray[next];
      bool intersects = false;
      for (size_t b = 0; b < inputBlocks.size(); ++b)
      {
        svtkDataSet* ds = inputBlocks[b];
        if (ds)
        {
          const double* ibounds = ds->GetBounds();
          if ((intersects = (svtkBoundingBox(sbounds).Intersects(ibounds) == 1)) == true)
          {
            break;
          }
        }
      }
      if (intersects)
      {
        neighbors.push_back(gid);
        break;
      }
    }
  }

  std::vector<std::vector<int> > allNbrs;
  diy::mpi::all_gather(comm, neighbors, allNbrs);
  for (int gid = 0; gid < comm.size(); ++gid)
  {
    if (gid == comm.rank())
    {
      continue;
    }

    std::vector<int>& nbrs = allNbrs[gid];
    if ((std::find(nbrs.begin(), nbrs.end(), comm.rank()) != nbrs.end()) &&
      (std::find(neighbors.begin(), neighbors.end(), gid) == neighbors.end()))
    {
      neighbors.push_back(gid);
    }
  }
}

//----------------------------------------------------------------------------
struct DiyBlock
{
  std::vector<std::vector<double> > SourceBlocksBounds;
  std::vector<svtkDataSet*> InputBlocks;
  std::vector<svtkDataSet*> OutputBlocks;
  Partition* PointsLookup;
};

struct ImplicitPoints
{
  int Extents[6];
  double Origin[3];
  double Spacing[3];

  int BlockStart[3];
  int BlockDim[3];
  int BlockId;
};

struct PointsList
{
  std::vector<Point> Explicit;
  std::vector<ImplicitPoints> Implicit;
};

inline void ComputeExtentsForBounds(const double origin[3], const double spacing[3],
  const int extents[6], const double bounds[6], int result[6])
{
  for (int i = 0; i < 3; ++i)
  {
    if (spacing[i] == 0.0)
    {
      result[2 * i] = result[2 * i + 1] = 0;
    }
    else
    {
      result[2 * i] = std::max(
        extents[2 * i], static_cast<int>(std::floor((bounds[2 * i] - origin[i]) / spacing[i])));
      result[2 * i + 1] = std::min(extents[2 * i + 1],
        static_cast<int>(std::ceil((bounds[2 * i + 1] - origin[i]) / spacing[i])));
    }
  }
}

inline bool ComparePointsByBlockId(const Point& p1, const Point& p2)
{
  return p1.BlockId < p2.BlockId;
}

// Send input points that overlap remote's source bounds
void FindPointsToSend(DiyBlock* block, const diy::Master::ProxyWithLink& cp)
{
  diy::Link* link = cp.link();
  for (int l = 0; l < link->size(); ++l)
  {
    diy::BlockID neighbor = link->target(l);
    PointsList points;

    svtkBoundingBox fullBounds;
    std::vector<double>& boundsArray = block->SourceBlocksBounds[neighbor.proc];
    for (size_t next = 0; next < boundsArray.size(); next += 6)
    {
      double* sbounds = &boundsArray[next];
      block->PointsLookup->FindPointsInBounds(sbounds, points.Explicit);
      fullBounds.AddBounds(sbounds);
    }
    // group the points by BlockId
    std::sort(points.Explicit.begin(), points.Explicit.end(), ComparePointsByBlockId);

    for (size_t i = 0; i < block->InputBlocks.size(); ++i)
    {
      svtkImageData* img = svtkImageData::SafeDownCast(block->InputBlocks[i]);
      if (img)
      {
        svtkBoundingBox imgBounds(img->GetBounds());
        if (imgBounds.IntersectBox(fullBounds))
        {
          int* inExtents = img->GetExtent();
          double* inOrigin = img->GetOrigin();
          double* inSpacing = img->GetSpacing();

          double sendBounds[6];
          imgBounds.GetBounds(sendBounds);
          int sendExtents[6];
          ComputeExtentsForBounds(inOrigin, inSpacing, inExtents, sendBounds, sendExtents);

          ImplicitPoints pts;
          std::copy(sendExtents, sendExtents + 6, pts.Extents);
          std::copy(inOrigin, inOrigin + 3, pts.Origin);
          std::copy(inSpacing, inSpacing + 3, pts.Spacing);
          for (int j = 0; j < 3; ++j)
          {
            pts.BlockStart[j] = inExtents[2 * j];
            pts.BlockDim[j] = inExtents[2 * j + 1] - inExtents[2 * j] + 1;
          }
          pts.BlockId = static_cast<int>(i);
          points.Implicit.push_back(pts);
        }
      }
    }

    cp.enqueue(neighbor, points);
  }
}

class EnqueueDataArray
{
public:
  EnqueueDataArray(const diy::Master::ProxyWithLink& cp, const diy::BlockID& dest)
    : Proxy(&cp)
    , Dest(dest)
    , Masks(nullptr)
    , RBegin(0)
    , REnd(0)
  {
  }

  void SetMaskArray(const char* masks) { this->Masks = masks; }

  void SetRange(svtkIdType begin, svtkIdType end)
  {
    this->RBegin = begin;
    this->REnd = end;
  }

  template <typename ArrayType>
  void operator()(ArrayType* array) const
  {
    using T = svtk::GetAPIType<ArrayType>;

    this->Proxy->enqueue(this->Dest, std::string(array->GetName()));
    this->Proxy->enqueue(this->Dest, array->GetDataType());
    this->Proxy->enqueue(this->Dest, array->GetNumberOfComponents());

    const auto range = svtk::DataArrayTupleRange(array, this->RBegin, this->REnd);
    const char* mask = this->Masks + this->RBegin;

    for (const auto tuple : range)
    {
      if (*mask++)
      {
        for (const T comp : tuple)
        {
          this->Proxy->enqueue(this->Dest, comp);
        }
      }
    }
  }

private:
  const diy::Master::ProxyWithLink* Proxy;
  diy::BlockID Dest;
  const char* Masks;
  svtkIdType RBegin, REnd;
};

// Perform resampling of local and remote input points
void PerformResampling(
  DiyBlock* block, const diy::Master::ProxyWithLink& cp, svtkCompositeDataProbeFilter* prober)
{
  diy::Link* link = cp.link();

  // local points
  for (size_t i = 0; i < block->InputBlocks.size(); ++i)
  {
    svtkDataSet* in = block->InputBlocks[i];
    if (in)
    {
      prober->SetInputData(in);
      prober->Update();
      block->OutputBlocks[i]->ShallowCopy(prober->GetOutput());
    }
  }

  // remote points
  for (int i = 0; i < link->size(); ++i)
  {
    diy::BlockID bid = link->target(i);
    if (!cp.incoming(bid.gid))
    {
      continue;
    }

    PointsList plist;
    cp.dequeue(bid.gid, plist);

    EnqueueDataArray enqueuer(cp, bid);
    if (!plist.Explicit.empty())
    {
      std::vector<Point>& points = plist.Explicit;
      svtkIdType totalPoints = static_cast<svtkIdType>(points.size());

      svtkNew<svtkPoints> pts;
      pts->SetDataTypeToDouble();
      pts->Allocate(totalPoints);
      for (svtkIdType j = 0; j < totalPoints; ++j)
      {
        pts->InsertNextPoint(points[j].Position);
      }
      svtkNew<svtkUnstructuredGrid> ds;
      ds->SetPoints(pts);

      prober->SetInputData(ds);
      prober->Update();
      svtkIdType numberOfValidPoints = prober->GetValidPoints()->GetNumberOfTuples();
      if (numberOfValidPoints == 0)
      {
        continue;
      }

      svtkDataSet* result = prober->GetOutput();
      const char* maskArrayName = prober->GetValidPointMaskArrayName();
      svtkPointData* resPD = result->GetPointData();
      const char* masks = svtkCharArray::SafeDownCast(resPD->GetArray(maskArrayName))->GetPointer(0);

      // blockwise send
      std::vector<svtkIdType> pointIds;
      svtkIdType blockBegin = 0;
      svtkIdType blockEnd = blockBegin;
      while (blockBegin < totalPoints)
      {
        int blockId = points[blockBegin].BlockId;

        pointIds.clear();
        while (blockEnd < totalPoints && points[blockEnd].BlockId == blockId)
        {
          if (masks[blockEnd])
          {
            pointIds.push_back(points[blockEnd].PointId);
          }
          ++blockEnd;
        }

        cp.enqueue(bid, blockId);
        cp.enqueue(bid, static_cast<svtkIdType>(pointIds.size())); // send valid points only
        cp.enqueue(bid, resPD->GetNumberOfArrays());
        cp.enqueue(bid, &pointIds[0], pointIds.size());

        enqueuer.SetMaskArray(masks);
        enqueuer.SetRange(blockBegin, blockEnd);
        for (svtkIdType j = 0; j < resPD->GetNumberOfArrays(); ++j)
        {
          svtkDataArray* field = resPD->GetArray(j);
          if (!svtkArrayDispatch::Dispatch::Execute(field, enqueuer))
          {
            svtkGenericWarningMacro(<< "Dispatch failed, fallback to svtkDataArray Get/Set");
            enqueuer(field);
          }
        }

        blockBegin = blockEnd;
      }
    }
    if (!plist.Implicit.empty())
    {
      for (size_t j = 0; j < plist.Implicit.size(); ++j)
      {
        ImplicitPoints& points = plist.Implicit[j];

        svtkNew<svtkImageData> ds;
        ds->SetExtent(points.Extents);
        ds->SetOrigin(points.Origin);
        ds->SetSpacing(points.Spacing);

        prober->SetInputData(ds);
        prober->Update();
        svtkIdType numberOfValidPoints = prober->GetValidPoints()->GetNumberOfTuples();
        if (numberOfValidPoints == 0)
        {
          continue;
        }

        svtkDataSet* result = prober->GetOutput();
        const char* maskArrayName = prober->GetValidPointMaskArrayName();
        svtkPointData* resPD = result->GetPointData();
        const char* masks =
          svtkCharArray::SafeDownCast(resPD->GetArray(maskArrayName))->GetPointer(0);

        cp.enqueue(bid, points.BlockId);
        cp.enqueue(bid, numberOfValidPoints); // send valid points only
        cp.enqueue(bid, resPD->GetNumberOfArrays());

        svtkIdType ptId = 0;
        for (int z = points.Extents[4]; z <= points.Extents[5]; ++z)
        {
          for (int y = points.Extents[2]; y <= points.Extents[3]; ++y)
          {
            for (int x = points.Extents[0]; x <= points.Extents[1]; ++x, ++ptId)
            {
              if (masks[ptId])
              {
                svtkIdType pointId = (x - points.BlockStart[0]) +
                  (y - points.BlockStart[1]) * points.BlockDim[0] +
                  (z - points.BlockStart[2]) * points.BlockDim[0] * points.BlockDim[1];
                cp.enqueue(bid, pointId);
              }
            }
          }
        }

        enqueuer.SetMaskArray(masks);
        enqueuer.SetRange(0, result->GetNumberOfPoints());
        for (svtkIdType k = 0; k < resPD->GetNumberOfArrays(); ++k)
        {
          svtkDataArray* field = resPD->GetArray(k);
          if (!svtkArrayDispatch::Dispatch::Execute(field, enqueuer))
          {
            svtkGenericWarningMacro(<< "Dispatch failed, fallback to svtkDataArray Get/Set");
            enqueuer(field);
          }
        }
      }
    }
  }
}

class DequeueDataArray
{
public:
  DequeueDataArray(const diy::Master::ProxyWithLink& proxy, int sourceGID)
    : Proxy(&proxy)
    , SourceGID(sourceGID)
    , PointIds(nullptr)
  {
  }

  void SetPointIds(const std::vector<svtkIdType>& pointIds) { this->PointIds = &pointIds; }

  template <typename ArrayType>
  void operator()(ArrayType* array) const
  {
    using T = svtk::GetAPIType<ArrayType>;
    auto range = svtk::DataArrayTupleRange(array);

    using CompRefT = typename decltype(range)::ComponentReferenceType;

    for (const svtkIdType ptId : *this->PointIds)
    {
      for (CompRefT compRef : range[ptId])
      {
        T val;
        this->Proxy->dequeue(this->SourceGID, val);
        compRef = val;
      }
    }
  }

private:
  const diy::Master::ProxyWithLink* Proxy;
  int SourceGID;

  const std::vector<svtkIdType>* PointIds;
};

// receive resampled points
void ReceiveResampledPoints(
  DiyBlock* block, const diy::Master::ProxyWithLink& cp, const char* maskArrayName)
{
  int numBlocks = static_cast<int>(block->InputBlocks.size());
  std::vector<std::map<std::string, int> > arrayReceiveCounts(numBlocks);

  diy::Master::IncomingQueues& in = *cp.incoming();
  for (diy::Master::IncomingQueues::iterator i = in.begin(); i != in.end(); ++i)
  {
    if (!i->second)
    {
      continue;
    }

    std::vector<svtkIdType> pointIds;

    DequeueDataArray dequeuer(cp, i->first);
    while (i->second)
    {
      int blockId;
      svtkIdType numberOfPoints;
      int numberOfArrays;

      cp.dequeue(i->first, blockId);
      cp.dequeue(i->first, numberOfPoints);
      cp.dequeue(i->first, numberOfArrays);
      svtkDataSet* ds = block->OutputBlocks[blockId];

      pointIds.resize(numberOfPoints);
      cp.dequeue(i->first, &pointIds[0], numberOfPoints);

      dequeuer.SetPointIds(pointIds);
      for (int j = 0; j < numberOfArrays; ++j)
      {
        std::string name;
        int type;
        int numComponents;
        cp.dequeue(i->first, name);
        cp.dequeue(i->first, type);
        cp.dequeue(i->first, numComponents);
        ++arrayReceiveCounts[blockId][name];

        svtkDataArray* da = ds->GetPointData()->GetArray(name.c_str());
        if (!da)
        {
          da = svtkDataArray::CreateDataArray(type);
          da->SetName(name.c_str());
          da->SetNumberOfComponents(numComponents);
          da->SetNumberOfTuples(ds->GetNumberOfPoints());
          if (name == maskArrayName)
          {
            svtkCharArray* maskArray = svtkCharArray::SafeDownCast(da);
            maskArray->FillValue(0);
          }
          ds->GetPointData()->AddArray(da);
        }

        if (!svtkArrayDispatch::Dispatch::Execute(da, dequeuer))
        {
          svtkGenericWarningMacro(<< "Dispatch failed, fallback to svtkDataArray Get/Set");
          dequeuer(da);
        }
      }
    }
  }

  // Discard arrays that were only received from some of the sources. Such arrays
  // will have invalid values for points that have valid masks from other sources.
  for (int i = 0; i < numBlocks; ++i)
  {
    std::map<std::string, int>& recvCnt = arrayReceiveCounts[i];
    int maxCount = recvCnt[maskArrayName]; // maskArray is always received
    for (std::map<std::string, int>::iterator it = recvCnt.begin(); it != recvCnt.end(); ++it)
    {
      if (it->second != maxCount)
      {
        block->OutputBlocks[i]->GetPointData()->RemoveArray(it->first.c_str());
      }
    }
  }
}

} // anonymous namespace

//---------------------------------------------------------------------------
int svtkPResampleWithDataSet::RequestData(
  svtkInformation* request, svtkInformationVector** inputVector, svtkInformationVector* outputVector)
{
  if (!this->Controller || this->Controller->GetNumberOfProcesses() == 1)
  {
    return this->Superclass::RequestData(request, inputVector, outputVector);
  }

  svtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  svtkInformation* sourceInfo = inputVector[1]->GetInformationObject(0);
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  diy::mpi::communicator comm = svtkDIYUtilities::GetCommunicator(this->Controller);

  DiyBlock block; // one diy-block per rank
  int mygid = comm.rank();

  // compute and communicate the bounds of all the source blocks in all the ranks
  svtkDataObject* source = sourceInfo->Get(svtkDataObject::DATA_OBJECT());
  std::vector<double> srcBounds;
  ForEachDataSetBlock(source, GetBlockBounds(srcBounds));
  diy::mpi::all_gather(comm, srcBounds, block.SourceBlocksBounds);

  // copy the input structure to output
  svtkDataObject* input = inInfo->Get(svtkDataObject::DATA_OBJECT());
  svtkDataObject* output = outInfo->Get(svtkDataObject::DATA_OBJECT());
  CopyDataSetStructure(input, output);
  // flatten the composite datasets to make them easier to handle
  ForEachDataSetBlock(input, FlattenCompositeDataset(block.InputBlocks));
  ForEachDataSetBlock(output, FlattenCompositeDataset(block.OutputBlocks));

  // partition the input points, using the user specified partition algorithm,
  // to make it easier to find the set of points inside a bounding-box
  if (this->UseBalancedPartitionForPointsLookup)
  {
    block.PointsLookup = new BalancedPartition;
  }
  else
  {
    block.PointsLookup = new RegularPartition;
  }
  // We don't want ImageData points in the lookup structure
  {
    std::vector<svtkDataSet*> dsblocks = block.InputBlocks;
    for (size_t i = 0; i < dsblocks.size(); ++i)
    {
      if (svtkImageData::SafeDownCast(dsblocks[i]))
      {
        dsblocks[i] = nullptr;
      }
    }
    block.PointsLookup->CreatePartition(dsblocks);
  }

  // find the neighbors of this rank for communication purposes
  std::vector<int> neighbors;
  FindNeighbors(comm, block.SourceBlocksBounds, block.InputBlocks, neighbors);

  diy::Link* link = new diy::Link;
  for (size_t i = 0; i < neighbors.size(); ++i)
  {
    diy::BlockID bid;
    bid.gid = bid.proc = neighbors[i];
    link->add_neighbor(bid);
  }

  diy::Master master(comm, 1);
  master.add(mygid, &block, link);

  this->Prober->SetSourceData(source);
  // find and send local points that overlap remote source blocks
  master.foreach (&FindPointsToSend);
  // the lookup structures are no longer required
  delete block.PointsLookup;
  block.PointsLookup = nullptr;
  master.exchange();
  // perform resampling on local and remote points
  master.foreach ([&](DiyBlock* block_, const diy::Master::ProxyWithLink& cp) {
    PerformResampling(block_, cp, this->Prober.GetPointer());
  });
  master.exchange();
  // receive resampled points and set the values in output
  master.foreach ([&](DiyBlock* block_, const diy::Master::ProxyWithLink& cp) {
    ReceiveResampledPoints(block_, cp, this->Prober->GetValidPointMaskArrayName());
  });

  if (this->MarkBlankPointsAndCells)
  {
    // mark the blank points and cells of output
    for (size_t i = 0; i < block.OutputBlocks.size(); ++i)
    {
      svtkDataSet* ds = block.OutputBlocks[i];
      if (ds)
      {
        this->SetBlankPointsAndCells(ds);
      }
    }
  }

  return 1;
}

//----------------------------------------------------------------------------
namespace diy
{

template <>
struct Serialization<PointsList>
{
  static void save(BinaryBuffer& bb, const PointsList& plist)
  {
    diy::save(bb, plist.Implicit);
    diy::save(bb, plist.Explicit);
  }

  static void load(BinaryBuffer& bb, PointsList& plist)
  {
    diy::load(bb, plist.Implicit);
    diy::load(bb, plist.Explicit);
  }
};

} // namespace diy