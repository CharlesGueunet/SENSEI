/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPKdTree.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*----------------------------------------------------------------------------
 Copyright (c) Sandia Corporation
 See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.
----------------------------------------------------------------------------*/

#include "svtkPKdTree.h"
#include "svtkCellCenters.h"
#include "svtkCellData.h"
#include "svtkCommand.h"
#include "svtkDataSet.h"
#include "svtkIdList.h"
#include "svtkIntArray.h"
#include "svtkKdNode.h"
#include "svtkMath.h"
#include "svtkMultiProcessController.h"
#include "svtkObjectFactory.h"
#include "svtkPointData.h"
#include "svtkPoints.h"
#include "svtkSocketController.h"
#include "svtkSubGroup.h"
#include "svtkTimerLog.h"
#include "svtkUnstructuredGrid.h"

#include <algorithm>
#include <cassert>
#include <queue>

namespace
{
class TimeLog // Similar to svtkTimerLogScope, but can be disabled at runtime.
{
  const std::string Event;
  int Timing;

public:
  TimeLog(const char* event, int timing)
    : Event(event ? event : "")
    , Timing(timing)
  {
    if (this->Timing)
    {
      svtkTimerLog::MarkStartEvent(this->Event.c_str());
    }
  }

  ~TimeLog()
  {
    if (this->Timing)
    {
      svtkTimerLog::MarkEndEvent(this->Event.c_str());
    }
  }

  static void StartEvent(const char* event, int timing)
  {
    if (timing)
    {
      svtkTimerLog::MarkStartEvent(event);
    }
  }

  static void EndEvent(const char* event, int timing)
  {
    if (timing)
    {
      svtkTimerLog::MarkEndEvent(event);
    }
  }

private:
  // Explicit disable copy/assignment to prevent MSVC from complaining (C4512)
  TimeLog(const TimeLog&) = delete;
  TimeLog& operator=(const TimeLog&) = delete;
};
}

#define SCOPETIMER(msg)                                                                            \
  TimeLog _timer("PkdTree: " msg, this->Timing);                                                   \
  (void)_timer
#define TIMER(msg) TimeLog::StartEvent("PkdTree: " msg, this->Timing)
#define TIMERDONE(msg) TimeLog::EndEvent("PkdTree: " msg, this->Timing)

svtkStandardNewMacro(svtkPKdTree);

const int svtkPKdTree::NoRegionAssignment = 0;   // default
const int svtkPKdTree::ContiguousAssignment = 1; // default if RegionAssignmentOn
const int svtkPKdTree::UserDefinedAssignment = 2;
const int svtkPKdTree::RoundRobinAssignment = 3;

#define FreeList(list)                                                                             \
  if (list)                                                                                        \
  {                                                                                                \
    delete[] list;                                                                                 \
    list = nullptr;                                                                                \
  }
#define FreeObject(item)                                                                           \
  if (item)                                                                                        \
  {                                                                                                \
    item->Delete();                                                                                \
    item = nullptr;                                                                                \
  }

#define SVTKERROR(s)                                                                                \
  {                                                                                                \
    svtkErrorMacro(<< "(process " << this->MyId << ") " << s);                                      \
  }
#define SVTKWARNING(s)                                                                              \
  {                                                                                                \
    svtkWarningMacro(<< "(process " << this->MyId << ") " << s);                                    \
  }

svtkPKdTree::svtkPKdTree()
{
  this->RegionAssignment = ContiguousAssignment;

  this->Controller = nullptr;
  this->SubGroup = nullptr;

  this->NumProcesses = 1;
  this->MyId = 0;

  this->InitializeRegionAssignmentLists();
  this->InitializeProcessDataLists();
  this->InitializeFieldArrayMinMax();
  this->InitializeGlobalIndexLists();

  this->TotalNumCells = 0;

  this->PtArray = nullptr;
  this->PtArray2 = nullptr;
  this->CurrentPtArray = nullptr;
  this->NextPtArray = nullptr;
}
svtkPKdTree::~svtkPKdTree()
{
  this->SetController(nullptr);
  this->FreeSelectBuffer();
  this->FreeDoubleBuffer();

  this->FreeGlobalIndexLists();
  this->FreeRegionAssignmentLists();
  this->FreeProcessDataLists();
  this->FreeFieldArrayMinMax();
}
void svtkPKdTree::SetController(svtkMultiProcessController* c)
{
  if (this->Controller == c)
  {
    return;
  }

  if ((c == nullptr) || (c->GetNumberOfProcesses() == 0))
  {
    this->NumProcesses = 1;
    this->MyId = 0;
  }

  this->Modified();

  if (this->Controller != nullptr)
  {
    this->Controller->UnRegister(this);
    this->Controller = nullptr;
  }

  if (c == nullptr)
  {
    return;
  }

  svtkSocketController* sc = svtkSocketController::SafeDownCast(c);

  if (sc)
  {
    svtkErrorMacro(<< "svtkPKdTree communication will fail with a socket controller");

    return;
  }

  this->NumProcesses = c->GetNumberOfProcesses();

  this->Controller = c;
  this->MyId = c->GetLocalProcessId();
  c->Register(this);
}
//--------------------------------------------------------------------
// Parallel k-d tree build, Floyd and Rivest (1975) select algorithm
// for median finding.
//--------------------------------------------------------------------

int svtkPKdTree::AllCheckForFailure(int rc, const char* where, const char* how)
{
  int vote;
  char errmsg[256];

  if (this->NumProcesses > 1)
  {
    this->SubGroup->ReduceSum(&rc, &vote, 1, 0);
    this->SubGroup->Broadcast(&vote, 1, 0);
  }
  else
  {
    vote = rc;
  }

  if (vote)
  {
    if (rc)
    {
      snprintf(errmsg, sizeof(errmsg), "%s on my node (%s)", how, where);
    }
    else
    {
      snprintf(errmsg, sizeof(errmsg), "%s on a remote node (%s)", how, where);
    }
    SVTKWARNING(errmsg);

    return 1;
  }
  return 0;
}

void svtkPKdTree::AllCheckParameters()
{
  SCOPETIMER("AllCheckParameters");

  int param[10];
  int param0[10];

  // All the parameters that determine how k-d tree is built and
  //  what tables get created afterward - there's no point in
  //  trying to build unless these match on all processes.

  param[0] = this->ValidDirections;
  param[1] = this->GetMinCells();
  param[2] = this->GetNumberOfRegionsOrLess();
  param[3] = this->GetNumberOfRegionsOrMore();
  param[4] = this->RegionAssignment;
  param[5] = 0;
  param[6] = 0;
  param[7] = 0;
  param[8] = 0;
  param[9] = 0;

  if (this->MyId == 0)
  {
    this->SubGroup->Broadcast(param, 10, 0);
    return;
  }

  this->SubGroup->Broadcast(param0, 10, 0);

  int diff = 0;

  for (int i = 0; i < 10; i++)
  {
    if (param0[i] != param[i])
    {
      diff = 1;
      break;
    }
  }
  if (diff)
  {
    SVTKWARNING("Changing my runtime parameters to match process 0");

    this->ValidDirections = param0[0];
    this->SetMinCells(param0[1]);
    this->SetNumberOfRegionsOrLess(param0[2]);
    this->SetNumberOfRegionsOrMore(param0[3]);
    this->RegionAssignment = param0[4];
  }
}

#define BoundsToMinMax(bounds, min, max)                                                           \
  {                                                                                                \
    min[0] = bounds[0];                                                                            \
    min[1] = bounds[2];                                                                            \
    min[2] = bounds[4];                                                                            \
    max[0] = bounds[1];                                                                            \
    max[1] = bounds[3];                                                                            \
    max[2] = bounds[5];                                                                            \
  }
#define MinMaxToBounds(bounds, min, max)                                                           \
  {                                                                                                \
    bounds[0] = min[0];                                                                            \
    bounds[2] = min[1];                                                                            \
    bounds[4] = min[2];                                                                            \
    bounds[1] = max[0];                                                                            \
    bounds[3] = max[1];                                                                            \
    bounds[5] = max[2];                                                                            \
  }
#define BoundsToMinMaxUpdate(bounds, min, max)                                                     \
  {                                                                                                \
    min[0] = (bounds[0] < min[0] ? bounds[0] : min[0]);                                            \
    min[1] = (bounds[2] < min[1] ? bounds[2] : min[1]);                                            \
    min[2] = (bounds[4] < min[2] ? bounds[4] : min[2]);                                            \
    max[0] = (bounds[1] > max[0] ? bounds[1] : max[0]);                                            \
    max[1] = (bounds[3] > max[1] ? bounds[3] : max[1]);                                            \
    max[2] = (bounds[5] > max[2] ? bounds[5] : max[2]);                                            \
  }

bool svtkPKdTree::VolumeBounds(double* volBounds)
{
  int i;

  // Get the spatial bounds of the whole volume
  double localMin[3], localMax[3], globalMin[3], globalMax[3];

  int number_of_datasets = this->GetNumberOfDataSets();
  if (number_of_datasets == 0)
  {
    // Cannot determine volume bounds.
    return false;
  }

  for (i = 0; i < number_of_datasets; i++)
  {
    this->GetDataSet(i)->GetBounds(volBounds);

    if (i == 0)
    {
      BoundsToMinMax(volBounds, localMin, localMax);
    }
    else
    {
      BoundsToMinMaxUpdate(volBounds, localMin, localMax);
    }
  }

  // trick to reduce the number of global communications for getting both
  // min and max
  double localReduce[6], globalReduce[6];
  for (i = 0; i < 3; i++)
  {
    localReduce[i] = localMin[i];
    localReduce[i + 3] = -localMax[i];
  }
  this->SubGroup->ReduceMin(localReduce, globalReduce, 6, 0);
  this->SubGroup->Broadcast(globalReduce, 6, 0);

  for (i = 0; i < 3; i++)
  {
    globalMin[i] = globalReduce[i];
    globalMax[i] = -globalReduce[i + 3];
  }

  MinMaxToBounds(volBounds, globalMin, globalMax);

  // push out a little if flat

  double diff[3], aLittle = 0.0;

  for (i = 0; i < 3; i++)
  {
    diff[i] = volBounds[2 * i + 1] - volBounds[2 * i];
    aLittle = (diff[i] > aLittle) ? diff[i] : aLittle;
  }
  if ((aLittle /= 100.0) <= 0.0)
  {
    SVTKERROR("VolumeBounds - degenerate volume");
    return false;
  }

  this->FudgeFactor = aLittle * 10e-4;

  for (i = 0; i < 3; i++)
  {
    if (diff[i] <= 0)
    {
      volBounds[2 * i] -= aLittle;
      volBounds[2 * i + 1] += aLittle;
    }
    else
    {
      volBounds[2 * i] -= this->GetFudgeFactor();
      volBounds[2 * i + 1] += this->GetFudgeFactor();
    }
  }
  return true;
}

// BuildLocator must be called by all processes in the parallel application

void svtkPKdTree::BuildLocator()
{
  SCOPETIMER("BuildLocator");

  int fail = 0;
  int rebuildLocator = 0;

  if ((this->Top == nullptr) || (this->BuildTime < this->GetMTime()) || this->NewGeometry())
  {
    // We don't have a k-d tree, or parameters that affect the
    // build of the tree have changed, or input geometry has changed.

    rebuildLocator = 1;
  }

  if (this->NumProcesses == 1)
  {
    if (rebuildLocator)
    {
      this->SingleProcessBuildLocator();
    }
    return;
  }
  this->UpdateProgress(0);

  TIMER("Determine if we need to rebuild");

  this->SubGroup = svtkSubGroup::New();
  this->SubGroup->Initialize(
    0, this->NumProcesses - 1, this->MyId, 0x00001000, this->Controller->GetCommunicator());

  int vote;
  this->SubGroup->ReduceSum(&rebuildLocator, &vote, 1, 0);
  this->SubGroup->Broadcast(&vote, 1, 0);

  rebuildLocator = (vote > 0);

  TIMERDONE("Determine if we need to rebuild");

  if (rebuildLocator)
  {
    TIMER("Build k-d tree");
    this->InvokeEvent(svtkCommand::StartEvent);

    this->FreeSearchStructure();
    this->ReleaseTables();

    this->AllCheckParameters(); // global operation to ensure same parameters

    double volBounds[6];
    if (this->VolumeBounds(volBounds) == false) // global operation to get bounds
    {
      goto doneError;
    }
    this->UpdateProgress(0.1);

    if (this->UserDefinedCuts)
    {
      fail = this->ProcessUserDefinedCuts(volBounds);
    }
    else
    {
      fail = this->MultiProcessBuildLocator(volBounds);
    }

    if (fail)
    {
      TIMERDONE("Build k-d tree");
      goto doneError;
    }

    this->SetActualLevel();
    this->BuildRegionList();

    TIMERDONE("Build k-d tree");

    this->InvokeEvent(svtkCommand::EndEvent);
  }

  // Even if locator is not rebuilt, we should update
  // region assignments since they may have changed.

  this->UpdateRegionAssignment();

  goto done;

doneError:

  this->FreeRegionAssignmentLists();
  this->FreeSearchStructure();

done:

  FreeObject(this->SubGroup);

  this->SetCalculator(this->Top);

  this->UpdateBuildTime();
  this->UpdateProgress(1.0);
}
int svtkPKdTree::MultiProcessBuildLocator(double* volBounds)
{
  SCOPETIMER("MultiProcessBuildLocator");

  int retVal = 0;

  svtkDebugMacro(<< "Creating Kdtree in parallel");

  if (this->GetTiming())
  {
    if (this->TimerLog == nullptr)
      this->TimerLog = svtkTimerLog::New();
  }

  // Locally, create a single list of the coordinates of the centers of the
  //   cells of my data sets

  this->PtArray = nullptr;

  this->ProgressOffset = 0.1;
  this->ProgressScale = 0.5;

  this->PtArray = this->ComputeCellCenters();
  svtkIdType totalPts = this->GetNumberOfCells(); // total on local node
  this->CurrentPtArray = this->PtArray;

  //   int fail = (this->PtArray == nullptr);
  int fail = ((this->PtArray == nullptr) && (totalPts > 0));

  if (this->AllCheckForFailure(fail, "MultiProcessBuildLocator", "memory allocation"))
  {
    goto doneError6;
  }

  // Get total number of cells across all processes, assign global indices
  //   for select operation

  fail = this->BuildGlobalIndexLists(totalPts);
  this->UpdateProgress(0.7);

  if (fail)
  {
    goto doneError6;
  }

  // In parallel, build the k-d tree structure, partitioning all
  //   the points into spatial regions.  Sub-groups of processors
  //   will form svtkSubGroups to divide sub-regions of space.

  FreeObject(this->SubGroup);

  fail = this->BreadthFirstDivide(volBounds);
  this->UpdateProgress(0.9);

  this->SubGroup = svtkSubGroup::New();
  this->SubGroup->Initialize(
    0, this->NumProcesses - 1, this->MyId, 0x00002000, this->Controller->GetCommunicator());

  if (this->AllCheckForFailure(fail, "BreadthFirstDivide", "memory allocation"))
  {
    goto doneError6;
  }

  FreeObject(this->SubGroup);

  // I only have a partial tree at this point, the regions in which
  //   I participated.  Now collect the entire tree.

  this->SubGroup = svtkSubGroup::New();
  this->SubGroup->Initialize(
    0, this->NumProcesses - 1, this->MyId, 0x00003000, this->Controller->GetCommunicator());

  fail = this->CompleteTree();

  if (fail)
  {
    goto doneError6;
  }

  goto done6;

doneError6:

  this->FreeSearchStructure();
  retVal = 1;

done6:
  // no longer valid, we overwrote them during k-d tree parallel build
  delete[] this->PtArray;
  this->CurrentPtArray = this->PtArray = nullptr;

  FreeObject(this->SubGroup);

  this->FreeGlobalIndexLists();

  return retVal;
}

void svtkPKdTree::SingleProcessBuildLocator()
{
  SCOPETIMER("SingleProcessBuildLocator");

  svtkKdTree::BuildLocator();

  this->TotalNumCells = this->GetNumberOfCells();

  if (this->RegionAssignment != svtkPKdTree::NoRegionAssignment)
  {
    this->UpdateRegionAssignment();
  }
}

typedef struct _svtkNodeInfo
{
  svtkKdNode* kd;
  int L;
  int level;
  int tag;
} * svtkNodeInfo;

#define ENQUEUE(a, b, c, d)                                                                        \
  {                                                                                                \
    svtkNodeInfo rec = new struct _svtkNodeInfo;                                                     \
    rec->kd = a;                                                                                   \
    rec->L = b;                                                                                    \
    rec->level = c;                                                                                \
    rec->tag = d;                                                                                  \
    Queue.push(rec);                                                                               \
  }

int svtkPKdTree::BreadthFirstDivide(double* volBounds)
{
  SCOPETIMER("BreadthFirstDivide");

  int returnVal = 0;

  std::queue<svtkNodeInfo> Queue;

  if (this->AllocateDoubleBuffer())
  {
    SVTKERROR("memory allocation for double buffering");
    return 1;
  }

  this->AllocateSelectBuffer();

  svtkKdNode* kd = this->Top = svtkKdNode::New();

  kd->SetBounds(volBounds[0], volBounds[1], volBounds[2], volBounds[3], volBounds[4], volBounds[5]);

  kd->SetNumberOfPoints(this->TotalNumCells);

  kd->SetDataBounds(
    volBounds[0], volBounds[1], volBounds[2], volBounds[3], volBounds[4], volBounds[5]);

  int midpt = this->DivideRegion(kd, 0, 0, 0x00000001);

  if (midpt >= 0)
  {
    ENQUEUE(kd->GetLeft(), 0, 1, 0x00000002);
    ENQUEUE(kd->GetRight(), midpt, 1, 0x00000003);
  }
  else if (midpt < -1)
  {
    this->FreeSelectBuffer();
    this->FreeDoubleBuffer();

    return 1;
  }

  while (!Queue.empty())
  {
    svtkNodeInfo info = Queue.front();
    Queue.pop();

    kd = info->kd;
    int L = info->L;
    int level = info->level;
    int tag = info->tag;

    midpt = this->DivideRegion(kd, L, level, tag);

    if (midpt >= 0)
    {
      ENQUEUE(kd->GetLeft(), L, level + 1, tag << 1);

      ENQUEUE(kd->GetRight(), midpt, level + 1, (tag << 1) | 1);
    }
    else if (midpt < -1)
    {
      returnVal = 1; // have to keep going, or remote ops may hang
    }
    delete info;
  }

  this->FreeSelectBuffer();

  if (this->CurrentPtArray == this->PtArray2)
  {
    memcpy(this->PtArray, this->PtArray2, this->PtArraySize * sizeof(float));
  }

  this->FreeDoubleBuffer();

  return returnVal;
}
int svtkPKdTree::DivideRegion(svtkKdNode* kd, int L, int level, int tag)
{
  if (!this->DivideTest(kd->GetNumberOfPoints(), level))
    return -1;

  int numpoints = kd->GetNumberOfPoints();
  int R = L + numpoints - 1;

  if (numpoints < 2)
  {
    // Special case: not enough points to go around.
    int p = this->WhoHas(L);
    if (this->MyId != p)
      return -1;

    int maxdim = this->SelectCutDirection(kd);
    kd->SetDim(maxdim);

    svtkKdNode* left = svtkKdNode::New();
    svtkKdNode* right = svtkKdNode::New();
    kd->AddChildNodes(left, right);

    double bounds[6];
    kd->GetBounds(bounds);

    float* val = this->GetLocalVal(L);

    double coord;
    if (numpoints > 0)
    {
      coord = val[maxdim];
    }
    else
    {
      coord = (bounds[maxdim * 2] + bounds[maxdim * 2 + 1]) * 0.5;
    }

    left->SetBounds(bounds[0], ((maxdim == XDIM) ? coord : bounds[1]), bounds[2],
      ((maxdim == YDIM) ? coord : bounds[3]), bounds[4], ((maxdim == ZDIM) ? coord : bounds[5]));

    left->SetNumberOfPoints(numpoints);

    right->SetBounds(((maxdim == XDIM) ? coord : bounds[0]), bounds[1],
      ((maxdim == YDIM) ? coord : bounds[2]), bounds[3], ((maxdim == ZDIM) ? coord : bounds[4]),
      bounds[5]);

    right->SetNumberOfPoints(0);

    // Set the data bounds tightly around L.  This will inevitably mean some
    // regions that are empty will have their data bounds outside of them.
    // Hopefully, that will not screw up anything down the road.
    left->SetDataBounds(val[0], val[0], val[1], val[1], val[2], val[2]);
    right->SetDataBounds(val[0], val[0], val[1], val[1], val[2], val[2]);

    // Return L as the midpoint to guarantee that both left and right trees
    // are "owned" by the same process as the parent.  This is important
    // because only one process has not culled this node in the tree.
    return L;
  }

  int p1 = this->WhoHas(L);
  int p2 = this->WhoHas(R);

  if ((this->MyId < p1) || (this->MyId > p2))
  {
    return -1;
  }

  this->SubGroup = svtkSubGroup::New();
  this->SubGroup->Initialize(p1, p2, this->MyId, tag, this->Controller->GetCommunicator());

  int maxdim = this->SelectCutDirection(kd);

  kd->SetDim(maxdim);

  int midpt = this->Select(maxdim, L, R);

  if (midpt < L + 1)
  {
    // Couldn't divide.  Try a different direction.
    int newdim = svtkKdTree::XDIM - 1;
    svtkDebugMacro(<< "Could not divide along maxdim"
                  << " maxdim " << maxdim << " L " << L << " R " << R << " midpt " << midpt);
    while (midpt < L + 1)
    {
      do
      {
        newdim++;
        if (newdim > svtkKdTree::ZDIM)
        {
          // Exhausted all possible divisions.  All points must be at same
          // location.  Just split in the middle and hope for the best.
          svtkDebugMacro(<< "Must have coincident points.");
          newdim = maxdim;
          kd->SetDim(maxdim);
          // Add one to make sure there is always something to the left.
          midpt = (L + R) / 2 + 1;
          goto FindMidptBreakout;
        }
      } while ((newdim == maxdim) || ((this->ValidDirections & (1 << newdim)) == 0));
      kd->SetDim(newdim);
      midpt = this->Select(newdim, L, R);
      svtkDebugMacro(<< " newdim " << newdim << " L " << L << " R " << R << " midpt " << midpt);
    }
  FindMidptBreakout:
    // Pretend the dimension we used was the minimum.
    maxdim = newdim;
  }

  float newDataBounds[12];
  this->GetDataBounds(L, midpt, R, newDataBounds);
  svtkKdNode* left = svtkKdNode::New();
  svtkKdNode* right = svtkKdNode::New();

  if (this->AllCheckForFailure(
        (left == nullptr) || (right == nullptr), "Divide Region", "memory allocation"))
  {
    left->Delete();
    right->Delete();
    FreeObject(this->SubGroup);
    return -3;
  }

  double coord = (newDataBounds[maxdim * 2 + 1] +   // max on left side
                   newDataBounds[6 + maxdim * 2]) * // min on right side
    0.5;

  kd->AddChildNodes(left, right);

  double bounds[6];
  kd->GetBounds(bounds);

  left->SetBounds(bounds[0], ((maxdim == XDIM) ? coord : bounds[1]), bounds[2],
    ((maxdim == YDIM) ? coord : bounds[3]), bounds[4], ((maxdim == ZDIM) ? coord : bounds[5]));

  left->SetNumberOfPoints(midpt - L);

  right->SetBounds(((maxdim == XDIM) ? coord : bounds[0]), bounds[1],
    ((maxdim == YDIM) ? coord : bounds[2]), bounds[3], ((maxdim == ZDIM) ? coord : bounds[4]),
    bounds[5]);

  right->SetNumberOfPoints(R - midpt + 1);

  left->SetDataBounds(newDataBounds[0], newDataBounds[1], newDataBounds[2], newDataBounds[3],
    newDataBounds[4], newDataBounds[5]);

  right->SetDataBounds(newDataBounds[6], newDataBounds[7], newDataBounds[8], newDataBounds[9],
    newDataBounds[10], newDataBounds[11]);

  FreeObject(this->SubGroup);

  return midpt;
}

void svtkPKdTree::ExchangeVals(int pos1, int pos2)
{
  svtkCommunicator* comm = this->Controller->GetCommunicator();

  float* myval;
  float otherval[3];

  int player1 = this->WhoHas(pos1);
  int player2 = this->WhoHas(pos2);

  if ((player1 == this->MyId) && (player2 == this->MyId))
  {
    this->ExchangeLocalVals(pos1, pos2);
  }

  else if (player1 == this->MyId)
  {
    myval = this->GetLocalVal(pos1);

    comm->Send(myval, 3, player2, this->SubGroup->tag);

    comm->Receive(otherval, 3, player2, this->SubGroup->tag);

    this->SetLocalVal(pos1, otherval);
  }
  else if (player2 == this->MyId)
  {
    myval = this->GetLocalVal(pos2);

    comm->Receive(otherval, 3, player1, this->SubGroup->tag);

    comm->Send(myval, 3, player1, this->SubGroup->tag);

    this->SetLocalVal(pos2, otherval);
  }
}

// Given an array X with element indices ranging from L to R, and
// a K such that L <= K <= R, rearrange the elements such that
// X[K] contains the ith sorted element,  where i = K - L + 1, and
// all the elements X[j], j < k satisfy X[j] <= X[K], and all the
// elements X[j], j > k satisfy X[j] >= X[K].

#define sign(x) (((x) < 0) ? (-1) : (1))

void svtkPKdTree::_select(int L, int R, int K, int dim)
{
  int N, I, J, S, SD, LL, RR;
  float Z;

  while (R > L)
  {
    if (R - L > 600)
    {
      // "Recurse on a sample of size S to get an estimate for the
      // (K-L+1)-th smallest element into X[K], biased slightly so
      // that the (K-L+1)-th element is expected to lie in the
      // smaller set after partitioning"

      N = R - L + 1;
      I = K - L + 1;
      Z = static_cast<float>(log(float(N)));
      S = static_cast<int>(.5 * exp(2 * Z / 3));
      SD = static_cast<int>(.5 * sqrt(Z * S * ((float)(N - S) / N)) * sign(I - N / 2));
      LL = svtkMath::Max(L, K - static_cast<int>((I * ((float)S / N))) + SD);
      RR = svtkMath::Min(R, K + static_cast<int>((N - I) * ((float)S / N)) + SD);
      this->_select(LL, RR, K, dim);
    }

    int p1 = this->WhoHas(L);
    int p2 = this->WhoHas(R);

    // "now adjust L,R so they surround the subset containing
    // the (K-L+1)-th smallest element"

    // Due to very severe worst case behavior when the
    // value at K (call it "T") is repeated many times in the array, we
    // rearrange the array into three intervals: the leftmost being values
    // less than T, the center being values equal to T, and the rightmost
    // being values greater than T.  Two integers are returned.  This first
    // is the global index of the start of the second interval.  The second
    // is the global index of the start of the third interval.  (If there
    // are no values greater than "T", the second integer will be R+1.)
    //
    // The original Floyd&Rivest arranged the array into two intervals,
    // one less than "T", one greater than (or equal to) "T".

    int* idx = this->PartitionSubArray(L, R, K, dim, p1, p2);

    I = idx[0];
    J = idx[1];

    if (K >= J)
    {
      L = J;
    }
    else if (K >= I)
    {
      L = R; // partitioning is done, K is in the interval of T's
    }
    else
    {
      R = I - 1;
    }
  }
}
int svtkPKdTree::Select(int dim, int L, int R)
{
  int K = ((R + L) / 2) + 1;

  this->_select(L, R, K, dim);

  if (K == L)
    return K;

  // The global array is now re-ordered, partitioned around X[K].
  // (In particular, for all i, i<K, X[i] <= X[K] and for all i,
  // i > K, X[i] >= X[K].)
  // However the value at X[K] may occur more than once, and by
  // construction of the reordered array, there is a J <= K such that
  // for all i < J, X[i] < X[K] and for all J <= i < K X[i] = X[K].
  //
  // We want to roll K back to this value J, so that all points are
  // unambiguously assigned to one region or the other.

  int hasK = this->WhoHas(K);
  int hasKrank = this->SubGroup->getLocalRank(hasK);

  int hasKleft = this->WhoHas(K - 1);
  int hasKleftrank = this->SubGroup->getLocalRank(hasKleft);

  float Kval;
  float Kleftval;
  float* pt;

  if (hasK == this->MyId)
  {
    pt = this->GetLocalVal(K) + dim;
    Kval = *pt;
  }

  this->SubGroup->Broadcast(&Kval, 1, hasKrank);

  if (hasKleft == this->MyId)
  {
    pt = this->GetLocalVal(K - 1) + dim;
    Kleftval = *pt;
  }

  this->SubGroup->Broadcast(&Kleftval, 1, hasKleftrank);

  if (Kleftval != Kval)
    return K;

  int firstKval = this->TotalNumCells; // greater than any valid index

  if ((this->MyId <= hasKleft) && (this->NumCells[this->MyId] > 0))
  {
    int start = this->EndVal[this->MyId];
    if (start > K - 1)
      start = K - 1;

    pt = this->GetLocalVal(start) + dim;

    if (*pt == Kval)
    {
      firstKval = start;

      int finish = this->StartVal[this->MyId];

      for (int idx = start - 1; idx >= finish; idx--)
      {
        pt -= 3;
        if (*pt < Kval)
          break;

        firstKval--;
      }
    }
  }

  int newK;

  this->SubGroup->ReduceMin(&firstKval, &newK, 1, hasKrank);
  this->SubGroup->Broadcast(&newK, 1, hasKrank);

  return newK;
}

int svtkPKdTree::_whoHas(int L, int R, int pos)
{
  if (L == R)
  {
    return L;
  }

  int M = (L + R) >> 1;

  if (pos < this->StartVal[M])
  {
    return _whoHas(L, M - 1, pos);
  }
  else if (pos < this->StartVal[M + 1])
  {
    return M;
  }
  else
  {
    return _whoHas(M + 1, R, pos);
  }
}
int svtkPKdTree::WhoHas(int pos)
{
  if ((pos < 0) || (pos >= this->TotalNumCells))
  {
    return -1;
  }
  return _whoHas(0, this->NumProcesses - 1, pos);
}
float* svtkPKdTree::GetLocalVal(int pos)
{
  if ((pos < this->StartVal[this->MyId]) || (pos > this->EndVal[this->MyId]))
  {
    return nullptr;
  }
  int localPos = pos - this->StartVal[this->MyId];

  return this->CurrentPtArray + (3 * localPos);
}
float* svtkPKdTree::GetLocalValNext(int pos)
{
  if ((pos < this->StartVal[this->MyId]) || (pos > this->EndVal[this->MyId]))
  {
    return nullptr;
  }
  int localPos = pos - this->StartVal[this->MyId];

  return this->NextPtArray + (3 * localPos);
}
void svtkPKdTree::SetLocalVal(int pos, float* val)
{
  if ((pos < this->StartVal[this->MyId]) || (pos > this->EndVal[this->MyId]))
  {
    SVTKERROR("SetLocalVal - bad index");
    return;
  }

  int localOffset = (pos - this->StartVal[this->MyId]) * 3;

  this->CurrentPtArray[localOffset] = val[0];
  this->CurrentPtArray[localOffset + 1] = val[1];
  this->CurrentPtArray[localOffset + 2] = val[2];
}
void svtkPKdTree::ExchangeLocalVals(int pos1, int pos2)
{
  float temp[3];

  float* pt1 = this->GetLocalVal(pos1);
  float* pt2 = this->GetLocalVal(pos2);

  if (!pt1 || !pt2)
  {
    SVTKERROR("ExchangeLocalVal - bad index");
    return;
  }

  temp[0] = pt1[0];
  temp[1] = pt1[1];
  temp[2] = pt1[2];

  pt1[0] = pt2[0];
  pt1[1] = pt2[1];
  pt1[2] = pt2[2];

  pt2[0] = temp[0];
  pt2[1] = temp[1];
  pt2[2] = temp[2];
}

void svtkPKdTree::DoTransfer(int from, int to, int fromIndex, int toIndex, int count)
{
  float *fromPt, *toPt;

  svtkCommunicator* comm = this->Controller->GetCommunicator();

  int nitems = count * 3;

  int me = this->MyId;

  int tag = this->SubGroup->tag;

  if ((from == me) && (to == me))
  {
    fromPt = this->GetLocalVal(fromIndex);
    toPt = this->GetLocalValNext(toIndex);

    memcpy(toPt, fromPt, nitems * sizeof(float));
  }
  else if (from == me)
  {
    fromPt = this->GetLocalVal(fromIndex);

    comm->Send(fromPt, nitems, to, tag);
  }
  else if (to == me)
  {
    toPt = this->GetLocalValNext(toIndex);

    comm->Receive(toPt, nitems, from, tag);
  }
}

// Partition global array into three intervals, the first all values < T,
// the second all values = T, the third all values > T.  Return two
// global indices: The index to the beginning of the second interval, and
// the index to the beginning of the third interval.  "T" is the value
// at array index K.
//
// If there is no third interval, the second index returned will be R+1.

int* svtkPKdTree::PartitionSubArray(int L, int R, int K, int dim, int p1, int p2)
{
  int rootrank = this->SubGroup->getLocalRank(p1);

  int me = this->MyId;

  if ((me < p1) || (me > p2))
  {
    this->SubGroup->Broadcast(&this->SelectBuffer[0], 2, rootrank);
    return &this->SelectBuffer[0];
  }

  if (p1 == p2)
  {
    int* idx = this->PartitionAboutMyValue(L, R, K, dim);

    this->SubGroup->Broadcast(idx, 2, rootrank);

    return idx;
  }

  // Each process will rearrange their subarray myL-myR into a left region
  // of values less than X[K], a center region of values equal to X[K], and
  // a right region of values greater than X[K].  "I" will be the index
  // of the first value in the center region, or it will equal "J" if there
  // is no center region.  "J" will be the index to the start of the
  // right region, or it will be R+1 if there is no right region.

  int tag = this->SubGroup->tag;

  svtkSubGroup* sg = svtkSubGroup::New();
  sg->Initialize(p1, p2, me, tag, this->Controller->GetCommunicator());

  int hasK = this->WhoHas(K);

  int Krank = sg->getLocalRank(hasK);

  int myL = this->StartVal[me];
  int myR = this->EndVal[me];

  if (myL < L)
    myL = L;
  if (myR > R)
    myR = R;

  // Get Kth element

  float T;

  if (hasK == me)
  {
    T = this->GetLocalVal(K)[dim];
  }

  sg->Broadcast(&T, 1, Krank);

  int* idx; // dividing points in rearranged sub array

  if (hasK == me)
  {
    idx = this->PartitionAboutMyValue(myL, myR, K, dim);
  }
  else
  {
    idx = this->PartitionAboutOtherValue(myL, myR, T, dim);
  }

  // Copy these right away.  Implementation uses SelectBuffer
  // which is about to be overwritten.

  int I = idx[0];
  int J = idx[1];

  // Now the ugly part.  The processes redistribute the array so that
  // globally the interval [L:R] is partitioned into an interval of values
  // less than T, and interval of values equal to T, and an interval of
  // values greater than T.

  int nprocs = p2 - p1 + 1;

  int* buf = &this->SelectBuffer[0];

  int* left = buf;
  buf += nprocs; // global index of my leftmost
  int* right = buf;
  buf += nprocs; // global index of my rightmost
  int* Ival = buf;
  buf += nprocs; // global index of my first val = T
  int* Jval = buf;
  buf += nprocs; // global index of my first val > T

  int* leftArray = buf;
  buf += nprocs; // number of my vals < T
  int* leftUsed = buf;
  buf += nprocs; // how many scheduled to be sent so far

  int* centerArray = buf;
  buf += nprocs; // number of my vals = T
  int* centerUsed = buf;
  buf += nprocs; // how many scheduled to be sent so far

  int* rightArray = buf;
  buf += nprocs; // number of my vals > T
  int* rightUsed = buf;
  buf += nprocs; // how many scheduled to be sent so far

  rootrank = sg->getLocalRank(p1);

  sg->Gather(&myL, left, 1, rootrank);
  sg->Broadcast(left, nprocs, rootrank);

  sg->Gather(&myR, right, 1, rootrank);
  sg->Broadcast(right, nprocs, rootrank);

  sg->Gather(&I, Ival, 1, rootrank);
  sg->Broadcast(Ival, nprocs, rootrank);

  sg->Gather(&J, Jval, 1, rootrank);
  sg->Broadcast(Jval, nprocs, rootrank);

  sg->Delete();

  int leftRemaining = 0;
  int centerRemaining = 0;

  int p, sndr, recvr;

  for (p = 0; p < nprocs; p++)
  {
    leftArray[p] = Ival[p] - left[p];
    centerArray[p] = Jval[p] - Ival[p];
    rightArray[p] = right[p] - Jval[p] + 1;

    leftRemaining += leftArray[p];
    centerRemaining += centerArray[p];

    leftUsed[p] = 0;
    centerUsed[p] = 0;
    rightUsed[p] = 0;
  }

  int FirstCenter = left[0] + leftRemaining;
  int FirstRight = FirstCenter + centerRemaining;

  int nextLeftProc = 0;
  int nextCenterProc = 0;
  int nextRightProc = 0;

  int need, have, take;

  if ((myL > this->StartVal[me]) || (myR < this->EndVal[me]))
  {
    memcpy(this->NextPtArray, this->CurrentPtArray, this->PtArraySize * sizeof(float));
  }

  for (recvr = 0; recvr < nprocs; recvr++)
  {
    need = leftArray[recvr] + centerArray[recvr] + rightArray[recvr];
    have = 0;

    if (leftRemaining >= 0)
    {
      for (sndr = nextLeftProc; sndr < nprocs; sndr++)
      {
        take = leftArray[sndr] - leftUsed[sndr];

        if (take == 0)
          continue;

        take = (take > need) ? need : take;

        this->DoTransfer(
          sndr + p1, recvr + p1, left[sndr] + leftUsed[sndr], left[recvr] + have, take);

        have += take;
        need -= take;
        leftRemaining -= take;

        leftUsed[sndr] += take;

        if (need == 0)
          break;
      }

      if (leftUsed[sndr] == leftArray[sndr])
      {
        nextLeftProc = sndr + 1;
      }
      else
      {
        nextLeftProc = sndr;
      }
    }

    if (need == 0)
      continue;

    if (centerRemaining >= 0)
    {
      for (sndr = nextCenterProc; sndr < nprocs; sndr++)
      {
        take = centerArray[sndr] - centerUsed[sndr];

        if (take == 0)
          continue;

        take = (take > need) ? need : take;

        // Just copy the values, since we know what they are
        this->DoTransfer(sndr + p1, recvr + p1, left[sndr] + leftArray[sndr] + centerUsed[sndr],
          left[recvr] + have, take);

        have += take;
        need -= take;
        centerRemaining -= take;

        centerUsed[sndr] += take;

        if (need == 0)
          break;
      }

      if (centerUsed[sndr] == centerArray[sndr])
      {
        nextCenterProc = sndr + 1;
      }
      else
      {
        nextCenterProc = sndr;
      }
    }

    if (need == 0)
      continue;

    for (sndr = nextRightProc; sndr < nprocs; sndr++)
    {
      take = rightArray[sndr] - rightUsed[sndr];

      if (take == 0)
        continue;

      take = (take > need) ? need : take;

      this->DoTransfer(sndr + p1, recvr + p1,
        left[sndr] + leftArray[sndr] + centerArray[sndr] + rightUsed[sndr], left[recvr] + have,
        take);

      have += take;
      need -= take;

      rightUsed[sndr] += take;

      if (need == 0)
        break;
    }

    if (rightUsed[sndr] == rightArray[sndr])
    {
      nextRightProc = sndr + 1;
    }
    else
    {
      nextRightProc = sndr;
    }
  }

  this->SwitchDoubleBuffer();

  this->SelectBuffer[0] = FirstCenter;
  this->SelectBuffer[1] = FirstRight;

  rootrank = this->SubGroup->getLocalRank(p1);

  this->SubGroup->Broadcast(&this->SelectBuffer[0], 2, rootrank);

  return &this->SelectBuffer[0];
}

// This routine partitions the array from element L through element
// R into three segments.  This first contains values less than T, the
// next contains values equal to T, the last has values greater than T.
//
// This routine returns two values.  The first is the index of the
// first value equal to T, the second is the index of the first value
// greater than T.  If there is no value equal to T, the first value
// will equal the second value.  If there is no value greater than T,
// the second value returned will be R+1.
//
// This function is different than PartitionAboutMyValue, because in
// that function we know that "T" appears in the array.  In this
// function, "T" may or may not appear in the array.

int* svtkPKdTree::PartitionAboutOtherValue(int L, int R, float T, int dim)
{
  float *Ipt, *Jpt, Lval, Rval;
  int* vals = &this->SelectBuffer[0];
  int numTValues = 0;
  int numGreater = 0;
  int numLess = 0;
  int totalVals = R - L + 1;

  if (totalVals == 0)
  {
    // Special case: no values.
    vals[0] = vals[1] = L;
    return vals;
  }

  Ipt = this->GetLocalVal(L) + dim;
  Lval = *Ipt;

  if (Lval == T)
  {
    numTValues++;
  }
  else if (Lval > T)
  {
    numGreater++;
  }
  else
  {
    numLess++;
  }

  Jpt = this->GetLocalVal(R) + dim;
  Rval = *Jpt;

  if (Rval == T)
    numTValues++;
  else if (Rval > T)
    numGreater++;
  else
    numLess++;

  int I = L;
  int J = R;

  if ((Lval >= T) && (Rval >= T))
  {
    while (--J > I)
    {
      Jpt -= 3;
      if (*Jpt < T)
      {
        break;
      }
      if (*Jpt == T)
      {
        numTValues++;
      }
      else
      {
        numGreater++;
      }
    }
  }
  else if ((Lval < T) && (Rval < T))
  {
    Ipt = this->GetLocalVal(I) + dim;

    while (++I < J)
    {
      Ipt += 3;
      if (*Ipt >= T)
      {
        if (*Ipt == T)
        {
          numTValues++;
        }
        break;
      }
      numLess++;
    }
  }
  else if ((Lval < T) && (Rval >= T))
  {
    this->ExchangeLocalVals(I, J);
  }
  else if ((Lval >= T) && (Rval < T))
  {
    // first loop will fix this
  }

  if (numLess == totalVals)
  {
    vals[0] = vals[1] = R + 1; // special case - all less than T
    return vals;
  }
  else if (numTValues == totalVals)
  {
    vals[0] = L; // special case - all equal to T
    vals[1] = R + 1;
    return vals;
  }
  else if (numGreater == totalVals)
  {
    vals[0] = vals[1] = L; // special case - all greater than T
    return vals;
  }

  while (I < J)
  {
    // By design, I < J and value at I is >= T, and value
    // at J is < T, hence the exchange.

    this->ExchangeLocalVals(I, J);

    while (++I < J)
    {
      Ipt += 3;
      if (*Ipt >= T)
      {
        if (*Ipt == T)
        {
          numTValues++;
        }
        break;
      }
    }
    if (I == J)
      break;

    while (--J > I)
    {
      Jpt -= 3;
      if (*Jpt < T)
      {
        break;
      }
      if (*Jpt == T)
      {
        numTValues++;
      }
    }
  }

  // I and J are at the first value that is >= T.

  if (numTValues == 0)
  {
    vals[0] = I;
    vals[1] = I;
    return vals;
  }

  // Move all T's to the center interval

  vals[0] = I; // the first T will be here when we're done

  Ipt = this->GetLocalVal(I) + dim;
  I = I - 1;
  Ipt -= 3;

  J = R + 1;
  Jpt = this->GetLocalVal(R) + dim;
  Jpt += 3;

  while (I < J)
  {
    while (++I < J)
    {
      Ipt += 3;
      if (*Ipt != T)
      {
        break;
      }
    }
    if (I == J)
    {
      break;
    }

    while (--J > I)
    {
      Jpt -= 3;
      if (*Jpt == T)
      {
        break;
      }
    }

    if (I < J)
    {
      this->ExchangeLocalVals(I, J);
    }
  }

  // Now I and J are at the first value that is > T, and the T's are
  // to the left.

  vals[1] = I; // the first > T

  return vals;
}

// This routine partitions the array from element L through element
// R into three segments.  This first contains values less than T, the
// next contains values equal to T, the last has values greater than T.
// T is the element at K, where L <= K <= R.
//
// This routine returns two integers.  The first is the index of the
// first value equal to T, the second is the index of the first value
// greater than T.  If there is no value greater than T, the second
// value returned will be R+1.

int* svtkPKdTree::PartitionAboutMyValue(int L, int R, int K, int dim)
{
  float *Ipt, *Jpt;
  float T;
  int I, J;
  int manyTValues = 0;
  int* vals = &this->SelectBuffer[0];

  // Set up so after first exchange in the loop, we have either
  //   X[L] = T and X[R] >= T
  // or
  //   X[L] < T and X[R] = T
  //

  float* pt = this->GetLocalVal(K);

  T = pt[dim];

  this->ExchangeLocalVals(L, K);

  pt = this->GetLocalVal(R);

  if (pt[dim] >= T)
  {
    if (pt[dim] == T)
    {
      manyTValues = 1;
    }
    else
    {
      this->ExchangeLocalVals(R, L);
    }
  }

  I = L;
  J = R;

  Ipt = this->GetLocalVal(I) + dim;
  Jpt = this->GetLocalVal(J) + dim;

  while (I < J)
  {
    this->ExchangeLocalVals(I, J);

    while (--J > I)
    {
      Jpt -= 3;
      if (*Jpt < T)
      {
        break;
      }
      if (!manyTValues && (J > L) && (*Jpt == T))
      {
        manyTValues = 1;
      }
    }

    if (I == J)
    {
      break;
    }

    while (++I < J)
    {
      Ipt += 3;

      if (*Ipt >= T)
      {
        if (!manyTValues && (*Ipt == T))
        {
          manyTValues = 1;
        }
        break;
      }
    }
  }

  // I and J are at the rightmost value < T ( or at L if all values
  // are >= T)

  pt = this->GetLocalVal(L);

  float Lval = pt[dim];

  if (Lval == T)
  {
    this->ExchangeLocalVals(L, J);
  }
  else
  {
    this->ExchangeLocalVals(++J, R);
  }

  // Now J is at the leftmost value >= T.  (It is at a T value.)

  vals[0] = J;
  vals[1] = J + 1;

  // Arrange array so all values equal to T are together

  if (manyTValues)
  {
    I = J;
    Ipt = this->GetLocalVal(I) + dim;

    J = R + 1;
    Jpt = this->GetLocalVal(R) + dim;
    Jpt += 3;

    while (I < J)
    {
      while (++I < J)
      {
        Ipt += 3;
        if (*Ipt != T)
        {
          break;
        }
      }
      if (I == J)
      {
        break;
      }

      while (--J > I)
      {
        Jpt -= 3;
        if (*Jpt == T)
        {
          break;
        }
      }

      if (I < J)
      {
        this->ExchangeLocalVals(I, J);
      }
    }
    // I and J are at the first value that is > T

    vals[1] = I;
  }

  return vals;
}

//--------------------------------------------------------------------
// Compute the bounds for the data in a region
//--------------------------------------------------------------------

void svtkPKdTree::GetLocalMinMax(int L, int R, int me, float* min, float* max)
{
  int i, d;
  int from = this->StartVal[me];
  int to = this->EndVal[me];

  if (L > from)
  {
    from = L;
  }
  if (R < to)
  {
    to = R;
  }

  if (from <= to)
  {
    from -= this->StartVal[me];
    to -= this->StartVal[me];

    float* val = this->CurrentPtArray + from * 3;

    for (d = 0; d < 3; d++)
    {
      min[d] = max[d] = val[d];
    }

    for (i = from + 1; i <= to; i++)
    {
      val += 3;

      for (d = 0; d < 3; d++)
      {
        if (val[d] < min[d])
        {
          min[d] = val[d];
        }
        else if (val[d] > max[d])
        {
          max[d] = val[d];
        }
      }
    }
  }
  else
  {
    // this guy has none of the data, but still must participate
    //   in ReduceMax and ReduceMin

    double* regionMin = this->Top->GetMinBounds();
    double* regionMax = this->Top->GetMaxBounds();

    for (d = 0; d < 3; d++)
    {
      min[d] = (float)regionMax[d];
      max[d] = (float)regionMin[d];
    }
  }
}
void svtkPKdTree::GetDataBounds(int L, int K, int R, float globalBounds[12])
{
  float localMinLeft[3]; // Left region is L through K-1
  float localMaxLeft[3];
  float globalMinLeft[3];
  float globalMaxLeft[3];
  float localMinRight[3]; // Right region is K through R
  float localMaxRight[3];
  float globalMinRight[3];
  float globalMaxRight[3];

  this->GetLocalMinMax(L, K - 1, this->MyId, localMinLeft, localMaxLeft);

  this->GetLocalMinMax(K, R, this->MyId, localMinRight, localMaxRight);

  this->SubGroup->ReduceMin(localMinLeft, globalMinLeft, 3, 0);
  this->SubGroup->Broadcast(globalMinLeft, 3, 0);

  this->SubGroup->ReduceMax(localMaxLeft, globalMaxLeft, 3, 0);
  this->SubGroup->Broadcast(globalMaxLeft, 3, 0);

  this->SubGroup->ReduceMin(localMinRight, globalMinRight, 3, 0);
  this->SubGroup->Broadcast(globalMinRight, 3, 0);

  this->SubGroup->ReduceMax(localMaxRight, globalMaxRight, 3, 0);
  this->SubGroup->Broadcast(globalMaxRight, 3, 0);

  float* left = globalBounds;
  float* right = globalBounds + 6;

  MinMaxToBounds(left, globalMinLeft, globalMaxLeft);

  MinMaxToBounds(right, globalMinRight, globalMaxRight);
}

//--------------------------------------------------------------------
// Complete the tree - Different nodes of tree were computed by different
//   processors.  Now put it together.
//--------------------------------------------------------------------

int svtkPKdTree::CompleteTree()
{
  SCOPETIMER("CompleteTree");

  // calculate depth of entire tree

  int depth;
  int myDepth = svtkPKdTree::ComputeDepth(this->Top);

  this->SubGroup->ReduceMax(&myDepth, &depth, 1, 0);
  this->SubGroup->Broadcast(&depth, 1, 0);

  // fill out nodes of tree

  int fail = svtkPKdTree::FillOutTree(this->Top, depth);

  if (this->AllCheckForFailure(fail, "CompleteTree", "memory allocation"))
  {
    return 1;
  }

  // Processor 0 collects all the nodes of the k-d tree, and then
  //   processes the tree to ensure region boundaries are
  //   consistent.  The completed tree is then broadcast.
  std::vector<int> buf(this->NumProcesses);

#ifdef YIELDS_INCONSISTENT_REGION_BOUNDARIES

  this->RetrieveData(this->Top, &buf[0]);

#else

  this->ReduceData(this->Top, &buf[0]);

  if (this->MyId == 0)
  {
    CheckFixRegionBoundaries(this->Top);
  }

  this->BroadcastData(this->Top);
#endif

  return 0;
}

void svtkPKdTree::PackData(svtkKdNode* kd, double* data)
{
  int i, v;

  data[0] = (double)kd->GetDim();
  data[1] = (double)kd->GetLeft()->GetNumberOfPoints();
  data[2] = (double)kd->GetRight()->GetNumberOfPoints();

  double* lmin = kd->GetLeft()->GetMinBounds();
  double* lmax = kd->GetLeft()->GetMaxBounds();
  double* lminData = kd->GetLeft()->GetMinDataBounds();
  double* lmaxData = kd->GetLeft()->GetMaxDataBounds();
  double* rmin = kd->GetRight()->GetMinBounds();
  double* rmax = kd->GetRight()->GetMaxBounds();
  double* rminData = kd->GetRight()->GetMinDataBounds();
  double* rmaxData = kd->GetRight()->GetMaxDataBounds();

  v = 3;
  for (i = 0; i < 3; i++)
  {
    data[v++] = lmin[i];
    data[v++] = lmax[i];
    data[v++] = lminData[i];
    data[v++] = lmaxData[i];
    data[v++] = rmin[i];
    data[v++] = rmax[i];
    data[v++] = rminData[i];
    data[v++] = rmaxData[i];
  }
}
void svtkPKdTree::UnpackData(svtkKdNode* kd, double* data)
{
  int i, v;

  kd->SetDim((int)data[0]);
  kd->GetLeft()->SetNumberOfPoints((int)data[1]);
  kd->GetRight()->SetNumberOfPoints((int)data[2]);

  double lmin[3], rmin[3], lmax[3], rmax[3];
  double lminData[3], rminData[3], lmaxData[3], rmaxData[3];

  v = 3;
  for (i = 0; i < 3; i++)
  {
    lmin[i] = data[v++];
    lmax[i] = data[v++];
    lminData[i] = data[v++];
    lmaxData[i] = data[v++];
    rmin[i] = data[v++];
    rmax[i] = data[v++];
    rminData[i] = data[v++];
    rmaxData[i] = data[v++];
  }

  kd->GetLeft()->SetBounds(lmin[0], lmax[0], lmin[1], lmax[1], lmin[2], lmax[2]);
  kd->GetLeft()->SetDataBounds(
    lminData[0], lmaxData[0], lminData[1], lmaxData[1], lminData[2], lmaxData[2]);

  kd->GetRight()->SetBounds(rmin[0], rmax[0], rmin[1], rmax[1], rmin[2], rmax[2]);
  kd->GetRight()->SetDataBounds(
    rminData[0], rmaxData[0], rminData[1], rmaxData[1], rminData[2], rmaxData[2]);
}
void svtkPKdTree::ReduceData(svtkKdNode* kd, int* sources)
{
  int i;
  double data[27];
  svtkCommunicator* comm = this->Controller->GetCommunicator();

  if (kd->GetLeft() == nullptr)
    return;

  int ihave = (kd->GetDim() < 3);

  this->SubGroup->Gather(&ihave, sources, 1, 0);
  this->SubGroup->Broadcast(sources, this->NumProcesses, 0);

  // a contiguous group of process IDs built this node, the first
  // in the group sends it to node 0 if node 0 doesn't have it

  if (sources[0] == 0)
  {
    int root = -1;

    for (i = 1; i < this->NumProcesses; i++)
    {
      if (sources[i])
      {
        root = i;
        break;
      }
    }
    if (root == -1)
    {

      // Normally BuildLocator will create a complete tree, but
      // it may refuse to divide a region if all the data is at
      // the same point along the axis it wishes to divide.  In
      // that case, this region was not divided, so just return.

      svtkKdTree::DeleteAllDescendants(kd);

      return;
    }

    if (root == this->MyId)
    {
      svtkPKdTree::PackData(kd, data);

      comm->Send(data, 27, 0, 0x1111);
    }
    else if (0 == this->MyId)
    {
      comm->Receive(data, 27, root, 0x1111);

      svtkPKdTree::UnpackData(kd, data);
    }
  }

  this->ReduceData(kd->GetLeft(), sources);

  this->ReduceData(kd->GetRight(), sources);
}
void svtkPKdTree::BroadcastData(svtkKdNode* kd)
{
  double data[27];

  if (kd->GetLeft() == nullptr)
    return;

  if (0 == this->MyId)
  {
    svtkPKdTree::PackData(kd, data);
  }

  this->SubGroup->Broadcast(data, 27, 0);

  if (this->MyId > 0)
  {
    svtkPKdTree::UnpackData(kd, data);
  }

  this->BroadcastData(kd->GetLeft());

  this->BroadcastData(kd->GetRight());
}
void svtkPKdTree::CheckFixRegionBoundaries(svtkKdNode* tree)
{
  if (tree->GetLeft() == nullptr)
    return;

  int nextDim = tree->GetDim();

  svtkKdNode* left = tree->GetLeft();
  svtkKdNode* right = tree->GetRight();

  double* min = tree->GetMinBounds();
  double* max = tree->GetMaxBounds();
  double* lmin = left->GetMinBounds();
  double* lmax = left->GetMaxBounds();
  double* rmin = right->GetMinBounds();
  double* rmax = right->GetMaxBounds();

  for (int dim = 0; dim < 3; dim++)
  {
    if ((lmin[dim] - min[dim]) != 0.0)
      lmin[dim] = min[dim];
    if ((rmax[dim] - max[dim]) != 0.0)
      rmax[dim] = max[dim];

    if (dim != nextDim) // the dimension I did *not* divide along
    {
      if ((lmax[dim] - max[dim]) != 0.0)
        lmax[dim] = max[dim];
      if ((rmin[dim] - min[dim]) != 0.0)
        rmin[dim] = min[dim];
    }
    else
    {
      if ((lmax[dim] - rmin[dim]) != 0.0)
        lmax[dim] = rmin[dim];
    }
  }

  CheckFixRegionBoundaries(left);
  CheckFixRegionBoundaries(right);
}
#ifdef YIELDS_INCONSISTENT_REGION_BOUNDARIES

void svtkPKdTree::RetrieveData(svtkKdNode* kd, int* sources)
{
  int i;
  double data[27];

  if (kd->GetLeft() == nullptr)
    return;

  int ihave = (kd->GetDim() < 3);

  this->SubGroup->Gather(&ihave, sources, 1, 0);
  this->SubGroup->Broadcast(sources, this->NumProcesses, 0);

  // a contiguous group of process IDs built this node, the first
  // in the group broadcasts the results to everyone else

  int root = -1;

  for (i = 0; i < this->NumProcesses; i++)
  {
    if (sources[i])
    {
      root = i;
      break;
    }
  }
  if (root == -1)
  {
    // Normally BuildLocator will create a complete tree, but
    // it may refuse to divide a region if all the data is at
    // the same point along the axis it wishes to divide.  In
    // that case, this region was not divided, so just return.

    svtkKdTree::DeleteAllDescendants(kd);

    return;
  }

  if (root == this->MyId)
  {
    svtkPKdTree::PackData(kd, data);
  }

  this->SubGroup->Broadcast(data, 27, root);

  if (!ihave)
  {
    svtkPKdTree::UnpackData(kd, data);
  }

  this->RetrieveData(kd->GetLeft(), sources);

  this->RetrieveData(kd->GetRight(), sources);

  return;
}
#endif

int svtkPKdTree::FillOutTree(svtkKdNode* kd, int level)
{
  if (level == 0)
    return 0;

  if (kd->GetLeft() == nullptr)
  {
    svtkKdNode* left = svtkKdNode::New();

    if (!left)
      goto doneError2;

    left->SetBounds(-1, -1, -1, -1, -1, -1);
    left->SetDataBounds(-1, -1, -1, -1, -1, -1);
    left->SetNumberOfPoints(-1);

    svtkKdNode* right = svtkKdNode::New();

    if (!right)
      goto doneError2;

    right->SetBounds(-1, -1, -1, -1, -1, -1);
    right->SetDataBounds(-1, -1, -1, -1, -1, -1);
    right->SetNumberOfPoints(-1);

    kd->AddChildNodes(left, right);
  }

  if (svtkPKdTree::FillOutTree(kd->GetLeft(), level - 1))
    goto doneError2;

  if (svtkPKdTree::FillOutTree(kd->GetRight(), level - 1))
    goto doneError2;

  return 0;

doneError2:

  return 1;
}

int svtkPKdTree::ComputeDepth(svtkKdNode* kd)
{
  int leftDepth = 0;
  int rightDepth = 0;

  if ((kd->GetLeft() == nullptr) && (kd->GetRight() == nullptr))
    return 0;

  if (kd->GetLeft())
  {
    leftDepth = svtkPKdTree::ComputeDepth(kd->GetLeft());
  }
  if (kd->GetRight())
  {
    rightDepth = svtkPKdTree::ComputeDepth(kd->GetRight());
  }

  if (leftDepth > rightDepth)
    return leftDepth + 1;
  else
    return rightDepth + 1;
}

//--------------------------------------------------------------------
// lists, lists, lists
//--------------------------------------------------------------------

int svtkPKdTree::AllocateDoubleBuffer()
{
  this->FreeDoubleBuffer();

  this->PtArraySize = this->NumCells[this->MyId] * 3;

  this->PtArray2 = new float[this->PtArraySize];

  this->CurrentPtArray = this->PtArray;
  this->NextPtArray = this->PtArray2;

  return (this->PtArray2 == nullptr);
}
void svtkPKdTree::SwitchDoubleBuffer()
{
  float* temp = this->CurrentPtArray;

  this->CurrentPtArray = this->NextPtArray;
  this->NextPtArray = temp;
}
void svtkPKdTree::FreeDoubleBuffer()
{
  FreeList(this->PtArray2);
  this->CurrentPtArray = this->PtArray;
  this->NextPtArray = nullptr;
}

void svtkPKdTree::AllocateSelectBuffer()
{
  std::fill(this->SelectBuffer.begin(), this->SelectBuffer.end(), 0);
  this->SelectBuffer.resize(this->NumProcesses * 10, 0);
}
void svtkPKdTree::FreeSelectBuffer()
{
  this->SelectBuffer.clear();
}

#define FreeListOfLists(list, len)                                                                 \
  {                                                                                                \
    int i;                                                                                         \
    if (list)                                                                                      \
    {                                                                                              \
      for (i = 0; i < (len); i++)                                                                  \
      {                                                                                            \
        if (list[i])                                                                               \
          delete[] list[i];                                                                        \
      }                                                                                            \
      delete[] list;                                                                               \
      list = nullptr;                                                                              \
    }                                                                                              \
  }

#define MakeList(field, type, len)                                                                 \
  {                                                                                                \
    if ((len) > 0)                                                                                 \
    {                                                                                              \
      field = new type[len];                                                                       \
      if (field)                                                                                   \
      {                                                                                            \
        memset(field, 0, (len) * sizeof(type));                                                    \
      }                                                                                            \
    }                                                                                              \
  }

// global index lists -----------------------------------------------

void svtkPKdTree::InitializeGlobalIndexLists()
{
  this->StartVal.clear();
  this->EndVal.clear();
  this->NumCells.clear();
}
void svtkPKdTree::AllocateAndZeroGlobalIndexLists()
{
  this->FreeGlobalIndexLists();

  std::fill(this->StartVal.begin(), this->StartVal.end(), 0);
  this->StartVal.resize(this->NumProcesses, 0);
  std::fill(this->EndVal.begin(), this->EndVal.end(), 0);
  this->EndVal.resize(this->NumProcesses, 0);
  std::fill(this->NumCells.begin(), this->NumCells.end(), 0);
  this->NumCells.resize(this->NumProcesses, 0);
}
void svtkPKdTree::FreeGlobalIndexLists()
{
  this->StartVal.clear();
  this->EndVal.clear();
  this->NumCells.clear();
}
int svtkPKdTree::BuildGlobalIndexLists(svtkIdType numMyCells)
{
  SCOPETIMER("BuildGlobalIndexLists");

  this->AllocateAndZeroGlobalIndexLists();

  this->SubGroup->Gather(&numMyCells, &this->NumCells[0], 1, 0);

  this->SubGroup->Broadcast(&this->NumCells[0], this->NumProcesses, 0);

  this->StartVal[0] = 0;
  this->EndVal[0] = this->NumCells[0] - 1;

  this->TotalNumCells = this->NumCells[0];

  for (int i = 1; i < this->NumProcesses; i++)
  {
    this->StartVal[i] = this->EndVal[i - 1] + 1;
    this->EndVal[i] = this->EndVal[i - 1] + this->NumCells[i];

    this->TotalNumCells += this->NumCells[i];
  }

  return 0;
}

// Region assignment lists ---------------------------------------------

void svtkPKdTree::InitializeRegionAssignmentLists()
{
  this->RegionAssignmentMap.clear();
  this->ProcessAssignmentMap.clear();
  this->NumRegionsAssigned.clear();
}
void svtkPKdTree::AllocateAndZeroRegionAssignmentLists()
{
  std::fill(this->RegionAssignmentMap.begin(), this->RegionAssignmentMap.end(), 0);
  this->RegionAssignmentMap.resize(this->GetNumberOfRegions(), 0);
  std::fill(this->NumRegionsAssigned.begin(), this->NumRegionsAssigned.end(), 0);
  this->NumRegionsAssigned.resize(this->NumProcesses, 0);
  for (auto& it : this->ProcessAssignmentMap)
  {
    it.clear();
  }
  this->ProcessAssignmentMap.resize(this->NumProcesses);
}
void svtkPKdTree::FreeRegionAssignmentLists()
{
  this->RegionAssignmentMap.clear();
  this->NumRegionsAssigned.clear();
  this->ProcessAssignmentMap.clear();
}

// Process data tables ------------------------------------------------

void svtkPKdTree::InitializeProcessDataLists()
{
  this->DataLocationMap.clear();

  this->NumProcessesInRegion.clear();
  this->ProcessList.clear();

  this->NumRegionsInProcess.clear();
  this->ParallelRegionList.clear();

  this->CellCountList.clear();
}

void svtkPKdTree::AllocateAndZeroProcessDataLists()
{
  int nRegions = this->GetNumberOfRegions();
  int nProcesses = this->NumProcesses;

  this->FreeProcessDataLists();

  std::fill(this->DataLocationMap.begin(), this->DataLocationMap.end(), 0);
  this->DataLocationMap.resize(nRegions * nProcesses, 0);
  std::fill(this->NumProcessesInRegion.begin(), this->NumProcessesInRegion.end(), 0);
  this->NumProcessesInRegion.resize(nRegions, 0);

  for (auto& it : this->ProcessList)
  {
    it.clear();
  }
  this->ProcessList.resize(nRegions);

  std::fill(this->NumRegionsInProcess.begin(), this->NumRegionsInProcess.end(), 0);
  this->NumRegionsInProcess.resize(nProcesses, 0);

  for (auto& it : this->ParallelRegionList)
  {
    it.clear();
  }
  this->ParallelRegionList.resize(nProcesses);

  for (auto& it : this->CellCountList)
  {
    it.clear();
  }
  this->CellCountList.resize(nRegions);
}
void svtkPKdTree::FreeProcessDataLists()
{
  this->CellCountList.clear();

  this->ParallelRegionList.clear();

  this->NumRegionsInProcess.clear();
  this->ProcessList.clear();

  this->NumProcessesInRegion.clear();
  this->DataLocationMap.clear();
}

// Field array global min and max -----------------------------------

void svtkPKdTree::InitializeFieldArrayMinMax()
{
  this->NumCellArrays = this->NumPointArrays = 0;
  this->CellDataMin.clear();
  this->CellDataMax.clear();
  this->PointDataMin.clear();
  this->PointDataMax.clear();
  this->CellDataName.clear();
  this->PointDataName.clear();
}

void svtkPKdTree::AllocateAndZeroFieldArrayMinMax()
{
  this->NumCellArrays = 0;
  this->NumPointArrays = 0;

  for (int set = 0; set < this->GetNumberOfDataSets(); set++)
  {
    this->NumCellArrays += this->GetDataSet(set)->GetCellData()->GetNumberOfArrays();
    this->NumPointArrays += this->GetDataSet(set)->GetPointData()->GetNumberOfArrays();
  }

  // Find maximum number of cell and point arrays. Set this number on all processes.
  // This handles the case where some processes have datasets with no cell or point
  // arrays, which may happen if a dataset on a process has no points or cells.
  if (this->NumProcesses > 1)
  {
    int tmpNumArrays[2], maxNumArrays[2];
    tmpNumArrays[0] = this->NumCellArrays;
    tmpNumArrays[1] = this->NumPointArrays;
    this->Controller->AllReduce(tmpNumArrays, maxNumArrays, 2, svtkCommunicator::MAX_OP);
    this->NumCellArrays = maxNumArrays[0];
    this->NumPointArrays = maxNumArrays[1];
  }

  this->FreeFieldArrayMinMax();

  if (this->NumCellArrays > 0)
  {
    std::fill(this->CellDataMin.begin(), this->CellDataMin.end(), 0);
    this->CellDataMin.resize(this->NumCellArrays, 0);
    std::fill(this->CellDataMax.begin(), this->CellDataMax.end(), 0);
    this->CellDataMax.resize(this->NumCellArrays, 0);

    std::fill(this->CellDataName.begin(), this->CellDataName.end(), std::string());
    this->CellDataName.resize(this->NumCellArrays, nullptr);
  }

  if (this->NumPointArrays > 0)
  {
    std::fill(this->PointDataMin.begin(), this->PointDataMin.end(), 0);
    this->PointDataMin.resize(this->NumPointArrays, 0);
    std::fill(this->PointDataMax.begin(), this->PointDataMax.end(), 0);
    this->PointDataMax.resize(this->NumPointArrays, 0);

    std::fill(this->PointDataName.begin(), this->PointDataName.end(), std::string());
    this->PointDataName.resize(this->NumPointArrays);
  }
}
void svtkPKdTree::FreeFieldArrayMinMax()
{
  this->CellDataMin.clear();
  this->CellDataMax.clear();
  this->PointDataMin.clear();
  this->PointDataMax.clear();

  this->CellDataName.clear();
  this->PointDataName.clear();

  this->NumCellArrays = this->NumPointArrays = 0;
}

void svtkPKdTree::ReleaseTables()
{
  SCOPETIMER("ReleaseTables");

  if (this->RegionAssignment != UserDefinedAssignment)
  {
    this->FreeRegionAssignmentLists();
  }
  this->FreeProcessDataLists();
  this->FreeFieldArrayMinMax();
}

//--------------------------------------------------------------------
// Create tables indicating which processes have data for which
//  regions.
//--------------------------------------------------------------------

int svtkPKdTree::CreateProcessCellCountData()
{
  int proc, reg;
  int retval = 0;
  char *procData(nullptr), *myData(nullptr);

  this->SubGroup = svtkSubGroup::New();
  this->SubGroup->Initialize(
    0, this->NumProcesses - 1, this->MyId, 0x0000f000, this->Controller->GetCommunicator());

  this->AllocateAndZeroProcessDataLists();

  int fail = this->Top ? 0 : 1;

  if (this->AllCheckForFailure(fail, "BuildRegionProcessTables", "memory allocation"))
  {
    this->FreeProcessDataLists();
    this->SubGroup->Delete();
    this->SubGroup = nullptr;
    return 1;
  }

  // Build table indicating which processes have data for which regions

  std::vector<int> cellCounts;
  fail = this->CollectLocalRegionProcessData(cellCounts) ? 0 : 1;

  if (this->AllCheckForFailure(fail, "BuildRegionProcessTables", "error"))
  {
    this->FreeProcessDataLists();
    return 1;
  }

  myData = &(this->DataLocationMap[this->MyId * this->GetNumberOfRegions()]);

  for (reg = 0; reg < this->GetNumberOfRegions(); reg++)
  {
    if (cellCounts[reg] > 0)
      myData[reg] = 1;
  }

  if (this->NumProcesses > 1)
  {
    this->SubGroup->Gather(myData, &this->DataLocationMap[0], this->GetNumberOfRegions(), 0);
    this->SubGroup->Broadcast(
      &this->DataLocationMap[0], this->GetNumberOfRegions() * this->NumProcesses, 0);
  }

  // Other helpful tables - not the fastest way to create this
  //   information, but it uses the least memory

  procData = &this->DataLocationMap[0];

  for (proc = 0; proc < this->NumProcesses; proc++)
  {
    for (reg = 0; reg < this->GetNumberOfRegions(); reg++)
    {
      if (*procData)
      {
        this->NumProcessesInRegion[reg]++;
        this->NumRegionsInProcess[proc]++;
      }
      procData++;
    }
  }
  for (reg = 0; reg < this->GetNumberOfRegions(); reg++)
  {
    int nprocs = this->NumProcessesInRegion[reg];

    if (nprocs > 0)
    {
      this->ProcessList[reg].resize(nprocs);
      this->ProcessList[reg][0] = -1;
      this->CellCountList[reg].resize(nprocs);
      this->CellCountList[reg][0] = -1;
    }
  }
  for (proc = 0; proc < this->NumProcesses; proc++)
  {
    int nregs = this->NumRegionsInProcess[proc];

    if (nregs > 0)
    {
      this->ParallelRegionList[proc].resize(nregs);
      this->ParallelRegionList[proc][0] = -1;
    }
  }

  procData = &this->DataLocationMap[0];

  for (proc = 0; proc < this->NumProcesses; proc++)
  {

    for (reg = 0; reg < this->GetNumberOfRegions(); reg++)
    {
      if (*procData)
      {
        this->AddEntry(&this->ProcessList[reg][0], this->NumProcessesInRegion[reg], proc);

        this->AddEntry(&this->ParallelRegionList[proc][0], this->NumRegionsInProcess[proc], reg);
      }
      procData++;
    }
  }

  // Cell counts per process per region
  std::vector<int> tempbuf;
  int* tempbufptr = &cellCounts[0];
  if (this->NumProcesses > 1)
  {
    tempbuf.resize(this->GetNumberOfRegions() * this->NumProcesses);

    this->SubGroup->Gather(&cellCounts[0], &tempbuf[0], this->GetNumberOfRegions(), 0);
    this->SubGroup->Broadcast(&tempbuf[0], this->NumProcesses * this->GetNumberOfRegions(), 0);
    tempbufptr = &tempbuf[0];
  }

  for (proc = 0; proc < this->NumProcesses; proc++)
  {
    int* procCount = tempbufptr + (proc * this->GetNumberOfRegions());

    for (reg = 0; reg < this->GetNumberOfRegions(); reg++)
    {
      if (procCount[reg] > 0)
      {
        this->AddEntry(
          &this->CellCountList[reg][0], this->NumProcessesInRegion[reg], procCount[reg]);
      }
    }
  }

  this->SubGroup->Delete();
  this->SubGroup = nullptr;

  return retval;
}

//--------------------------------------------------------------------
// Create list of global min and max for cell and point field arrays
//--------------------------------------------------------------------

int svtkPKdTree::CreateGlobalDataArrayBounds()
{
  int set = 0;
  this->SubGroup = nullptr;

  if (this->NumProcesses > 1)
  {
    this->SubGroup = svtkSubGroup::New();
    this->SubGroup->Initialize(
      0, this->NumProcesses - 1, this->MyId, 0x0000f000, this->Controller->GetCommunicator());
  }

  this->AllocateAndZeroFieldArrayMinMax();

  TIMER("Get global ranges");

  int ar;
  double range[2];
  int nc = 0;
  int np = 0;

  // This code assumes that if more than one dataset was input to svtkPKdTree,
  // each process input the datasets in the same order.

  if (this->NumCellArrays > 0)
  {
    for (set = 0; set < this->GetNumberOfDataSets(); set++)
    {
      int ncellarrays = this->GetDataSet(set)->GetCellData()->GetNumberOfArrays();

      for (ar = 0; ar < ncellarrays; ar++)
      {
        svtkDataArray* array = this->GetDataSet(set)->GetCellData()->GetArray(ar);

        array->GetRange(range);

        this->CellDataMin[nc] = range[0];
        this->CellDataMax[nc] = range[1];

        svtkPKdTree::StrDupWithNew(array->GetName(), this->CellDataName[nc]);
        nc++;
      }
    }

    if (this->NumProcesses > 1)
    {
      this->SubGroup->ReduceMin(
        &this->CellDataMin[0], &this->CellDataMin[0], this->NumCellArrays, 0);
      this->SubGroup->Broadcast(&this->CellDataMin[0], this->NumCellArrays, 0);

      this->SubGroup->ReduceMax(
        &this->CellDataMax[0], &this->CellDataMax[0], this->NumCellArrays, 0);
      this->SubGroup->Broadcast(&this->CellDataMax[0], this->NumCellArrays, 0);
    }
  }

  if (this->NumPointArrays > 0)
  {
    for (set = 0; set < this->GetNumberOfDataSets(); set++)
    {
      int npointarrays = this->GetDataSet(set)->GetPointData()->GetNumberOfArrays();

      for (ar = 0; ar < npointarrays; ar++)
      {
        svtkDataArray* array = this->GetDataSet(set)->GetPointData()->GetArray(ar);

        array->GetRange(range);

        this->PointDataMin[np] = range[0];
        this->PointDataMax[np] = range[1];

        StrDupWithNew(array->GetName(), this->PointDataName[np]);
        np++;
      }
    }

    if (this->NumProcesses > 1)
    {
      this->SubGroup->ReduceMin(
        &this->PointDataMin[0], &this->PointDataMin[0], this->NumPointArrays, 0);
      this->SubGroup->Broadcast(&this->PointDataMin[0], this->NumPointArrays, 0);

      this->SubGroup->ReduceMax(
        &this->PointDataMax[0], &this->PointDataMax[0], this->NumPointArrays, 0);
      this->SubGroup->Broadcast(&this->PointDataMax[0], this->NumPointArrays, 0);
    }
  }

  TIMERDONE("Get global ranges");

  FreeObject(this->SubGroup);

  return 0;
}
bool svtkPKdTree::CollectLocalRegionProcessData(std::vector<int>& cellCounts)
{
  int numRegions = this->GetNumberOfRegions();
  std::fill(cellCounts.begin(), cellCounts.end(), 0);
  cellCounts.resize(numRegions, 0);

  TIMER("Get cell regions");

  int* IDs = this->AllGetRegionContainingCell();

  TIMERDONE("Get cell regions");

  for (int set = 0; set < this->GetNumberOfDataSets(); set++)
  {
    int ncells = this->GetDataSet(set)->GetNumberOfCells();

    TIMER("Increment cell counts");

    for (int i = 0; i < ncells; i++)
    {
      int regionId = IDs[i];

      if ((regionId < 0) || (regionId >= numRegions))
      {
        SVTKERROR("CollectLocalRegionProcessData - corrupt data");
        cellCounts.clear();
        return false;
      }
      cellCounts[regionId]++;
    }

    IDs += ncells;

    TIMERDONE("Increment cell counts");
  }
  return true;
}
void svtkPKdTree::AddEntry(int* list, int len, int id)
{
  int i = 0;

  while ((i < len) && (list[i] != -1))
    i++;

  if (i == len)
    return; // error

  list[i++] = id;

  if (i < len)
    list[i] = -1;
}
#ifdef SVTK_USE_64BIT_IDS
void svtkPKdTree::AddEntry(svtkIdType* list, int len, svtkIdType id)
{
  int i = 0;

  while ((i < len) && (list[i] != -1))
    i++;

  if (i == len)
    return; // error

  list[i++] = id;

  if (i < len)
    list[i] = -1;
}
#endif
int svtkPKdTree::BinarySearch(svtkIdType* list, int len, svtkIdType which)
{
  svtkIdType mid, left, right;

  mid = -1;

  if (len <= 3)
  {
    for (int i = 0; i < len; i++)
    {
      if (list[i] == which)
      {
        mid = i;
        break;
      }
    }
  }
  else
  {
    mid = len >> 1;
    left = 0;
    right = len - 1;

    while (list[mid] != which)
    {
      if (list[mid] < which)
      {
        left = mid + 1;
      }
      else
      {
        right = mid - 1;
      }

      if (right > left + 1)
      {
        mid = (left + right) >> 1;
      }
      else
      {
        if (list[left] == which)
          mid = left;
        else if (list[right] == which)
          mid = right;
        else
          mid = -1;
        break;
      }
    }
  }
  return mid;
}
//--------------------------------------------------------------------
// Assign responsibility for each spatial region to one process
//--------------------------------------------------------------------

int svtkPKdTree::UpdateRegionAssignment()
{
  SCOPETIMER("UpdateRegionAssignment");

  int returnVal = 0;

  if (this->RegionAssignment == ContiguousAssignment)
  {
    returnVal = this->AssignRegionsContiguous();
  }
  else if (this->RegionAssignment == RoundRobinAssignment)
  {
    returnVal = this->AssignRegionsRoundRobin();
  }

  return returnVal;
}
int svtkPKdTree::AssignRegionsRoundRobin()
{
  this->RegionAssignment = RoundRobinAssignment;

  if (this->Top == nullptr)
  {
    return 0;
  }

  int nProcesses = this->NumProcesses;
  int nRegions = this->GetNumberOfRegions();

  this->AllocateAndZeroRegionAssignmentLists();

  for (int i = 0, procID = 0; i < nRegions; i++)
  {
    this->RegionAssignmentMap[i] = procID;
    this->NumRegionsAssigned[procID]++;

    procID = ((procID == nProcesses - 1) ? 0 : procID + 1);
  }
  this->BuildRegionListsForProcesses();

  return 0;
}
int svtkPKdTree::AssignRegions(int* map, int len)
{
  this->AllocateAndZeroRegionAssignmentLists();

  std::fill(this->RegionAssignmentMap.begin(), this->RegionAssignmentMap.end(), 0);
  this->RegionAssignmentMap.resize(len);

  this->RegionAssignment = UserDefinedAssignment;

  for (int i = 0; i < len; i++)
  {
    if ((map[i] < 0) || (map[i] >= this->NumProcesses))
    {
      this->FreeRegionAssignmentLists();
      SVTKERROR("AssignRegions - invalid process id " << map[i]);
      return 1;
    }

    this->RegionAssignmentMap[i] = map[i];
    this->NumRegionsAssigned[map[i]]++;
  }

  this->BuildRegionListsForProcesses();

  return 0;
}
void svtkPKdTree::AddProcessRegions(int procId, svtkKdNode* kd)
{
  svtkIntArray* leafNodeIds = svtkIntArray::New();

  svtkKdTree::GetLeafNodeIds(kd, leafNodeIds);

  int nLeafNodes = leafNodeIds->GetNumberOfTuples();

  for (int n = 0; n < nLeafNodes; n++)
  {
    this->RegionAssignmentMap[leafNodeIds->GetValue(n)] = procId;
    this->NumRegionsAssigned[procId]++;
  }

  leafNodeIds->Delete();
}
int svtkPKdTree::AssignRegionsContiguous()
{
  int p;

  this->RegionAssignment = ContiguousAssignment;

  if (this->Top == nullptr)
  {
    return 0;
  }

  int nProcesses = this->NumProcesses;
  int nRegions = this->GetNumberOfRegions();

  if (nRegions <= nProcesses)
  {
    this->AssignRegionsRoundRobin();
    this->RegionAssignment = ContiguousAssignment;
    return 0;
  }

  this->AllocateAndZeroRegionAssignmentLists();

  int floorLogP, ceilLogP;

  for (floorLogP = 0; (nProcesses >> floorLogP) > 0; floorLogP++)
  {
    // empty loop.
  }
  floorLogP--;

  int P = 1 << floorLogP;

  if (nProcesses == P)
  {
    ceilLogP = floorLogP;
  }
  else
  {
    ceilLogP = floorLogP + 1;
  }

  svtkKdNode** nodes = new svtkKdNode*[P];

  this->GetRegionsAtLevel(floorLogP, nodes);

  if (floorLogP == ceilLogP)
  {
    for (p = 0; p < nProcesses; p++)
    {
      this->AddProcessRegions(p, nodes[p]);
    }
  }
  else
  {
    int nodesLeft = 1 << ceilLogP;
    int procsLeft = nProcesses;
    int procId = 0;

    for (int i = 0; i < P; i++)
    {
      if (nodesLeft > procsLeft)
      {
        this->AddProcessRegions(procId, nodes[i]);

        procsLeft -= 1;
        procId += 1;
      }
      else
      {
        this->AddProcessRegions(procId, nodes[i]->GetLeft());
        this->AddProcessRegions(procId + 1, nodes[i]->GetRight());

        procsLeft -= 2;
        procId += 2;
      }
      nodesLeft -= 2;
    }
  }

  delete[] nodes;

  this->BuildRegionListsForProcesses();

  return 0;
}
void svtkPKdTree::BuildRegionListsForProcesses()
{
  int* count = new int[this->NumProcesses];

  for (int p = 0; p < this->NumProcesses; p++)
  {
    this->ProcessAssignmentMap[p].resize(this->NumRegionsAssigned[p]);

    count[p] = 0;
  }

  int regionAssignmentMapLength = this->GetRegionAssignmentMapLength();
  for (int r = 0; r < regionAssignmentMapLength; r++)
  {
    int proc = this->RegionAssignmentMap[r];
    int next = count[proc];

    this->ProcessAssignmentMap[proc][next] = r;

    count[proc] = next + 1;
  }

  delete[] count;
}

//--------------------------------------------------------------------
// Queries
//--------------------------------------------------------------------
int svtkPKdTree::FindNextLocalArrayIndex(
  const char* n, const std::vector<std::string>& names, int len, int start)
{
  int index = -1;
  size_t nsize = strlen(n);

  // normally a very small list, maybe 1 to 5 names

  for (int i = start; i < len; i++)
  {
    if (!strncmp(n, names[i].c_str(), nsize))
    {
      index = i;
      break;
    }
  }
  return index;
}
int svtkPKdTree::GetCellArrayGlobalRange(const char* n, double range[2])
{
  int first = 1;
  double tmp[2] = { 0, 0 };
  int start = 0;

  while (1)
  {
    // Cell array name may appear more than once if multiple datasets
    // were processed.

    int index =
      svtkPKdTree::FindNextLocalArrayIndex(n, this->CellDataName, this->NumCellArrays, start);

    if (index >= 0)
    {
      if (first)
      {
        this->GetCellArrayGlobalRange(index, range);
        first = 0;
      }
      else
      {
        this->GetCellArrayGlobalRange(index, tmp);
        range[0] = (tmp[0] < range[0]) ? tmp[0] : range[0];
        range[1] = (tmp[1] > range[1]) ? tmp[1] : range[1];
      }
      start = index + 1;
    }
    else
    {
      break;
    }
  }

  int fail = (first != 0);

  return fail;
}
int svtkPKdTree::GetCellArrayGlobalRange(const char* n, float range[2])
{
  double tmp[2] = { 0, 0 };

  int fail = this->GetCellArrayGlobalRange(n, tmp);

  if (!fail)
  {
    range[0] = (float)tmp[0];
    range[1] = (float)tmp[1];
  }

  return fail;
}
int svtkPKdTree::GetPointArrayGlobalRange(const char* n, double range[2])
{
  int first = 1;
  double tmp[2] = { 0, 0 };
  int start = 0;

  while (1)
  {
    // Point array name may appear more than once if multiple datasets
    // were processed.

    int index =
      svtkPKdTree::FindNextLocalArrayIndex(n, this->PointDataName, this->NumPointArrays, start);

    if (index >= 0)
    {
      if (first)
      {
        this->GetPointArrayGlobalRange(index, range);
        first = 0;
      }
      else
      {
        this->GetPointArrayGlobalRange(index, tmp);
        range[0] = (tmp[0] < range[0]) ? tmp[0] : range[0];
        range[1] = (tmp[1] > range[1]) ? tmp[1] : range[1];
      }
      start = index + 1;
    }
    else
    {
      break;
    }
  }

  int fail = (first != 0);

  return fail;
}
int svtkPKdTree::GetPointArrayGlobalRange(const char* n, float range[2])
{
  double tmp[2] = { 0, 0 };

  int fail = this->GetPointArrayGlobalRange(n, tmp);

  if (!fail)
  {
    range[0] = (float)tmp[0];
    range[1] = (float)tmp[1];
  }

  return fail;
}
int svtkPKdTree::GetCellArrayGlobalRange(int arrayIndex, float range[2])
{
  double tmp[2];
  int fail = this->GetCellArrayGlobalRange(arrayIndex, tmp);
  if (!fail)
  {
    range[0] = (float)tmp[0];
    range[1] = (float)tmp[1];
  }
  return fail;
}
int svtkPKdTree::GetCellArrayGlobalRange(int arrayIndex, double range[2])
{
  if (arrayIndex < 0 || arrayIndex >= this->NumCellArrays || this->CellDataMin.empty())
  {
    return 1;
  }

  range[0] = this->CellDataMin[arrayIndex];
  range[1] = this->CellDataMax[arrayIndex];

  return 0;
}
int svtkPKdTree::GetPointArrayGlobalRange(int arrayIndex, float range[2])
{
  double tmp[2];
  int fail = this->GetPointArrayGlobalRange(arrayIndex, tmp);
  if (!fail)
  {
    range[0] = (float)tmp[0];
    range[1] = (float)tmp[1];
  }
  return fail;
}
int svtkPKdTree::GetPointArrayGlobalRange(int arrayIndex, double range[2])
{
  if (arrayIndex < 0 || arrayIndex >= this->NumPointArrays || this->PointDataMin.empty())
  {
    return 1;
  }

  range[0] = this->PointDataMin[arrayIndex];
  range[1] = this->PointDataMax[arrayIndex];

  return 0;
}

int svtkPKdTree::ViewOrderAllProcessesInDirection(const double dop[3], svtkIntArray* orderedList)
{
  assert("pre: orderedList_exists" && orderedList != nullptr);

  svtkIntArray* regionList = svtkIntArray::New();

  this->ViewOrderAllRegionsInDirection(dop, regionList);

  orderedList->SetNumberOfValues(this->NumProcesses);

  int nextId = 0;

  // if regions were not assigned contiguously, this
  // produces the wrong result

  for (int r = 0; r < this->GetNumberOfRegions();)
  {
    int procId = this->RegionAssignmentMap[regionList->GetValue(r)];

    orderedList->SetValue(nextId++, procId);

    int nregions = this->NumRegionsAssigned[procId];

    r += nregions;
  }

  regionList->Delete();

  return this->NumProcesses;
}

int svtkPKdTree::ViewOrderAllProcessesFromPosition(const double pos[3], svtkIntArray* orderedList)
{
  assert("pre: orderedList_exists" && orderedList != nullptr);

  svtkIntArray* regionList = svtkIntArray::New();

  this->ViewOrderAllRegionsFromPosition(pos, regionList);

  orderedList->SetNumberOfValues(this->NumProcesses);

  int nextId = 0;

  // if regions were not assigned contiguously, this
  // produces the wrong result

  for (int r = 0; r < this->GetNumberOfRegions();)
  {
    int procId = this->RegionAssignmentMap[regionList->GetValue(r)];

    orderedList->SetValue(nextId++, procId);

    int nregions = this->NumRegionsAssigned[procId];

    r += nregions;
  }

  regionList->Delete();

  return this->NumProcesses;
}

int svtkPKdTree::GetRegionAssignmentList(int procId, svtkIntArray* list)
{
  if ((procId < 0) || (procId >= this->NumProcesses))
  {
    SVTKERROR("GetRegionAssignmentList - invalid process id");
    return 0;
  }

  if (this->RegionAssignmentMap.empty())
  {
    this->UpdateRegionAssignment();

    if (this->RegionAssignmentMap.empty())
    {
      return 0;
    }
  }

  int nregions = this->NumRegionsAssigned[procId];
  int* regionIds = &this->ProcessAssignmentMap[procId][0];

  list->Initialize();
  list->SetNumberOfValues(nregions);

  for (int i = 0; i < nregions; i++)
  {
    list->SetValue(i, regionIds[i]);
  }

  return nregions;
}

void svtkPKdTree::GetAllProcessesBorderingOnPoint(float x, float y, float z, svtkIntArray* list)
{
  svtkIntArray* regions = svtkIntArray::New();
  double* subRegionBounds;
  list->Initialize();

  for (int procId = 0; procId < this->NumProcesses; procId++)
  {
    this->GetRegionAssignmentList(procId, regions);

    int nSubRegions = this->MinimalNumberOfConvexSubRegions(regions, &subRegionBounds);

    double* b = subRegionBounds;

    for (int r = 0; r < nSubRegions; r++)
    {
      if ((((x == b[0]) || (x == b[1])) &&
            ((y >= b[2]) && (y <= b[3]) && (z >= b[4]) && (z <= b[5]))) ||
        (((y == b[2]) || (y == b[3])) &&
          ((x >= b[0]) && (x <= b[1]) && (z >= b[4]) && (z <= b[5]))) ||
        (((z == b[4]) || (z == b[5])) &&
          ((x >= b[0]) && (x <= b[1]) && (y >= b[2]) && (y <= b[3]))))
      {
        list->InsertNextValue(procId);
        break;
      }

      b += 6;
    }
  }

  regions->Delete();
}

int svtkPKdTree::GetProcessAssignedToRegion(int regionId)
{
  if (this->RegionAssignmentMap.empty() || (regionId < 0) ||
    (regionId >= this->GetNumberOfRegions()))
  {
    return -1;
  }

  return this->RegionAssignmentMap[regionId];
}
int svtkPKdTree::HasData(int processId, int regionId)
{
  if (this->DataLocationMap.empty() || (processId < 0) || (processId >= this->NumProcesses) ||
    (regionId < 0) || (regionId >= this->GetNumberOfRegions()))
  {
    SVTKERROR("HasData - invalid request");
    return 0;
  }

  int where = this->GetNumberOfRegions() * processId + regionId;

  return this->DataLocationMap[where];
}

int svtkPKdTree::GetTotalProcessesInRegion(int regionId)
{
  if (this->NumProcessesInRegion.empty() || (regionId < 0) ||
    (regionId >= this->GetNumberOfRegions()))
  {
    SVTKERROR("GetTotalProcessesInRegion - invalid request");
    return 0;
  }

  return this->NumProcessesInRegion[regionId];
}

int svtkPKdTree::GetProcessListForRegion(int regionId, svtkIntArray* processes)
{
  if (this->ProcessList.empty() || (regionId < 0) || (regionId >= this->GetNumberOfRegions()))
  {
    SVTKERROR("GetProcessListForRegion - invalid request");
    return 0;
  }

  int nProcesses = this->NumProcessesInRegion[regionId];

  for (int i = 0; i < nProcesses; i++)
  {
    processes->InsertNextValue(this->ProcessList[regionId][i]);
  }

  return nProcesses;
}

int svtkPKdTree::GetProcessesCellCountForRegion(int regionId, int* count, int len)
{
  if (this->CellCountList.empty() || (regionId < 0) || (regionId >= this->GetNumberOfRegions()))
  {
    SVTKERROR("GetProcessesCellCountForRegion - invalid request");
    return 0;
  }

  int nProcesses = this->NumProcessesInRegion[regionId];

  nProcesses = (len < nProcesses) ? len : nProcesses;

  for (int i = 0; i < nProcesses; i++)
  {
    count[i] = this->CellCountList[regionId][i];
  }

  return nProcesses;
}

int svtkPKdTree::GetProcessCellCountForRegion(int processId, int regionId)
{
  if (this->CellCountList.empty() || (regionId < 0) || (regionId >= this->GetNumberOfRegions()) ||
    (processId < 0) || (processId >= this->NumProcesses))
  {
    SVTKERROR("GetProcessCellCountForRegion - invalid request");
    return 0;
  }

  int nProcesses = this->NumProcessesInRegion[regionId];

  int which = -1;

  for (int i = 0; i < nProcesses; i++)
  {
    if (this->ProcessList[regionId][i] == processId)
    {
      which = i;
      break;
    }
  }

  if (which == -1)
  {
    return 0;
  }
  return this->CellCountList[regionId][which];
}

int svtkPKdTree::GetTotalRegionsForProcess(int processId)
{
  if (this->NumRegionsInProcess.empty() || (processId < 0) || (processId >= this->NumProcesses))
  {
    SVTKERROR("GetTotalRegionsForProcess - invalid request");
    return 0;
  }

  return this->NumRegionsInProcess[processId];
}

int svtkPKdTree::GetRegionListForProcess(int processId, svtkIntArray* regions)
{
  if (this->ParallelRegionList.empty() || (processId < 0) || (processId >= this->NumProcesses))
  {
    SVTKERROR("GetRegionListForProcess - invalid request");
    return 0;
  }

  int nRegions = this->NumRegionsInProcess[processId];

  for (int i = 0; i < nRegions; i++)
  {
    regions->InsertNextValue(this->ParallelRegionList[processId][i]);
  }

  return nRegions;
}
int svtkPKdTree::GetRegionsCellCountForProcess(int processId, int* count, int len)
{
  if (this->CellCountList.empty() || (processId < 0) || (processId >= this->NumProcesses))
  {
    SVTKERROR("GetRegionsCellCountForProcess - invalid request");
    return 0;
  }

  int nRegions = this->NumRegionsInProcess[processId];

  nRegions = (len < nRegions) ? len : nRegions;

  for (int i = 0; i < nRegions; i++)
  {
    int regionId = this->ParallelRegionList[processId][i];
    int iam;

    for (iam = 0; iam < this->NumProcessesInRegion[regionId]; iam++)
    {
      if (this->ProcessList[regionId][iam] == processId)
        break;
    }

    count[i] = this->CellCountList[regionId][iam];
  }
  return nRegions;
}
svtkIdType svtkPKdTree::GetCellListsForProcessRegions(
  int processId, int set, svtkIdList* inRegionCells, svtkIdList* onBoundaryCells)
{
  if ((set < 0) || (set >= this->GetNumberOfDataSets()))
  {
    svtkErrorMacro(<< "svtkPKdTree::GetCellListsForProcessRegions no such data set");
    return 0;
  }
  return this->GetCellListsForProcessRegions(
    processId, this->GetDataSet(set), inRegionCells, onBoundaryCells);
}
svtkIdType svtkPKdTree::GetCellListsForProcessRegions(
  int processId, svtkIdList* inRegionCells, svtkIdList* onBoundaryCells)
{
  return this->GetCellListsForProcessRegions(
    processId, this->GetDataSet(0), inRegionCells, onBoundaryCells);
}
svtkIdType svtkPKdTree::GetCellListsForProcessRegions(
  int processId, svtkDataSet* set, svtkIdList* inRegionCells, svtkIdList* onBoundaryCells)
{
  svtkIdType totalCells = 0;

  if ((inRegionCells == nullptr) && (onBoundaryCells == nullptr))
  {
    return totalCells;
  }

  // Get the list of regions owned by this process

  svtkIntArray* regions = svtkIntArray::New();

  int nregions = this->GetRegionAssignmentList(processId, regions);

  if (nregions == 0)
  {
    if (inRegionCells)
    {
      inRegionCells->Initialize();
    }
    if (onBoundaryCells)
    {
      onBoundaryCells->Initialize();
    }
    regions->Delete();
    return totalCells;
  }

  totalCells = this->GetCellLists(regions, set, inRegionCells, onBoundaryCells);

  regions->Delete();

  return totalCells;
}
void svtkPKdTree::PrintTiming(ostream& os, svtkIndent indent)
{
  os << indent << "Total cells in distributed data: " << this->TotalNumCells << endl;

  if (this->NumProcesses)
  {
    os << indent << "Average cells per processor: ";
    os << this->TotalNumCells / this->NumProcesses << endl;
  }
  svtkTimerLog::DumpLogWithIndents(&os, (float)0.0);
}
void svtkPKdTree::PrintTables(ostream& os, svtkIndent indent)
{
  int nregions = this->GetNumberOfRegions();
  int nprocs = this->NumProcesses;
  int r, p, n;

  if (!this->RegionAssignmentMap.empty())
  {
    int* map = &this->RegionAssignmentMap[0];
    int* num = &this->NumRegionsAssigned[0];
    int halfr = this->GetRegionAssignmentMapLength() / 2;
    int halfp = nprocs / 2;

    os << indent << "Region assignments:" << endl;
    for (r = 0; r < halfr; r++)
    {
      os << indent << "  region " << r << " to process " << map[r];
      os << "    region " << r + halfr << " to process " << map[r + halfr];
      os << endl;
    }
    for (p = 0; p < halfp; p++)
    {
      os << indent << "  " << num[p] << " regions to process " << p;
      os << "    " << num[p + halfp] << " regions to process " << p + halfp;
      os << endl;
    }
    if (nprocs > halfp * 2)
    {
      os << indent << "  " << num[nprocs - 1];
      os << " regions to process " << nprocs - 1 << endl;
    }
  }

  if (!this->ProcessList.empty())
  {
    os << indent << "Processes holding data for each region:" << endl;
    for (r = 0; r < nregions; r++)
    {
      n = this->NumProcessesInRegion[r];

      os << indent << " region " << r << " (" << n << " processes): ";

      for (p = 0; p < n; p++)
      {
        if (p && (p % 10 == 0))
          os << endl << indent << "   ";
        os << this->ProcessList[r][p] << " ";
      }
      os << endl;
    }
  }
  if (!this->ParallelRegionList.empty())
  {
    os << indent << "Regions held by each process:" << endl;
    for (p = 0; p < nprocs; p++)
    {
      n = this->NumRegionsInProcess[p];

      os << indent << " process " << p << " (" << n << " regions): ";

      for (r = 0; r < n; r++)
      {
        if (r && (r % 10 == 0))
          os << endl << indent << "   ";
        os << this->ParallelRegionList[p][r] << " ";
      }
      os << endl;
    }
  }
  if (!this->CellCountList.empty())
  {
    os << indent << "Number of cells per process per region:" << endl;
    for (r = 0; r < nregions; r++)
    {
      n = this->NumProcessesInRegion[r];

      os << indent << " region: " << r << "  ";
      for (p = 0; p < n; p++)
      {
        if (p && (p % 5 == 0))
          os << endl << indent << "   ";
        os << this->ProcessList[r][p] << " - ";
        os << this->CellCountList[r][p] << " cells, ";
      }
      os << endl;
    }
  }
}

void svtkPKdTree::StrDupWithNew(const char* s, std::string& output)
{
  if (s)
  {
    size_t len = strlen(s);
    if (len == 0)
    {
      output.resize(1);
      output[0] = '\0';
    }
    else
    {
      output = s;
    }
  }
  else
  {
    output.clear();
  }
}

void svtkPKdTree::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "RegionAssignment: " << this->RegionAssignment << endl;

  os << indent << "Controller: " << this->Controller << endl;
  os << indent << "SubGroup: " << this->SubGroup << endl;
  os << indent << "NumProcesses: " << this->NumProcesses << endl;
  os << indent << "MyId: " << this->MyId << endl;

  os << indent << "RegionAssignmentMap (size): " << this->RegionAssignmentMap.size() << endl;
  os << indent << "NumRegionsAssigned (size): " << this->NumRegionsAssigned.size() << endl;
  os << indent << "NumProcessesInRegion (size): " << this->NumProcessesInRegion.size() << endl;
  os << indent << "ProcessList (size): " << this->ProcessList.size() << endl;
  os << indent << "NumRegionsInProcess (size): " << this->NumRegionsInProcess.size() << endl;
  os << indent << "ParallelRegionList (size): " << this->ParallelRegionList.size() << endl;
  os << indent << "CellCountList (size): " << this->CellCountList.size() << endl;

  os << indent << "StartVal (size): " << this->StartVal.size() << endl;
  os << indent << "EndVal (size): " << this->EndVal.size() << endl;
  os << indent << "NumCells (size): " << this->NumCells.size() << endl;
  os << indent << "TotalNumCells: " << this->TotalNumCells << endl;

  os << indent << "PtArray: " << this->PtArray << endl;
  os << indent << "PtArray2: " << this->PtArray2 << endl;
  os << indent << "CurrentPtArray: " << this->CurrentPtArray << endl;
  os << indent << "NextPtArray: " << this->NextPtArray << endl;
  os << indent << "SelectBuffer (size): " << this->SelectBuffer.size() << endl;
}