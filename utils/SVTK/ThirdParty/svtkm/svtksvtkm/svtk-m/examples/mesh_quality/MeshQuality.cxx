//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//  Copyright 2018 UT-Battelle, LLC.
//  Copyright 2018 Los Alamos National Security.
//
//  Under the terms of Contract DE-NA0003525 with NTESS,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================

#include <string>
#include <vector>
#include <svtkm/cont/DataSet.h>
#include <svtkm/cont/DataSetBuilderExplicit.h>
#include <svtkm/cont/testing/Testing.h>
#include <svtkm/filter/MeshQuality.h>
#include <svtkm/io/reader/SVTKDataSetReader.h>
#include <svtkm/io/writer/SVTKDataSetWriter.h>


/**
 * This example program takes an input SVTK unstructured grid file and computes
 * the mesh quality of its cells. The mesh quality of a cell type (e.g., tetrahedron,
 * hexahedron, triangle) is defined by a metric, which is user-specified.
 * The summary statistics of this metric, computed over all cells of its assigned type,
 * are written as a new field in the output dataset. There is an output mesh quality
 * field for each different cell type that has been assigned a non-empty quality metric
 * by the user. Additionally, an ending field is written with the output metric value for
 * each cell in the input dataset. The output dataset is named according to the
 * user-provided input parameter to this file, or "output.svtk" by default if no parameter
 * is provided.
*/

//Adapted from svtkm/cont/testing/MakeTestDataSet.h
//Modified the content of the Make3DExplicitDataSetZoo() function
inline svtkm::cont::DataSet Make3DExplicitDataSet()
{
  svtkm::cont::DataSet dataSet;
  svtkm::cont::DataSetBuilderExplicit dsb;

  using CoordType = svtkm::Vec3f_64;

  std::vector<CoordType> coords = {
    { 0.00, 0.00, 0.00 }, { 1.00, 0.00, 0.00 }, { 2.00, 0.00, 0.00 }, { 0.00, 0.00, 1.00 },
    { 1.00, 0.00, 1.00 }, { 2.00, 0.00, 1.00 }, { 0.00, 1.00, 0.00 }, { 1.00, 1.00, 0.00 },
    { 2.00, 1.00, 0.00 }, { 0.00, 1.00, 1.00 }, { 1.00, 1.00, 1.00 }, { 2.00, 1.00, 1.00 },
    { 0.00, 2.00, 0.00 }, { 1.00, 2.00, 0.00 }, { 2.00, 2.00, 0.00 }, { 0.00, 2.00, 1.00 },
    { 1.00, 2.00, 1.00 }, { 2.00, 2.00, 1.00 }, { 1.00, 3.00, 1.00 }, { 2.75, 0.00, 1.00 },
    { 3.00, 0.00, 0.75 }, { 3.00, 0.25, 1.00 }, { 3.00, 1.00, 1.00 }, { 3.00, 1.00, 0.00 },
    { 2.57, 2.00, 1.00 }, { 3.00, 1.75, 1.00 }, { 3.00, 1.75, 0.75 }, { 3.00, 0.00, 0.00 },
    { 2.57, 0.42, 0.57 }, { 2.59, 1.43, 0.71 }
  };

  std::vector<svtkm::UInt8> shapes;
  std::vector<svtkm::IdComponent> numindices;
  std::vector<svtkm::Id> conn;

  //Construct the shapes/cells of the dataset
  //This is a zoo of points, lines, polygons, and polyhedra
  shapes.push_back(svtkm::CELL_SHAPE_HEXAHEDRON);
  numindices.push_back(8);
  conn.push_back(0);
  conn.push_back(3);
  conn.push_back(4);
  conn.push_back(1);
  conn.push_back(6);
  conn.push_back(9);
  conn.push_back(10);
  conn.push_back(7);

  shapes.push_back(svtkm::CELL_SHAPE_HEXAHEDRON);
  numindices.push_back(8);
  conn.push_back(1);
  conn.push_back(4);
  conn.push_back(5);
  conn.push_back(2);
  conn.push_back(7);
  conn.push_back(10);
  conn.push_back(11);
  conn.push_back(8);

  shapes.push_back(svtkm::CELL_SHAPE_TETRA);
  numindices.push_back(4);
  conn.push_back(24);
  conn.push_back(26);
  conn.push_back(25);
  conn.push_back(29);

  shapes.push_back(svtkm::CELL_SHAPE_TETRA);
  numindices.push_back(4);
  conn.push_back(8);
  conn.push_back(17);
  conn.push_back(11);
  conn.push_back(29);

  shapes.push_back(svtkm::CELL_SHAPE_PYRAMID);
  numindices.push_back(5);
  conn.push_back(24);
  conn.push_back(17);
  conn.push_back(8);
  conn.push_back(23);
  conn.push_back(29);

  shapes.push_back(svtkm::CELL_SHAPE_PYRAMID);
  numindices.push_back(5);
  conn.push_back(25);
  conn.push_back(22);
  conn.push_back(11);
  conn.push_back(17);
  conn.push_back(29);

  shapes.push_back(svtkm::CELL_SHAPE_WEDGE);
  numindices.push_back(6);
  conn.push_back(8);
  conn.push_back(14);
  conn.push_back(17);
  conn.push_back(7);
  conn.push_back(13);
  conn.push_back(16);

  shapes.push_back(svtkm::CELL_SHAPE_WEDGE);
  numindices.push_back(6);
  conn.push_back(11);
  conn.push_back(8);
  conn.push_back(17);
  conn.push_back(10);
  conn.push_back(7);
  conn.push_back(16);

  shapes.push_back(svtkm::CELL_SHAPE_VERTEX);
  numindices.push_back(1);
  conn.push_back(0);

  shapes.push_back(svtkm::CELL_SHAPE_VERTEX);
  numindices.push_back(1);
  conn.push_back(29);

  shapes.push_back(svtkm::CELL_SHAPE_LINE);
  numindices.push_back(2);
  conn.push_back(0);
  conn.push_back(1);

  shapes.push_back(svtkm::CELL_SHAPE_LINE);
  numindices.push_back(2);
  conn.push_back(11);
  conn.push_back(15);

  shapes.push_back(svtkm::CELL_SHAPE_TRIANGLE);
  numindices.push_back(3);
  conn.push_back(2);
  conn.push_back(4);
  conn.push_back(15);

  shapes.push_back(svtkm::CELL_SHAPE_TRIANGLE);
  numindices.push_back(3);
  conn.push_back(5);
  conn.push_back(6);
  conn.push_back(7);

  shapes.push_back(svtkm::CELL_SHAPE_QUAD);
  numindices.push_back(4);
  conn.push_back(0);
  conn.push_back(3);
  conn.push_back(5);
  conn.push_back(2);

  shapes.push_back(svtkm::CELL_SHAPE_QUAD);
  numindices.push_back(4);
  conn.push_back(5);
  conn.push_back(4);
  conn.push_back(10);
  conn.push_back(11);

  shapes.push_back(svtkm::CELL_SHAPE_POLYGON);
  numindices.push_back(3);
  conn.push_back(4);
  conn.push_back(7);
  conn.push_back(1);

  shapes.push_back(svtkm::CELL_SHAPE_POLYGON);
  numindices.push_back(4);
  conn.push_back(1);
  conn.push_back(6);
  conn.push_back(7);
  conn.push_back(2);

  dataSet = dsb.Create(coords, shapes, numindices, conn, "coordinates");

  return dataSet;
}

int TestMetrics(const char* outFileName,
                svtkm::cont::DataSet data,
                svtkm::filter::MeshQuality& filter)
{
  svtkm::cont::DataSet outputData;
  try
  {
    svtkm::io::writer::SVTKDataSetWriter writer("testZoo_withPolygons.svtk");
    writer.WriteDataSet(data);

    outputData = filter.Execute(data);
    std::cout << "filter finished\n";
  }
  catch (svtkm::cont::ErrorExecution&)
  {
    //TODO: need to add something else here...
    std::cerr << "Error occured while executing the filter. Exiting" << std::endl;
    return 1;
  }
  try
  {
    svtkm::io::writer::SVTKDataSetWriter writer(outFileName);
    writer.WriteDataSet(outputData);
    std::cout << "finished writing data\n";
  }
  catch (svtkm::io::ErrorIO&)
  {
    //TODO: need to add something else here...
    std::cerr << "Error occured while writing the output data set. Exiting" << std::endl;
    return 1;
  }
  return 0;
}

int main(int argc, char* argv[])
{
  //
  //  Check usage, set filename and read input
  //
  const char* outFileName = nullptr;
  switch (argc)
  {
    case 1:
      std::cerr << "Usage: " << std::endl
                << "$ " << argv[0] << " <input.svtk_file> <output.svtk_fileName>" << std::endl;
      return 1;
    case 2:
      outFileName = "output.svtk";
      break;
    default:
      outFileName = argv[argc - 1];
      break;
  }
  svtkm::cont::DataSet input;
  svtkm::io::reader::SVTKDataSetReader reader(argv[1]);


  // A cell metric is now computed for every shape type that exists in the
  // input dataset.
  svtkm::filter::CellMetric shapeMetric = svtkm::filter::CellMetric::VOLUME;

  try
  {
    input = reader.ReadDataSet(); //FIELD not supported errors here, but doesnt affect data
    //input = Make3DExplicitDataSet();
  }
  catch (svtkm::io::ErrorIO&)
  {
    std::cerr << "Error occured while reading input. Exiting" << std::endl;
    return 1;
  }

  svtkm::filter::MeshQuality filter(shapeMetric);
  TestMetrics(outFileName, input, filter);
  return 0;
}