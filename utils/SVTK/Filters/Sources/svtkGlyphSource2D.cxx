/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkGlyphSource2D.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "svtkGlyphSource2D.h"

#include "svtkCellArray.h"
#include "svtkCellData.h"
#include "svtkIdList.h"
#include "svtkInformation.h"
#include "svtkInformationVector.h"
#include "svtkMath.h"
#include "svtkObjectFactory.h"
#include "svtkPolyData.h"
#include "svtkUnsignedCharArray.h"

svtkStandardNewMacro(svtkGlyphSource2D);

//----------------------------------------------------------------------------
svtkGlyphSource2D::svtkGlyphSource2D()
{
  this->Center[0] = 0.0;
  this->Center[1] = 0.0;
  this->Center[2] = 0.0;
  this->Scale = 1.0;
  this->Scale2 = 1.5;
  this->Color[0] = 1.0;
  this->Color[1] = 1.0;
  this->Color[2] = 1.0;
  this->Filled = 1;
  this->Cross = 0;
  this->Dash = 0;
  this->RotationAngle = 0.0;
  this->Resolution = 8;
  this->OutputPointsPrecision = SINGLE_PRECISION;
  this->GlyphType = SVTK_VERTEX_GLYPH;

  this->SetNumberOfInputPorts(0);
}

//----------------------------------------------------------------------------
int svtkGlyphSource2D::RequestData(svtkInformation* svtkNotUsed(request),
  svtkInformationVector** svtkNotUsed(inputVector), svtkInformationVector* outputVector)
{
  // get the info object
  svtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the output
  svtkPolyData* output = svtkPolyData::SafeDownCast(outInfo->Get(svtkDataObject::DATA_OBJECT()));

  // Allocate storage
  svtkPoints* pts = svtkPoints::New();

  // Set the desired precision for the points in the output.
  if (this->OutputPointsPrecision == svtkAlgorithm::DOUBLE_PRECISION)
  {
    pts->SetDataType(SVTK_DOUBLE);
  }
  else
  {
    pts->SetDataType(SVTK_FLOAT);
  }

  pts->Allocate(6, 6);
  svtkCellArray* verts = svtkCellArray::New();
  verts->AllocateEstimate(1, 1);
  svtkCellArray* lines = svtkCellArray::New();
  lines->AllocateEstimate(4, 2);
  svtkCellArray* polys = svtkCellArray::New();
  polys->AllocateEstimate(1, 4);
  svtkUnsignedCharArray* colors = svtkUnsignedCharArray::New();
  colors->SetNumberOfComponents(3);
  colors->Allocate(2, 2);
  colors->SetName("Colors");

  this->ConvertColor();

  // Special options
  if (this->Dash)
  {
    int filled = this->Filled;
    this->Filled = 0;
    this->CreateDash(pts, lines, polys, colors, this->Scale2);
    this->Filled = filled;
  }
  if (this->Cross)
  {
    int filled = this->Filled;
    this->Filled = 0;
    this->CreateCross(pts, lines, polys, colors, this->Scale2);
    this->Filled = filled;
  }

  // Call the right function
  switch (this->GlyphType)
  {
    case SVTK_NO_GLYPH:
      break;
    case SVTK_VERTEX_GLYPH:
      this->CreateVertex(pts, verts, colors);
      break;
    case SVTK_DASH_GLYPH:
      this->CreateDash(pts, lines, polys, colors, 1.0);
      break;
    case SVTK_CROSS_GLYPH:
      this->CreateCross(pts, lines, polys, colors, 1.0);
      break;
    case SVTK_THICKCROSS_GLYPH:
      this->CreateThickCross(pts, lines, polys, colors);
      break;
    case SVTK_TRIANGLE_GLYPH:
      this->CreateTriangle(pts, lines, polys, colors);
      break;
    case SVTK_SQUARE_GLYPH:
      this->CreateSquare(pts, lines, polys, colors);
      break;
    case SVTK_CIRCLE_GLYPH:
      this->CreateCircle(pts, lines, polys, colors);
      break;
    case SVTK_DIAMOND_GLYPH:
      this->CreateDiamond(pts, lines, polys, colors);
      break;
    case SVTK_ARROW_GLYPH:
      this->CreateArrow(pts, lines, polys, colors);
      break;
    case SVTK_THICKARROW_GLYPH:
      this->CreateThickArrow(pts, lines, polys, colors);
      break;
    case SVTK_HOOKEDARROW_GLYPH:
      this->CreateHookedArrow(pts, lines, polys, colors);
      break;
    case SVTK_EDGEARROW_GLYPH:
      this->CreateEdgeArrow(pts, lines, polys, colors);
      break;
  }

  this->TransformGlyph(pts);

  // Clean up
  output->SetPoints(pts);
  pts->Delete();

  output->SetVerts(verts);
  verts->Delete();

  output->SetLines(lines);
  lines->Delete();

  output->SetPolys(polys);
  polys->Delete();

  output->GetCellData()->SetScalars(colors);
  colors->Delete();

  return 1;
}

void svtkGlyphSource2D::ConvertColor()
{
  this->RGB[0] = static_cast<unsigned char>(255.0 * this->Color[0]);
  this->RGB[1] = static_cast<unsigned char>(255.0 * this->Color[1]);
  this->RGB[2] = static_cast<unsigned char>(255.0 * this->Color[2]);
}

void svtkGlyphSource2D::TransformGlyph(svtkPoints* pts)
{
  double x[3];
  svtkIdType i;
  svtkIdType numPts = pts->GetNumberOfPoints();

  if (this->RotationAngle == 0.0)
  {
    for (i = 0; i < numPts; i++)
    {
      pts->GetPoint(i, x);
      x[0] = this->Center[0] + this->Scale * x[0];
      x[1] = this->Center[1] + this->Scale * x[1];
      pts->SetPoint(i, x);
    }
  }
  else
  {
    double angle = svtkMath::RadiansFromDegrees(this->RotationAngle);
    double xt;
    for (i = 0; i < numPts; i++)
    {
      pts->GetPoint(i, x);
      xt = x[0] * cos(angle) - x[1] * sin(angle);
      x[1] = x[0] * sin(angle) + x[1] * cos(angle);
      x[0] = xt;
      x[0] = this->Center[0] + this->Scale * x[0];
      x[1] = this->Center[1] + this->Scale * x[1];
      pts->SetPoint(i, x);
    }
  }
}

void svtkGlyphSource2D::CreateVertex(
  svtkPoints* pts, svtkCellArray* verts, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[1];
  ptIds[0] = pts->InsertNextPoint(0.0, 0.0, 0.0);
  verts->InsertNextCell(1, ptIds);
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

void svtkGlyphSource2D::CreateCross(svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys,
  svtkUnsignedCharArray* colors, double scale)
{
  svtkIdType ptIds[4];

  if (this->Filled)
  {
    this->CreateThickCross(pts, lines, polys, colors);
  }
  else
  {
    ptIds[0] = pts->InsertNextPoint(-0.5 * scale, 0.0, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5 * scale, 0.0, 0.0);
    lines->InsertNextCell(2, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
    ptIds[0] = pts->InsertNextPoint(0.0, -0.5 * scale, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.0, 0.5 * scale, 0.0);
    lines->InsertNextCell(2, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
}

void svtkGlyphSource2D::CreateThickCross(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  if (this->Filled)
  {
    svtkIdType ptIds[4];
    ptIds[0] = pts->InsertNextPoint(-0.5, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, -0.1, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.5, 0.1, 0.0);
    ptIds[3] = pts->InsertNextPoint(-0.5, 0.1, 0.0);
    polys->InsertNextCell(4, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
    ptIds[0] = pts->InsertNextPoint(-0.1, -0.5, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.1, -0.5, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.1, 0.5, 0.0);
    ptIds[3] = pts->InsertNextPoint(-0.1, 0.5, 0.0);
    polys->InsertNextCell(4, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
  else
  {
    svtkIdType ptIds[13];
    ptIds[0] = pts->InsertNextPoint(-0.5, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(-0.1, -0.1, 0.0);
    ptIds[2] = pts->InsertNextPoint(-0.1, -0.5, 0.0);
    ptIds[3] = pts->InsertNextPoint(0.1, -0.5, 0.0);
    ptIds[4] = pts->InsertNextPoint(0.1, -0.1, 0.0);
    ptIds[5] = pts->InsertNextPoint(0.5, -0.1, 0.0);
    ptIds[6] = pts->InsertNextPoint(0.5, 0.1, 0.0);
    ptIds[7] = pts->InsertNextPoint(0.1, 0.1, 0.0);
    ptIds[8] = pts->InsertNextPoint(0.1, 0.5, 0.0);
    ptIds[9] = pts->InsertNextPoint(-0.1, 0.5, 0.0);
    ptIds[10] = pts->InsertNextPoint(-0.1, 0.1, 0.0);
    ptIds[11] = pts->InsertNextPoint(-0.5, 0.1, 0.0);
    ptIds[12] = ptIds[0];
    lines->InsertNextCell(13, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
}

void svtkGlyphSource2D::CreateTriangle(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[4];

  ptIds[0] = pts->InsertNextPoint(-0.375, -0.25, 0.0);
  ptIds[1] = pts->InsertNextPoint(0.0, 0.5, 0.0);
  ptIds[2] = pts->InsertNextPoint(0.375, -0.25, 0.0);

  if (this->Filled)
  {
    polys->InsertNextCell(3, ptIds);
  }
  else
  {
    ptIds[3] = ptIds[0];
    lines->InsertNextCell(4, ptIds);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

void svtkGlyphSource2D::CreateSquare(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[5];

  ptIds[0] = pts->InsertNextPoint(-0.5, -0.5, 0.0);
  ptIds[1] = pts->InsertNextPoint(0.5, -0.5, 0.0);
  ptIds[2] = pts->InsertNextPoint(0.5, 0.5, 0.0);
  ptIds[3] = pts->InsertNextPoint(-0.5, 0.5, 0.0);

  if (this->Filled)
  {
    polys->InsertNextCell(4, ptIds);
  }
  else
  {
    ptIds[4] = ptIds[0];
    lines->InsertNextCell(5, ptIds);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

void svtkGlyphSource2D::CreateCircle(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdList* ptIds = svtkIdList::New();
  if (this->Filled) // it's a polygon!
  {
    ptIds->SetNumberOfIds(this->Resolution);
  }
  else
  {
    ptIds->SetNumberOfIds(this->Resolution + 1);
  }

  double x[3], theta;

  // generate points around a circle
  x[2] = 0.0;
  theta = 2.0 * svtkMath::Pi() / static_cast<double>(this->Resolution);
  for (int i = 0; i < this->Resolution; i++)
  {
    x[0] = 0.5 * cos(i * theta);
    x[1] = 0.5 * sin(i * theta);
    ptIds->SetId(i, pts->InsertNextPoint(x));
  }

  if (this->Filled)
  {
    polys->InsertNextCell(ptIds);
  }
  else
  { // close the line
    ptIds->SetId(this->Resolution, ptIds->GetId(0));
    lines->InsertNextCell(ptIds);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);

  ptIds->Delete();
}

void svtkGlyphSource2D::CreateDiamond(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[5];

  ptIds[0] = pts->InsertNextPoint(0.0, -0.5, 0.0);
  ptIds[1] = pts->InsertNextPoint(0.5, 0.0, 0.0);
  ptIds[2] = pts->InsertNextPoint(0.0, 0.5, 0.0);
  ptIds[3] = pts->InsertNextPoint(-0.5, 0.0, 0.0);

  if (this->Filled)
  {
    polys->InsertNextCell(4, ptIds);
  }
  else
  {
    ptIds[4] = ptIds[0];
    lines->InsertNextCell(5, ptIds);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

void svtkGlyphSource2D::CreateArrow(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  if (this->Filled) // create two convex polygons
  {
    this->CreateThickArrow(pts, lines, polys, colors);
  }
  else
  {
    // stem
    svtkIdType ptIds[3];
    ptIds[0] = pts->InsertNextPoint(-0.5, 0.0, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, 0.0, 0.0);
    lines->InsertNextCell(2, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);

    // arrow head
    ptIds[0] = pts->InsertNextPoint(0.2, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, 0.0, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.2, 0.1, 0.0);
    lines->InsertNextCell(3, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
}

void svtkGlyphSource2D::CreateThickArrow(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[8];

  ptIds[0] = pts->InsertNextPoint(-0.5, -0.1, 0.0);
  ptIds[1] = pts->InsertNextPoint(0.1, -0.1, 0.0);
  ptIds[2] = pts->InsertNextPoint(0.1, -0.2, 0.0);
  ptIds[3] = pts->InsertNextPoint(0.5, 0.0, 0.0);
  ptIds[4] = pts->InsertNextPoint(0.1, 0.2, 0.0);
  ptIds[5] = pts->InsertNextPoint(0.1, 0.1, 0.0);
  ptIds[6] = pts->InsertNextPoint(-0.5, 0.1, 0.0);

  if (this->Filled) // create two convex polygons
  {
    polys->InsertNextCell(4);
    polys->InsertCellPoint(ptIds[0]);
    polys->InsertCellPoint(ptIds[1]);
    polys->InsertCellPoint(ptIds[5]);
    polys->InsertCellPoint(ptIds[6]);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);

    polys->InsertNextCell(5, ptIds + 1);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
  else
  {
    ptIds[7] = ptIds[0];
    lines->InsertNextCell(8, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
}

void svtkGlyphSource2D::CreateHookedArrow(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  if (this->Filled)
  {
    // create two convex polygons
    svtkIdType ptIds[4];
    ptIds[0] = pts->InsertNextPoint(-0.5, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.1, -0.1, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.1, 0.075, 0.0);
    ptIds[3] = pts->InsertNextPoint(-0.5, 0.075, 0.0);
    polys->InsertNextCell(4, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);

    ptIds[0] = pts->InsertNextPoint(0.1, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, -0.1, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.1, 0.2, 0.0);
    polys->InsertNextCell(3, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
  else
  {
    svtkIdType ptIds[3];
    ptIds[0] = pts->InsertNextPoint(-0.5, 0.0, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, 0.0, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.2, 0.1, 0.0);
    lines->InsertNextCell(3, ptIds);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
  }
}

void svtkGlyphSource2D::CreateEdgeArrow(
  svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys, svtkUnsignedCharArray* colors)
{
  svtkIdType ptIds[3];

  double x = 0.5 / sqrt(3.0);
  ptIds[0] = pts->InsertNextPoint(-1.0, x, 0.0);
  ptIds[1] = pts->InsertNextPoint(0.0, 0.0, 0.0);
  ptIds[2] = pts->InsertNextPoint(-1.0, -x, 0.0);

  if (this->Filled)
  {
    polys->InsertNextCell(3, ptIds);
  }
  else
  {
    lines->InsertNextCell(3, ptIds);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

void svtkGlyphSource2D::CreateDash(svtkPoints* pts, svtkCellArray* lines, svtkCellArray* polys,
  svtkUnsignedCharArray* colors, double scale)
{
  if (this->Filled)
  {
    svtkIdType ptIds[4];
    ptIds[0] = pts->InsertNextPoint(-0.5, -0.1, 0.0);
    ptIds[1] = pts->InsertNextPoint(0.5, -0.1, 0.0);
    ptIds[2] = pts->InsertNextPoint(0.5, 0.1, 0.0);
    ptIds[3] = pts->InsertNextPoint(-0.5, 0.1, 0.0);
    polys->InsertNextCell(4, ptIds);
  }
  else
  {
    svtkIdType ptIds2D[2];
    ptIds2D[0] = pts->InsertNextPoint(-0.5 * scale, 0.0, 0.0);
    ptIds2D[1] = pts->InsertNextPoint(0.5 * scale, 0.0, 0.0);
    colors->InsertNextValue(this->RGB[0]);
    colors->InsertNextValue(this->RGB[1]);
    colors->InsertNextValue(this->RGB[2]);
    lines->InsertNextCell(2, ptIds2D);
  }
  colors->InsertNextValue(this->RGB[0]);
  colors->InsertNextValue(this->RGB[1]);
  colors->InsertNextValue(this->RGB[2]);
}

//----------------------------------------------------------------------------
void svtkGlyphSource2D::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Center: (" << this->Center[0] << ", " << this->Center[1] << ", "
     << this->Center[2] << ")\n";

  os << indent << "Scale: " << this->Scale << "\n";
  os << indent << "Scale2: " << this->Scale2 << "\n";
  os << indent << "Rotation Angle: " << this->RotationAngle << "\n";
  os << indent << "Resolution: " << this->Resolution << "\n";

  os << indent << "Color: (" << this->Color[0] << ", " << this->Color[1] << ", " << this->Color[2]
     << ")\n";

  os << indent << "Filled: " << (this->Filled ? "On\n" : "Off\n");
  os << indent << "Dash: " << (this->Dash ? "On\n" : "Off\n");
  os << indent << "Cross: " << (this->Cross ? "On\n" : "Off\n");

  os << indent << "Glyph Type";
  switch (this->GlyphType)
  {
    case SVTK_NO_GLYPH:
      os << "No Glyph\n";
      break;
    case SVTK_VERTEX_GLYPH:
      os << "Vertex\n";
      break;
    case SVTK_DASH_GLYPH:
      os << "Dash\n";
      break;
    case SVTK_CROSS_GLYPH:
      os << "Cross\n";
      break;
    case SVTK_THICKCROSS_GLYPH:
      os << "Cross\n";
      break;
    case SVTK_TRIANGLE_GLYPH:
      os << "Triangle\n";
      break;
    case SVTK_SQUARE_GLYPH:
      os << "Square\n";
      break;
    case SVTK_CIRCLE_GLYPH:
      os << "Circle\n";
      break;
    case SVTK_DIAMOND_GLYPH:
      os << "Diamond\n";
      break;
    case SVTK_ARROW_GLYPH:
      os << "Arrow\n";
      break;
    case SVTK_THICKARROW_GLYPH:
      os << "Arrow\n";
      break;
    case SVTK_HOOKEDARROW_GLYPH:
      os << "Hooked Arrow\n";
      break;
    case SVTK_EDGEARROW_GLYPH:
      os << "Edge Arrow\n";
      break;
  }
  os << indent << "Output Points Precision: " << this->OutputPointsPrecision << "\n";
}