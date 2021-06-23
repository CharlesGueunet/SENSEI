//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#ifndef svtk_m_cont_internal_DeviceAdapterAlgorithmGeneral_h
#define svtk_m_cont_internal_DeviceAdapterAlgorithmGeneral_h

#include <svtkm/cont/ArrayHandle.h>
#include <svtkm/cont/ArrayHandleDiscard.h>
#include <svtkm/cont/ArrayHandleImplicit.h>
#include <svtkm/cont/ArrayHandleIndex.h>
#include <svtkm/cont/ArrayHandleStreaming.h>
#include <svtkm/cont/ArrayHandleView.h>
#include <svtkm/cont/ArrayHandleZip.h>
#include <svtkm/cont/BitField.h>
#include <svtkm/cont/Logging.h>
#include <svtkm/cont/internal/FunctorsGeneral.h>

#include <svtkm/exec/internal/ErrorMessageBuffer.h>
#include <svtkm/exec/internal/TaskSingular.h>

#include <svtkm/BinaryPredicates.h>
#include <svtkm/TypeTraits.h>

#include <svtkm/internal/Windows.h>

#include <type_traits>

namespace svtkm
{
namespace cont
{
namespace internal
{

/// \brief
///
/// This struct provides algorithms that implement "general" device adapter
/// algorithms. If a device adapter provides implementations for Schedule,
/// and Synchronize, the rest of the algorithms can be implemented by calling
/// these functions.
///
/// It should be noted that we recommend that you also implement Sort,
/// ScanInclusive, and ScanExclusive for improved performance.
///
/// An easy way to implement the DeviceAdapterAlgorithm specialization is to
/// subclass this and override the implementation of methods as necessary.
/// As an example, the code would look something like this.
///
/// \code{.cpp}
/// template<>
/// struct DeviceAdapterAlgorithm<DeviceAdapterTagFoo>
///    : DeviceAdapterAlgorithmGeneral<DeviceAdapterAlgorithm<DeviceAdapterTagFoo>,
///                                    DeviceAdapterTagFoo>
/// {
///   template<class Functor>
///   SVTKM_CONT static void Schedule(Functor functor,
///                                        svtkm::Id numInstances)
///   {
///     ...
///   }
///
///   template<class Functor>
///   SVTKM_CONT static void Schedule(Functor functor,
///                                        svtkm::Id3 maxRange)
///   {
///     ...
///   }
///
///   SVTKM_CONT static void Synchronize()
///   {
///     ...
///   }
/// };
/// \endcode
///
/// You might note that DeviceAdapterAlgorithmGeneral has two template
/// parameters that are redundant. Although the first parameter, the class for
/// the actual DeviceAdapterAlgorithm class containing Schedule, and
/// Synchronize is the same as DeviceAdapterAlgorithm<DeviceAdapterTag>, it is
/// made a separate template parameter to avoid a recursive dependence between
/// DeviceAdapterAlgorithmGeneral.h and DeviceAdapterAlgorithm.h
///
template <class DerivedAlgorithm, class DeviceAdapterTag>
struct DeviceAdapterAlgorithmGeneral
{
  //--------------------------------------------------------------------------
  // Get Execution Value
  // This method is used internally to get a single element from the execution
  // array. Might want to expose this and/or allow actual device adapter
  // implementations to provide one.
private:
  template <typename T, class CIn>
  SVTKM_CONT static T GetExecutionValue(const svtkm::cont::ArrayHandle<T, CIn>& input, svtkm::Id index)
  {
    using OutputArrayType = svtkm::cont::ArrayHandle<T, svtkm::cont::StorageTagBasic>;

    OutputArrayType output;
    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(1, DeviceAdapterTag());

    CopyKernel<decltype(inputPortal), decltype(outputPortal)> kernel(
      inputPortal, outputPortal, index);

    DerivedAlgorithm::Schedule(kernel, 1);

    return output.GetPortalConstControl().Get(0);
  }

public:
  //--------------------------------------------------------------------------
  // BitFieldToUnorderedSet
  template <typename IndicesStorage>
  SVTKM_CONT static svtkm::Id BitFieldToUnorderedSet(
    const svtkm::cont::BitField& bits,
    svtkm::cont::ArrayHandle<Id, IndicesStorage>& indices)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id numBits = bits.GetNumberOfBits();

    auto bitsPortal = bits.PrepareForInput(DeviceAdapterTag{});
    auto indicesPortal = indices.PrepareForOutput(numBits, DeviceAdapterTag{});

    std::atomic<svtkm::UInt64> popCount;
    popCount.store(0, std::memory_order_seq_cst);

    using Functor = BitFieldToUnorderedSetFunctor<decltype(bitsPortal), decltype(indicesPortal)>;
    Functor functor{ bitsPortal, indicesPortal, popCount };

    DerivedAlgorithm::Schedule(functor, functor.GetNumberOfInstances());
    DerivedAlgorithm::Synchronize();

    numBits = static_cast<svtkm::Id>(popCount.load(std::memory_order_seq_cst));

    indices.Shrink(numBits);
    return numBits;
  }

  //--------------------------------------------------------------------------
  // Copy
  template <typename T, typename U, class CIn, class COut>
  SVTKM_CONT static void Copy(const svtkm::cont::ArrayHandle<T, CIn>& input,
                             svtkm::cont::ArrayHandle<U, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    const svtkm::Id inSize = input.GetNumberOfValues();
    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(inSize, DeviceAdapterTag());

    CopyKernel<decltype(inputPortal), decltype(outputPortal)> kernel(inputPortal, outputPortal);
    DerivedAlgorithm::Schedule(kernel, inSize);
  }

  //--------------------------------------------------------------------------
  // CopyIf
  template <typename T, typename U, class CIn, class CStencil, class COut, class UnaryPredicate>
  SVTKM_CONT static void CopyIf(const svtkm::cont::ArrayHandle<T, CIn>& input,
                               const svtkm::cont::ArrayHandle<U, CStencil>& stencil,
                               svtkm::cont::ArrayHandle<T, COut>& output,
                               UnaryPredicate unary_predicate)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    SVTKM_ASSERT(input.GetNumberOfValues() == stencil.GetNumberOfValues());
    svtkm::Id arrayLength = stencil.GetNumberOfValues();

    using IndexArrayType = svtkm::cont::ArrayHandle<svtkm::Id, svtkm::cont::StorageTagBasic>;
    IndexArrayType indices;

    auto stencilPortal = stencil.PrepareForInput(DeviceAdapterTag());
    auto indexPortal = indices.PrepareForOutput(arrayLength, DeviceAdapterTag());

    StencilToIndexFlagKernel<decltype(stencilPortal), decltype(indexPortal), UnaryPredicate>
      indexKernel(stencilPortal, indexPortal, unary_predicate);

    DerivedAlgorithm::Schedule(indexKernel, arrayLength);

    svtkm::Id outArrayLength = DerivedAlgorithm::ScanExclusive(indices, indices);

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(outArrayLength, DeviceAdapterTag());

    CopyIfKernel<decltype(inputPortal),
                 decltype(stencilPortal),
                 decltype(indexPortal),
                 decltype(outputPortal),
                 UnaryPredicate>
      copyKernel(inputPortal, stencilPortal, indexPortal, outputPortal, unary_predicate);
    DerivedAlgorithm::Schedule(copyKernel, arrayLength);
  }

  template <typename T, typename U, class CIn, class CStencil, class COut>
  SVTKM_CONT static void CopyIf(const svtkm::cont::ArrayHandle<T, CIn>& input,
                               const svtkm::cont::ArrayHandle<U, CStencil>& stencil,
                               svtkm::cont::ArrayHandle<T, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    ::svtkm::NotZeroInitialized unary_predicate;
    DerivedAlgorithm::CopyIf(input, stencil, output, unary_predicate);
  }

  //--------------------------------------------------------------------------
  // CopySubRange
  template <typename T, typename U, class CIn, class COut>
  SVTKM_CONT static bool CopySubRange(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::Id inputStartIndex,
                                     svtkm::Id numberOfElementsToCopy,
                                     svtkm::cont::ArrayHandle<U, COut>& output,
                                     svtkm::Id outputIndex = 0)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    const svtkm::Id inSize = input.GetNumberOfValues();

    // Check if the ranges overlap and fail if they do.
    if (input == output && ((outputIndex >= inputStartIndex &&
                             outputIndex < inputStartIndex + numberOfElementsToCopy) ||
                            (inputStartIndex >= outputIndex &&
                             inputStartIndex < outputIndex + numberOfElementsToCopy)))
    {
      return false;
    }

    if (inputStartIndex < 0 || numberOfElementsToCopy < 0 || outputIndex < 0 ||
        inputStartIndex >= inSize)
    { //invalid parameters
      return false;
    }

    //determine if the numberOfElementsToCopy needs to be reduced
    if (inSize < (inputStartIndex + numberOfElementsToCopy))
    { //adjust the size
      numberOfElementsToCopy = (inSize - inputStartIndex);
    }

    const svtkm::Id outSize = output.GetNumberOfValues();
    const svtkm::Id copyOutEnd = outputIndex + numberOfElementsToCopy;
    if (outSize < copyOutEnd)
    { //output is not large enough
      if (outSize == 0)
      { //since output has nothing, just need to allocate to correct length
        output.Allocate(copyOutEnd);
      }
      else
      { //we currently have data in this array, so preserve it in the new
        //resized array
        svtkm::cont::ArrayHandle<U, COut> temp;
        temp.Allocate(copyOutEnd);
        DerivedAlgorithm::CopySubRange(output, 0, outSize, temp);
        output = temp;
      }
    }

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForInPlace(DeviceAdapterTag());

    CopyKernel<decltype(inputPortal), decltype(outputPortal)> kernel(
      inputPortal, outputPortal, inputStartIndex, outputIndex);
    DerivedAlgorithm::Schedule(kernel, numberOfElementsToCopy);
    return true;
  }

  //--------------------------------------------------------------------------
  // Count Set Bits
  SVTKM_CONT static svtkm::Id CountSetBits(const svtkm::cont::BitField& bits)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    auto bitsPortal = bits.PrepareForInput(DeviceAdapterTag{});

    std::atomic<svtkm::UInt64> popCount;
    popCount.store(0, std::memory_order_relaxed);

    using Functor = CountSetBitsFunctor<decltype(bitsPortal)>;
    Functor functor{ bitsPortal, popCount };

    DerivedAlgorithm::Schedule(functor, functor.GetNumberOfInstances());
    DerivedAlgorithm::Synchronize();

    return static_cast<svtkm::Id>(popCount.load(std::memory_order_seq_cst));
  }

  //--------------------------------------------------------------------------
  // Fill Bit Field (bool, resize)
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, bool value, svtkm::Id numBits)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    if (numBits == 0)
    {
      bits.Shrink(0);
      return;
    }

    auto portal = bits.PrepareForOutput(numBits, DeviceAdapterTag{});

    using WordType =
      typename svtkm::cont::BitField::template ExecutionTypes<DeviceAdapterTag>::WordTypePreferred;

    using Functor = FillBitFieldFunctor<decltype(portal), WordType>;
    Functor functor{ portal, value ? ~WordType{ 0 } : WordType{ 0 } };

    const svtkm::Id numWords = portal.template GetNumberOfWords<WordType>();
    DerivedAlgorithm::Schedule(functor, numWords);
  }

  //--------------------------------------------------------------------------
  // Fill Bit Field (bool)
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, bool value)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    const svtkm::Id numBits = bits.GetNumberOfBits();
    if (numBits == 0)
    {
      return;
    }

    auto portal = bits.PrepareForOutput(numBits, DeviceAdapterTag{});

    using WordType =
      typename svtkm::cont::BitField::template ExecutionTypes<DeviceAdapterTag>::WordTypePreferred;

    using Functor = FillBitFieldFunctor<decltype(portal), WordType>;
    Functor functor{ portal, value ? ~WordType{ 0 } : WordType{ 0 } };

    const svtkm::Id numWords = portal.template GetNumberOfWords<WordType>();
    DerivedAlgorithm::Schedule(functor, numWords);
  }

  //--------------------------------------------------------------------------
  // Fill Bit Field (mask, resize)
  template <typename WordType>
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, WordType word, svtkm::Id numBits)
  {
    SVTKM_STATIC_ASSERT_MSG(svtkm::cont::BitField::IsValidWordType<WordType>{}, "Invalid word type.");

    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    if (numBits == 0)
    {
      bits.Shrink(0);
      return;
    }

    auto portal = bits.PrepareForOutput(numBits, DeviceAdapterTag{});

    // If less than 32 bits, repeat the word until we get a 32 bit pattern.
    // Using this for the pattern prevents races while writing small numbers
    // to adjacent memory locations.
    auto repWord = RepeatTo32BitsIfNeeded(word);
    using RepWordType = decltype(repWord);

    using Functor = FillBitFieldFunctor<decltype(portal), RepWordType>;
    Functor functor{ portal, repWord };

    const svtkm::Id numWords = portal.template GetNumberOfWords<RepWordType>();
    DerivedAlgorithm::Schedule(functor, numWords);
  }

  //--------------------------------------------------------------------------
  // Fill Bit Field (mask)
  template <typename WordType>
  SVTKM_CONT static void Fill(svtkm::cont::BitField& bits, WordType word)
  {
    SVTKM_STATIC_ASSERT_MSG(svtkm::cont::BitField::IsValidWordType<WordType>{}, "Invalid word type.");
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    const svtkm::Id numBits = bits.GetNumberOfBits();
    if (numBits == 0)
    {
      return;
    }

    auto portal = bits.PrepareForOutput(numBits, DeviceAdapterTag{});

    // If less than 32 bits, repeat the word until we get a 32 bit pattern.
    // Using this for the pattern prevents races while writing small numbers
    // to adjacent memory locations.
    auto repWord = RepeatTo32BitsIfNeeded(word);
    using RepWordType = decltype(repWord);

    using Functor = FillBitFieldFunctor<decltype(portal), RepWordType>;
    Functor functor{ portal, repWord };

    const svtkm::Id numWords = portal.template GetNumberOfWords<RepWordType>();
    DerivedAlgorithm::Schedule(functor, numWords);
  }

  //--------------------------------------------------------------------------
  // Fill ArrayHandle
  template <typename T, typename S>
  SVTKM_CONT static void Fill(svtkm::cont::ArrayHandle<T, S>& handle, const T& value)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    const svtkm::Id numValues = handle.GetNumberOfValues();
    if (numValues == 0)
    {
      return;
    }

    auto portal = handle.PrepareForOutput(numValues, DeviceAdapterTag{});
    FillArrayHandleFunctor<decltype(portal)> functor{ portal, value };
    DerivedAlgorithm::Schedule(functor, numValues);
  }

  //--------------------------------------------------------------------------
  // Fill ArrayHandle (resize)
  template <typename T, typename S>
  SVTKM_CONT static void Fill(svtkm::cont::ArrayHandle<T, S>& handle,
                             const T& value,
                             const svtkm::Id numValues)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);
    if (numValues == 0)
    {
      handle.Shrink(0);
      return;
    }

    auto portal = handle.PrepareForOutput(numValues, DeviceAdapterTag{});
    FillArrayHandleFunctor<decltype(portal)> functor{ portal, value };
    DerivedAlgorithm::Schedule(functor, numValues);
  }

  //--------------------------------------------------------------------------
  // Lower Bounds
  template <typename T, class CIn, class CVal, class COut>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id arraySize = values.GetNumberOfValues();

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto valuesPortal = values.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(arraySize, DeviceAdapterTag());

    LowerBoundsKernel<decltype(inputPortal), decltype(valuesPortal), decltype(outputPortal)> kernel(
      inputPortal, valuesPortal, outputPortal);

    DerivedAlgorithm::Schedule(kernel, arraySize);
  }

  template <typename T, class CIn, class CVal, class COut, class BinaryCompare>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output,
                                    BinaryCompare binary_compare)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id arraySize = values.GetNumberOfValues();

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto valuesPortal = values.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(arraySize, DeviceAdapterTag());

    LowerBoundsComparisonKernel<decltype(inputPortal),
                                decltype(valuesPortal),
                                decltype(outputPortal),
                                BinaryCompare>
      kernel(inputPortal, valuesPortal, outputPortal, binary_compare);

    DerivedAlgorithm::Schedule(kernel, arraySize);
  }

  template <class CIn, class COut>
  SVTKM_CONT static void LowerBounds(const svtkm::cont::ArrayHandle<svtkm::Id, CIn>& input,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& values_output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DeviceAdapterAlgorithmGeneral<DerivedAlgorithm, DeviceAdapterTag>::LowerBounds(
      input, values_output, values_output);
  }

  //--------------------------------------------------------------------------
  // Reduce
  template <typename T, typename U, class CIn>
  SVTKM_CONT static U Reduce(const svtkm::cont::ArrayHandle<T, CIn>& input, U initialValue)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::Reduce(input, initialValue, svtkm::Add());
  }

  template <typename T, typename U, class CIn, class BinaryFunctor>
  SVTKM_CONT static U Reduce(const svtkm::cont::ArrayHandle<T, CIn>& input,
                            U initialValue,
                            BinaryFunctor binary_functor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    //Crazy Idea:
    //We create a implicit array handle that wraps the input
    //array handle. The implicit functor is passed the input array handle, and
    //the number of elements it needs to sum. This way the implicit handle
    //acts as the first level reduction. Say for example reducing 16 values
    //at a time.
    //
    //Now that we have an implicit array that is 1/16 the length of full array
    //we can use scan inclusive to compute the final sum
    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    ReduceKernel<decltype(inputPortal), U, BinaryFunctor> kernel(
      inputPortal, initialValue, binary_functor);

    svtkm::Id length = (input.GetNumberOfValues() / 16);
    length += (input.GetNumberOfValues() % 16 == 0) ? 0 : 1;
    auto reduced = svtkm::cont::make_ArrayHandleImplicit(kernel, length);

    svtkm::cont::ArrayHandle<U, svtkm::cont::StorageTagBasic> inclusiveScanStorage;
    const U scanResult =
      DerivedAlgorithm::ScanInclusive(reduced, inclusiveScanStorage, binary_functor);
    return scanResult;
  }

  //--------------------------------------------------------------------------
  // Streaming Reduce
  template <typename T, typename U, class CIn>
  SVTKM_CONT static U StreamingReduce(const svtkm::Id numBlocks,
                                     const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     U initialValue)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::StreamingReduce(numBlocks, input, initialValue, svtkm::Add());
  }

  template <typename T, typename U, class CIn, class BinaryFunctor>
  SVTKM_CONT static U StreamingReduce(const svtkm::Id numBlocks,
                                     const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     U initialValue,
                                     BinaryFunctor binary_functor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id fullSize = input.GetNumberOfValues();
    svtkm::Id blockSize = fullSize / numBlocks;
    if (fullSize % numBlocks != 0)
      blockSize += 1;

    U lastResult = svtkm::TypeTraits<U>::ZeroInitialization();
    for (svtkm::Id block = 0; block < numBlocks; block++)
    {
      svtkm::Id numberOfInstances = blockSize;
      if (block == numBlocks - 1)
        numberOfInstances = fullSize - blockSize * block;

      svtkm::cont::ArrayHandleStreaming<svtkm::cont::ArrayHandle<T, CIn>> streamIn(
        input, block, blockSize, numberOfInstances);

      if (block == 0)
        lastResult = DerivedAlgorithm::Reduce(streamIn, initialValue, binary_functor);
      else
        lastResult = DerivedAlgorithm::Reduce(streamIn, lastResult, binary_functor);
    }
    return lastResult;
  }

  //--------------------------------------------------------------------------
  // Reduce By Key
  template <typename T,
            typename U,
            class KIn,
            class VIn,
            class KOut,
            class VOut,
            class BinaryFunctor>
  SVTKM_CONT static void ReduceByKey(const svtkm::cont::ArrayHandle<T, KIn>& keys,
                                    const svtkm::cont::ArrayHandle<U, VIn>& values,
                                    svtkm::cont::ArrayHandle<T, KOut>& keys_output,
                                    svtkm::cont::ArrayHandle<U, VOut>& values_output,
                                    BinaryFunctor binary_functor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    using KeysOutputType = svtkm::cont::ArrayHandle<U, KOut>;

    SVTKM_ASSERT(keys.GetNumberOfValues() == values.GetNumberOfValues());
    const svtkm::Id numberOfKeys = keys.GetNumberOfValues();

    if (numberOfKeys <= 1)
    { //we only have a single key/value so that is our output
      DerivedAlgorithm::Copy(keys, keys_output);
      DerivedAlgorithm::Copy(values, values_output);
      return;
    }

    //we need to determine based on the keys what is the keystate for
    //each key. The states are start, middle, end of a series and the special
    //state start and end of a series
    svtkm::cont::ArrayHandle<ReduceKeySeriesStates> keystate;

    {
      auto inputPortal = keys.PrepareForInput(DeviceAdapterTag());
      auto keyStatePortal = keystate.PrepareForOutput(numberOfKeys, DeviceAdapterTag());
      ReduceStencilGeneration<decltype(inputPortal), decltype(keyStatePortal)> kernel(
        inputPortal, keyStatePortal);
      DerivedAlgorithm::Schedule(kernel, numberOfKeys);
    }

    //next step is we need to reduce the values for each key. This is done
    //by running an inclusive scan over the values array using the stencil.
    //
    // this inclusive scan will write out two values, the first being
    // the value summed currently, the second being 0 or 1, with 1 being used
    // when this is a value of a key we need to write ( END or START_AND_END)
    {
      svtkm::cont::ArrayHandle<ReduceKeySeriesStates> stencil;
      svtkm::cont::ArrayHandle<U, VOut> reducedValues;

      auto scanInput = svtkm::cont::make_ArrayHandleZip(values, keystate);
      auto scanOutput = svtkm::cont::make_ArrayHandleZip(reducedValues, stencil);

      DerivedAlgorithm::ScanInclusive(
        scanInput, scanOutput, ReduceByKeyAdd<BinaryFunctor>(binary_functor));

      //at this point we are done with keystate, so free the memory
      keystate.ReleaseResources();

      // all we need know is an efficient way of doing the write back to the
      // reduced global memory. this is done by using CopyIf with the
      // stencil and values we just created with the inclusive scan
      DerivedAlgorithm::CopyIf(reducedValues, stencil, values_output, ReduceByKeyUnaryStencilOp());

    } //release all temporary memory

    // Don't bother with the keys_output if it's an ArrayHandleDiscard -- there
    // will be a runtime exception in Unique() otherwise:
    if (!svtkm::cont::IsArrayHandleDiscard<KeysOutputType>::value)
    {
      //find all the unique keys
      DerivedAlgorithm::Copy(keys, keys_output);
      DerivedAlgorithm::Unique(keys_output);
    }
  }

  //--------------------------------------------------------------------------
  // Scan Exclusive
  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static T ScanExclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output,
                                   BinaryFunctor binaryFunctor,
                                   const T& initialValue)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id numValues = input.GetNumberOfValues();
    if (numValues <= 0)
    {
      output.Shrink(0);
      return initialValue;
    }

    svtkm::cont::ArrayHandle<T, svtkm::cont::StorageTagBasic> inclusiveScan;
    T result = DerivedAlgorithm::ScanInclusive(input, inclusiveScan, binaryFunctor);

    auto inputPortal = inclusiveScan.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(numValues, DeviceAdapterTag());

    InclusiveToExclusiveKernel<decltype(inputPortal), decltype(outputPortal), BinaryFunctor>
      inclusiveToExclusive(inputPortal, outputPortal, binaryFunctor, initialValue);

    DerivedAlgorithm::Schedule(inclusiveToExclusive, numValues);

    return binaryFunctor(initialValue, result);
  }

  template <typename T, class CIn, class COut>
  SVTKM_CONT static T ScanExclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::ScanExclusive(
      input, output, svtkm::Sum(), svtkm::TypeTraits<T>::ZeroInitialization());
  }

  //--------------------------------------------------------------------------
  // Scan Exclusive Extend
  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static void ScanExtended(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::cont::ArrayHandle<T, COut>& output,
                                     BinaryFunctor binaryFunctor,
                                     const T& initialValue)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id numValues = input.GetNumberOfValues();
    if (numValues <= 0)
    {
      output.Allocate(1);
      output.GetPortalControl().Set(0, initialValue);
      return;
    }

    svtkm::cont::ArrayHandle<T, svtkm::cont::StorageTagBasic> inclusiveScan;
    T result = DerivedAlgorithm::ScanInclusive(input, inclusiveScan, binaryFunctor);

    auto inputPortal = inclusiveScan.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(numValues + 1, DeviceAdapterTag());

    InclusiveToExtendedKernel<decltype(inputPortal), decltype(outputPortal), BinaryFunctor>
      inclusiveToExtended(inputPortal,
                          outputPortal,
                          binaryFunctor,
                          initialValue,
                          binaryFunctor(initialValue, result));

    DerivedAlgorithm::Schedule(inclusiveToExtended, numValues + 1);
  }

  template <typename T, class CIn, class COut>
  SVTKM_CONT static void ScanExtended(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                     svtkm::cont::ArrayHandle<T, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DerivedAlgorithm::ScanExtended(
      input, output, svtkm::Sum(), svtkm::TypeTraits<T>::ZeroInitialization());
  }

  //--------------------------------------------------------------------------
  // Scan Exclusive By Key
  template <typename KeyT,
            typename ValueT,
            typename KIn,
            typename VIn,
            typename VOut,
            class BinaryFunctor>
  SVTKM_CONT static void ScanExclusiveByKey(const svtkm::cont::ArrayHandle<KeyT, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<ValueT, VIn>& values,
                                           svtkm::cont::ArrayHandle<ValueT, VOut>& output,
                                           const ValueT& initialValue,
                                           BinaryFunctor binaryFunctor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    SVTKM_ASSERT(keys.GetNumberOfValues() == values.GetNumberOfValues());

    // 0. Special case for 0 and 1 element input
    svtkm::Id numberOfKeys = keys.GetNumberOfValues();

    if (numberOfKeys == 0)
    {
      return;
    }
    else if (numberOfKeys == 1)
    {
      output.PrepareForOutput(1, DeviceAdapterTag());
      output.GetPortalControl().Set(0, initialValue);
      return;
    }

    // 1. Create head flags
    //we need to determine based on the keys what is the keystate for
    //each key. The states are start, middle, end of a series and the special
    //state start and end of a series
    svtkm::cont::ArrayHandle<ReduceKeySeriesStates> keystate;

    {
      auto inputPortal = keys.PrepareForInput(DeviceAdapterTag());
      auto keyStatePortal = keystate.PrepareForOutput(numberOfKeys, DeviceAdapterTag());
      ReduceStencilGeneration<decltype(inputPortal), decltype(keyStatePortal)> kernel(
        inputPortal, keyStatePortal);
      DerivedAlgorithm::Schedule(kernel, numberOfKeys);
    }

    // 2. Shift input and initialize elements at head flags position to initValue
    svtkm::cont::ArrayHandle<ValueT, svtkm::cont::StorageTagBasic> temp;
    {
      auto inputPortal = values.PrepareForInput(DeviceAdapterTag());
      auto keyStatePortal = keystate.PrepareForInput(DeviceAdapterTag());
      auto tempPortal = temp.PrepareForOutput(numberOfKeys, DeviceAdapterTag());

      ShiftCopyAndInit<ValueT,
                       decltype(inputPortal),
                       decltype(keyStatePortal),
                       decltype(tempPortal)>
        kernel(inputPortal, keyStatePortal, tempPortal, initialValue);
      DerivedAlgorithm::Schedule(kernel, numberOfKeys);
    }
    // 3. Perform a ScanInclusiveByKey
    DerivedAlgorithm::ScanInclusiveByKey(keys, temp, output, binaryFunctor);
  }

  template <typename KeyT, typename ValueT, class KIn, typename VIn, typename VOut>
  SVTKM_CONT static void ScanExclusiveByKey(const svtkm::cont::ArrayHandle<KeyT, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<ValueT, VIn>& values,
                                           svtkm::cont::ArrayHandle<ValueT, VOut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DerivedAlgorithm::ScanExclusiveByKey(
      keys, values, output, svtkm::TypeTraits<ValueT>::ZeroInitialization(), svtkm::Sum());
  }

  //--------------------------------------------------------------------------
  // Streaming exclusive scan
  template <typename T, class CIn, class COut>
  SVTKM_CONT static T StreamingScanExclusive(const svtkm::Id numBlocks,
                                            const svtkm::cont::ArrayHandle<T, CIn>& input,
                                            svtkm::cont::ArrayHandle<T, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::StreamingScanExclusive(
      numBlocks, input, output, svtkm::Sum(), svtkm::TypeTraits<T>::ZeroInitialization());
  }

  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static T StreamingScanExclusive(const svtkm::Id numBlocks,
                                            const svtkm::cont::ArrayHandle<T, CIn>& input,
                                            svtkm::cont::ArrayHandle<T, COut>& output,
                                            BinaryFunctor binary_functor,
                                            const T& initialValue)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id fullSize = input.GetNumberOfValues();
    svtkm::Id blockSize = fullSize / numBlocks;
    if (fullSize % numBlocks != 0)
      blockSize += 1;

    T lastResult = svtkm::TypeTraits<T>::ZeroInitialization();
    for (svtkm::Id block = 0; block < numBlocks; block++)
    {
      svtkm::Id numberOfInstances = blockSize;
      if (block == numBlocks - 1)
        numberOfInstances = fullSize - blockSize * block;

      svtkm::cont::ArrayHandleStreaming<svtkm::cont::ArrayHandle<T, CIn>> streamIn(
        input, block, blockSize, numberOfInstances);

      svtkm::cont::ArrayHandleStreaming<svtkm::cont::ArrayHandle<T, COut>> streamOut(
        output, block, blockSize, numberOfInstances);

      if (block == 0)
      {
        streamOut.AllocateFullArray(fullSize);
        lastResult =
          DerivedAlgorithm::ScanExclusive(streamIn, streamOut, binary_functor, initialValue);
      }
      else
      {
        lastResult =
          DerivedAlgorithm::ScanExclusive(streamIn, streamOut, binary_functor, lastResult);
      }

      streamOut.SyncControlArray();
    }
    return lastResult;
  }

  //--------------------------------------------------------------------------
  // Scan Inclusive
  template <typename T, class CIn, class COut>
  SVTKM_CONT static T ScanInclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::ScanInclusive(input, output, svtkm::Add());
  }

  template <typename T, class CIn, class COut, class BinaryFunctor>
  SVTKM_CONT static T ScanInclusive(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                   svtkm::cont::ArrayHandle<T, COut>& output,
                                   BinaryFunctor binary_functor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DerivedAlgorithm::Copy(input, output);

    svtkm::Id numValues = output.GetNumberOfValues();
    if (numValues < 1)
    {
      return svtkm::TypeTraits<T>::ZeroInitialization();
    }

    auto portal = output.PrepareForInPlace(DeviceAdapterTag());
    using ScanKernelType = ScanKernel<decltype(portal), BinaryFunctor>;


    svtkm::Id stride;
    for (stride = 2; stride - 1 < numValues; stride *= 2)
    {
      ScanKernelType kernel(portal, binary_functor, stride, stride / 2 - 1);
      DerivedAlgorithm::Schedule(kernel, numValues / stride);
    }

    // Do reverse operation on odd indices. Start at stride we were just at.
    for (stride /= 2; stride > 1; stride /= 2)
    {
      ScanKernelType kernel(portal, binary_functor, stride, stride - 1);
      DerivedAlgorithm::Schedule(kernel, numValues / stride);
    }

    return GetExecutionValue(output, numValues - 1);
  }

  template <typename KeyT, typename ValueT, class KIn, class VIn, class VOut>
  SVTKM_CONT static void ScanInclusiveByKey(const svtkm::cont::ArrayHandle<KeyT, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<ValueT, VIn>& values,
                                           svtkm::cont::ArrayHandle<ValueT, VOut>& values_output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    return DerivedAlgorithm::ScanInclusiveByKey(keys, values, values_output, svtkm::Add());
  }

  template <typename KeyT, typename ValueT, class KIn, class VIn, class VOut, class BinaryFunctor>
  SVTKM_CONT static void ScanInclusiveByKey(const svtkm::cont::ArrayHandle<KeyT, KIn>& keys,
                                           const svtkm::cont::ArrayHandle<ValueT, VIn>& values,
                                           svtkm::cont::ArrayHandle<ValueT, VOut>& values_output,
                                           BinaryFunctor binary_functor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    SVTKM_ASSERT(keys.GetNumberOfValues() == values.GetNumberOfValues());
    const svtkm::Id numberOfKeys = keys.GetNumberOfValues();

    if (numberOfKeys <= 1)
    { //we only have a single key/value so that is our output
      DerivedAlgorithm::Copy(values, values_output);
      return;
    }

    //we need to determine based on the keys what is the keystate for
    //each key. The states are start, middle, end of a series and the special
    //state start and end of a series
    svtkm::cont::ArrayHandle<ReduceKeySeriesStates> keystate;

    {
      auto inputPortal = keys.PrepareForInput(DeviceAdapterTag());
      auto keyStatePortal = keystate.PrepareForOutput(numberOfKeys, DeviceAdapterTag());
      ReduceStencilGeneration<decltype(inputPortal), decltype(keyStatePortal)> kernel(
        inputPortal, keyStatePortal);
      DerivedAlgorithm::Schedule(kernel, numberOfKeys);
    }

    //next step is we need to reduce the values for each key. This is done
    //by running an inclusive scan over the values array using the stencil.
    //
    // this inclusive scan will write out two values, the first being
    // the value summed currently, the second being 0 or 1, with 1 being used
    // when this is a value of a key we need to write ( END or START_AND_END)
    {
      svtkm::cont::ArrayHandle<ValueT, VOut> reducedValues;
      svtkm::cont::ArrayHandle<ReduceKeySeriesStates> stencil;
      auto scanInput = svtkm::cont::make_ArrayHandleZip(values, keystate);
      auto scanOutput = svtkm::cont::make_ArrayHandleZip(reducedValues, stencil);

      DerivedAlgorithm::ScanInclusive(
        scanInput, scanOutput, ReduceByKeyAdd<BinaryFunctor>(binary_functor));
      //at this point we are done with keystate, so free the memory
      keystate.ReleaseResources();
      DerivedAlgorithm::Copy(reducedValues, values_output);
    }
  }

  //--------------------------------------------------------------------------
  // Sort
  template <typename T, class Storage, class BinaryCompare>
  SVTKM_CONT static void Sort(svtkm::cont::ArrayHandle<T, Storage>& values,
                             BinaryCompare binary_compare)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id numValues = values.GetNumberOfValues();
    if (numValues < 2)
    {
      return;
    }
    svtkm::Id numThreads = 1;
    while (numThreads < numValues)
    {
      numThreads *= 2;
    }
    numThreads /= 2;

    auto portal = values.PrepareForInPlace(DeviceAdapterTag());
    using MergeKernel = BitonicSortMergeKernel<decltype(portal), BinaryCompare>;
    using CrossoverKernel = BitonicSortCrossoverKernel<decltype(portal), BinaryCompare>;

    for (svtkm::Id crossoverSize = 1; crossoverSize < numValues; crossoverSize *= 2)
    {
      DerivedAlgorithm::Schedule(CrossoverKernel(portal, binary_compare, crossoverSize),
                                 numThreads);
      for (svtkm::Id mergeSize = crossoverSize / 2; mergeSize > 0; mergeSize /= 2)
      {
        DerivedAlgorithm::Schedule(MergeKernel(portal, binary_compare, mergeSize), numThreads);
      }
    }
  }

  template <typename T, class Storage>
  SVTKM_CONT static void Sort(svtkm::cont::ArrayHandle<T, Storage>& values)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DerivedAlgorithm::Sort(values, DefaultCompareFunctor());
  }

  //--------------------------------------------------------------------------
  // Sort by Key
public:
  template <typename T, typename U, class StorageT, class StorageU>
  SVTKM_CONT static void SortByKey(svtkm::cont::ArrayHandle<T, StorageT>& keys,
                                  svtkm::cont::ArrayHandle<U, StorageU>& values)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    //combine the keys and values into a ZipArrayHandle
    //we than need to specify a custom compare function wrapper
    //that only checks for key side of the pair, using a custom compare functor.
    auto zipHandle = svtkm::cont::make_ArrayHandleZip(keys, values);
    DerivedAlgorithm::Sort(zipHandle, internal::KeyCompare<T, U>());
  }

  template <typename T, typename U, class StorageT, class StorageU, class BinaryCompare>
  SVTKM_CONT static void SortByKey(svtkm::cont::ArrayHandle<T, StorageT>& keys,
                                  svtkm::cont::ArrayHandle<U, StorageU>& values,
                                  BinaryCompare binary_compare)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    //combine the keys and values into a ZipArrayHandle
    //we than need to specify a custom compare function wrapper
    //that only checks for key side of the pair, using the custom compare
    //functor that the user passed in
    auto zipHandle = svtkm::cont::make_ArrayHandleZip(keys, values);
    DerivedAlgorithm::Sort(zipHandle, internal::KeyCompare<T, U, BinaryCompare>(binary_compare));
  }

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
                                  BinaryFunctor binaryFunctor)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id numValues = svtkm::Min(input1.GetNumberOfValues(), input2.GetNumberOfValues());
    if (numValues <= 0)
    {
      return;
    }

    auto input1Portal = input1.PrepareForInput(DeviceAdapterTag());
    auto input2Portal = input2.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(numValues, DeviceAdapterTag());

    BinaryTransformKernel<decltype(input1Portal),
                          decltype(input2Portal),
                          decltype(outputPortal),
                          BinaryFunctor>
      binaryKernel(input1Portal, input2Portal, outputPortal, binaryFunctor);
    DerivedAlgorithm::Schedule(binaryKernel, numValues);
  }

  //};
  //--------------------------------------------------------------------------
  // Unique
  template <typename T, class Storage>
  SVTKM_CONT static void Unique(svtkm::cont::ArrayHandle<T, Storage>& values)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DerivedAlgorithm::Unique(values, svtkm::Equal());
  }

  template <typename T, class Storage, class BinaryCompare>
  SVTKM_CONT static void Unique(svtkm::cont::ArrayHandle<T, Storage>& values,
                               BinaryCompare binary_compare)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::cont::ArrayHandle<svtkm::Id, svtkm::cont::StorageTagBasic> stencilArray;
    svtkm::Id inputSize = values.GetNumberOfValues();

    using WrappedBOpType = internal::WrappedBinaryOperator<bool, BinaryCompare>;
    WrappedBOpType wrappedCompare(binary_compare);

    auto valuesPortal = values.PrepareForInput(DeviceAdapterTag());
    auto stencilPortal = stencilArray.PrepareForOutput(inputSize, DeviceAdapterTag());
    ClassifyUniqueComparisonKernel<decltype(valuesPortal), decltype(stencilPortal), WrappedBOpType>
      classifyKernel(valuesPortal, stencilPortal, wrappedCompare);

    DerivedAlgorithm::Schedule(classifyKernel, inputSize);

    svtkm::cont::ArrayHandle<T, svtkm::cont::StorageTagBasic> outputArray;

    DerivedAlgorithm::CopyIf(values, stencilArray, outputArray);

    values.Allocate(outputArray.GetNumberOfValues());
    DerivedAlgorithm::Copy(outputArray, values);
  }

  //--------------------------------------------------------------------------
  // Upper bounds
  template <typename T, class CIn, class CVal, class COut>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id arraySize = values.GetNumberOfValues();

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto valuesPortal = values.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(arraySize, DeviceAdapterTag());

    UpperBoundsKernel<decltype(inputPortal), decltype(valuesPortal), decltype(outputPortal)> kernel(
      inputPortal, valuesPortal, outputPortal);
    DerivedAlgorithm::Schedule(kernel, arraySize);
  }

  template <typename T, class CIn, class CVal, class COut, class BinaryCompare>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<T, CIn>& input,
                                    const svtkm::cont::ArrayHandle<T, CVal>& values,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& output,
                                    BinaryCompare binary_compare)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    svtkm::Id arraySize = values.GetNumberOfValues();

    auto inputPortal = input.PrepareForInput(DeviceAdapterTag());
    auto valuesPortal = values.PrepareForInput(DeviceAdapterTag());
    auto outputPortal = output.PrepareForOutput(arraySize, DeviceAdapterTag());

    UpperBoundsKernelComparisonKernel<decltype(inputPortal),
                                      decltype(valuesPortal),
                                      decltype(outputPortal),
                                      BinaryCompare>
      kernel(inputPortal, valuesPortal, outputPortal, binary_compare);

    DerivedAlgorithm::Schedule(kernel, arraySize);
  }

  template <class CIn, class COut>
  SVTKM_CONT static void UpperBounds(const svtkm::cont::ArrayHandle<svtkm::Id, CIn>& input,
                                    svtkm::cont::ArrayHandle<svtkm::Id, COut>& values_output)
  {
    SVTKM_LOG_SCOPE_FUNCTION(svtkm::cont::LogLevel::Perf);

    DeviceAdapterAlgorithmGeneral<DerivedAlgorithm, DeviceAdapterTag>::UpperBounds(
      input, values_output, values_output);
  }
};

} // namespace internal

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
template <typename DeviceTag>
class DeviceTaskTypes
{
public:
  template <typename WorkletType, typename InvocationType>
  static svtkm::exec::internal::TaskSingular<WorkletType, InvocationType> MakeTask(
    WorkletType& worklet,
    InvocationType& invocation,
    svtkm::Id,
    svtkm::Id globalIndexOffset = 0)
  {
    using Task = svtkm::exec::internal::TaskSingular<WorkletType, InvocationType>;
    return Task(worklet, invocation, globalIndexOffset);
  }

  template <typename WorkletType, typename InvocationType>
  static svtkm::exec::internal::TaskSingular<WorkletType, InvocationType> MakeTask(
    WorkletType& worklet,
    InvocationType& invocation,
    svtkm::Id3,
    svtkm::Id globalIndexOffset = 0)
  {
    using Task = svtkm::exec::internal::TaskSingular<WorkletType, InvocationType>;
    return Task(worklet, invocation, globalIndexOffset);
  }
};
}
} // namespace svtkm::cont

#endif //svtk_m_cont_internal_DeviceAdapterAlgorithmGeneral_h