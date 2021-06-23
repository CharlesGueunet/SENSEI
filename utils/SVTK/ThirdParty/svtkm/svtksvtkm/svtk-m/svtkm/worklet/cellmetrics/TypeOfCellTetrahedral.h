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
#ifndef svtk_m_worklet_cellmetrics_TypeOfCellTetrahedral
#define svtk_m_worklet_cellmetrics_TypeOfCellTetrahedral
/**
 * Returns the L0 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL0(const CollectionOfPoints& pts)
{
  const Vector L0(pts[1] - pts[0]);
  return L0;
}

/**
 * Returns the L1 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL1(const CollectionOfPoints& pts)
{
  const Vector L1(pts[2] - pts[1]);
  return L1;
}

/**
 * Returns the L2 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL2(const CollectionOfPoints& pts)
{
  const Vector L2(pts[0] - pts[2]);
  return L2;
}

/**
 * Returns the L3 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL3(const CollectionOfPoints& pts)
{
  const Vector L3(pts[3] - pts[0]);
  return L3;
}

/**
 * Returns the L4 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL4(const CollectionOfPoints& pts)
{
  const Vector L4(pts[3] - pts[1]);
  return L4;
}

/**
 * Returns the L5 vector, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the quadrilateral.
 *  \return Returns the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Vector GetTetraL5(const CollectionOfPoints& pts)
{
  const Vector L5(pts[3] - pts[2]);
  return L5;
}

/**
 * Returns the L0 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL0Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l0 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL0<Scalar, Vector, CollectionOfPoints>(pts)));
  return l0;
}

/**
 * Returns the L1 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL1Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l1 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL1<Scalar, Vector, CollectionOfPoints>(pts)));
  return l1;
}

/**
 * Returns the L2 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL2Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l2 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL2<Scalar, Vector, CollectionOfPoints>(pts)));
  return l2;
}

/**
 * Returns the L3 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL3Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l3 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL3<Scalar, Vector, CollectionOfPoints>(pts)));
  return l3;
}

/**
 * Returns the L4 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL4Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l4 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL4<Scalar, Vector, CollectionOfPoints>(pts)));
  return l4;
}

/**
 * Returns the L5 vector's magnitude, as defined by the verdict manual.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the magnitude of the vector.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraL5Magnitude(const CollectionOfPoints& pts)
{
  const Scalar l5 =
    svtkm::Sqrt(svtkm::MagnitudeSquared(GetTetraL5<Scalar, Vector, CollectionOfPoints>(pts)));
  return l5;
}

/**
 * Returns the Max of the magnitude of each vector which makes up the edges of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the max.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraLMax(const CollectionOfPoints& pts)
{
  const Scalar l0 = GetTetraL0Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l1 = GetTetraL1Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l2 = GetTetraL2Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l3 = GetTetraL3Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l4 = GetTetraL4Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l5 = GetTetraL5Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar lmax = svtkm::Max(l0, svtkm::Max(l1, svtkm::Max(l2, svtkm::Max(l3, svtkm::Max(l4, l5)))));
  return lmax;
}

/**
 * Returns the Min of the magnitude of each vector which makes up the sides of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the min.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraLMin(const CollectionOfPoints& pts)
{
  const Scalar l0 = GetTetraL0Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l1 = GetTetraL1Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l2 = GetTetraL2Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l3 = GetTetraL3Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l4 = GetTetraL4Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar l5 = GetTetraL5Magnitude<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar lmin = svtkm::Min(l0, svtkm::Min(l1, svtkm::Min(l2, svtkm::Min(l3, svtkm::Min(l4, l5)))));
  return lmin;
}

/**
 * Returns the surface area of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the area.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraArea(const CollectionOfPoints& pts)
{
  const Vector L0 = GetTetraL0<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L1 = GetTetraL1<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L2 = GetTetraL2<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L3 = GetTetraL3<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L4 = GetTetraL4<Scalar, Vector, CollectionOfPoints>(pts);

  const Scalar a = svtkm::Sqrt(svtkm::MagnitudeSquared(svtkm::Cross(L2, L0)));
  const Scalar b = svtkm::Sqrt(svtkm::MagnitudeSquared(svtkm::Cross(L3, L0)));
  const Scalar c = svtkm::Sqrt(svtkm::MagnitudeSquared(svtkm::Cross(L4, L1)));
  const Scalar d = svtkm::Sqrt(svtkm::MagnitudeSquared(svtkm::Cross(L3, L2)));
  const Scalar hhalf(0.5);

  const Scalar area = hhalf * (a + b + c + d);
  return area;
}

/**
 * Returns the volume of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the volume.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraVolume(const CollectionOfPoints& pts)
{
  const Vector L0 = GetTetraL0<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L2 = GetTetraL2<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L3 = GetTetraL3<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar six(6.0);

  return svtkm::Dot(svtkm::Cross(L2, L0), L3) / six;
}

/**
 * Returns the inradius of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the inradius.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraInradius(const CollectionOfPoints& pts)
{
  const Scalar three(3.0);
  const Scalar volume = GetTetraVolume<Scalar, Vector, CollectionOfPoints>(pts);
  const Scalar area = GetTetraArea<Scalar, Vector, CollectionOfPoints>(pts);

  return (three * volume) / area;
}


/**
 * Returns the circumradius of the Tetra.
 *
 *  \param [in] pts The four points which define the Tetra.
 *  \return Returns the circumradius.
 */
template <typename Scalar, typename Vector, typename CollectionOfPoints>
SVTKM_EXEC Scalar GetTetraCircumradius(const CollectionOfPoints& pts)
{
  const Vector L0 = GetTetraL0<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L1 = GetTetraL1<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L2 = GetTetraL2<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L3 = GetTetraL3<Scalar, Vector, CollectionOfPoints>(pts);
  const Vector L4 = GetTetraL4<Scalar, Vector, CollectionOfPoints>(pts);

  const Scalar l0l0 = svtkm::MagnitudeSquared(L0);
  const Scalar l2l2 = svtkm::MagnitudeSquared(L2);
  const Scalar l3l3 = svtkm::MagnitudeSquared(L3);

  const Vector A = l3l3 * svtkm::Cross(L2, L0);
  const Vector B = l2l2 * svtkm::Cross(L3, L0);
  const Vector C = l0l0 * svtkm::Cross(L3, L2);

  const Vector D(A + B + C);
  const Scalar d = svtkm::Sqrt(svtkm::MagnitudeSquared(D));

  const Scalar twelve(12.0);
  const Scalar volume = GetTetraVolume<Scalar, Vector, CollectionOfPoints>(pts);

  const Scalar circumradius = d / (twelve * volume);
  return circumradius;
}

#endif