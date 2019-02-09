#include "ProgrammableDataAdaptor.h"
#include "MeshMetadata.h"
#include "Histogram.h"

#include <vtkDataObject.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkDoubleArray.h>

#include <vector>
#include <iostream>

#include <mpi.h>

using std::cerr;
using std::endl;

int main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);

  //
  //                                              *
  //                                              * *
  //                                            * * *
  //                                            * * * *
  //                                          * * * * *
  //                                        * * * * * * *
  std::vector<unsigned int> baselineHist = {1,2,4,6,5,3,1};
  //                                        0,1,2,3,4,5,6

  std::vector<double> data = {0, 1,1, 2,2,2,2,
    3,3,3,3,3,3, 4,4,4,4,4, 5,5,5, 6};

  // get number of meshes callback
  auto getNumberOfMeshes = [](unsigned int &n) -> int
    {
    cerr << "===getNumberOfMeshes" << endl;
    n = 1;
    return 0;
    };

  // get mesh name callback
  auto getMeshMetadata = [](unsigned int id, sensei::MeshMetadataPtr &metadata) -> int
    {
    cerr << "===getMeshMetadata" << endl;
    if (id == 0)
      {
      metadata->MeshName = "image";
      metadata->MeshType = VTK_IMAGE_DATA;
      metadata->BlockType = VTK_IMAGE_DATA;
      metadata->NumBlocks = 1;
      metadata->NumBlocksLocal = {1};
      metadata->NumArrays = 1;
      metadata->ArrayName = {"data"};
      metadata->ArrayCentering = {vtkDataObject::POINT};
      metadata->ArrayType = {VTK_DOUBLE};
      metadata->ArrayComponents = {1};
      return 0;
      }
    return -1;
    };

  // get mesh callback
  auto getMesh = [&data](const std::string &meshName,
    bool, vtkDataObject *&mesh) -> int
    {
    cerr << "===getMesh" << endl;
    if (meshName == "image")
      {
      vtkImageData *im = vtkImageData::New();
      im->SetDimensions(data.size(), 1, 1);
      mesh = im;
      return 0;
      }
    return -1;
    };

  // add array callback
  auto addArray = [&data](vtkDataObject *mesh,
    const std::string &meshName, int assoc, const std::string &name) -> int
    {
    cerr << "===addArray" << endl;
    if ((meshName == "image") && (assoc == vtkDataObject::POINT) && (name == "data"))
      {
      vtkDoubleArray *da = vtkDoubleArray::New();
      da->SetName("data");
      da->SetArray(data.data(), data.size(), 1);

      static_cast<vtkImageData*>(mesh)->GetPointData()->AddArray(da);
      da->Delete();
      return 0;
      }
    return -1;
    };

  // release data callback
  auto releaseData = []() -> int
    {
    cerr << "===releaseData" << endl;
    return 0;
    };

  sensei::ProgrammableDataAdaptor *pda = sensei::ProgrammableDataAdaptor::New();
  pda->SetGetNumberOfMeshesCallback(getNumberOfMeshes);
  pda->SetGetMeshMetadataCallback(getMeshMetadata);
  pda->SetGetMeshCallback(getMesh);
  pda->SetAddArrayCallback(addArray);
  pda->SetReleaseDataCallback(releaseData);


  sensei::MeshMetadataPtr mmd = sensei::MeshMetadata::New();
  pda->GetMeshMetadata(0, mmd);

  sensei::Histogram *ha = sensei::Histogram::New();
  ha->Initialize(7, mmd->MeshName, mmd->ArrayCentering[0], mmd->ArrayName[0], "");
  ha->Execute(pda);

  pda->ReleaseData();

  double min = 0.0;
  double max = 0.0;
  std::vector<unsigned int> hist;
  ha->GetHistogram(min, max, hist);

  int result = -1;
  if (hist == baselineHist)
    result = 0;

  pda->Delete();
  ha->Delete();

  MPI_Finalize();

  return result;
}
