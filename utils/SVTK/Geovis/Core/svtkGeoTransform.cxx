/*=========================================================================

  Program:   Visualization Toolkit
  Module:    svtkGeoTransform.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
-------------------------------------------------------------------------*/

#include "svtkGeoTransform.h"

#include "svtkDoubleArray.h"
#include "svtkGeoProjection.h"
#include "svtkMath.h"
#include "svtkObjectFactory.h"
#include "svtkPoints.h"

#include "svtk_libproj.h"
#include <cmath>

svtkStandardNewMacro(svtkGeoTransform);
svtkCxxSetObjectMacro(svtkGeoTransform, SourceProjection, svtkGeoProjection);
svtkCxxSetObjectMacro(svtkGeoTransform, DestinationProjection, svtkGeoProjection);

svtkGeoTransform::svtkGeoTransform()
{
  this->SourceProjection = nullptr;
  this->DestinationProjection = nullptr;
}

svtkGeoTransform::~svtkGeoTransform()
{
  if (this->SourceProjection)
  {
    this->SourceProjection->Delete();
  }
  if (this->DestinationProjection)
  {
    this->DestinationProjection->Delete();
  }
}

void svtkGeoTransform::PrintSelf(ostream& os, svtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "SourceProjection: " << this->SourceProjection << "\n";
  os << indent << "DestinationProjection: " << this->DestinationProjection << "\n";
}

void svtkGeoTransform::TransformPoints(svtkPoints* srcPts, svtkPoints* dstPts)
{
  if (!srcPts || !dstPts)
  {
    return;
  }

  svtkDoubleArray* srcCoords = svtkArrayDownCast<svtkDoubleArray>(srcPts->GetData());
  svtkDoubleArray* dstCoords = svtkArrayDownCast<svtkDoubleArray>(dstPts->GetData());
  if (!srcCoords || !dstCoords)
  { // data not in a form we can use directly anyway...
    this->Superclass::TransformPoints(srcPts, dstPts);
    return;
  }
  dstCoords->DeepCopy(srcCoords);

  projPJ src = this->SourceProjection ? this->SourceProjection->GetProjection() : nullptr;
  projPJ dst = this->DestinationProjection ? this->DestinationProjection->GetProjection() : nullptr;
  if (!src && !dst)
  {
    // we've already copied srcCoords to dstCoords and src=dst=0 implies no transform...
    return;
  }

  if (srcCoords->GetNumberOfComponents() < 2)
  {
    svtkErrorMacro(<< "Source coordinate array " << srcCoords << " only has "
                  << srcCoords->GetNumberOfComponents()
                  << " components and at least 2 are required for geographic projections.");
    return;
  }

  this->InternalTransformPoints(
    dstCoords->GetPointer(0), dstCoords->GetNumberOfTuples(), dstCoords->GetNumberOfComponents());
}

void svtkGeoTransform::Inverse()
{
  svtkGeoProjection* tmp = this->SourceProjection;
  this->SourceProjection = this->DestinationProjection;
  this->DestinationProjection = tmp;
  this->Modified();
}

void svtkGeoTransform::InternalTransformPoint(const float in[3], float out[3])
{
  double ind[3];
  double oud[3];
  int i;
  for (i = 0; i < 3; ++i)
    ind[i] = in[i];
  this->InternalTransformPoint(ind, oud);
  for (i = 0; i < 3; ++i)
    out[i] = static_cast<float>(oud[i]);
}

void svtkGeoTransform::InternalTransformPoint(const double in[3], double out[3])
{
  for (int i = 0; i < 3; ++i)
  {
    out[i] = in[i];
  }
  this->InternalTransformPoints(out, 1, 3);
}

void svtkGeoTransform::InternalTransformDerivative(
  const float in[3], float out[3], float derivative[3][3])
{
  double ind[3];
  double oud[3];
  double drd[3][3];
  int i;
  for (i = 0; i < 3; ++i)
    ind[i] = in[i];
  this->InternalTransformDerivative(ind, oud, drd);
  for (i = 0; i < 3; ++i)
  {
    out[i] = static_cast<float>(oud[i]);
    for (int j = 0; j < 3; ++j)
    {
      derivative[i][j] = drd[i][j];
    }
  }
}

void svtkGeoTransform::InternalTransformDerivative(
  const double in[3], double out[3], double derivative[3][3])
{
  // FIXME: Need to use pj_factors for both source and inverted dest projection
  (void)in;
  (void)out;
  (void)derivative;
}

svtkAbstractTransform* svtkGeoTransform::MakeTransform()
{
  svtkGeoTransform* geoTrans = svtkGeoTransform::New();
  return geoTrans;
}

void svtkGeoTransform::InternalTransformPoints(double* x, svtkIdType numPts, int stride)
{
  projPJ src = this->SourceProjection ? this->SourceProjection->GetProjection() : nullptr;
  projPJ dst = this->DestinationProjection ? this->DestinationProjection->GetProjection() : nullptr;
  int delta = stride - 2;
  projLP lp;
  projXY xy;
  if (src)
  {
    // Convert from src system to lat/long using inverse of src transform
    double* coord = x;
    for (svtkIdType i = 0; i < numPts; ++i)
    {
#if PROJ_VERSION_MAJOR >= 5
      xy.x = coord[0];
      xy.y = coord[1];
#else
      xy.u = coord[0];
      xy.v = coord[1];
#endif
      lp = pj_inv(xy, src);
#if PROJ_VERSION_MAJOR >= 5
      coord[0] = lp.lam;
      coord[1] = lp.phi;
#else
      coord[0] = lp.u;
      coord[1] = lp.v;
#endif
      coord += stride;
    }
  }
  else // ! src
  {
    // src coords are in degrees, convert to radians
    double* coord = x;
    for (svtkIdType i = 0; i < numPts; ++i)
    {
      for (int j = 0; j < 2; ++j, ++coord)
      {
        *coord = svtkMath::RadiansFromDegrees(*coord);
      }
      coord += delta;
    }
  }
  if (dst)
  {
    double* coord = x;
    for (svtkIdType i = 0; i < numPts; ++i)
    {
#if PROJ_VERSION_MAJOR >= 5
      lp.lam = coord[0];
      lp.phi = coord[1];
#else
      lp.u = coord[0];
      lp.v = coord[1];
#endif
      xy = pj_fwd(lp, dst);
#if PROJ_VERSION_MAJOR >= 5
      coord[0] = xy.x;
      coord[1] = xy.y;
#else
      coord[0] = xy.u;
      coord[1] = xy.v;
#endif
      coord += stride;
    }
  }
  else // ! dst
  {
    // dst coords are in radians, convert to degrees
    double* coord = x;
    for (svtkIdType i = 0; i < numPts; ++i)
    {
      for (int j = 0; j < 2; ++j, ++coord)
      {
        *coord = svtkMath::DegreesFromRadians(*coord);
      }
      coord += delta;
    }
  }
}

int svtkGeoTransform::ComputeUTMZone(double lon, double lat)
{
  lon = std::fmod(lon + 180, 360) - 180;
  lat = std::fmod(lat + 90, 180) - 90;
  int result = 0;
  // UTM is not defined outside of these limits
  if (lat <= 84 && lat >= -80)
  {
    // first special case
    if (lat >= 72 && lon >= 0 && lon < 42)
    {
      if (lon < 9)
      {
        result = 31;
      }
      else if (lon < 21)
      {
        result = 33;
      }
      else if (lon < 33)
      {
        result = 35;
      }
      else
      {
        result = 37;
      }
    }
    // second special case
    else if (lat >= 56 && lat < 64 && lon >= 0 && lon < 12)
    {
      if (lon < 3)
      {
        result = 31;
      }
      else
      {
        result = 32;
      }
    }
    else
    {
      // general case: zones are 6 degrees, from 1 to 60.
      result = (static_cast<int>(lon) + 180) / 6 + 1;
    }
  }
  return result;
}