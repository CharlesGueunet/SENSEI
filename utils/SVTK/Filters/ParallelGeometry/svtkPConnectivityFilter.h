/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkPConnectivityFilter.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   svtkPConnectivityFilter
 * @brief   Parallel version of svtkConnectivityFilter
 *
 * This class computes connectivity of a distributed data set in parallel.
 *
 * Problem
 * =======
 *
 * Datasets are distributed among ranks in a distributed process (Figure 1).
 * svtkConnectivityFilter already runs in parallel on each piece in a typical
 * SVTK application run with MPI, but it does not produce correct results. As
 * Figure 2 shows, distributed pieces of each connected component may end up
 * with different labels.
 *
 * ![Figure 1: Pieces in a distributed data set colored by processor
 * rank.](svtkPConnectivityFilterFigure1.png)
 *
 * ![Figure 2: Left). Incorrect parallel labeling. Right). Correct
 * labeling.](svtkPConnectivityFilterFigure2.png)
 *
 * The part missing from a fully parallel connectivity filter implementation is
 * the identification of which pieces on different ranks are actually connected.
 * This parallel filter provides that missing piece.
 *
 * Approach
 * ========
 *
 * Run svtkConnectivityFilter on each rank’s piece and resolve the connected
 * pieces afterwards. The implementation uses svtkMPIProcessController to
 * communicate among processes.
 *
 * Steps in the svtkPConnectivityFilter
 * -----------------------------------
 *
 * ### High-level steps
 *
 * + Run local connectivity algorithm.
 *
 * + Identify region connections across ranks and create a graph of these links.
 *
 * + Run a connected components algorithm on the graph created in the previous
 *   step to unify the regions across ranks.
 *
 * + Relabel cells and points with their “global” RegionIds.
 *
 * ### Low-level steps
 *
 * + In GenerateData(), invoke the superclass’s GenerateData() method. Make
 * temporary changes to extract all connected regions - we’ll handle the
 * different extraction modes at the end. Example results on 3 ranks are shown
 * in Figure 3 where color indicates RegionId computed by svtkConnectivityFilter.
 *
 * + Check for errors in superclass GenerateData() on any rank and exit the
 * algorithm if any encountered an error-indicating return code.
 *
 * ![Figure 3: Results after svtkConnectivityFilter superclass is called on each
 * piece.](svtkPConnectivityFilterFigure3.png)
 *
 * + AllGatherv the number of connected RegionIds from each rank and AllGatherv
 * the RegionIds themselves.
 *
 * + Gather all axis-aligned bounding boxes from all other ranks. This is used
 * to compute potential neighbors with which each rank should exchange points and
 * RegionIds.
 *
 * ![Figure 4: Point and associated RegionId exchange.](svtkPConnectivityFilterFigure4.png)
 *
 * + Each rank gathers up points potentially coincident with points on neighboring
 * ranks and sends them to their neighbors as well
 * as the RegionId assigned to each point.
 *
 * + Each rank runs through the received points and determines which points it owns
 * using a locator object. If a point is found on the local rank, add the
 * RegionId from the remote point to a set associated with the local
 * RegionId. This signifies that the local RegionId is connected to the remote
 * RegionId associated with the point.
 *
 * + Each rank gathers the local-RegionId-to-remote-RegionId links from all
 * other ranks.
 *
 * + From these links, each rank generates a graph structure of the global
 * links. The graph structure is identical on all ranks. (Optimization
 * opportunity: To reduce communication, this link exchange could be avoided and
 * the graph could be made distributed. This is just more complicated to
 * program, however).
 *
 * ![Figure 5: Connected region graph depicted by black line
 * segments.](svtkPConnectivityFilterFigure5.png)
 *
 * + Run a connected components algorithm that relabels the RegionIds, yielding
 * the full connectivity graph across ranks. Figure 6 shows an example result.
 *
 * + Relabel the remaining RegionIds by a contiguous set of RegionIds (e.g., go
 * from [0, 5, 8, 9] to [0, 1, 2, 3]).
 *
 * ![Figure 6: Connected components of graph linking RegionIds across
 * ranks.](svtkPConnectivityFilterFigure6.png)
 *
 * + From the RegionId graph, relabel points and cells in the output. The result
 * is shown in Figure 7.
 *
 * ![Figure 7: Dataset relabeled with global connected
 * RegionIds.](svtkPConnectivityFilterFigure7.png)
 *
 * + Handle ScalarConnectivy option and ExtractionMode after full region
 * connectivity is determined by identifying the correct RegionId and extracting
 * it by thresholding.
 *
 * Caveats
 * =======
 *
 * This parallel implementation does not support a number of features that the
 * svtkConnectivityFilter class supports, including:
 *
 *   - ScalarConnectivity
 *   - SVTK_EXTRACT_POINT_SEEDED_REGIONS extraction mode
 *   - SVTK_EXTRACT_CELL_SEEDED_REGIONS extraction mode
 *   - SVTK_EXTRACT_SPECIFIED_REGIONS extraction mode
 */

#ifndef svtkPConnectivityFilter_h
#define svtkPConnectivityFilter_h

#include "svtkConnectivityFilter.h"
#include "svtkFiltersParallelGeometryModule.h" // For export macro

class SVTKFILTERSPARALLELGEOMETRY_EXPORT svtkPConnectivityFilter : public svtkConnectivityFilter
{
public:
  svtkTypeMacro(svtkPConnectivityFilter, svtkConnectivityFilter);
  void PrintSelf(ostream& os, svtkIndent indent) override;

  static svtkPConnectivityFilter* New();

protected:
  svtkPConnectivityFilter();
  ~svtkPConnectivityFilter() override;

  int RequestData(svtkInformation*, svtkInformationVector**, svtkInformationVector*) override;

private:
  svtkPConnectivityFilter(const svtkPConnectivityFilter&) = delete;
  void operator=(const svtkPConnectivityFilter&) = delete;
};

#endif