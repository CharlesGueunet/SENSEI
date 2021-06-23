//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_worklet_lcs_LagrangianStructureHelpers_h
#define svtk_m_worklet_lcs_LagrangianStructureHelpers_h

#include <svtkm/Matrix.h>
#include <svtkm/Swap.h>
#include <svtkm/Types.h>

namespace svtkm
{
namespace worklet
{
namespace detail
{

template <typename T>
SVTKM_EXEC_CONT void ComputeLeftCauchyGreenTensor(svtkm::Matrix<T, 2, 2>& jacobian)
{
  svtkm::Vec<T, 2> j1 = svtkm::MatrixGetRow(jacobian, 0);
  svtkm::Vec<T, 2> j2 = svtkm::MatrixGetRow(jacobian, 1);

  // Left Cauchy Green Tensor is J*J^T
  // j1[0] j1[1] | j1[0] j2[0]
  // j2[0] j2[1] | j1[1] j2[1]

  T a = j1[0] * j1[0] + j1[1] * j1[1];
  T b = j1[0] * j2[0] + j1[1] * j2[1];

  T d = j2[0] * j2[0] + j2[1] * j2[1];

  svtkm::MatrixSetRow(jacobian, 0, svtkm::Vec<T, 2>(a, b));
  svtkm::MatrixSetRow(jacobian, 1, svtkm::Vec<T, 2>(b, d));
}

template <typename T>
SVTKM_EXEC_CONT void ComputeLeftCauchyGreenTensor(svtkm::Matrix<T, 3, 3>& jacobian)
{
  svtkm::Vec<T, 3> j1 = svtkm::MatrixGetRow(jacobian, 0);
  svtkm::Vec<T, 3> j2 = svtkm::MatrixGetRow(jacobian, 1);
  svtkm::Vec<T, 3> j3 = svtkm::MatrixGetRow(jacobian, 2);

  // Left Cauchy Green Tensor is J*J^T
  // j1[0]  j1[1] j1[2] |  j1[0]  j2[0]  j3[0]
  // j2[0]  j2[1] j2[2] |  j1[1]  j2[1]  j3[1]
  // j3[0]  j3[1] j3[2] |  j1[2]  j2[2]  j3[2]

  T a = j1[0] * j1[0] + j1[1] * j1[1] + j1[2] * j1[2];
  T b = j1[0] * j2[0] + j1[1] * j2[1] + j1[2] * j2[2];
  T c = j1[0] * j3[0] + j1[1] * j3[1] + j1[2] * j3[2];

  T d = j2[0] * j2[0] + j2[1] * j2[1] + j2[2] * j2[2];
  T e = j2[0] * j3[0] + j2[1] * j3[1] + j2[2] * j3[2];

  T f = j3[0] * j3[0] + j3[1] * j3[1] + j3[2] * j3[2];

  svtkm::MatrixSetRow(jacobian, 0, svtkm::Vec<T, 3>(a, b, c));
  svtkm::MatrixSetRow(jacobian, 1, svtkm::Vec<T, 3>(b, d, e));
  svtkm::MatrixSetRow(jacobian, 2, svtkm::Vec<T, 3>(d, e, f));
}

template <typename T>
SVTKM_EXEC_CONT void ComputeRightCauchyGreenTensor(svtkm::Matrix<T, 2, 2>& jacobian)
{
  svtkm::Vec<T, 2> j1 = svtkm::MatrixGetRow(jacobian, 0);
  svtkm::Vec<T, 2> j2 = svtkm::MatrixGetRow(jacobian, 1);

  // Right Cauchy Green Tensor is J^T*J
  // j1[0]  j2[0] | j1[0]  j1[1]
  // j1[1]  j2[1] | j2[0]  j2[1]

  T a = j1[0] * j1[0] + j2[0] * j2[0];
  T b = j1[0] * j1[1] + j2[0] * j2[1];

  T d = j1[1] * j1[1] + j2[1] * j2[1];

  j1 = svtkm::Vec<T, 2>(a, b);
  j2 = svtkm::Vec<T, 2>(b, d);
}

template <typename T>
SVTKM_EXEC_CONT void ComputeRightCauchyGreenTensor(svtkm::Matrix<T, 3, 3>& jacobian)
{
  svtkm::Vec<T, 3> j1 = svtkm::MatrixGetRow(jacobian, 0);
  svtkm::Vec<T, 3> j2 = svtkm::MatrixGetRow(jacobian, 1);
  svtkm::Vec<T, 3> j3 = svtkm::MatrixGetRow(jacobian, 2);

  // Right Cauchy Green Tensor is J^T*J
  // j1[0]  j2[0]  j3[0] | j1[0]  j1[1] j1[2]
  // j1[1]  j2[1]  j3[1] | j2[0]  j2[1] j2[2]
  // j1[2]  j2[2]  j3[2] | j3[0]  j3[1] j3[2]

  T a = j1[0] * j1[0] + j2[0] * j2[0] + j3[0] * j3[0];
  T b = j1[0] * j1[1] + j2[0] * j2[1] + j3[0] * j3[1];
  T c = j1[0] * j1[2] + j2[0] * j2[2] + j3[0] * j3[2];

  T d = j1[1] * j1[1] + j2[1] * j2[1] + j3[1] * j3[1];
  T e = j1[1] * j1[2] + j2[1] * j2[2] + j3[1] * j3[2];

  T f = j1[2] * j1[2] + j2[2] * j2[2] + j3[2] * j3[2];

  j1 = svtkm::Vec<T, 3>(a, b, c);
  j2 = svtkm::Vec<T, 3>(b, d, e);
  j3 = svtkm::Vec<T, 3>(d, e, f);
}

template <typename T>
SVTKM_EXEC_CONT void Jacobi(svtkm::Matrix<T, 2, 2> tensor, svtkm::Vec<T, 2>& eigen)
{
  svtkm::Vec<T, 2> j1 = svtkm::MatrixGetRow(tensor, 0);
  svtkm::Vec<T, 2> j2 = svtkm::MatrixGetRow(tensor, 1);

  // Assume a symetric matrix
  // a b
  // b c
  T a = j1[0];
  T b = j1[1];
  T c = j2[1];

  T trace = (a + c) / 2.0f;
  T det = a * c - b * b;
  T sqrtr = svtkm::Sqrt(trace * trace - det);

  // Arrange eigen values from largest to smallest.
  eigen[0] = trace + sqrtr;
  eigen[1] = trace - sqrtr;
}

template <typename T>
SVTKM_EXEC_CONT void Jacobi(svtkm::Matrix<T, 3, 3> tensor, svtkm::Vec<T, 3>& eigen)
{
  svtkm::Vec<T, 3> j1 = svtkm::MatrixGetRow(tensor, 0);
  svtkm::Vec<T, 3> j2 = svtkm::MatrixGetRow(tensor, 1);
  svtkm::Vec<T, 3> j3 = svtkm::MatrixGetRow(tensor, 2);

  // Assume a symetric matrix
  // a b c
  // b d e
  // c e f
  T a = j1[0];
  T b = j1[1];
  T c = j1[2];
  T d = j2[1];
  T e = j2[2];
  T f = j3[2];

  T x = (a + d + f) / 3.0f; // trace

  a -= x;
  d -= x;
  f -= x;

  // Det / 2;
  T q = (a * d * f + b * e * c + c * b * e - c * d * c - e * e * a - f * b * b) / 2.0f;
  T r = (a * a + b * b + c * c + b * b + d * d + e * e + c * c + e * e + f * f) / 6.0f;

  T D = (r * r * r - q * q);
  T phi = 0.0f;

  if (D < svtkm::Epsilon<T>())
    phi = 0.0f;
  else
  {
    phi = svtkm::ATan(svtkm::Sqrt(D) / q) / 3.0f;

    if (phi < 0)
      phi += static_cast<T>(svtkm::Pi());
  }

  const T sqrt3 = svtkm::Sqrt(3.0f);
  const T sqrtr = svtkm::Sqrt(r);

  T sinphi = 0.0f, cosphi = 0.0f;
  sinphi = svtkm::Sin(phi);
  cosphi = svtkm::Cos(phi);

  T w0 = x + 2.0f * sqrtr * cosphi;
  T w1 = x - sqrtr * (cosphi - sqrt3 * sinphi);
  T w2 = x - sqrtr * (cosphi + sqrt3 * sinphi);

  // Arrange eigen values from largest to smallest.
  if (w1 > w0)
    svtkm::Swap(w0, w1);
  if (w2 > w0)
    svtkm::Swap(w0, w2);
  if (w2 > w1)
    svtkm::Swap(w1, w2);

  eigen[0] = w0;
  eigen[1] = w1;
  eigen[2] = w2;
}

} // namespace detail
} // namespace worklet
} // namespace svtkm
#endif //svtk_m_worklet_lcs_LagrangianStructureHelpers_h