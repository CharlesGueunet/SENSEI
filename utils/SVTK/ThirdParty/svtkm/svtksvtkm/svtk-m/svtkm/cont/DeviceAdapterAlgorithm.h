//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef svtk_m_cont_DeviceAdapterAlgorithm_h
#define svtk_m_cont_DeviceAdapterAlgorithm_h

#include <svtkm/Types.h>

#include <svtkm/cont/DeviceAdapterTag.h>
#include <svtkm/cont/Logging.h>
#include <svtkm/cont/internal/ArrayManagerExecution.h>


#ifdef _WIN32
#include <sys/timeb.h>
#include <sys/types.h>
#else // _WIN32
#include <limits.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace svtkm
{
namespace cont
{

/// \brief Struct containing device adapter algorithms.
///
/// This struct, templated on the device adapter tag, comprises static methods
/// that implement the algorithms provided by the device adapter. The default
/// struct is not implemented. Device adapter implementations must specialize
/// the template.
///
template <class DeviceAdapterTag>
struct DeviceAdapterAlgorithm
#ifdef SVTKM_DOXYGEN_ONLY
{
  /// \brief Create a unique, unsorted list of indices denoting which bits are
  /// set in a bitfield.
  ///
  /// Returns the total number of set bits.
  template <typename IndicesStorage>
  SVTKM_CONT static svtkm::Id BitFieldToUnorderedSet(
    const svtkm::cont::BitField& bits,
    svtkm::cont::ArrayHandle<Id, IndicesStorage>& indices);

  /// \brief Copy the contents of one ArrayHandle to another
  ///
  /// Copies the contents of \c input to \c output. The array \c output will be
  /// allocated to the same size of \c input. If output has already been
  /// allocated we will reallocate and clear any current values.
  ///
  template <typename T, typename U, class CIn, class COut>
  SVTKM_CONT static void Copy(const svtkm::cont::ArrayHandle<T, CIn>& input,
                             svtkm::cont::ArrayHandle<U, COut>& output);

  /// \brief Conditionally copy elements in the input array to the output array.
  ///
  /// Calls the parallel primitive function of stream compaction on the \c
  /// input to remove unwanted elements. The result of the stream compaction is
  /// placed in \c output. The values in \c stencil are used to determine which
  /// \c input values are placed into \c output, with all stencil values not
  /// equal to the default constructor being considered valid.
  /// The size of \c output will be modified after this call as we can't know
  /// the number of elements that will be removed by the stream compaction
  /// algorithm.
  ///
  template <typename T, typename U, class CIn, class CStencil, class COut>
  SVTKM_CONT static void CopyIf(const svtkm::cont::ArrayHandle<T, CIn>& input,
                               const svtkm::cont::ArrayHandle<U, CStencil>& stencil,
                               svtkm::cont::ArrayHandle<T, COut>& output);

  /// \brief Conditionally copy elements in the input array to the output array.
  ///
  /// Calls the parallel primitive function of stream compaction on the \c
  /// input to remove unwanted elements. The result of the stream compaction is
  /// placed in \c output. The values in \c stencil are passed to the unary
  /// comparison object which is used to determine which /c input values are
  /// placed into \c output.
  /// The size of \c output will be modified after this call as we can't know
  /// the number of elements that will be removed by the stream compaction
  /// algorithm.
  ///
  template <typename T, typename U, class CIn, class CStencil, class COut, class UnaryPredicate>
  SVTKM_CONT static void CopyIf(const svtkm::cont::ArrayHandle<T, CIn>& input,
                               const svtkm::cont::ArrayHandle<U, CStencil>& stencil,
                               svtkm::cont::ArrayHandle<T, COut>& output,
                               UnaryPredicate unary_predicate);

  /// \brief Copy the contents of a section of one ArrayHandle to another
  ///
  /// Copies the a range of elements of \c input to \c output. The number of
  /// elements is determined by \c numberOfElementsToCopy, and initial start
  /// position is determined by \c inputStartIndex. You can control where
  /// in the destination the copy should occur by specifying the \c outputIndex
  ///
  /// If inputStartIndex + numberOfElementsToCopy is greater than the length
  /// of \c input we will only copy until we reach the end of the input array
  ///
  /// If the \c outputIndex + numberOfElementsToCopy is greater than the
  /// length of \c output we will reallocate the output array so it can
  /// fit the number of elements we desire.
  ///
  /// \par Requirements:
  /// \arg If \c input and \c output share memory, the input and output ranges
  /// must not overlap.
  ///
  template <typename T, typename U, class CIn, class COut>
  SVTKM_CONT static bool CopySubRange(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::Id inputStartIndex,
                                     svtkm::Id numberOfElementsToCopy,
                                     svtkm::cont::ArrayHandle<U, COut>& output,
                                     svtkm::Id outputIndex = 0);

  /// \brief Returns the total number of "1" bits in BitField.
  SVTKM_CONT static svtkm::Id CountSetBits(const svtkm::cont::BitField& bits);

  /// \brief Fill the BitField with a specific pattern of bits.
  /// For boolean values, all bits are set to 1 if value is true, or 0 if value
  /// is false.
  /// For word masks, the word type must be an unsigned integral type, which
  /// will be stamped across the BitField.
  /// If numBits is provided, the BitField is resized appropriately.
  /// @{
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, bool value, svtkm::Id numBits);
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, bool value);
  template <typename WordType>
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, WordType word, svtkm::Id numBits);
  template <typename WordType>
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, WordType word);
  /// @}

  /// Fill @a array with @a value. If @a numValues is specified, the array will
  /// be resized.
  /// @{
  template <typename T, typename S>
  SVTKM_CONT static void Fill(svtkm::cont::ArrayHandle<T, S>& array, const T& value);
  template <typename T, typename S>
  SVTKM_CONT static void Fill(svtkm::cont::ArrayHandle<T, S>& array,
                             const T& value,
                             const svtkm::Id numValues);
  /// @}

  /// \brief Output is the first index in input for each item in values that wouldn't alter the ordering of input
  ///
  /// LowerBounds is a vectorized search. From each value in \c values it finds
  /// the first place the item can be inserted in the ordered \c input array and
  /// stores the index in \c output.
  ///
  /// \par Requirements:
  /// \arg \c input must already be sorted
  ///
  template <typename T, class CIn, class CVal, class COut>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output);

  /// \brief Output is the first index in input for each item in values that wouldn't alter the ordering of input
  ///
  /// LowerBounds is a vectorized search. From each value in \c values it finds
  /// the first place the item can be inserted in the ordered \c input array and
  /// stores the index in \c output. Uses the custom comparison functor to
  /// determine the correct location for each item.
  ///
  /// \par Requirements:
  /// \arg \c input must already be sorted
  ///
  template <typename T, class CIn, class CVal, class COut, class BinaryCompare>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output,
                                    BinaryCompare binary_compare);

  /// \brief A special version of LowerBounds that does an in place operation.
  ///
  /// This version of lower bounds performs an in place operation where each
  /// value in the \c values_output array is replaced by the index in \c input
  /// where it occurs. Because this is an in place operation, the type of the
  /// arrays is limited to svtkm::Id.
  ///
  template <class CIn, class COut>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<svtkm::Id, CIn>& input,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& values_output);

  /// \brief Compute a accumulated sum operation on the input ArrayHandle
  ///
  /// Computes an accumulated sum on the \c input ArrayHandle, returning the
  /// total sum. Reduce is similar to the stl accumulate sum function,
  /// exception that Reduce doesn't do a serial summation. This means that if
  /// you have defined a custom plus operator for T it must be commutative,
  /// or you will get inconsistent results.
  ///
  /// \return The total sum.
  template <typename T, typename U, class CIn>
  SVTKM_CONT static U Reduce(const svtkm::cont::ArrayHandle<T, CIn>& input, U initialValue);

  /// \brief Compute a accumulated sum operation on the input ArrayHandle
  ///
  /// Computes an accumulated sum (or any user binary operation) on the
  /// \c input ArrayHandle, returning the total sum. Reduce is
  /// similar to the stl accumulate sum function, exception that Reduce
  /// doesn't do a serial summation. This means that if you have defined a
  /// custom plus operator for T it must be commutative, or you will get
  /// inconsistent results.
  ///
  /// \return The total sum.
  template <typename T, typename U, class CIn, class BinaryFunctor>
  SVTKM_CONT static U Reduce(const svtkm::cont::ArrayHandle<T, CIn>& input,
                            U initialValue,
                            BinaryFunctor binary_functor);

  /// \brief Compute a accumulated sum operation on the input key value pairs
  ///
  /// Computes a segmented accumulated sum (or any user binary operation) on the
  /// \c keys and \c values ArrayHandle(s). Each segmented accumulated sum is
  /// run on consecutive equal keys with the binary operation applied to all
  /// values inside that range. Once finished a single key and value is created
  /// for each segment.
  ///
  template <typename T,
            typename U,
            class CKeyIn,
            class CValIn,
            class CKeyOut,
            class CValOut,
            class BinaryFunctor>
  SVTKM_CONT static void ReduceByKey(const svtkm::cont::ArrayHandle<T, CKeyIn>& keys,
                                    const svtkm::cont::ArrayHandle<U, CValIn>& values,
                                    svtkm::cont::ArrayHandle<T, CKeyOut>& keys_output,
                                    svtkm::cont::ArrayHandle<U, CValOut>& values_output,
                                    BinaryFunctor binary_functor);

  /// \brief Compute an inclusive prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an inclusive prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. InclusiveScan is
  /// similar to the stl partial sum function, exception that InclusiveScan
  /// doesn't do a serial summation. This means that if you have defined a
  /// custom plus operator for T it must be associative, or you will get
  /// inconsistent results. When the input and output ArrayHandles are the same
  /// ArrayHandle the operation will be done inplace.
  ///
  /// \return The total sum.
  ///
  template <typename T, class CIn, class COut>
  SVTKM_CONT static T ScanInclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output);


  /// \brief Compute an inclusive prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an inclusive prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. InclusiveScan is
  /// similar to the stl partial sum function, exception that InclusiveScan
  /// doesn't do a serial summation. This means that if you have defined a
  /// custom plus operator for T it must be associative, or you will get
  /// inconsistent results. When the input and output ArrayHandles are the same
  /// ArrayHandle the operation will be done inplace.
  ///
  /// \return The total sum.
  ///
  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static T ScanInclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output,
                                   BinaryFunctor binary_functor);

  /// \brief Compute a segmented inclusive prefix sum operation on the input key value pairs.
  ///
  /// Computes a segmented inclusive prefix sum (or any user binary operation)
  /// on the \c keys and \c values ArrayHandle(s). Each segmented inclusive
  /// prefix sum is run on consecutive equal keys with the binary operation
  /// applied to all values inside that range. Once finished the result is
  /// stored in \c values_output ArrayHandle.
  ///
  template <typename T,
            typename U,
            typename KIn,
            typename VIn,
            typename VOut,
            typename BinaryFunctor>
  SVTKM_CONT static void ScanInclusiveByKey(const svtkm::cont::ArrayHandle<T, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<U, VIn>& values,
                                           svtkm::cont::ArrayHandle<U, VOut>& values_output,
                                           BinaryFunctor binary_functor);

  /// \brief Compute a segmented inclusive prefix sum operation on the input key value pairs.
  ///
  /// Computes a segmented inclusive prefix sum on the \c keys and \c values
  /// ArrayHandle(s). Each segmented inclusive prefix sum is run on consecutive
  /// equal keys with the binary operation svtkm::Add applied to all values inside
  /// that range. Once finished the result is stored in \c values_output ArrayHandle.
  ///
  template <typename T, typename U, typename KIn, typename VIn, typename VOut>
  SVTKM_CONT static void ScanInclusiveByKey(const svtkm::cont::ArrayHandle<T, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<U, VIn>& values,
                                           svtkm::cont::ArrayHandle<U, VOut>& values_output);

  /// \brief Compute an exclusive prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an exclusive prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. ExclusiveScan is
  /// similar to the stl partial sum function, exception that ExclusiveScan
  /// doesn't do a serial summation. This means that if you have defined a
  /// custom plus operator for T it must be associative, or you will get
  /// inconsistent results. When the input and output ArrayHandles are the same
  /// ArrayHandle the operation will be done inplace.
  ///
  /// \return The total sum.
  ///
  template <typename T, class CIn, class COut>
  SVTKM_CONT static T ScanExclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output);

  /// \brief Compute an exclusive prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an exclusive prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. ExclusiveScan is
  /// similar to the stl partial sum function, exception that ExclusiveScan
  /// doesn't do a serial summation. This means that if you have defined a
  /// custom plus operator for T it must be associative, or you will get
  /// inconsistent results. When the input and output ArrayHandles are the same
  /// ArrayHandle the operation will be done inplace.
  ///
  /// \return The total sum.
  ///
  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static T ScanExclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output,
                                   BinaryFunctor binaryFunctor,
                                   const T& initialValue)

    /// \brief Compute a segmented exclusive prefix sum operation on the input key value pairs.
    ///
    /// Computes a segmented exclusive prefix sum (or any user binary operation)
    /// on the \c keys and \c values ArrayHandle(s). Each segmented exclusive
    /// prefix sum is run on consecutive equal keys with the binary operation
    /// applied to all values inside that range. Once finished the result is
    /// stored in \c values_output ArrayHandle.
    ///
    template <typename T,
              typename U,
              typename KIn,
              typename VIn,
              typename VOut,
              class BinaryFunctor>
    SVTKM_CONT static void ScanExclusiveByKey(const svtkm::cont::ArrayHandle<T, KIn>& keys,
                                             const svtkm::cont::ArrayHandle<U, VIn>& values,
                                             svtkm::cont::ArrayHandle<U, VOut>& output,
                                             const U& initialValue,
                                             BinaryFunctor binaryFunctor);

  /// \brief Compute a segmented exclusive prefix sum operation on the input key value pairs.
  ///
  /// Computes a segmented inclusive prefix sum on the \c keys and \c values
  /// ArrayHandle(s). Each segmented inclusive prefix sum is run on consecutive
  /// equal keys with the binary operation svtkm::Add applied to all values inside
  /// that range. Once finished the result is stored in \c values_output ArrayHandle.
  ///
  template <typename T, typename U, class KIn, typename VIn, typename VOut>
  SVTKM_CONT static void ScanExclusiveByKey(const svtkm::cont::ArrayHandle<T, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<U, VIn>& values,
                                           svtkm::cont::ArrayHandle<U, VOut>& output);

  /// \brief Streaming version of scan exclusive
  ///
  /// Computes a scan one block at a time.
  ///
  /// \return The total sum.
  ///
  template <typename T, class CIn, class COut>
  SVTKM_CONT static T StreamingScanExclusive(const svtkm::Id numBlocks,
                                            const svtkm::cont::ArrayHandle<T, CIn>& input,
                                            svtkm::cont::ArrayHandle<T, COut>& output);

  /// \brief Compute an extended prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an extended prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. This produces an output
  /// array that contains both an inclusive scan (in elements [1, size)) and an
  /// exclusive scan (in elements [0, size-1)). By using ArrayHandleView,
  /// arrays containing both inclusive and exclusive scans can be generated
  /// from an extended scan with minimal memory usage.
  ///
  /// This algorithm may also be more efficient than ScanInclusive and
  /// ScanExclusive on some devices, since it may be able to avoid copying the
  /// total sum to the control environment to return.
  ///
  /// ScanExtended is similar to the stl partial sum function, exception that
  /// ScanExtended doesn't do a serial summation. This means that if you have
  /// defined a custom plus operator for T it must be associative, or you will
  /// get inconsistent results.
  ///
  /// This overload of ScanExtended uses svtkm::Add for the binary functor, and
  /// uses zero for the initial value of the scan operation.
  ///
  template <typename T, class CIn, class COut>
  SVTKM_CONT static void ScanExtended(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::cont::ArrayHandle<T, COut>& output);

  /// \brief Compute an extended prefix sum operation on the input ArrayHandle.
  ///
  /// Computes an extended prefix sum operation on the \c input ArrayHandle,
  /// storing the results in the \c output ArrayHandle. This produces an output
  /// array that contains both an inclusive scan (in elements [1, size)) and an
  /// exclusive scan (in elements [0, size-1)). By using ArrayHandleView,
  /// arrays containing both inclusive and exclusive scans can be generated
  /// from an extended scan with minimal memory usage.
  ///
  /// This algorithm may also be more efficient than ScanInclusive and
  /// ScanExclusive on some devices, since it may be able to avoid copying the
  /// total sum to the control environment to return.
  ///
  /// ScanExtended is similar to the stl partial sum function, exception that
  /// ScanExtended doesn't do a serial summation. This means that if you have
  /// defined a custom plus operator for T it must be associative, or you will
  /// get inconsistent results.
  ///
  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static void ScanExtended(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::cont::ArrayHandle<T, COut>& output,
                                     BinaryFunctor binaryFunctor,
                                     const T& initialValue);

  /// \brief Schedule many instances of a function to run on concurrent threads.
  ///
  /// Calls the \c functor on several threads. This is the function used in the
  /// control environment to spawn activity in the execution environment. \c
  /// functor is a function-like object that can be invoked with the calling
  /// specification <tt>functor(svtkm::Id index)</tt>. It also has a method called
  /// from the control environment to establish the error reporting buffer with
  /// the calling specification <tt>functor.SetErrorMessageBuffer(const
  /// svtkm::exec::internal::ErrorMessageBuffer &errorMessage)</tt>. This object
  /// can be stored in the functor's state such that if RaiseError is called on
  /// it in the execution environment, an ErrorExecution will be thrown from
  /// Schedule.
  ///
  /// The argument of the invoked functor uniquely identifies the thread or
  /// instance of the invocation. There should be one invocation for each index
  /// in the range [0, \c numInstances].
  ///
  template <class Functor>
  SVTKM_CONT static void Schedule(Functor functor, svtkm::Id numInstances);

  /// \brief Schedule many instances of a function to run on concurrent threads.
  ///
  /// Calls the \c functor on several threads. This is the function used in the
  /// control environment to spawn activity in the execution environment. \c
  /// functor is a function-like object that can be invoked with the calling
  /// specification <tt>functor(svtkm::Id3 index)</tt> or <tt>functor(svtkm::Id
  /// index)</tt>. It also has a method called from the control environment to
  /// establish the error reporting buffer with the calling specification
  /// <tt>functor.SetErrorMessageBuffer(const
  /// svtkm::exec::internal::ErrorMessageBuffer &errorMessage)</tt>. This object
  /// can be stored in the functor's state such that if RaiseError is called on
  /// it in the execution environment, an ErrorExecution will be thrown from
  /// Schedule.
  ///
  /// The argument of the invoked functor uniquely identifies the thread or
  /// instance of the invocation. It is at the device adapter's discretion
  /// whether to schedule on 1D or 3D indices, so the functor should have an
  /// operator() overload for each index type. If 3D indices are used, there is
  /// one invocation for every i, j, k value between [0, 0, 0] and \c rangeMax.
  /// If 1D indices are used, this Schedule behaves as if <tt>Schedule(functor,
  /// rangeMax[0]*rangeMax[1]*rangeMax[2])</tt> were called.
  ///
  template <class Functor, class IndiceType>
  SVTKM_CONT static void Schedule(Functor functor, svtkm::Id3 rangeMax);

  /// \brief Unstable ascending sort of input array.
  ///
  /// Sorts the contents of \c values so that they in ascending value. Doesn't
  /// guarantee stability
  ///
  template <typename T, class Storage>
  SVTKM_CONT static void Sort(svtkm::cont::ArrayHandle<T, Storage>& values);

  /// \brief Unstable ascending sort of input array.
  ///
  /// Sorts the contents of \c values so that they in ascending value based
  /// on the custom compare functor.
  ///
  /// BinaryCompare should be a strict weak ordering comparison operator
  ///
  template <typename T, class Storage, class BinaryCompare>
  SVTKM_CONT static void Sort(svtkm::cont::ArrayHandle<T, Storage>& values,
                             BinaryCompare binary_compare);

  /// \brief Unstable ascending sort of keys and values.
  ///
  /// Sorts the contents of \c keys and \c values so that they in ascending value based
  /// on the values of keys.
  ///
  template <typename T, typename U, class StorageT, class StorageU>
  SVTKM_CONT static void SortByKey(svtkm::cont::ArrayHandle<T, StorageT>& keys,
                                  svtkm::cont::ArrayHandle<U, StorageU>& values);

  /// \brief Unstable ascending sort of keys and values.
  ///
  /// Sorts the contents of \c keys and \c values so that they in ascending value based
  /// on the custom compare functor.
  ///
  /// BinaryCompare should be a strict weak ordering comparison operator
  ///
  template <typename T, typename U, class StorageT, class StorageU, class BinaryCompare>
  SVTKM_CONT static void SortByKey(svtkm::cont::ArrayHandle<T, StorageT>& keys,
                                  svtkm::cont::ArrayHandle<U, StorageU>& values,
                                  BinaryCompare binary_compare)

    /// \brief Completes any asynchronous operations running on the device.
    ///
    /// Waits for any asynchronous operations running on the device to complete.
    ///
    SVTKM_CONT static void Synchronize();

  /// \brief Apply a given binary operation function element-wise to input arrays.
  ///
  /// Apply the give binary operation to pairs of elements from the two input array
  /// \c input1 and \c input2. The number of elements in the input arrays do not
  /// have to be the same, in this case, only the smaller of the two numbers of elements
  /// will be applied.
  /// Outputs of the binary operation is stored in \c output.
  ///
  template <typename T,
            typename U,
            typename V,
            typename StorageT,
            typename StorageU,
            typename StorageV,
            typename BinaryFunctor>
  SVTKM_CONT static void Transform(const svtkm::cont::ArrayHandle<T, StorageT>& input1,
                                  const svtkm::cont::ArrayHandle<U, StorageU>& input2,
                                  svtkm::cont::ArrayHandle<V, StorageV>& output,
                                  BinaryFunctor binaryFunctor);

  /// \brief Reduce an array to only the unique values it contains
  ///
  /// Removes all duplicate values in \c values that are adjacent to each
  /// other. Which means you should sort the input array unless you want
  /// duplicate values that aren't adjacent. Note the values array size might
  /// be modified by this operation.
  ///
  template <typename T, class Storage>
  SVTKM_CONT static void Unique(svtkm::cont::ArrayHandle<T, Storage>& values);

  /// \brief Reduce an array to only the unique values it contains
  ///
  /// Removes all duplicate values in \c values that are adjacent to each
  /// other. Which means you should sort the input array unless you want
  /// duplicate values that aren't adjacent. Note the values array size might
  /// be modified by this operation.
  ///
  /// Uses the custom binary predicate Comparison to determine if something
  /// is unique. The predicate must return true if the two items are the same.
  ///
  template <typename T, class Storage, class BinaryCompare>
  SVTKM_CONT static void Unique(svtkm::cont::ArrayHandle<T, Storage>& values,
                               BinaryCompare binary_compare);

  /// \brief Output is the last index in input for each item in values that wouldn't alter the ordering of input
  ///
  /// UpperBounds is a vectorized search. From each value in \c values it finds
  /// the last place the item can be inserted in the ordered \c input array and
  /// stores the index in \c output.
  ///
  /// \par Requirements:
  /// \arg \c input must already be sorted
  ///
  template <typename T, class CIn, class CVal, class COut>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output);

  /// \brief Output is the last index in input for each item in values that wouldn't alter the ordering of input
  ///
  /// LowerBounds is a vectorized search. From each value in \c values it finds
  /// the last place the item can be inserted in the ordered \c input array and
  /// stores the index in \c output. Uses the custom comparison functor to
  /// determine the correct location for each item.
  ///
  /// \par Requirements:
  /// \arg \c input must already be sorted
  ///
  template <typename T, class CIn, class CVal, class COut, class BinaryCompare>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output,
                                    BinaryCompare binary_compare);

  /// \brief A special version of UpperBounds that does an in place operation.
  ///
  /// This version of lower bounds performs an in place operation where each
  /// value in the \c values_output array is replaced by the last index in
  /// \c input where it occurs. Because this is an in place operation, the type
  /// of the arrays is limited to svtkm::Id.
  ///
  template <class CIn, class COut>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<svtkm::Id, CIn>& input,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& values_output);
};
#else  // SVTKM_DOXYGEN_ONLY
  ;
#endif //SVTKM_DOXYGEN_ONLY

/// \brief Class providing a device-specific timer.
///
/// The class provide the actual implementation used by svtkm::cont::Timer.
/// A default implementation is provided but device adapters should provide
/// one (in conjunction with DeviceAdapterAlgorithm) where appropriate.  The
/// interface for this class is exactly the same as svtkm::cont::Timer.
///
template <class DeviceAdapterTag>
class DeviceAdapterTimerImplementation
{
public:
  struct TimeStamp
  {
    svtkm::Int64 Seconds;
    svtkm::Int64 Microseconds;
  };
  /// When a timer is constructed, all threads are synchronized and the
  /// current time is marked so that GetElapsedTime returns the number of
  /// seconds elapsed since the construction.
  SVTKM_CONT DeviceAdapterTimerImplementation() { this->Reset(); }

  /// Resets the timer. All further calls to GetElapsedTime will report the
  /// number of seconds elapsed since the call to this. This method
  /// synchronizes all asynchronous operations.
  ///
  SVTKM_CONT void Reset()
  {
    this->StartReady = false;
    this->StopReady = false;
  }

  SVTKM_CONT void Start()
  {
    this->Reset();
    this->StartTime = this->GetCurrentTime();
    this->StartReady = true;
  }

  SVTKM_CONT void Stop()
  {
    this->StopTime = this->GetCurrentTime();
    this->StopReady = true;
  }

  SVTKM_CONT bool Started() const { return this->StartReady; }

  SVTKM_CONT bool Stopped() const { return this->StopReady; }

  SVTKM_CONT bool Ready() const { return true; }

  /// Returns the elapsed time in seconds between the construction of this
  /// class or the last call to Reset and the time this function is called. The
  /// time returned is measured in wall time. GetElapsedTime may be called any
  /// number of times to get the progressive time. This method synchronizes all
  /// asynchronous operations.
  ///
  SVTKM_CONT svtkm::Float64 GetElapsedTime() const
  {
    assert(this->StartReady);
    if (!this->StartReady)
    {
      SVTKM_LOG_S(svtkm::cont::LogLevel::Error,
                 "Start() function should be called first then trying to call GetElapsedTime().");
      return 0;
    }

    TimeStamp startTime = this->StartTime;
    TimeStamp stopTime = this->StopReady ? this->StopTime : this->GetCurrentTime();

    svtkm::Float64 elapsedTime;
    elapsedTime = svtkm::Float64(stopTime.Seconds - startTime.Seconds);
    elapsedTime +=
      (svtkm::Float64(stopTime.Microseconds - startTime.Microseconds) / svtkm::Float64(1000000));

    return elapsedTime;
  }

  SVTKM_CONT TimeStamp GetCurrentTime() const
  {
    svtkm::cont::DeviceAdapterAlgorithm<DeviceAdapterTag>::Synchronize();

    TimeStamp retval;
#ifdef _WIN32
    timeb currentTime;
    ::ftime(&currentTime);
    retval.Seconds = currentTime.time;
    retval.Microseconds = 1000 * currentTime.millitm;
#else
    timeval currentTime;
    gettimeofday(&currentTime, nullptr);
    retval.Seconds = currentTime.tv_sec;
    retval.Microseconds = currentTime.tv_usec;
#endif
    return retval;
  }

  bool StartReady;
  bool StopReady;
  TimeStamp StartTime;
  TimeStamp StopTime;
};

/// \brief Class providing a device-specific runtime support detector.
///
/// The class provide the actual implementation used by
/// svtkm::cont::RuntimeDeviceInformation.
///
/// A default implementation is provided but device adapters which require
/// physical hardware or other special runtime requirements should provide
/// one (in conjunction with DeviceAdapterAlgorithm) where appropriate.
///
template <class DeviceAdapterTag>
class DeviceAdapterRuntimeDetector
{
public:
/// Returns true if the given device adapter is supported on the current
/// machine.
///
/// No default implementation is provided as it could possible cause
/// ODR violations when headers are included in differing order.
#ifdef SVTKM_DOXYGEN_ONLY
  SVTKM_CONT bool Exists() const;
#endif
};

/// \brief Class providing a device-specific support for atomic operations.
///
/// AtomicInterfaceControl provides atomic operations for the control
/// environment, and may be subclassed to implement the device interface when
/// appropriate for a CPU-based device.
template <typename DeviceTag>
class AtomicInterfaceExecution;

/// \brief Class providing a device-specific support for selecting the optimal
/// Task type for a given worklet.
///
/// When worklets are launched inside the execution environment we need to
/// ask the device adapter what is the preferred execution style, be it
/// a tiled iteration pattern, or strided. This class
///
/// By default if not specialized for a device adapter the default
/// is to use svtkm::exec::internal::TaskSingular
///
/// The class provide the actual implementation used by
/// svtkm::cont::DeviceTaskTypes.
///
template <typename DeviceTag>
class DeviceTaskTypes;
}
} // namespace svtkm::cont

#endif //svtk_m_cont_DeviceAdapterAlgorithm_h