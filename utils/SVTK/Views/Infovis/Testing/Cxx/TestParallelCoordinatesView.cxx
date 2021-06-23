#include <svtkVersion.h>

#include <svtkFloatArray.h>
#include <svtkParallelCoordinatesRepresentation.h>
#include <svtkParallelCoordinatesView.h>
#include <svtkPointData.h>
#include <svtkPolyData.h>
#include <svtkRenderWindow.h>
#include <svtkRenderWindowInteractor.h>
#include <svtkSmartPointer.h>

int TestParallelCoordinatesView(int, char*[])
{
  int curves = 1;

  svtkSmartPointer<svtkFloatArray> array1 = svtkSmartPointer<svtkFloatArray>::New();
  array1->SetName("Array1");
  array1->SetNumberOfComponents(1);
  array1->InsertNextValue(0);
  array1->InsertNextValue(1);
  array1->InsertNextValue(2);
  array1->InsertNextValue(3);
  array1->InsertNextValue(4);

  svtkSmartPointer<svtkFloatArray> array2 = svtkSmartPointer<svtkFloatArray>::New();
  array2->SetName("Array2");
  array2->SetNumberOfComponents(1);
  array2->InsertNextValue(-0);
  array2->InsertNextValue(-1);
  array2->InsertNextValue(-2);
  array2->InsertNextValue(-3);
  array2->InsertNextValue(-4);

  svtkSmartPointer<svtkFloatArray> array3 = svtkSmartPointer<svtkFloatArray>::New();
  array3->SetName("Array3");
  array3->SetNumberOfComponents(1);
  array3->InsertNextValue(0);
  array3->InsertNextValue(1);
  array3->InsertNextValue(4);
  array3->InsertNextValue(9);
  array3->InsertNextValue(16);

  svtkSmartPointer<svtkFloatArray> array4 = svtkSmartPointer<svtkFloatArray>::New();
  array4->SetName("Array4");
  array4->SetNumberOfComponents(1);
  array4->InsertNextValue(0);
  array4->InsertNextValue(2);
  array4->InsertNextValue(4);
  array4->InsertNextValue(6);
  array4->InsertNextValue(8);

  svtkSmartPointer<svtkFloatArray> array5 = svtkSmartPointer<svtkFloatArray>::New();
  array5->SetName("Array5");
  array5->SetNumberOfComponents(1);
  array5->InsertNextValue(0);
  array5->InsertNextValue(1);
  array5->InsertNextValue(0.5);
  array5->InsertNextValue(0.33);
  array5->InsertNextValue(0.25);

  svtkSmartPointer<svtkFloatArray> array6 = svtkSmartPointer<svtkFloatArray>::New();
  array6->SetName("Array6");
  array6->SetNumberOfComponents(1);
  array6->InsertNextValue(3);
  array6->InsertNextValue(6);
  array6->InsertNextValue(2);
  array6->InsertNextValue(4);
  array6->InsertNextValue(9);

  svtkSmartPointer<svtkPolyData> polydata = svtkSmartPointer<svtkPolyData>::New();
  polydata->GetPointData()->AddArray(array1);
  polydata->GetPointData()->AddArray(array2);
  polydata->GetPointData()->AddArray(array3);
  polydata->GetPointData()->AddArray(array4);
  polydata->GetPointData()->AddArray(array5);
  polydata->GetPointData()->AddArray(array6);

  // Set up the parallel coordinates representation to be used in the View
  svtkSmartPointer<svtkParallelCoordinatesRepresentation> rep =
    svtkSmartPointer<svtkParallelCoordinatesRepresentation>::New();
  rep->SetInputData(polydata);

  // List all of the attribute arrays you want plotted in parallel coordinates
  rep->SetInputArrayToProcess(0, 0, 0, 0, "Array1");
  rep->SetInputArrayToProcess(1, 0, 0, 0, "Array2");
  rep->SetInputArrayToProcess(2, 0, 0, 0, "Array3");
  rep->SetInputArrayToProcess(3, 0, 0, 0, "Array4");
  rep->SetInputArrayToProcess(4, 0, 0, 0, "Array5");
  rep->SetInputArrayToProcess(5, 0, 0, 0, "Array6");

  rep->SetUseCurves(curves);
  rep->SetLineOpacity(0.5);

  // Set up the Parallel Coordinates View and hook in the Representation
  svtkSmartPointer<svtkParallelCoordinatesView> view =
    svtkSmartPointer<svtkParallelCoordinatesView>::New();
  view->SetRepresentation(rep);
  view->SetInspectMode(1);

  // Brush Mode determines the type of interaction you perform to select data
  view->SetBrushModeToLasso();
  view->SetBrushOperatorToReplace();

  // Set up render window
  view->GetRenderWindow()->SetSize(600, 300);
  view->ResetCamera();
  view->Render();

  // Start interaction event loop
  view->GetInteractor()->Start();

  return EXIT_SUCCESS;
}