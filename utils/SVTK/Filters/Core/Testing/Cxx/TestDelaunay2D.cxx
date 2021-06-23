/*=========================================================================

  Program:   Visualization Toolkit
  Module:    TestDelaunay2D.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

// This test was created following the bug reported by Gilles Rougeron.
// Some points were not connected in the output triangulation.
// A fix was added to svtkDelaunay2D. This test exercices the new
// functionality.

#include "svtkActor.h"
#include "svtkCellArray.h"
#include "svtkDelaunay2D.h"
#include "svtkPolyData.h"
#include "svtkPolyDataMapper.h"
#include "svtkRegressionTestImage.h"
#include "svtkRenderWindow.h"
#include "svtkRenderWindowInteractor.h"
#include "svtkRenderer.h"
#include "svtkShrinkPolyData.h"

//#define WRITE_IMAGE

#ifdef WRITE_IMAGE
#include "svtkPNGWriter.h"
#include "svtkWindowToImageFilter.h"
#endif

int TestDelaunay2D(int argc, char* argv[])
{
  svtkPoints* newPts = svtkPoints::New();
  newPts->InsertNextPoint(1.5026018771810041, 1.5026019428618222, 0.0);
  newPts->InsertNextPoint(-1.5026020085426373, 1.5026018115001829, 0.0);
  newPts->InsertNextPoint(-1.5026018353814194, -1.5026019846614038, 0.0);
  newPts->InsertNextPoint(1.5026019189805875, -1.5026019010622396, 0.0);
  newPts->InsertNextPoint(5.2149123972752491, 5.2149126252263240, 0.0);
  newPts->InsertNextPoint(-5.2149128531773883, 5.2149121693241645, 0.0);
  newPts->InsertNextPoint(-5.2149122522061022, -5.2149127702954603, 0.0);
  newPts->InsertNextPoint(5.2149125423443916, -5.2149124801571842, 0.0);
  newPts->InsertNextPoint(8.9272229173694946, 8.9272233075908254, 0.0);
  newPts->InsertNextPoint(-8.9272236978121402, 8.9272225271481460, 0.0);
  newPts->InsertNextPoint(-8.9272226690307868, -8.9272235559295172, 0.0);
  newPts->InsertNextPoint(8.9272231657081953, -8.9272230592521282, 0.0);
  newPts->InsertNextPoint(12.639533437463740, 12.639533989955329, 0.0);
  newPts->InsertNextPoint(-12.639534542446890, 12.639532884972127, 0.0);
  newPts->InsertNextPoint(-12.639533085855469, -12.639534341563573, 0.0);
  newPts->InsertNextPoint(12.639533789072001, -12.639533638347073, 0.0);

  svtkIdType inNumPts = newPts->GetNumberOfPoints();
  cout << "input numPts= " << inNumPts << endl;

  svtkPolyData* pointCloud = svtkPolyData::New();
  pointCloud->SetPoints(newPts);
  newPts->Delete();

  svtkDelaunay2D* delaunay2D = svtkDelaunay2D::New();
  delaunay2D->SetInputData(pointCloud);
  pointCloud->Delete();
  delaunay2D->Update();

  svtkPolyData* triangulation = delaunay2D->GetOutput();

  svtkIdType outNumPts = triangulation->GetNumberOfPoints();
  svtkIdType outNumCells = triangulation->GetNumberOfCells();
  svtkIdType outNumPolys = triangulation->GetNumberOfPolys();
  svtkIdType outNumLines = triangulation->GetNumberOfLines();
  svtkIdType outNumVerts = triangulation->GetNumberOfVerts();

  cout << "output numPts= " << outNumPts << endl;
  cout << "output numCells= " << outNumCells << endl;
  cout << "output numPolys= " << outNumPolys << endl;
  cout << "output numLines= " << outNumLines << endl;
  cout << "output numVerts= " << outNumVerts << endl;

  if (outNumPts != inNumPts)
  {
    cout << "ERROR: output numPts " << outNumPts << " doesn't match input numPts=" << inNumPts
         << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  if (!outNumCells)
  {
    cout << "ERROR: output numCells= " << outNumCells << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  if (outNumPolys != outNumCells)
  {
    cout << "ERROR: output numPolys= " << outNumPolys
         << " doesn't match output numCells= " << outNumCells << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  if (outNumLines)
  {
    cout << "ERROR: output numLines= " << outNumLines << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  if (outNumVerts)
  {
    cout << "ERROR: output numVerts= " << outNumVerts << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  // check that every point is connected
  triangulation->BuildLinks();

  svtkIdList* cellIds = svtkIdList::New();
  svtkIdType numUnconnectedPts = 0;

  for (svtkIdType ptId = 0; ptId < outNumPts; ptId++)
  {
    triangulation->GetPointCells(ptId, cellIds);
    if (!cellIds->GetNumberOfIds())
    {
      numUnconnectedPts++;
    }
  }

  cellIds->Delete();

  cout << "Triangulation has " << numUnconnectedPts << " unconnected points" << endl;

  if (numUnconnectedPts)
  {
    cout << "ERROR: Triangulation has " << numUnconnectedPts << " unconnected points" << endl;
    delaunay2D->Delete();
    return EXIT_FAILURE;
  }

  svtkShrinkPolyData* shrink = svtkShrinkPolyData::New();
  shrink->SetInputConnection(delaunay2D->GetOutputPort());

  svtkPolyDataMapper* mapper = svtkPolyDataMapper::New();
  mapper->SetInputConnection(shrink->GetOutputPort());

  svtkActor* actor = svtkActor::New();
  actor->SetMapper(mapper);

  svtkRenderer* ren = svtkRenderer::New();
  ren->AddActor(actor);

  svtkRenderWindow* renWin = svtkRenderWindow::New();
  renWin->AddRenderer(ren);

  svtkRenderWindowInteractor* iren = svtkRenderWindowInteractor::New();
  iren->SetRenderWindow(renWin);

  iren->Initialize();

  renWin->Render();
#ifdef WRITE_IMAGE
  svtkWindowToImageFilter* windowToImage = svtkWindowToImageFilter::New();
  windowToImage->SetInput(renWin);

  svtkPNGWriter* PNGWriter = svtkPNGWriter::New();
  PNGWriter->SetInputConnection(windowToImage->GetOutputPort());
  windowToImage->Delete();
  PNGWriter->SetFileName("TestDelaunay2D.png");
  PNGWriter->Write();
  PNGWriter->Delete();
#endif
  int retVal = svtkRegressionTestImage(renWin);
  if (retVal == svtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }

  // Clean up
  delaunay2D->Delete();
  shrink->Delete();
  mapper->Delete();
  actor->Delete();
  ren->Delete();
  renWin->Delete();
  iren->Delete();

  return !retVal;
}