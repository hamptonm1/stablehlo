/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.
   Copyright 2022 The StableHLO Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "stablehlo/dialect/TypeInference.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <numeric>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "mlir/Dialect/Quant/QuantTypes.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "stablehlo/dialect/AssemblyFormat.h"

namespace mlir {
namespace hlo {

//===----------------------------------------------------------------------===//
// Utils for shape functions.
//===----------------------------------------------------------------------===//

// Checks if the vector `nums` has duplicates.
const auto hasDuplicates = [](const ArrayRef<int64_t> nums) {
  llvm::SmallDenseSet<int64_t> set(nums.begin(), nums.end());
  return set.size() != nums.size();
};

bool tensorsHaveSameElType(TypeRange types, bool ignoreFpPrecision = true) {
  if (!types.empty()) {
    auto tensorTy1 = types[0].cast<ShapedType>();
    Type tensorEl1 = tensorTy1.getElementType();
    for (auto otherTensor : llvm::drop_begin(types, 1)) {
      auto tensorTy2 = otherTensor.cast<ShapedType>();
      Type tensorEl2 = tensorTy2.getElementType();
      if (ignoreFpPrecision && tensorEl1.isa<FloatType>() &&
          tensorTy2.getElementType().isa<FloatType>())
        continue;
      if (tensorEl1 != tensorEl2) return false;
    }
  }
  return true;
}

// Return true if type1 and type2 are tensors and have the same
// element-type, else return false. With float element-types, ignore comparing
// floating-point precision if ignoreFpPrecision is True.
bool tensorsHaveSameElType(Type type1, Type type2,
                           bool ignoreFpPrecision = true) {
  return tensorsHaveSameElType({type1, type2}, ignoreFpPrecision);
}

// Return true if type1 and type2 are shape-compatible and have same element
// type. If 'ignoreFpPrecision' is True, then allow floats with different
// precisions while checking element-types.
bool compatibleShapeAndElementType(Type type1, Type type2,
                                   bool ignoreFpPrecision = false) {
  if (failed(verifyCompatibleShape(type1, type2))) return false;
  return tensorsHaveSameElType(type1, type2, ignoreFpPrecision);
}

bool verifyCompatibleDims(int64_t dimSize1, int64_t dimSize2) {
  return isDynamicDimSize(dimSize1) || isDynamicDimSize(dimSize2) ||
         dimSize1 == dimSize2;
}

// Convert a 1D dense int64 attribute to a list of values.
FailureOr<SmallVector<int64_t>> convert1DAttribute(
    std::optional<DenseIntElementsAttr> optionalAttr,
    std::optional<Location> loc, StringRef attrName) {
  if (!optionalAttr.has_value()) return SmallVector<int64_t>{};

  DenseIntElementsAttr attr = *optionalAttr;
  auto attrType = attr.getType().cast<RankedTensorType>();
  if (attrType.getRank() != 1)
    return emitOptionalError(loc, "expects the shape of ", attrName,
                             " attribute to be 1-D, but got {",
                             attrType.getShape(), "}.");
  auto values = attr.getValues<int64_t>();
  return SmallVector<int64_t>{values.begin(), values.end()};
}

FailureOr<SmallVector<std::pair<int64_t, int64_t>>> convertPaddingAttribute(
    std::optional<DenseIntElementsAttr> optionalAttr,
    std::optional<Location> loc) {
  if (!optionalAttr.has_value())
    return SmallVector<std::pair<int64_t, int64_t>>{};

  DenseIntElementsAttr attr = *optionalAttr;
  auto attrType = attr.getType().cast<RankedTensorType>();
  if (attrType.getRank() != 2 || attrType.getShape()[1] != 2)
    return emitOptionalError(
        loc, "expects the shape of padding-attribute to be {N, 2}, but got {",
        attrType.getShape(), "}.");

  auto it = attr.getValues<int64_t>().begin();
  SmallVector<std::pair<int64_t, int64_t>> out(attr.getNumElements() / 2);
  for (auto& item : out) {
    int64_t first = *it;
    ++it;
    int64_t second = *it;
    ++it;
    item = {first, second};
  }
  return out;
}

// Convert a 1D dense bool attribute to a list of values.
FailureOr<SmallVector<bool>> convertWindowReversalAttribute(
    std::optional<DenseElementsAttr> optionalAttr, std::optional<Location> loc,
    StringRef attrName) {
  if (!optionalAttr.has_value()) return SmallVector<bool>{};

  DenseElementsAttr attr = *optionalAttr;
  auto attrType = attr.getType().cast<RankedTensorType>();
  if (attrType.getRank() != 1)
    return emitOptionalError(loc, "expects the shape of ", attrName,
                             " attribute to be 1-D, but got {",
                             attrType.getShape(), "}.");
  auto values = attr.getValues<bool>();
  return SmallVector<bool>{values.begin(), values.end()};
}

// If a window with the given bound in some dimension is dilated with the given
// dilation factor in that dimension, then the value returned is the bound for
// the array in that dimension after dilation.
//
// For a 1D array with 3 entries 1, 2, 3, a dilation factor of 2 yields a new
// window with values 1, x, 2, x, 3, where x indicates holes left by the
// dilation. So DilatedBound(3, 2) == 5.
int64_t dilatedBound(int64_t bound, int64_t dilation) {
  assert(bound >= 0 && "The dimension to dilate must be >= 0");
  if (bound == 0) return 0;

  // Suppose the array has three entries 123 and the dilation factor is 4. Then
  // the dilated array has 9 entries 1xxx2xxx3. Here, each original entry except
  // the last expands into 4 entries, so that is (bound - 1) * dilation. Then we
  // add 1 to account for the final input element.
  return (bound - 1) * dilation + 1;
}

// Returns the number of valid positions of a window with the given size and
// stride within an array with the given bound. This is the bound of an output
// array with one element per valid position of the window.
//
// For example, for arguments of (bound=5, window_size=2, stride=2), the
// returned value is 2. There are valid positions at offset 0 and offset 2,
// while offset 4 is not valid since the window's last entry would be at 5,
// which is beyond the bound of 5.
int64_t stridedBound(int64_t bound, int64_t windowSize, int64_t stride) {
  assert(windowSize >= 0 && "Expected window size to be >= 0");
  assert(bound >= 0 && "Expected bound to be >= 0");

  if (bound == 0 || windowSize > bound) return 0;

  // Without considering stride, the maximum valid offset is bound -
  // window_size. Taking stride into account, the valid offsets then have the
  // form q * stride for q = 0, ..., Q such that q * stride <= bound -
  // window_size. This implies that Q equals floor(bound - window_size /
  // stride). There are Q + 1 valid values of q, yielding the formula below.
  return (bound - windowSize) / stride + 1;
}

LogicalResult verifyPairwiseCompatibleShapes(TypeRange values) {
  for (auto type1 : values)
    for (auto type2 : values)
      if (failed(verifyCompatibleShape(type1, type2))) return failure();
  return success();
}

LogicalResult verifyBatchNorm(std::optional<Location> location,
                              ValueRange multiDimOperands,
                              ValueRange singleDimOperands,
                              int64_t featureIndex) {
  // batch_norm_grad_c3
  if (failed(verifyPairwiseCompatibleShapes(multiDimOperands.getTypes())))
    return emitOptionalError(
        location,
        "expects multi-dimensional operands to have compatible shapes.");

  // batch_norm_grad_c4, batch_norm_inference_c3...batch_norm_inference_c6,
  // batch_norm_training_c3, batch_norm_training_c4
  if (failed(verifyPairwiseCompatibleShapes(singleDimOperands.getTypes())))
    return emitOptionalError(
        location,
        "expects single-dimensional operands to have compatible shapes.");

  auto multiDimType = multiDimOperands[0].getType().cast<RankedTensorType>();
  // batch_norm_grad_c1, batch_norm_inference_c1, batch_norm_training_c1
  if (featureIndex >= multiDimType.getRank())
    return emitOptionalError(
        location,
        "expects featureIndex to be smaller than the rank of "
        "multi-dimensional operands; got featureIndex ",
        featureIndex, ", and rank ", multiDimType.getRank(), ".");

  // batch_norm_grad_c1, batch_norm_inference_c1, batch_norm_training_c1
  if (featureIndex < 0)
    return emitOptionalError(location, "expects featureIndex to be a ",
                             "non-negative number, got ", featureIndex, ".");

  const int64_t featureCount = multiDimType.getDimSize(featureIndex);
  const int64_t singleDimSize =
      singleDimOperands[0].getType().cast<RankedTensorType>().getDimSize(0);

  // batch_norm_grad_c5, batch_norm_inference_c3...batch_norm_inference_c6,
  // batch_norm_training_c3, batch_norm_training_c4
  if (!verifyCompatibleDims(singleDimSize, featureCount))
    return emitOptionalError(
        location,
        "expects the size of single-dimensional operands to be compatible with "
        "feature count, but the size of single-dimensional operands is ",
        dimSizeToString(singleDimSize), " and the feature count is ",
        dimSizeToString(featureCount), ".");

  return success();
}

LogicalResult inferBatchNormOp(
    std::optional<Location> location, ValueRange multiDimOperands,
    ValueRange singleDimOperands, int64_t featureIndex,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes,
    bool is_inference) {
  if (failed(verifyBatchNorm(location, multiDimOperands, singleDimOperands,
                             featureIndex)))
    return failure();

  // Batch norm ops require operands to be ranked.
  auto multiDimType = multiDimOperands[0].getType().cast<RankedTensorType>();
  // batch_norm_grad_c3, batch_norm_inference_c7, batch_norm_training_c7
  inferredReturnShapes.emplace_back(multiDimType.getShape(),
                                    multiDimType.getElementType(),
                                    multiDimType.getEncoding());

  if (is_inference) return success();

  SmallVector<int64_t> singleDimShape{multiDimType.getDimSize(featureIndex)};

  ArrayRef<int64_t> multiDimBounds =
      encodingToBounds(multiDimType.getEncoding());
  SmallVector<int64_t> singleDimBounds;
  if (!multiDimBounds.empty())
    singleDimBounds.emplace_back(multiDimBounds[featureIndex]);

  auto singleDimReturnShape = ShapedTypeComponents(
      singleDimShape, multiDimType.getElementType(),
      singleDimBounds.empty()
          ? nullptr
          : boundsToEncoding(multiDimType.getEncoding(), singleDimBounds));
  // batch_norm_grad_c4, batch_norm_training_c5
  inferredReturnShapes.emplace_back(singleDimReturnShape);
  // batch_norm_grad_c4, batch_norm_training_c6
  inferredReturnShapes.emplace_back(singleDimReturnShape);
  return success();
}

// Verifies various properties of window-attributes (viz., stride, padding,
// lhs_dilation and rhs_dilation) and collects all the window-attributes for
// each kernel spatial dimensions.
FailureOr<SmallVector<WindowDimension>>
verifyWindowAttributesAndInferWindowDimensions(
    ArrayRef<int64_t> windowDimensions, ArrayRef<int64_t> windowStrides,
    ArrayRef<std::pair<int64_t, int64_t>> padding,
    ArrayRef<int64_t> lhsDilation, ArrayRef<int64_t> rhsDilation,
    ArrayRef<bool> windowReversal, std::optional<Location> loc) {
  const auto verifySize = [&](const size_t attrSize,
                              StringRef attrName) -> LogicalResult {
    if (attrSize == 0 || attrSize == windowDimensions.size()) return success();
    return emitOptionalError(
        loc, "expects ", attrName,
        " to have same dimension-size as size of window dimensions (",
        windowDimensions.size(), "), but got: ", attrSize, ".");
  };
  // reduce_window_c6, select_and_scatter_c6
  if (failed(verifySize(windowStrides.size(), "window-strides")))
    return failure();
  // reduce_window_c8
  if (failed(verifySize(lhsDilation.size(), "base-dilation factors")))
    return failure();
  // reduce_window_c10
  if (failed(verifySize(rhsDilation.size(), "window-dilation factors")))
    return failure();
  // reduce_window_c12
  if (failed(verifySize(padding.size(), "padding-entries"))) return failure();
  if (failed(verifySize(windowReversal.size(), "window-reversal")))
    return failure();

  SmallVector<WindowDimension> window(windowDimensions.size());
  for (size_t i = 0; i < windowDimensions.size(); i++) {
    WindowDimension& dim = window[i];

    dim.size = windowDimensions[i];
    // reduce_window_c5, select_and_scatter_c5
    if (!isDynamicDimSize(dim.size) && dim.size <= 0)
      return emitOptionalError(loc,
                               "expects window to have positive value for ", i,
                               "-th window dimension, but got ", dim.size, ".");

    if (!windowStrides.empty()) dim.stride = windowStrides[i];
    // reduce_window_c7, select_and_scatter_c7
    if (dim.stride <= 0)
      return emitOptionalError(
          loc, "expects window to have positive stride for ", i,
          "-th window dimension, but got ", dim.stride, ".");

    if (!lhsDilation.empty()) dim.baseDilation = lhsDilation[i];
    // reduce_window_c9
    if (dim.baseDilation <= 0)
      return emitOptionalError(
          loc, "expects window to have positive base dilation factor for ", i,
          "-th window dimension, but got ", dim.baseDilation, ".");

    if (!rhsDilation.empty()) dim.windowDilation = rhsDilation[i];
    // reduce_window_c11
    if (dim.windowDilation <= 0)
      return emitOptionalError(
          loc, "expects window to have positive window dilation factor for ", i,
          "-th window dimension, but got ", dim.windowDilation, ".");

    if (!padding.empty()) {
      dim.paddingLow = padding[i].first;
      dim.paddingHigh = padding[i].second;
    }
  }

  return window;
}

// Infer the shape of the output window.
//  Foreach dimension d,
//    output-window-shape[d] =
//            stridedBound(padding_low + dilatedBound(base_shape[d]) +
//            padding_high,
//                         dilatedBound(window_shape[d]))
//      where (padding_low, padding_high) is the padding-pair for d.
SmallVector<int64_t> inferWindowOutputShape(ArrayRef<int64_t> baseShape,
                                            ArrayRef<WindowDimension> window) {
  assert(baseShape.size() == window.size() &&
         "Size of window dimensions must match the size of base shape.");

  SmallVector<int64_t> outputDimensions(window.size());
  for (int64_t i = 0; i < static_cast<int64_t>(window.size()); ++i) {
    if (isDynamicDimSize(baseShape[i]) || isDynamicDimSize(window[i].size)) {
      outputDimensions[i] = ShapedType::kDynamic;
    } else {
      const auto& dim = window[i];

      const int64_t dilatedBase = dilatedBound(baseShape[i], dim.baseDilation);
      const int64_t paddedDilatedBase =
          dim.paddingLow + dilatedBase + dim.paddingHigh;
      const int64_t dilatedWindow = dilatedBound(dim.size, dim.windowDilation);

      outputDimensions[i] =
          stridedBound(paddedDilatedBase, dilatedWindow, dim.stride);
    }
  }

  return outputDimensions;
}

unsigned potentiallyComplexBitWidth(Type type) {
  auto complexTy = type.dyn_cast<ComplexType>();
  return complexTy ? 2 * complexTy.getElementType().getIntOrFloatBitWidth()
                   : type.getIntOrFloatBitWidth();
}

LogicalResult verifyReplicaGroups(std::optional<Location> location,
                                  DenseIntElementsAttr replicaGroups,
                                  bool allGroupsMustHaveSameSize,
                                  bool useGlobalDeviceIds,
                                  std::optional<size_t> expectedGroupSize) {
  auto replicaGroupType = replicaGroups.getType().cast<RankedTensorType>();
  // all_gather_i3, all_to_all_i5
  if (replicaGroupType.getRank() != 2)
    return emitOptionalError(location,
                             "replica groups should be a rank 2 tensor");

  // Revisit the following check in light of #498.
  if (useGlobalDeviceIds &&
      (replicaGroupType.getShape()[0] * replicaGroupType.getShape()[1] == 0))
    return emitOptionalError(location,
                             "if `use_global_device_ids` is set, the replica "
                             "groups cannot be empty");

  auto replicaIds = replicaGroups.getValues<int64_t>();

  llvm::SmallSet<int64_t, 8> replicaIdsSeen;
  for (int64_t replicaId : replicaIds) {
    // Replica groups are stored in a 2D tensor. If the op supports non-uniform
    // groups, null replica IDs are stored as -1.
    // all_gather_c4
    if (replicaId == -1) {
      if (!allGroupsMustHaveSameSize) continue;
      return emitOptionalError(location, "Invalid replica id -1");
    }

    // all_gather_c2, all_reduce_c1, all_to_all_c5
    if (!replicaIdsSeen.insert(replicaId).second)
      return emitOptionalError(location, "replica id #", replicaId,
                               " seen more than once");
  }

  // all_gather_c4, all_reduce_c3, all_to_all_c7
  for (size_t id = 0; id < replicaIdsSeen.size(); id++)
    if (!replicaIdsSeen.contains(id))
      return emitOptionalError(location, "replica id #", id,
                               " not seen in replica groups");

  // all_to_all_c8
  if (allGroupsMustHaveSameSize && expectedGroupSize &&
      (replicaIds.size() / replicaGroupType.getShape()[0] !=
       *expectedGroupSize))
    return emitOptionalError(location, "group size of replica_groups must be ",
                             *expectedGroupSize);

  return success();
}

LogicalResult verifyReduceOpInputsAndInferShape(
    std::optional<Location> location, SmallVector<ShapedType> inputTypes,
    SmallVector<ShapedType> initValueTypes, DenseIntElementsAttr dimensions,
    SmallVector<int64_t>& newDimensions, Attribute& encoding) {
  // reduce_i3
  if (dimensions.getType().getRank() != 1)
    return emitOptionalError(location, "dimensions must be rank 1");

  // Check for unranked tensors in input operands.
  uint64_t numInputs = inputTypes.size();
  int64_t rankedInputIdx = -1;
  for (uint64_t inputIdx = 0; inputIdx < numInputs; ++inputIdx) {
    if (inputTypes[inputIdx].hasRank()) {
      rankedInputIdx = inputIdx;
      break;
    }
  }
  bool allInputsUnranked = (rankedInputIdx == -1);
  // reduce_c1
  if (!allInputsUnranked) {
    for (uint64_t inputIdx = 0; inputIdx < numInputs; ++inputIdx)
      if (failed(mlir::verifyCompatibleShape(inputTypes[rankedInputIdx],
                                             inputTypes[inputIdx])))
        return emitOptionalError(
            location, "expects all inputs to have compatible shapes. Shape at",
            " input-index ", inputIdx,
            " is not compatible with shape at input-index ", rankedInputIdx);
  }

  DenseSet<int64_t> dimensionsToReduceSet;
  for (int64_t dimension : dimensions.getValues<int64_t>()) {
    // reduce_c4
    if ((!allInputsUnranked &&
         dimension >= inputTypes[rankedInputIdx].getRank()) ||
        dimension < 0)
      return emitOptionalError(
          location, "Out-of-bounds dimension ", dimension, ", expected to be ",
          allInputsUnranked
              ? "> 0"
              : "less than the input-tensor rank " +
                    std::to_string(inputTypes[rankedInputIdx].getRank()));

    // reduce_c5
    if (!dimensionsToReduceSet.insert(dimension).second)
      return emitOptionalError(location,
                               "Duplicate reduction dimension: ", dimension);
  }

  if (!allInputsUnranked) {
    auto rankedInput = inputTypes[rankedInputIdx].cast<RankedTensorType>();
    ArrayRef<int64_t> inputBounds = encodingToBounds(rankedInput.getEncoding());
    SmallVector<int64_t> newBounds;
    for (int inputIdx = 0; inputIdx < rankedInput.getRank(); ++inputIdx) {
      if (!dimensionsToReduceSet.count(inputIdx)) {
        newDimensions.push_back(rankedInput.getDimSize(inputIdx));
        if (!inputBounds.empty()) newBounds.push_back(inputBounds[inputIdx]);
      }
    }

    // Set encoding based on the bounds only if the bounds is not empty.
    encoding = nullptr;
    if (!newBounds.empty())
      encoding = boundsToEncoding(rankedInput.getEncoding(), newBounds);
  }
  return success();
}

LogicalResult verifyReducerShape(std::optional<Location> loc, Block& block,
                                 ArrayRef<ShapedType> inputTypes,
                                 ArrayRef<ShapedType> initValueTypes,
                                 ArrayRef<int64_t> allowedDimensions) {
  int64_t numInputs = inputTypes.size();

  // all_reduce_c5, reduce_c6, reduce_scatter_c7, reduce_window_c13,
  // scatter_c15, select_and_scatter_c10
  if (static_cast<int64_t>(block.getArguments().size()) != numInputs * 2)
    return emitOptionalError(loc, "Reduction-region must take ", numInputs * 2,
                             " parameters, but takes ",
                             block.getArguments().size(), " parameter(s)");

  // all_reduce_c5, reduce_c6, reduce_scatter_c7, reduce_window_c13,
  // scatter_c15, select_and_scatter_c10
  if (block.getTerminator()->getOperands().empty())
    return emitOptionalError(
        loc, "The reduction-region expected to return some value(s)");

  // all_reduce_c5, reduce_c6, reduce_scatter_c7, reduce_window_c13,
  // scatter_c15, select_and_scatter_c10
  if (static_cast<int64_t>(block.getTerminator()->getOperands().size()) !=
      numInputs)
    return emitOptionalError(loc, "Reduction-region here must produce ",
                             numInputs, " tensors, but produces ",
                             block.getTerminator()->getOperands().size(),
                             " instead");

  // all_reduce_c5, reduce_c6, reduce_scatter_c7, reduce_window_c13,
  // scatter_c15, select_and_scatter_c10
  SmallVector<ShapedType> accumulatorSubShapes;
  for (Value retOperand : block.getTerminator()->getOperands()) {
    auto shapedTy = retOperand.getType().dyn_cast<ShapedType>();
    if (!shapedTy)
      return emitOptionalError(loc,
                               "Reduction-region here must produce "
                               "tensor-typed result(s), but produces ",
                               retOperand.getType(), " instead");

    accumulatorSubShapes.push_back(shapedTy);
  }

  for (int64_t inputIdx = 0; inputIdx < numInputs; ++inputIdx) {
    // all_reduce_c5, reduce_c2, reduce_scatter_c7, reduce_window_c13,
    // scatter_c15, select_and_scatter_c10
    if (!compatibleShapeAndElementType(accumulatorSubShapes[inputIdx],
                                       block.getArgument(inputIdx).getType()))
      return emitOptionalError(
          loc, "The type of reduction-region's parameter at index ", inputIdx,
          " is different than the corresponding result type: ",
          block.getArgument(inputIdx).getType(), " vs ",
          accumulatorSubShapes[inputIdx]);

    // all_reduce_c5, reduce_c2, reduce_scatter_c7, reduce_window_c13,
    // scatter_c15, select_and_scatter_c3, select_and_scatter_c10
    if (!compatibleShapeAndElementType(
            accumulatorSubShapes[inputIdx],
            block.getArgument(numInputs + inputIdx).getType(),
            /*ignoreFpPrecision=*/true))
      return emitOptionalError(
          loc, "The type of reduction-region's parameter at index ",
          numInputs + inputIdx,
          " is different than the corresponding result type: ",
          block.getArgument(numInputs + inputIdx).getType(), " vs ",
          accumulatorSubShapes[inputIdx]);

    // all_reduce_c5, reduce_c6, reduce_scatter_c7, reduce_window_c13,
    // reduce_window_i2, scatter_c6, scatter_c15, select_and_scatter_c10
    if (!compatibleShapeAndElementType(accumulatorSubShapes[inputIdx],
                                       initValueTypes[inputIdx],
                                       /*ignoreFpPrecision=*/true))
      return emitOptionalError(
          loc, "The type of reduction-region's result type at index ", inputIdx,
          " differs from the op's corresponding init-value type: ",
          accumulatorSubShapes[inputIdx], " vs ", initValueTypes[inputIdx]);

    // reduce_c6, reduce_window_c3, scatter_c6, scatter_c15,
    // select_and_scatter_c10
    if (!tensorsHaveSameElType(
            inputTypes[inputIdx],
            block.getArgument(numInputs + inputIdx).getType(), true))
      return emitOptionalError(
          loc, "The element-type of reduction-region's argument at index ",
          numInputs + inputIdx, " is expected to be ",
          inputTypes[inputIdx].getElementType(), ", but got ",
          block.getArgument(numInputs + inputIdx).getType(), " as its type.");

    Type blockArgType = block.getArgument(numInputs + inputIdx).getType();
    auto blockArgTensorTy = blockArgType.cast<ShapedType>();

    auto allInputsUnranked = llvm::none_of(
        inputTypes, [&](ShapedType type) { return type.hasRank(); });
    if (allInputsUnranked || !blockArgTensorTy.hasRank()) return success();

    auto argShape = blockArgTensorTy.getShape();
    // reduce_c6, reduce_window_c13, select_and_scatter_c10
    if (argShape.size() > allowedDimensions.size())
      return emitOptionalError(
          loc, "The rank of reduction-region's argument at index ",
          numInputs + inputIdx,
          " is expected to be <= ", allowedDimensions.size(), ", got ",
          argShape.size());

    int64_t argShapeIdx = 0;
    for (int64_t outputShapeIdx = 0;
         outputShapeIdx < static_cast<int64_t>(allowedDimensions.size()) &&
         argShapeIdx < static_cast<int64_t>(argShape.size());
         outputShapeIdx++)
      if (verifyCompatibleDims(allowedDimensions[outputShapeIdx],
                               argShape[argShapeIdx]))
        argShapeIdx++;

    // reduce_c6, reduce_window_c13
    if (argShapeIdx != static_cast<int64_t>(argShape.size()))
      return emitOptionalError(
          loc, "The shape of reduction-region's argument at index ",
          numInputs + inputIdx,
          " is not compatible with that of reduce-op's input-parameter "
          "at index ",
          inputIdx);
  }

  return success();
}

LogicalResult verifyReduceWindowOpInputsAndInferWindow(
    std::optional<Location> location, SmallVector<ShapedType> inputTypes,
    SmallVector<ShapedType> initValueTypes,
    DenseIntElementsAttr windowDimensions,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> baseDilations,
    std::optional<DenseIntElementsAttr> windowDilations,
    std::optional<DenseIntElementsAttr> padding,
    SmallVector<int64_t>& windowDims,
    SmallVector<WindowDimension>& inferredWindow) {
  // reduce_window_c1
  if (inputTypes.empty())
    return emitOptionalError(location, "requires at least 1 input value");

  // Check for unranked tensors in input operands.
  uint64_t numInputs = inputTypes.size();
  int64_t rankedInputIdx = -1;
  for (uint64_t inputIdx = 0; inputIdx < numInputs; ++inputIdx) {
    if (inputTypes[inputIdx].hasRank()) {
      rankedInputIdx = inputIdx;
      break;
    }
  }
  bool allInputsUnranked = (rankedInputIdx == -1);

  // reduce_window_c2
  if (!allInputsUnranked) {
    for (uint64_t inputIdx = 0; inputIdx < numInputs; ++inputIdx)
      if (failed(mlir::verifyCompatibleShape(inputTypes[rankedInputIdx],
                                             inputTypes[inputIdx])))
        return emitOptionalError(
            location, "expects all inputs to have compatible shapes. Shape at",
            " input-index ", inputIdx,
            " is not compatible with shape at input-index ", rankedInputIdx);
  }

  // reduce_window_i3
  auto windowDimsOrErr =
      convert1DAttribute(windowDimensions, location, "window_dimensions");
  if (failed(windowDimsOrErr)) return failure();
  // reduce_window_i4
  auto windowStridesOrErr =
      convert1DAttribute(windowStrides, location, "window_strides");
  if (failed(windowStridesOrErr)) return failure();
  // reduce_window_i5
  auto baseDilationsOrErr =
      convert1DAttribute(baseDilations, location, "base_dilations");
  if (failed(baseDilationsOrErr)) return failure();
  // reduce_window_i6
  auto windowDilationsOrErr =
      convert1DAttribute(windowDilations, location, "window_dilations");
  if (failed(windowDilationsOrErr)) return failure();
  // reduce_window_c12, reduce_window_i7
  auto paddingOrErr = convertPaddingAttribute(padding, location);
  if (failed(paddingOrErr)) return failure();

  // reduce_window_c4
  for (const auto inputType : inputTypes) {
    if (!inputType.hasRank()) continue;
    if (inputType.getRank() != static_cast<int64_t>((*windowDimsOrErr).size()))
      return emitOptionalError(
          location, "expects window-dimensions size == input rank, but got ",
          "window-dimensions size: ", (*windowDimsOrErr).size(),
          " and input: ", inputType, " with rank = ", inputType.getRank(), ".");
  }

  // reduce_window_c5...reduce_window_c12
  auto windowOrErr = verifyWindowAttributesAndInferWindowDimensions(
      *windowDimsOrErr, *windowStridesOrErr, *paddingOrErr,
      /*lhsDilation=*/*baseDilationsOrErr,
      /*rhsDilation=*/*windowDilationsOrErr, /*windowReversal=*/std::nullopt,
      location);
  if (failed(windowOrErr)) return failure();

  windowDims.append(*windowDimsOrErr);
  inferredWindow.append(*windowOrErr);
  return success();
}

// Shape function can be called directly from autogenerated `build()` function,
// which may not guarantee the added region(s) in `odsState.regions` to be
// non-empty. Need check it here to avoid a crash for the ops that need regions
// in type inference, i.e. `IfOp/CaseOp/MapOp`.
LogicalResult verifyRegionNotEmpty(std::optional<Location> location,
                                   Region& region) {
  if (region.empty())
    return emitOptionalError(location, "expect non-empty region");
  return success();
}

// Checks:
//  P1. Same sizes for input, kernel and output spatialDims.
//  P2. Spatial and non-spatial dimensions (for input,kernel, &output) should
//      be unique and in range [0, num_dims), where num_dims = rank of input
//      (lhs/rhs) tensors.
//
//  Note that the spatial + non-spatial dimensions may not cover all the
//  dimensions in the range [0,num) because of the presence of 'unknown'
//  dimensions (ref. `printConvolutionDimensions()`)
LogicalResult isSpatialDimensionsValid(
    Type lhsType, int64_t inputBatchDimension, int64_t inputFeatureDimension,
    ArrayRef<int64_t> inputSpatialDimensions,
    int64_t kernelInputFeatureDimension, int64_t kernelOutputFeatureDimension,
    ArrayRef<int64_t> kernelSpatialDimensions, int64_t outputBatchDimension,
    int64_t outputFeatureDimension, ArrayRef<int64_t> outputSpatialDimensions,
    std::optional<Location> location) {
  uint64_t spatialDimNum = inputSpatialDimensions.size();
  // P1.
  if ((spatialDimNum != kernelSpatialDimensions.size()) ||
      (spatialDimNum != outputSpatialDimensions.size()))
    return emitOptionalError(location,
                             "expects the same size for input, kernel "
                             "and output spatial-dimensions, but got ",
                             spatialDimNum, ", ",
                             kernelSpatialDimensions.size(), ", and ",
                             outputSpatialDimensions.size(), " resp.");

  // P2.
  SmallVector<int64_t> inputDimNums(spatialDimNum + 2);
  inputDimNums[0] = inputBatchDimension;
  inputDimNums[1] = inputFeatureDimension;
  std::copy(inputSpatialDimensions.begin(), inputSpatialDimensions.end(),
            inputDimNums.begin() + 2);

  SmallVector<int64_t> windowDimNums(spatialDimNum + 2);
  windowDimNums[0] = kernelInputFeatureDimension;
  windowDimNums[1] = kernelOutputFeatureDimension;
  std::copy(kernelSpatialDimensions.begin(), kernelSpatialDimensions.end(),
            windowDimNums.begin() + 2);

  SmallVector<int64_t> OutputDimNums(spatialDimNum + 2);
  OutputDimNums[0] = outputBatchDimension;
  OutputDimNums[1] = outputFeatureDimension;
  std::copy(outputSpatialDimensions.begin(), outputSpatialDimensions.end(),
            OutputDimNums.begin() + 2);

  auto numDims = lhsType.cast<RankedTensorType>().getRank();
  const auto inRange = [numDims](int64_t i) { return 0 <= i && i < numDims; };

  if (!llvm::all_of(inputDimNums, inRange) ||
      !llvm::all_of(windowDimNums, inRange) ||
      !llvm::all_of(OutputDimNums, inRange))
    return emitOptionalError(location,
                             "expects input, kernel, and output "
                             "dimension-numbers to be in-range [0, ",
                             numDims, ").");

  if (hasDuplicates(inputDimNums))
    return emitOptionalError(
        location, "expects input dimension-numbers to be unique, got {",
        inputDimNums, "}.");

  if (hasDuplicates(windowDimNums))
    return emitOptionalError(
        location, "expects kernel dimension-numbers to be unique, got {",
        windowDimNums, "}.");

  if (hasDuplicates(OutputDimNums))
    return emitOptionalError(
        location, "expects output dimension-numbers to be unique, got {",
        OutputDimNums, "}.");

  return success();
}

// Checks if the precision config has a valid size, if provided.
LogicalResult verifyPrecisionConfig(std::optional<Location> loc,
                                    std::optional<ArrayAttr> maybeArrayAttr) {
  if (!maybeArrayAttr.has_value()) return success();
  auto arrayAttr = maybeArrayAttr.value();
  if (!arrayAttr) return success();
  return arrayAttr.size() <= 2
             ? success()
             : emitOptionalError(loc,
                                 "expects precision config to be empty or have "
                                 "<= 2 elements.");
}

// Verifies the following properties:
//  P1. The input, kernel, and output spatial-dimensions are valid.
//  P2. Given,
//          input-dimensions: b * input-spatial-dims * f
//          kernel-dimensions: kernel-spatial-dims * i * o
//          output-dimensions: b' * out-spatial-dims * f'
//            where b = input-batch-dim
//            where f = input-feature-dim
//            where i = kernel-input-feature-dim
//            where o = kernel-output-feature-dim
//            where b' = output-batch-dim
//            where f' = output-feature-dim
//      Check the following properties w.r.t feature_group_count (fgc) and
//      batch_group_count (bgc).
//        * fgc > 0, bgc > 0 and !(fgc > 1 && bgc > 1)
//        * dim(lhs, b) % bgc == 0
//        * dim(lhs, f) % fgc == 0 and
//          dim(lhs, f) / fgc = dim(rhs, i)
//        * dim(rhs, o) (or dim(output, f')) % bgc == 0 and
//          dim(rhs, o) (or dim(output, f')) % fgc == 0
//  P3. Precision config is null, of size 0 or of size 2.
LogicalResult verifyConvolutionAttributes(
    std::optional<Location> location, Type lhsType, Type rhsType,
    int64_t inputBatchDimension, int64_t inputFeatureDimension,
    ArrayRef<int64_t> inputSpatialDimensions,
    int64_t kernelInputFeatureDimension, int64_t kernelOutputFeatureDimension,
    ArrayRef<int64_t> kernelSpatialDimensions, int64_t outputBatchDimension,
    int64_t outputFeatureDimension, ArrayRef<int64_t> outputSpatialDimensions,
    int64_t featureGroupCount, int64_t batchGroupCount,
    std::optional<ArrayAttr> precisionConfig) {
  // P1.
  if (failed(isSpatialDimensionsValid(
          lhsType, inputBatchDimension, inputFeatureDimension,
          inputSpatialDimensions, kernelInputFeatureDimension,
          kernelOutputFeatureDimension, kernelSpatialDimensions,
          outputBatchDimension, outputFeatureDimension, outputSpatialDimensions,
          location)))
    return failure();

  // P2.
  if (featureGroupCount <= 0)
    return emitOptionalError(
        location, "expects feature_group_count to be a positive number, got ",
        featureGroupCount, ".");

  if (batchGroupCount <= 0)
    return emitOptionalError(
        location, "expects batch_group_count to be a positive number, got ",
        batchGroupCount, ".");

  if (batchGroupCount > 1 && featureGroupCount > 1)
    return emitOptionalError(
        location,
        "expects batch_group_count and feature_group_count not to be both "
        "greater than 1. Got ",
        batchGroupCount, " and ", featureGroupCount, " resp.");

  auto rankedLhsType = lhsType.cast<RankedTensorType>();
  const int64_t inputFeatures = rankedLhsType.getShape()[inputFeatureDimension];
  const int64_t inputBatch = rankedLhsType.getShape()[inputBatchDimension];

  auto rankedRhsType = rhsType.cast<RankedTensorType>();
  const int64_t kernelInputFeatures =
      rankedRhsType.getShape()[kernelInputFeatureDimension];
  const int64_t kernelOutputFeatures =
      rankedRhsType.getShape()[kernelOutputFeatureDimension];

  if (!isDynamicDimSize(kernelOutputFeatures)) {
    if (kernelOutputFeatures % batchGroupCount != 0)
      return emitOptionalError(
          location, "expects output feature dimension size (",
          kernelOutputFeatures,
          ") to be a multiple of batch_group_count. Got batch_group_count = ",
          batchGroupCount, ".");

    if (kernelOutputFeatures % featureGroupCount != 0)
      return emitOptionalError(location,
                               "expects kernel output feature dimension (",
                               kernelOutputFeatures,
                               ") to be divisible by feature_group_count. For "
                               "feature_group_count = ",
                               featureGroupCount, ".");
  }

  if (!isDynamicDimSize(inputFeatures)) {
    if (inputFeatures % featureGroupCount != 0)
      return emitOptionalError(location, "expects input feature dimension (",
                               inputFeatures,
                               ") to be a multiple of feature_group_count. Got "
                               "feature_group_count = ",
                               featureGroupCount, ".");

    if (!isDynamicDimSize(kernelInputFeatures) &&
        inputFeatures / featureGroupCount != kernelInputFeatures)
      return emitOptionalError(
          location, "expects input feature dimension (", inputFeatures,
          ") / "
          "feature_group_count = kernel input feature dimension (",
          kernelInputFeatures,
          "). Got feature_group_count = ", featureGroupCount, ".");
  }

  if (!isDynamicDimSize(inputBatch) && inputBatch % batchGroupCount != 0)
    return emitOptionalError(location, "expects input batch dimension (",
                             inputBatch,
                             ") to be divisible by "
                             "batch_group_count. Got batch_group_count = ",
                             batchGroupCount, ".");

  // P3.
  if (failed(verifyPrecisionConfig(location, precisionConfig)))
    return failure();

  return success();
}

LogicalResult validateScatterDimensionNumbers(
    ShapedType operandType, ArrayRef<int64_t> scatterIndicesShape,
    ShapedType updateType, bool operandTypeRanked,
    bool scatterIndicesTypeRanked, bool updatesTypeRanked,
    ArrayRef<int64_t> updateWindowDims, ArrayRef<int64_t> insertedWindowDims,
    ArrayRef<int64_t> scatterDimsToOperandDims, int64_t indexVectorDim,
    std::optional<Location> loc) {
  // scatter_c2
  if (operandTypeRanked) {
    auto windowSize = updateWindowDims.size() + insertedWindowDims.size();
    if (operandType.getRank() != static_cast<int64_t>(windowSize))
      return emitOptionalError(loc,
                               "Expects rank-of operand to match "
                               "size-of('update_window_dims')  + "
                               "size-of('inserted_window_dims') i.e. ",
                               windowSize, " but got ", operandType.getRank(),
                               ".");
  }

  // scatter_c7
  if (!llvm::is_sorted(updateWindowDims))
    return emitOptionalError(loc,
                             "Expects update_window_dims to be sorted; got: [",
                             updateWindowDims, "].");

  // scatter_c7
  if (hasDuplicates(updateWindowDims))
    return emitOptionalError(loc,
                             "Expects update_window_dims to not repeat; got: [",
                             updateWindowDims, "].");

  // scatter_c8
  if (updatesTypeRanked) {
    for (int64_t windowDim : updateWindowDims) {
      if (windowDim < 0 || windowDim >= updateType.getRank())
        return emitOptionalError(
            loc,
            "Expects each element of update_window_dims to be in range "
            "[0, "
            "rank-of('updates') i.e. [0, ",
            updateType.getRank(), "). got: ", windowDim, ".");
    }
  }

  // scatter_c9
  if (!llvm::is_sorted(insertedWindowDims))
    return emitOptionalError(
        loc, "Expects inserted_window_dims to be sorted; got: [",
        insertedWindowDims, "].");

  // scatter_c9
  if (hasDuplicates(insertedWindowDims))
    return emitOptionalError(
        loc, "Expects inserted_window_dims to not repeat; got: [",
        insertedWindowDims, "].)");

  // scatter_c10
  if (operandTypeRanked) {
    for (int64_t insertedDim : insertedWindowDims)
      if (insertedDim < 0 || insertedDim >= operandType.getRank())
        return emitOptionalError(
            loc,
            "Expects each element of inserted_window_dims to be in range "
            "[0, rank-of('operand') i.e. [0, ",
            operandType.getRank(), "). got: ", insertedDim, ".");
  }

  // scatter_c11
  if (scatterIndicesTypeRanked) {
    if (indexVectorDim == static_cast<int64_t>(scatterIndicesShape.size()) &&
        scatterDimsToOperandDims.size() != 1)
      return emitOptionalError(
          loc, "Scatter op has ", scatterDimsToOperandDims.size(),
          " elements in scatter_dims_to_operand_dims and "
          "the bound of dimension index_vector_dim=",
          indexVectorDim,
          " of scatter_indices is 1. These two numbers must be equal.");

    if (!isDynamicDimSize(scatterIndicesShape[indexVectorDim]) &&
        static_cast<int64_t>(scatterDimsToOperandDims.size()) !=
            scatterIndicesShape[indexVectorDim])
      return emitOptionalError(loc, "Scatter op has ",
                               scatterDimsToOperandDims.size(),
                               " elements in scatter_dims_to_operand_dims and "
                               "the bound of dimension index_vector_dim=",
                               indexVectorDim, " of scatter_indices is ",
                               scatterIndicesShape[indexVectorDim],
                               ". These two numbers must be equal.");
  }

  // scatter_c12
  if (hasDuplicates(scatterDimsToOperandDims))
    return emitOptionalError(
        loc, "Expects scatter_dims_to_operand_dims to not repeat; got: [",
        scatterDimsToOperandDims, "].");

  // scatter_c13
  if (operandTypeRanked) {
    for (int64_t i = 0;
         i < static_cast<int64_t>(scatterDimsToOperandDims.size()); ++i) {
      int64_t scatterDimToOperandDim = scatterDimsToOperandDims[i];
      if (scatterDimToOperandDim < 0 ||
          scatterDimToOperandDim >= operandType.getRank())
        return emitOptionalError(
            loc, "Invalid scatter_dims_to_operand_dims mapping; domain is [0, ",
            operandType.getRank(), "), got: ", i, "->", scatterDimToOperandDim,
            ".");
    }
  }

  return success();
}

static LogicalResult verifyGather(
    std::optional<Location> location, ShapeAdaptor operandShape,
    ShapeAdaptor startIndicesShape, ShapeAdaptor sliceSizesShape,
    ArrayRef<int64_t> offsetDims, ArrayRef<int64_t> collapsedSliceDims,
    ArrayRef<int64_t> startIndexMap, int64_t indexVectorDim) {
  // gather_c9
  if (hasDuplicates(startIndexMap))
    return emitOptionalError(location,
                             "expects start_index_map to not repeat, got: [",
                             startIndexMap, "]");

  // gather_c10
  for (int64_t i = 0; i < static_cast<int64_t>(startIndexMap.size()); ++i)
    if (startIndexMap[i] < 0 ||
        (operandShape.hasRank() && startIndexMap[i] >= operandShape.getRank()))
      return emitOptionalError(
          location, "start_index_map[", i, "]: ", startIndexMap[i],
          " is out of bounds for ", "operand rank ", operandShape.getRank());

  if (startIndicesShape.hasRank()) {
    // gather_c2
    // index_vector_dim == start_indices.rank implies a trailing 1 on the shape
    // of start_indices.
    if (indexVectorDim > startIndicesShape.getRank() || indexVectorDim < 0)
      return emitOptionalError(location, "index_vector_dim ", indexVectorDim,
                               " is out of bounds for start indices with rank ",
                               startIndicesShape.getRank());

    // gather_c3
    bool impliedTrailingDim = indexVectorDim == startIndicesShape.getRank();
    if (impliedTrailingDim || !startIndicesShape.isDynamicDim(indexVectorDim)) {
      int64_t effectiveDimSize;
      if (impliedTrailingDim)
        effectiveDimSize = 1;
      else
        effectiveDimSize = startIndicesShape.getDimSize(indexVectorDim);
      if (effectiveDimSize != static_cast<int64_t>(startIndexMap.size()))
        return emitOptionalError(
            location, "start_index_map size (", startIndexMap.size(),
            ") is not equal to size of index dimension (", indexVectorDim,
            ") of start_indices (", effectiveDimSize, ")");
    }
  }

  // gather_c4
  if (!llvm::is_sorted(offsetDims))
    return emitOptionalError(
        location, "expects offset_dims to be sorted, got: [", offsetDims, "]");
  if (hasDuplicates(offsetDims))
    return emitOptionalError(
        location, "expects offset_dims to not repeat, got: [", offsetDims, "]");

  // gather_c6
  if (!llvm::is_sorted(collapsedSliceDims))
    return emitOptionalError(
        location, "expects collapsed_slice_dims to be sorted, got: [",
        collapsedSliceDims, "]");
  if (hasDuplicates(collapsedSliceDims))
    return emitOptionalError(
        location, "expects collapsed_slice_dims to not repeat, got: [",
        collapsedSliceDims, "]");

  // gather_c1
  int64_t impliedOperandRank = offsetDims.size() + collapsedSliceDims.size();
  if (operandShape.hasRank() && operandShape.getRank() != impliedOperandRank)
    return emitOptionalError(
        location, "offset_dims size (", offsetDims.size(),
        ") plus collapse_slice_dims size (", collapsedSliceDims.size(),
        ") is not equal to operand rank (", operandShape.getRank(), ")");

  // gather_i7
  if (sliceSizesShape.hasRank() && sliceSizesShape.getRank() != 1)
    return emitOptionalError(location, "slice_sizes.rank != 1");
  if (sliceSizesShape.hasStaticShape()) {
    int64_t sliceSize = sliceSizesShape.getNumElements();

    // gather_c11
    if (sliceSize != impliedOperandRank)
      return emitOptionalError(location, "slice_sizes size (", sliceSize,
                               ") not equal to (implied) operand rank (",
                               impliedOperandRank, ")");

    // gather_c7
    for (auto dim : collapsedSliceDims)
      if (dim < 0 || dim >= sliceSize)
        return emitOptionalError(location, "collapsed dimension ", dim,
                                 " is out of bounds for slice_sizes.size (",
                                 sliceSize, ")");
  }

  return success();
}

template <typename dimTy>
static void inferGatherShape(
    int64_t resultRank, llvm::function_ref<dimTy(int64_t)> getStartIndicesDim,
    llvm::function_ref<dimTy(int64_t)> getSliceDim,
    ArrayRef<int64_t> offsetDims, ArrayRef<int64_t> collapsedSliceDims,
    ArrayRef<int64_t> startIndexMap, int64_t indexVectorDim,
    SmallVectorImpl<dimTy>& shape) {
  // We don't necessarily know the rank of sliceSizes, but we do know that it
  // can't be larger than the highest collapsed dimension. So go through those
  // and populate the leading dimensions of adjustedSliceSizes. The trailing
  // dimensions can just be adjusted by an offset.
  const auto* maxCollapsedDimIt =
      std::max_element(collapsedSliceDims.begin(), collapsedSliceDims.end());
  int64_t maxCollapsedDim = -1;
  if (maxCollapsedDimIt != collapsedSliceDims.end())
    maxCollapsedDim = *maxCollapsedDimIt;

  SmallVector<dimTy> adjustedSliceSizePrefix;
  for (int dimIndex = 0; dimIndex <= maxCollapsedDim; ++dimIndex) {
    if (llvm::is_contained(collapsedSliceDims, dimIndex)) continue;
    adjustedSliceSizePrefix.push_back(getSliceDim(dimIndex));
  }
  auto getAdjustedSliceDim = [&](int64_t index) -> dimTy {
    if (index < static_cast<int64_t>(adjustedSliceSizePrefix.size()))
      return adjustedSliceSizePrefix[index];
    return getSliceDim(index + collapsedSliceDims.size());
  };

  // Dimensions in the output that aren't offset dimensions are called batch
  // dimensions.
  SmallVector<int64_t> batchDims;
  for (int dim = 0; dim < resultRank; ++dim)
    if (!llvm::is_contained(offsetDims, dim)) batchDims.push_back(dim);

  for (int i = 0; i < resultRank; ++i) {
    const auto* offsetDimsIt =
        std::find(offsetDims.begin(), offsetDims.end(), i);
    if (offsetDimsIt != offsetDims.end()) {
      auto index = std::distance(offsetDims.begin(), offsetDimsIt);
      shape.push_back(getAdjustedSliceDim(index));
      continue;
    }
    auto* batchDimsIt = std::find(batchDims.begin(), batchDims.end(), i);
    assert(batchDimsIt != batchDims.end());
    auto index = std::distance(batchDims.begin(), batchDimsIt);
    // This can never run into the special case where start_indices gets
    // implicitly expanded with a trailing 1 if
    // index_vector_dim = start_indices.rank because then index would equal
    // index_vector_dim, which means we'd be looking at index+1, which would be
    // out of bounds anyway.
    if (index >= indexVectorDim) ++index;
    shape.push_back(getStartIndicesDim(index));
  }
}

void reifyGatherDimSizes(int64_t resultRank,
                         llvm::function_ref<Value(int64_t)> getStartIndicesDim,
                         llvm::function_ref<Value(int64_t)> getSliceDim,
                         ArrayRef<int64_t> offsetDims,
                         ArrayRef<int64_t> collapsedSliceDims,
                         ArrayRef<int64_t> startIndexMap,
                         int64_t indexVectorDim,
                         SmallVectorImpl<Value>& shape) {
  inferGatherShape<Value>(resultRank, getStartIndicesDim, getSliceDim,
                          offsetDims, collapsedSliceDims, startIndexMap,
                          indexVectorDim, shape);
}

static LogicalResult inferGatherReturnTypeComponents(
    std::optional<Location> location, ShapeAdaptor operandShape,
    Value startIndices, llvm::function_ref<int64_t(int64_t)> getSliceDim,
    ArrayRef<int64_t> offsetDims, ArrayRef<int64_t> collapsedSliceDims,
    ArrayRef<int64_t> startIndexMap, int64_t indexVectorDim,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  Type elementType = operandShape.getElementType();
  ShapeAdaptor startIndicesShape(startIndices.getType());

  // We need this to determine the result rank. We could still place bounds on
  // the result rank if that was something ShapedTypeComponents could express.
  if (!startIndicesShape.hasRank()) {
    inferredReturnShapes.push_back(elementType);
    return success();
  }

  int64_t startIndicesRank = startIndicesShape.getRank();
  // If index_vector_dim == start_indices.rank, then an implicit trailing 1 is
  // appended to start_indices shape.
  if (indexVectorDim == startIndicesRank) ++startIndicesRank;
  int64_t resultRank = offsetDims.size() + startIndicesRank - 1;
  // gather_c5
  for (int64_t i = 0; i < static_cast<int64_t>(offsetDims.size()); ++i)
    if (offsetDims[i] < 0 || offsetDims[i] >= resultRank)
      return emitOptionalError(location, "offset_dims[", i,
                               "]: ", offsetDims[i], " is out of bounds for ",
                               "implied result rank ", resultRank);

  auto getStartIndicesDim = [&](int64_t index) {
    return startIndicesShape.getDimSize(index);
  };

  // gather_c13
  SmallVector<int64_t> shape;
  inferGatherShape<int64_t>(resultRank, getStartIndicesDim, getSliceDim,
                            offsetDims, collapsedSliceDims, startIndexMap,
                            indexVectorDim, shape);

  // The dimension sizes of result, corresponding to offset dimensions, depend
  // on attributes (like `collapsed_slice_dims` and `slice_sizes`) and hence are
  // always static. Whereas, the dimension sizes of result, corresponding to
  // batch dimensions, depends on input `start_indices` and could be dynamic.
  // The corresponding bounds, in that case,  are propagated from the
  // `start_indices`.
  Attribute encoding =
      startIndices.getType().cast<RankedTensorType>().getEncoding();
  ArrayRef<int64_t> startIndicesBounds = encodingToBounds(encoding);
  SmallVector<int64_t> inferredBounds(resultRank, ShapedType::kDynamic);
  if (!startIndicesBounds.empty()) {
    llvm::BitVector isOffsetDim(resultRank);
    for (auto offsetDim : offsetDims) isOffsetDim.set(offsetDim);

    int64_t startIndicesDim = 0;
    for (int resultDim = 0; resultDim < resultRank; ++resultDim) {
      if (isOffsetDim.test(resultDim)) continue;

      if (startIndicesDim == indexVectorDim) ++startIndicesDim;
      inferredBounds[resultDim] = startIndicesBounds[startIndicesDim++];
    }
  }

  inferredReturnShapes.emplace_back(shape, elementType,
                                    boundsToEncoding(encoding, inferredBounds));
  return success();
}

// Used by IfOp and CaseOp
LogicalResult inferConditionalOp(std::optional<Location> location,
                                 Value operand, RegionRange branches,
                                 SmallVectorImpl<Type>& inferredReturnTypes) {
  // case_i1, if_i1
  auto operandRankedTy = operand.getType().dyn_cast<RankedTensorType>();
  if (operandRankedTy && operandRankedTy.getRank() != 0)
    return emitOptionalError(location,
                             "operand should be rank 0 tensor but got rank ",
                             operandRankedTy.getRank());
  // case_c1
  if (branches.empty())
    return emitOptionalError(location, "expect at least one branch");
  for (auto region : branches)
    if (failed(verifyRegionNotEmpty(location, *region))) return failure();

  ValueTypeRange<OperandRange> branch0ResultTypes =
      branches[0]->front().getTerminator()->getOperandTypes();
  for (unsigned i = 0; i < branches.size(); ++i) {
    Twine branchName = "branch " + Twine(i);
    Region* region = branches[i];
    // case_c2, if_c1
    if (region->getNumArguments() != 0)
      return emitOptionalError(location, branchName,
                               " must have 0 arguments, but found ",
                               region->getNumArguments());
    // case_c3, if_c2
    auto branchResultTypes = region->front().getTerminator()->getOperandTypes();
    if (!hlo::isCompatibleForHloTypeInference(branch0ResultTypes,
                                              branchResultTypes))
      return emitOptionalError(location, "branch 0 and ", branchName,
                               " have mismatched return types: ",
                               branch0ResultTypes, " vs ", branchResultTypes);
  }
  // case_c4, if_c3
  for (unsigned i = 0; i < branch0ResultTypes.size(); ++i) {
    SmallVector<Type> inputTypes;
    for (auto branch : branches)
      inputTypes.push_back(
          branch->front().getTerminator()->getOperandTypes()[i]);
    auto inferredTypeOrErr = inferLeastSpecificType(location, inputTypes);
    if (failed(inferredTypeOrErr)) return failure();
    inferredReturnTypes.emplace_back(*inferredTypeOrErr);
  }
  return success();
}

LogicalResult verifyDimInBounds(std::optional<Location> loc, ShapedType type,
                                int64_t dim) {
  if (dim < 0)
    return emitOptionalError(
        loc, "requires non-negative dimension attribute; found (", dim, ")");
  if (type.hasRank() && dim >= type.getRank())
    return emitOptionalError(loc, "requires dimension attribute in range [0, ",
                             type.getRank(), "); found (", dim, ")");
  return success();
}

//===----------------------------------------------------------------------===//
// Shape functions for ops.
//===----------------------------------------------------------------------===//

LogicalResult inferAbsOp(std::optional<Location>, Value operand,
                         SmallVectorImpl<Type>& inferredReturnTypes) {
  auto operandTy = operand.getType().cast<ShapedType>();
  // abs_c2
  Type elementTy = operandTy.getElementType();
  if (auto complexTy = elementTy.dyn_cast<ComplexType>())
    elementTy = complexTy.getElementType();

  Type resultTy;
  // abs_c1
  if (auto rankedOperandTy = operandTy.dyn_cast<RankedTensorType>()) {
    resultTy = RankedTensorType::get(operandTy.getShape(), elementTy,
                                     rankedOperandTy.getEncoding());
  } else if (operandTy.hasRank()) {
    resultTy = RankedTensorType::get(operandTy.getShape(), elementTy);
  } else {
    resultTy = UnrankedTensorType::get(elementTy);
  }
  inferredReturnTypes.push_back(resultTy);
  return success();
}

LogicalResult inferAfterAllOp(HloDialectInterface* dialect,
                              std::optional<Location> location,
                              SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(dialect->createTokenType());
  return success();
}

LogicalResult inferAllToAllOp(
    std::optional<Location> location, Value operand, int64_t splitDimension,
    int64_t concatDimension, int64_t splitCount,
    DenseIntElementsAttr replicaGroups,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // all_to_all_c4
  if (splitCount <= 0)
    return emitOptionalError(location, "AllToAll split_count must be > 0");

  // all_to_all_c5, all_to_all_c7, all_to_all_i5
  if (failed(verifyReplicaGroups(location, replicaGroups,
                                 /*allGroupsMustHaveSameSize=*/true,
                                 /*useGlobalDeviceIds=*/false, splitCount)))
    return failure();

  // all_to_all_c1
  if (splitDimension < 0)
    return emitOptionalError(location,
                             "AllToAll split_dimension cannot be negative");

  // all_to_all_c3
  if (concatDimension < 0)
    return emitOptionalError(location,
                             "AllToAll concat_dimension cannot be negative");

  Type operandType = operand.getType();
  auto operandRankedType = operandType.dyn_cast<RankedTensorType>();
  if (!operandRankedType) {
    inferredReturnShapes.emplace_back(
        operandType.cast<ShapedType>().getElementType());
    return success();
  }

  int64_t inputRank = operandRankedType.getRank();
  // all_to_all_c1
  if (splitDimension >= inputRank)
    return emitOptionalError(location, "AllToAll split_dimension ",
                             splitDimension,
                             " is out-of-bounds for input rank ", inputRank);
  // all_to_all_c3
  if (concatDimension >= inputRank)
    return emitOptionalError(location, "AllToAll concat_dimension ",
                             concatDimension,
                             " is out-of-bounds for input rank ", inputRank);

  SmallVector<int64_t> resultShape(operandRankedType.getShape().begin(),
                                   operandRankedType.getShape().end());
  // all_to_all_c2
  if (isStaticDimSize(resultShape[splitDimension]) &&
      resultShape[splitDimension] % splitCount != 0)
    return emitOptionalError(
        location, "split dimension has size ", resultShape[splitDimension],
        ", expected to be a multiple of split_count ", splitCount);
  if (isStaticDimSize(resultShape[splitDimension]))
    resultShape[splitDimension] /= splitCount;
  if (isStaticDimSize(resultShape[concatDimension]))
    resultShape[concatDimension] *= splitCount;

  SmallVector<int64_t> resultBounds =
      to_vector(encodingToBounds(operandRankedType.getEncoding()));
  if (!resultBounds.empty()) {
    if (isStaticDimSize(resultBounds[splitDimension]))
      resultBounds[splitDimension] /= splitCount;
    if (isStaticDimSize(resultBounds[concatDimension]))
      resultBounds[concatDimension] *= splitCount;
  }

  inferredReturnShapes.emplace_back(
      resultShape, operandRankedType.getElementType(),
      boundsToEncoding(operandRankedType.getEncoding(), resultBounds));
  return success();
}

LogicalResult inferBatchNormGradOp(
    std::optional<Location> location, Value operand, Value scale, Value mean,
    Value variance, Value gradOutput, int64_t featureIndex,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  return inferBatchNormOp(location, {operand, gradOutput},
                          {scale, mean, variance}, featureIndex,
                          inferredReturnShapes, /*is_inference=*/false);
}

LogicalResult inferBatchNormInferenceOp(
    std::optional<Location> location, Value operand, Value scale, Value offset,
    Value mean, Value variance, int64_t featureIndex,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  return inferBatchNormOp(location, {operand}, {scale, offset, mean, variance},
                          featureIndex, inferredReturnShapes,
                          /*is_inference=*/true);
}

LogicalResult inferBatchNormTrainingOp(
    std::optional<Location> location, Value operand, Value scale, Value offset,
    int64_t featureIndex,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  return inferBatchNormOp(location, {operand}, {scale, offset}, featureIndex,
                          inferredReturnShapes, /*is_inference=*/false);
}

LogicalResult inferBroadcastOp(
    std::optional<Location> location, Value operand,
    ArrayRef<int64_t> broadcastSizes,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().dyn_cast<RankedTensorType>();
  if (!operandType) return failure();

  for (int64_t size : broadcastSizes)
    if (size < 0)
      return emitOptionalError(location,
                               "Broadcast with negative dimension size ", size);
  SmallVector<int64_t> shapeValues(broadcastSizes);
  llvm::append_range(shapeValues, operandType.getShape());

  inferredReturnShapes.emplace_back(shapeValues, operandType.getElementType());
  return success();
}

LogicalResult inferCaseOp(std::optional<Location> location, Value index,
                          RegionRange branches,
                          SmallVectorImpl<Type>& inferredReturnTypes) {
  return inferConditionalOp(location, index, branches, inferredReturnTypes);
}

LogicalResult inferCholeskyOp(
    std::optional<Location> location, Value a,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  Type aType = a.getType();
  RankedTensorType aRankedType = aType.dyn_cast<RankedTensorType>();
  if (!aRankedType) {
    // cholesky_c1
    inferredReturnShapes.emplace_back(
        aType.cast<ShapedType>().getElementType());
    return success();
  }

  ArrayRef<int64_t> aShape = aRankedType.getShape();
  // cholesky_c2
  if (aShape.size() < 2)
    return emitOptionalError(
        location, "argument 'a' must have rank >= 2, got shape ", aShape, ".");

  // cholesky_c3
  if (!verifyCompatibleDims(aShape[aShape.size() - 2],
                            aShape[aShape.size() - 1]))
    return emitOptionalError(
        location, "minor dimensions of 'a' must have equal size, got shape ",
        aShape, ".");

  // cholesky_c1
  inferredReturnShapes.emplace_back(aRankedType.getShape(),
                                    aRankedType.getElementType(),
                                    aRankedType.getEncoding());
  return success();
}

LogicalResult inferClampOp(
    std::optional<Location> location, Value min, Value operand, Value max,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().cast<RankedTensorType>();
  auto operandShape = operandType.getShape();
  auto minType = min.getType().cast<RankedTensorType>();

  // clamp_c1
  auto minShape = minType.getShape();
  if (failed(verifyCompatibleShape(minType, operandType)) &&
      minType.getRank() != 0)
    return emitOptionalError(
        location, "min shape [",
        llvm::make_range(minShape.begin(), minShape.end()),
        "] is not scalar and is not compatible to operand shape [",
        llvm::make_range(operandShape.begin(), operandShape.end()), "]");

  // clamp_c2
  auto maxType = max.getType().cast<RankedTensorType>();
  auto maxShape = maxType.getShape();
  if (failed(verifyCompatibleShape(maxType, operandType)) &&
      maxType.getRank() != 0)
    return emitOptionalError(
        location, "max shape [",
        llvm::make_range(maxShape.begin(), maxShape.end()),
        "] is not scalar and is not compatible to operand shape [",
        llvm::make_range(operandShape.begin(), operandShape.end()), "]");

  // clamp_c4
  inferredReturnShapes.emplace_back(operandType.cast<ShapedType>());
  return success();
}

LogicalResult inferCompareOp(
    MLIRContext* context, std::optional<Location>, Value lhs,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // compare_c1
  ShapedTypeComponents& components =
      inferredReturnShapes.emplace_back(IntegerType::get(context, /*width=*/1));
  auto argTy = lhs.getType().cast<ShapedType>();
  // compare_c2
  if (argTy.hasRank())
    components =
        ShapedTypeComponents(argTy.getShape(), components.getElementType());
  return success();
}

LogicalResult inferComplexOp(std::optional<Location> location, Value lhs,
                             SmallVectorImpl<Type>& inferredReturnTypes) {
  ShapedType operandType = lhs.getType().cast<ShapedType>();
  // complex_c3
  ComplexType elementTy = ComplexType::get(operandType.getElementType());
  // complex_c2
  inferredReturnTypes.push_back(getSameShapeTensorType(operandType, elementTy));
  return success();
}

LogicalResult inferConcatenateOp(std::optional<Location> location,
                                 TypeRange inputTypes, int64_t dimension,
                                 SmallVectorImpl<Type>& inferredReturnTypes) {
  // concatenate_c4
  if (dimension < 0)
    return emitOptionalError(location, "dimension ", dimension, " is negative");
  RankedTensorType firstRankedType;
  int firstRankedIndex = -1;
  for (uint64_t i = 0; i < inputTypes.size(); i++) {
    auto secondType = inputTypes[i].cast<ShapedType>();
    if (!secondType.hasRank()) continue;
    if (!firstRankedType) {
      firstRankedType = secondType.cast<RankedTensorType>();
      firstRankedIndex = i;
      // concatenate_c4
      if (firstRankedType.getRank() == 0)
        return emitOptionalError(location,
                                 "rank-0 values cannot be concatenated");
      // concatenate_c4
      if (dimension >= firstRankedType.getRank())
        return emitOptionalError(location, "dimension ", dimension,
                                 " is out-of-bounds for input rank ",
                                 firstRankedType.getRank());
      continue;
    }
    // concatenate_c2
    if (firstRankedType.getRank() != secondType.getRank())
      return emitOptionalError(location, "operands (", firstRankedIndex,
                               ") and (", i, ") do not match rank");

    auto firstShape = firstRankedType.getShape();
    auto secondShape = secondType.getShape();
    for (int d = 0; d < firstRankedType.getRank(); ++d) {
      // concatenate_c2
      if (d != dimension &&
          !verifyCompatibleDims(firstShape[d], secondShape[d]))
        return emitOptionalError(
            location, "shapes of operand (", firstRankedIndex, ") and (", i,
            ") do not match at non-concat "
            "index: (",
            llvm::make_range(firstShape.begin(), firstShape.end()), ") != (",
            llvm::make_range(secondShape.begin(), secondShape.end()),
            ") at non-concat index ", d);
    }
  }
  // concatenate_c5
  auto elementType = inputTypes[0].cast<ShapedType>().getElementType();
  if (!firstRankedType) {
    inferredReturnTypes.push_back(UnrankedTensorType::get(elementType));
    return success();
  }

  // Infer the most specific (size, bound) of all dimensions of the return type
  auto rank = firstRankedType.getRank();
  SmallVector<int64_t> inferredSizes(rank, ShapedType::kDynamic);
  SmallVector<int64_t> inferredBounds(rank, ShapedType::kDynamic);
  // Note: for the concatenate dimension, 0 should be the identity element:
  // Any dim size can keep unchanged when concatenated with 0
  inferredSizes[dimension] = 0;
  bool anyInputHaveBounds = false;

  // Note: unranked input types can't be ignored, consider these input types:
  // c0: (<5x?xf32>, <*xf32>) with concat dim 0 should infer <?x?xf32>
  // c1: (<5x?xf32>, <*xf32>) with concat dim 1 should infer <5x?xf32>
  // Instead, they should be replaced with dynamic tensors: tensor<?x...?x>
  for (const auto& it : llvm::enumerate(inputTypes)) {
    RankedTensorType rankedType = it.value().dyn_cast<RankedTensorType>();
    SmallVector<int64_t> bounds;
    if (rankedType)
      bounds = to_vector(encodingToBounds(rankedType.getEncoding()));
    if (!bounds.empty()) anyInputHaveBounds = true;

    for (int dim = 0; dim < rank; ++dim) {
      std::pair<int64_t, int64_t> inferredDimAndBound;

      int64_t leftSize = inferredSizes[dim];
      int64_t rightSize =
          rankedType ? rankedType.getShape()[dim] : ShapedType::kDynamic;
      int64_t leftBound = inferredBounds[dim];
      int64_t rightBound = bounds.empty() ? ShapedType::kDynamic : bounds[dim];
      if (dim == dimension) {
        inferredDimAndBound = inferConcatenatedDimAndBound(
            leftSize, rightSize, leftBound, rightBound);
      } else {
        auto inferredDimAndBoundOrErr = inferMostSpecificDimAndBound(
            location, dim, leftSize, rightSize, leftBound, rightBound);
        if (failed(inferredDimAndBoundOrErr)) return failure();
        inferredDimAndBound = *inferredDimAndBoundOrErr;
      }
      inferredSizes[dim] = inferredDimAndBound.first;
      inferredBounds[dim] = inferredDimAndBound.second;
    }
  }
  // concatenate_c5, concatenate_c6
  inferredReturnTypes.push_back(RankedTensorType::get(
      inferredSizes, elementType,
      boundsToEncoding(
          firstRankedType.getEncoding(),
          // Empty array as argument is an indicator to boundsToEncoding() that
          // there are no bounds at all in inputs, thus sparsity attributes will
          // be included in the return type
          anyInputHaveBounds ? inferredBounds : llvm::ArrayRef<int64_t>({}))));
  return success();
}

LogicalResult inferConstantOp(std::optional<Location>, ElementsAttr value,
                              SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(value.getType());
  return success();
}

LogicalResult inferConvertOp(
    std::optional<Location> location, Value operand,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().cast<ShapedType>();
  // convert_c1
  inferredReturnShapes.emplace_back(
      operandType.hasRank() ? operandType.getShape() : ArrayRef<int64_t>{});
  return success();
}

/*
 * We intend to verify the following properties
 *  P1. Verify the input, kernel types.
 *  P2. Verify the convolution atributes.
 *  P3. Verify and collect the window atributes.
 *  P4. Verify precision_config attribute.
 *  P5. Verify the return shape.
 *      TODO(b/232574102): Verify the element-type of return-value.
 */
LogicalResult inferConvolutionOp(
    std::optional<Location> location, Type lhsType, Type rhsType,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> padding,
    std::optional<DenseIntElementsAttr> lhsDilation,
    std::optional<DenseIntElementsAttr> rhsDilation,
    std::optional<DenseElementsAttr> windowReversal,
    int64_t inputBatchDimension, int64_t inputFeatureDimension,
    ArrayRef<int64_t> inputSpatialDimensions,
    int64_t kernelInputFeatureDimension, int64_t kernelOutputFeatureDimension,
    ArrayRef<int64_t> kernelSpatialDimensions, int64_t outputBatchDimension,
    int64_t outputFeatureDimension, ArrayRef<int64_t> outputSpatialDimensions,
    int64_t featureGroupCount, int64_t batchGroupCount,
    std::optional<ArrayAttr> precisionConfig,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto rankedLhsType = lhsType.dyn_cast<RankedTensorType>();
  auto rankedRhsType = rhsType.dyn_cast<RankedTensorType>();
  if (!rankedLhsType || !rankedRhsType) {
    inferredReturnShapes.push_back({});
    return success();
  }

  // P1.
  int numDims = rankedLhsType.getRank();
  if (numDims != rankedRhsType.getRank())
    return emitOptionalError(location,
                             "expects convolution arguments to have same "
                             "number of dimensions. Got: ",
                             rankedLhsType, " and ", rankedRhsType, ".");
  if (numDims < 2)
    return emitOptionalError(
        location,
        "expects convolution arguments to have >= 2 dimensions. Got: ",
        rankedLhsType, " and ", rankedRhsType, ".");
  // P2.
  if (failed(verifyConvolutionAttributes(
          location, lhsType, rhsType, inputBatchDimension,
          inputFeatureDimension, inputSpatialDimensions,
          kernelInputFeatureDimension, kernelOutputFeatureDimension,
          kernelSpatialDimensions, outputBatchDimension, outputFeatureDimension,
          outputSpatialDimensions, featureGroupCount, batchGroupCount,
          precisionConfig)))
    return failure();

  if ((size_t)numDims != inputSpatialDimensions.size() + 2)
    return emitOptionalError(location, "expects convolution arguments to have ",
                             inputSpatialDimensions.size() + 2,
                             " dimensions. Got: ", numDims);

  // P3.
  SmallVector<int64_t> windowDimensions(kernelSpatialDimensions.size());
  for (size_t i = 0; i < windowDimensions.size(); i++)
    windowDimensions[i] = rankedRhsType.getShape()[kernelSpatialDimensions[i]];

  auto paddingOrErr = convertPaddingAttribute(padding, location);
  if (failed(paddingOrErr)) return failure();

  // TODO: add missing tests for ConvolutionOp.
  auto windowStridesOrErr =
      convert1DAttribute(windowStrides, location, "window_strides");
  if (failed(windowStridesOrErr)) return failure();
  auto lhsDilationOrErr =
      convert1DAttribute(lhsDilation, location, "lhs_dilation");
  if (failed(lhsDilationOrErr)) return failure();
  auto rhsDilationOrErr =
      convert1DAttribute(rhsDilation, location, "rhs_dilation");
  if (failed(rhsDilationOrErr)) return failure();
  auto windowReversalOrErr = convertWindowReversalAttribute(
      windowReversal, location, "window_reversal");
  if (failed(windowReversalOrErr)) return failure();

  auto windowOrErr = verifyWindowAttributesAndInferWindowDimensions(
      windowDimensions, *windowStridesOrErr, *paddingOrErr, *lhsDilationOrErr,
      *rhsDilationOrErr, *windowReversalOrErr, location);
  if (failed(windowOrErr)) return failure();

  // P3.
  if (failed(verifyPrecisionConfig(location, precisionConfig)))
    return failure();

  // P5.
  SmallVector<int64_t> outputDimensions(rankedLhsType.getShape().size(),
                                        ShapedType::kDynamic);

  // Infer the output spatial dimensions.
  auto numSpatialDims = inputSpatialDimensions.size();
  SmallVector<int64_t> inputSpatialDimVals(numSpatialDims);
  for (int64_t i = 0; i < static_cast<int64_t>(numSpatialDims); ++i)
    inputSpatialDimVals[i] =
        rankedLhsType.getShape()[inputSpatialDimensions[i]];
  auto windowOutputShape =
      inferWindowOutputShape(inputSpatialDimVals, *windowOrErr);

  for (int64_t i = 0; i < static_cast<int64_t>(windowOrErr->size()); ++i)
    outputDimensions[outputSpatialDimensions[i]] = windowOutputShape[i];

  // Infer the output-batch-dimension and output-feature-dimension.
  const int64_t inputBatch = rankedLhsType.getShape()[inputBatchDimension];
  const int64_t kernelOutputFeatures =
      rankedRhsType.getShape()[kernelOutputFeatureDimension];
  outputDimensions[outputBatchDimension] = isDynamicDimSize(inputBatch)
                                               ? ShapedType::kDynamic
                                               : inputBatch / batchGroupCount;
  outputDimensions[outputFeatureDimension] = kernelOutputFeatures;

  inferredReturnShapes.emplace_back(outputDimensions);
  return success();
}

LogicalResult inferCreateTokenOp(HloDialectInterface* dialect,
                                 std::optional<Location> location,
                                 SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(dialect->createTokenType());
  return success();
}

LogicalResult inferDotOp(
    std::optional<Location> location, Value lhs, Value rhs,
    std::optional<ArrayAttr> precisionConfig,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  if (failed(verifyPrecisionConfig(location, precisionConfig)))
    return failure();

  auto lhsType = lhs.getType().dyn_cast<RankedTensorType>();
  auto rhsType = rhs.getType().dyn_cast<RankedTensorType>();
  if (!lhsType || !rhsType) {
    inferredReturnShapes.push_back({});
    return success();
  }

  SmallVector<int64_t> dimensions;
  if (1 == lhsType.getRank() && 1 == rhsType.getRank() &&
      // vector dot vector
      verifyCompatibleDims(lhsType.getDimSize(0), rhsType.getDimSize(0))) {
  } else if (2 == lhsType.getRank() && 1 == rhsType.getRank() &&
             verifyCompatibleDims(lhsType.getDimSize(1),
                                  rhsType.getDimSize(0))) {
    // matrix dot vector
    dimensions.push_back(lhsType.getDimSize(0));
  } else if (1 == lhsType.getRank() && 2 == rhsType.getRank() &&
             verifyCompatibleDims(lhsType.getDimSize(0),
                                  rhsType.getDimSize(0))) {
    // vector dot matrix
    dimensions.push_back(rhsType.getDimSize(1));
  } else if (2 == lhsType.getRank() && 2 == rhsType.getRank() &&
             verifyCompatibleDims(lhsType.getDimSize(1),
                                  rhsType.getDimSize(0))) {
    // matrix dot matrix
    dimensions.push_back(lhsType.getDimSize(0));
    dimensions.push_back(rhsType.getDimSize(1));
  } else {
    return emitOptionalError(location,
                             "expected both lhs/rhs ranks to be "
                             "either 1 or 2");
  }

  inferredReturnShapes.emplace_back(dimensions);
  return success();
}

LogicalResult inferDotGeneralOp(
    std::optional<Location> location, Type lhsType, Type rhsType,
    ArrayRef<int64_t> lhsBatchingDimensions,
    ArrayRef<int64_t> rhsBatchingDimensions,
    ArrayRef<int64_t> lhsContractingDimensions,
    ArrayRef<int64_t> rhsContractingDimensions,
    std::optional<ArrayAttr> precisionConfig,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // dot_general_c11
  if (failed(verifyPrecisionConfig(location, precisionConfig)))
    return failure();

  // dot_general_c1
  if (lhsBatchingDimensions.size() != rhsBatchingDimensions.size())
    return emitOptionalError(location,
                             "lhs and rhs should have the same "
                             "number of batching dimensions");

  // dot_general_c2
  if (lhsContractingDimensions.size() != rhsContractingDimensions.size())
    return emitOptionalError(location,
                             "lhs and rhs should have the same "
                             "number of contracting dimensions");

  llvm::SmallDenseSet<int64_t> dimSet;
  auto checkDimsDistinct =
      [&](ArrayRef<int64_t> batchingDims, ArrayRef<int64_t> contractingDims,
          llvm::SmallDenseSet<int64_t>& dimSet, llvm::StringRef lhs,
          llvm::StringRef rhs) -> LogicalResult {
    auto dims = llvm::concat<const int64_t>(batchingDims, contractingDims);
    for (auto dim : dims) {
      auto [_, wasInserted] = dimSet.insert(dim);
      if (!wasInserted)
        return emitOptionalError(location, "has duplicated dimension from ",
                                 lhs, " and ", rhs, ": ", dim);
    }
    return success();
  };

  // dot_general_c3
  if (failed(checkDimsDistinct(lhsBatchingDimensions, lhsContractingDimensions,
                               dimSet, "lhs_batching_dimensions",
                               "lhs_contracting_dimensions")))
    return failure();

  dimSet.clear();
  // dot_general_c4
  if (failed(checkDimsDistinct(rhsBatchingDimensions, rhsContractingDimensions,
                               dimSet, "rhs_batching_dimensions",
                               "rhs_contracting_dimensions")))
    return failure();

  auto checkDimsInRange = [&](int64_t rank, ArrayRef<int64_t> dims,
                              llvm::StringRef dimName) -> LogicalResult {
    auto inRange = [&](int64_t i) -> bool { return 0 <= i && i < rank; };
    const auto* dimsNotInRange =
        std::find_if_not(dims.begin(), dims.end(), inRange);
    if (dimsNotInRange != dims.end())
      return emitOptionalError(location, dimName, " value: ", *dimsNotInRange,
                               " is out of range: ", "[0, ", rank, ")");
    return success();
  };

  auto lhsRankedType = lhsType.dyn_cast<RankedTensorType>();
  if (lhsRankedType) {
    // dot_general_c5
    // dot_general_c6
    if (failed(checkDimsInRange(lhsRankedType.getRank(), lhsBatchingDimensions,
                                "lhs_batching_dimensions")) ||
        failed(checkDimsInRange(lhsRankedType.getRank(),
                                lhsContractingDimensions,
                                "lhs_contracting_dimensions")))
      return failure();
  }

  auto rhsRankedType = rhsType.dyn_cast<RankedTensorType>();
  if (rhsRankedType) {
    // dot_general_c7
    // dot_general_c8
    if (failed(checkDimsInRange(rhsRankedType.getRank(), rhsBatchingDimensions,
                                "rhs_batching_dimensions")) ||
        failed(checkDimsInRange(rhsRankedType.getRank(),
                                rhsContractingDimensions,
                                "rhs_contracting_dimensions")))
      return failure();
  }
  if (lhsRankedType && rhsRankedType) {
    // Dimension sizes must be compatible for lhs/rhs.
    auto lhsShape = lhsRankedType.getShape();
    auto rhsShape = rhsRankedType.getShape();

    for (auto [lhs, rhs] :
         llvm::zip(lhsBatchingDimensions, rhsBatchingDimensions)) {
      // dot_general_c9
      if (!verifyCompatibleDims(lhsShape[lhs], rhsShape[rhs]))
        return emitOptionalError(location,
                                 "batching dimension sizes must "
                                 "match for lhs/rhs");
    }

    for (auto [lhs, rhs] :
         llvm::zip(lhsContractingDimensions, rhsContractingDimensions)) {
      // dot_general_c10
      if (!verifyCompatibleDims(lhsShape[lhs], rhsShape[rhs]))
        return emitOptionalError(location,
                                 "contracting dimension sizes must "
                                 "match for lhs/rhs");
    }
  }

  if (!lhsRankedType || !rhsRankedType) {
    inferredReturnShapes.push_back({});
    return success();
  }

  // Infer the output dimensions of the operation.
  auto lhsShape = lhsRankedType.getShape();
  auto rhsShape = rhsRankedType.getShape();
  SmallVector<int64_t> dimensions;
  for (const int64_t lhsBatchingDim : lhsBatchingDimensions)
    dimensions.push_back(lhsShape[lhsBatchingDim]);
  for (int64_t i = 0; i < lhsRankedType.getRank(); i++)
    if (!llvm::is_contained(lhsBatchingDimensions, i) &&
        !llvm::is_contained(lhsContractingDimensions, i))
      dimensions.push_back(lhsShape[i]);
  for (int64_t i = 0; i < rhsRankedType.getRank(); i++)
    if (!llvm::is_contained(rhsBatchingDimensions, i) &&
        !llvm::is_contained(rhsContractingDimensions, i))
      dimensions.push_back(rhsShape[i]);

  // dot_general_c12
  inferredReturnShapes.emplace_back(dimensions);
  return success();
}

LogicalResult inferDynamicGatherOp(
    std::optional<Location> location, Value operand, Value startIndices,
    Value sliceSizes, ArrayRef<int64_t> offsetDims,
    ArrayRef<int64_t> collapsedSliceDims, ArrayRef<int64_t> startIndexMap,
    int64_t indexVectorDim,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  ShapeAdaptor operandShape(operand.getType());
  ShapeAdaptor startIndicesShape(startIndices.getType());
  ShapeAdaptor sliceSizesShape(sliceSizes.getType());

  if (failed(verifyGather(location, /*operandShape=*/operandShape,
                          /*startIndicesShape=*/startIndicesShape,
                          /*sliceSizesShape=*/sliceSizesShape, offsetDims,
                          collapsedSliceDims, startIndexMap, indexVectorDim)))
    return failure();

  auto getSliceDim = [&](int64_t index) {
    DenseIntElementsAttr sliceSizesAttr;
    if (!matchPattern(sliceSizes, m_Constant(&sliceSizesAttr)))
      return ShapedType::kDynamic;
    return sliceSizesAttr.getValues<APInt>()[index].getSExtValue();
  };
  return inferGatherReturnTypeComponents(
      location, operandShape, startIndices, getSliceDim, offsetDims,
      collapsedSliceDims, startIndexMap, indexVectorDim, inferredReturnShapes);
}

LogicalResult inferDynamicSliceOp(
    std::optional<Location> location, Type operandType,
    TypeRange startIndicesTypes, ArrayRef<int64_t> sliceSizes,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // dynamic_slice_c2
  int numSliceSizes = sliceSizes.size();
  int numStartIndices = startIndicesTypes.size();
  if (numStartIndices != numSliceSizes)
    return emitOptionalError(location, "has mismatched number of slice sizes (",
                             numSliceSizes, ") and number of start indices (",
                             numStartIndices, ")");
  auto rankedOperandType = operandType.dyn_cast<RankedTensorType>();
  if (!rankedOperandType) return failure();
  // dynamic_slice_c2
  if (rankedOperandType.getRank() != numStartIndices)
    return emitOptionalError(
        location, "has mismatched number of start indices (", numStartIndices,
        ") and the rank of operand (", rankedOperandType.getRank(), ")");

  // dynamic_slice_c3
  if (!tensorsHaveSameElType(startIndicesTypes))
    return emitOptionalError(location,
                             "start indices must have same element type");

  // dynamic_slice_c4
  for (int i = 0; i < numSliceSizes; ++i) {
    int64_t sliceSize = sliceSizes[i];
    if (sliceSize < 0)
      return emitOptionalError(
          location, "has negative size index to dynamic slice: ", sliceSize);
    if (!rankedOperandType.isDynamicDim(i)) {
      int64_t dimSize = rankedOperandType.getDimSize(i);
      if (sliceSize > dimSize)
        return emitOptionalError(location, "has slice size ", sliceSize,
                                 " greater than dimension size ", dimSize,
                                 " in dimension ", i, " of operand");
    }
  }

  // dynamic_slice_c5
  inferredReturnShapes.emplace_back(sliceSizes,
                                    rankedOperandType.getElementType());
  return success();
}

LogicalResult inferDynamicUpdateSliceOp(
    std::optional<Location> location, Value operand, Value update,
    ValueRange startIndices,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().cast<ShapedType>();
  auto updateType = update.getType().cast<ShapedType>();

  // dynamic_update_slice_c3
  if (updateType.hasRank() && operandType.hasRank() &&
      updateType.getRank() != operandType.getRank())
    return emitOptionalError(
        location,
        "update rank does not match operand rank: ", updateType.getRank(),
        " vs ", operandType.getRank(), ".");

  // dynamic_update_slice_c4
  if (operandType.hasRank() &&
      (int64_t)startIndices.size() != operandType.getRank())
    return emitOptionalError(
        location, "expects number of start_indices to match operand rank: ",
        startIndices.size(), " vs ", operandType.getRank(), ".");

  // dynamic_update_slice_c5
  if (!tensorsHaveSameElType(startIndices.getTypes()))
    return emitOptionalError(location,
                             "start indices must have same element type");

  // dynamic_update_slice_c6
  if (operandType.hasRank() && updateType.hasRank())
    for (auto [index, dims] : llvm::enumerate(
             llvm::zip(operandType.getShape(), updateType.getShape()))) {
      auto [operandDim, updateDim] = dims;
      if (isDynamicDimSize(updateDim)) continue;
      if (isStaticDimSize(operandDim)) {
        if (updateDim < 0 || updateDim > operandDim)
          return emitOptionalError(location, "expects size at dimension ",
                                   index, " of update to be in range [0, ",
                                   operandDim, "]. Got: ", updateDim, ".");
      } else {
        if (updateDim < 0)
          return emitOptionalError(
              location, "expects size at dimension ", index,
              " of update to be non-negative. Got: ", updateDim, ".");
      }
    }

  // dynamic_update_slice_c1
  if (operandType.hasRank())
    inferredReturnShapes.emplace_back(
        operandType.getShape(), operandType.getElementType(),
        operandType.cast<RankedTensorType>().getEncoding());
  else
    inferredReturnShapes.emplace_back(operandType.getElementType());
  return success();
}

// We intend to verify the following properties
// P1. 1 <= rank <= 3
// P2. Element types agree with fft_type
// P3. Operand shape dimensions agree with fft_length for the given fft_type
LogicalResult inferFftOp(
    std::optional<Location> location, Value operand, bool isFftTypeRfft,
    bool isFftTypeIrfft, ArrayRef<int64_t> fftLength,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  int64_t fftRank = fftLength.size();

  // P1.
  if (fftRank > 3 || fftRank < 1)
    return emitOptionalError(location, "rank must be between 1 and 3, but got ",
                             fftRank, ".");

  // P2. Element type agreement
  // FFT : C -> C
  // IFFT : C -> C
  // RFFT : R -> C
  // IRFFT : C -> R
  auto operandType = operand.getType().cast<ShapedType>();
  Type operandElementType = operandType.getElementType();
  // Check the input element type and infer return element type
  if (isFftTypeRfft) {
    if (!operandElementType.isF32() && !operandElementType.isF64())
      return emitOptionalError(
          location, "RFFT requires f32 or f64 input type, but is given ",
          operandElementType, ".");
  } else {
    if (!operandElementType.isa<ComplexType>())
      return emitOptionalError(location, "FFT/IFFT/IRFFT",
                               " take a complex tensor as input, but is given ",
                               operandType, ".");
  }
  // Generate the output element type
  Type resultElementType = operandElementType;
  if (isFftTypeRfft)  // RFFT : R -> C
    resultElementType = ComplexType::get(resultElementType);
  else if (isFftTypeIrfft)  // IRFFT : C -> R
    resultElementType = operandElementType.cast<ComplexType>().getElementType();

  // P3. Check input shape and infer return shape
  auto operandRankedType = operandType.dyn_cast<RankedTensorType>();
  if (!operandRankedType) {
    inferredReturnShapes.emplace_back(resultElementType);
    return success();
  }
  auto operandShape = operandRankedType.getShape();
  if (static_cast<int64_t>(operandShape.size()) < fftRank)
    return emitOptionalError(
        location, "operand rank must not be less than fft rank of ", fftRank,
        " for operand of type ", operandRankedType, ".");

  SmallVector<int64_t> resultShape = to_vector(operandShape);

  if (isFftTypeRfft) {
    auto shapeBack = operandShape.take_back(fftRank);
    for (auto [operandDim, fftDim] : llvm::zip(shapeBack, fftLength)) {
      if (!verifyCompatibleDims(operandDim, fftDim))
        return emitOptionalError(location,
                                 "RFFT requires innermost dimensions to be "
                                 "compatible with fft_length. Got: ",
                                 operandShape, " but wanted ", fftLength, ".");
    }
    if (fftLength[fftRank - 1] != 0)
      resultShape[resultShape.size() - 1] = fftLength[fftRank - 1] / 2 + 1;
  }
  if (isFftTypeIrfft) {
    auto shapeBack = operandShape.take_back(fftRank).drop_back();
    for (auto [operandDim, fftDim] : llvm::zip(shapeBack, fftLength)) {
      if (!verifyCompatibleDims(operandDim, fftDim))
        return emitOptionalError(location,
                                 "IRFFT requires non-final dimensions to be "
                                 "compatible with fft_length. Got: ",
                                 operandShape, " but wanted ", fftLength,
                                 ", and ", operandDim, " != ", fftDim, ".");
    }
    if ((!verifyCompatibleDims(operandShape[operandShape.size() - 1], 0) ||
         fftLength[fftRank - 1] != 0) &&
        !verifyCompatibleDims(operandShape[operandShape.size() - 1],
                              fftLength[fftRank - 1] / 2 + 1))
      return emitOptionalError(location,
                               "IRFFT requires innermost dimension to be "
                               "compatible with fft_length[-1]/2+1. Got: ",
                               operandShape[operandShape.size() - 1],
                               " but fft_length is ", fftLength, ".");
    resultShape[resultShape.size() - 1] = fftLength[fftRank - 1];
  }
  auto resultBounds = encodingToBounds(operandRankedType.getEncoding()).vec();
  if ((isFftTypeIrfft || isFftTypeRfft) && !resultBounds.empty())
    resultBounds.back() = ShapedType::kDynamic;
  inferredReturnShapes.emplace_back(
      resultShape, resultElementType,
      boundsToEncoding(operandRankedType.getEncoding(), resultBounds));
  return success();
}

LogicalResult inferGatherOp(
    std::optional<Location> location, Value operand, Value startIndices,
    ArrayRef<int64_t> offsetDims, ArrayRef<int64_t> collapsedSliceDims,
    ArrayRef<int64_t> startIndexMap, int64_t indexVectorDim,
    DenseIntElementsAttr sliceSizes,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  ShapeAdaptor operandShape(operand.getType());
  ShapeAdaptor startIndicesShape(startIndices.getType());

  // For some reason the getType call is necessary here
  if (failed(verifyGather(location,
                          /*operandShape=*/operandShape,
                          /*startIndicesShape=*/startIndicesShape,
                          /*sliceSizesShape=*/sliceSizes.getType(), offsetDims,
                          collapsedSliceDims, startIndexMap, indexVectorDim)))
    return failure();

  // gather_c8
  for (auto dim : collapsedSliceDims) {
    int64_t sliceDimSize = sliceSizes.getValues<int64_t>()[dim];
    if (sliceDimSize > 1)
      return emitOptionalError(location, "slice_sizes collapsed dimension ",
                               dim, " should <= 1 but got ", sliceDimSize);
  }

  // gather_c12
  if (operandShape.hasRank()) {
    for (const auto& it : llvm::enumerate(sliceSizes.getValues<int64_t>())) {
      if (operandShape.isDynamicDim(it.index())) continue;
      auto operandDimSize = operandShape.getDimSize(it.index());
      auto sliceDimSize = it.value();
      if (sliceDimSize < 0 || sliceDimSize > operandDimSize)
        return emitOptionalError(location, "slice size (", sliceDimSize,
                                 ") is out of bounds for operand dimension (",
                                 operandDimSize, ") at index ", it.index());
    }
  }

  auto getSliceDim = [&sliceSizes](int64_t index) -> int64_t {
    return sliceSizes.getValues<int64_t>()[index];
  };

  return inferGatherReturnTypeComponents(
      location, operandShape, startIndices, getSliceDim, offsetDims,
      collapsedSliceDims, startIndexMap, indexVectorDim, inferredReturnShapes);
}

LogicalResult inferGetDimensionSizeOp(
    std::optional<Location> location, Type operandType, int64_t dimension,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // get_dimension_size_c1
  if (failed(verifyDimInBounds(location, operandType.cast<ShapedType>(),
                               dimension)))
    return failure();
  inferredReturnShapes.emplace_back(
      ArrayRef<int64_t>{}, IntegerType::get(operandType.getContext(), 32));
  return success();
}

LogicalResult inferGetTupleElementOp(
    std::optional<Location> location, Value operand, int32_t index,
    SmallVectorImpl<Type>& inferredReturnTypes) {
  auto operandType = operand.getType().dyn_cast<TupleType>();
  if (!operandType) return failure();
  // get_tuple_element_c1
  if (index < 0 || index >= static_cast<int64_t>(operandType.size()))
    return emitOptionalError(location, "index ", index,
                             " is out of bounds of operand with size ",
                             operandType.size());

  // get_tuple_element_c2
  inferredReturnTypes.push_back(operandType.getType(index));
  return success();
}

LogicalResult inferImagOp(std::optional<Location> location, Value operand,
                          SmallVectorImpl<Type>& inferredReturnTypes) {
  // imag_c2
  inferredReturnTypes.push_back(
      createRealType(operand.getType().cast<ShapedType>()));
  return success();
}

LogicalResult inferIsFiniteOp(MLIRContext* context, std::optional<Location>,
                              Value x,
                              SmallVectorImpl<Type>& inferredReturnTypes) {
  auto argTy = x.getType().cast<ShapedType>();
  Builder b(context);
  inferredReturnTypes.push_back(getSameShapeTensorType(argTy, b.getI1Type()));
  return success();
}

LogicalResult inferIfOp(std::optional<Location> location, Value pred,
                        RegionRange branches,
                        SmallVectorImpl<Type>& inferredReturnTypes) {
  return inferConditionalOp(location, pred, branches, inferredReturnTypes);
}

LogicalResult inferMapOp(
    std::optional<Location> location, ValueRange inputs,
    DenseIntElementsAttr dimensions, Region& computation,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // map_i2
  if (dimensions.getType().getRank() != 1)
    return emitOptionalError(location,
                             "dimensions should be rank 1 but got rank ",
                             dimensions.getType().getRank());

  if (failed(verifyRegionNotEmpty(location, computation))) return failure();

  // map_c4
  auto& computationBlock = computation.front();
  auto computationArgs = computationBlock.getArguments();
  if (inputs.size() != computationArgs.size())
    return emitOptionalError(location,
                             "expects number of operands to match the arity of "
                             "map computation, but got: ",
                             inputs.size(), " and ", computationArgs.size());

  // map_c4
  for (const auto& indexedArg : llvm::enumerate(computationArgs)) {
    auto argType = indexedArg.value().getType().dyn_cast<RankedTensorType>();
    if (!argType || argType.getRank() != 0)
      return emitOptionalError(
          location,
          "computation arguments must be 0-rank tensor, but got: arg #",
          indexedArg.index(), " of type ", indexedArg.value().getType());
    auto operandElemTy = inputs[indexedArg.index()]
                             .getType()
                             .cast<ShapedType>()
                             .getElementType();
    if (argType.getElementType() != operandElemTy)
      return emitOptionalError(location,
                               "element type of operands and computation "
                               "arguments must match, but got: ",
                               operandElemTy, " and ",
                               argType.getElementType());
  }

  // map_c4
  auto computationOutputs = computationBlock.getTerminator()->getOperands();
  if (computationOutputs.size() != 1)
    return emitOptionalError(location,
                             "computation must return single output, but got: ",
                             computationOutputs.size());

  // map_c4
  auto computationOutputType =
      computationOutputs[0].getType().dyn_cast<RankedTensorType>();
  if (!computationOutputType || computationOutputType.getRank() != 0)
    return emitOptionalError(location,
                             "computation must return 0-rank tensor, but got: ",
                             computationOutputs[0].getType());

  // map_c3
  for (const auto& indexedValue :
       llvm::enumerate(dimensions.getValues<int64_t>())) {
    if (indexedValue.value() != static_cast<int64_t>(indexedValue.index()))
      return emitOptionalError(
          location,
          "requires monotonically increasing dimension numbers, but got: ",
          dimensions);
  }

  // map_c3
  ArrayRef<int64_t> resultShape;
  bool allInputsUnranked = true;
  for (auto operand : inputs) {
    auto operandType = operand.getType().cast<ShapedType>();
    if (operandType.hasRank()) {
      if (dimensions.size() !=
          static_cast<int64_t>(operandType.getShape().size()))
        return emitOptionalError(
            location,
            "applied to a subset of dimensions currently not supported: "
            "operand dimensions = ",
            operandType.getShape().size(),
            ", requested map dimensions size = ", dimensions.size());
      resultShape = operandType.getShape();
      allInputsUnranked = false;
    }
  }

  // map_c4
  if (allInputsUnranked)
    inferredReturnShapes.emplace_back(computationOutputType.getElementType());
  else
    inferredReturnShapes.emplace_back(resultShape,
                                      computationOutputType.getElementType());
  return success();
}

LogicalResult inferOptimizationBarrierOp(
    std::optional<Location> location, ValueRange operand,
    SmallVectorImpl<Type>& inferredReturnTypes) {
  // optimization_barrier_c1
  for (auto inputArgType : operand.getTypes())
    inferredReturnTypes.emplace_back(inputArgType);
  return success();
}

LogicalResult inferOutfeedOp(HloDialectInterface* dialect,
                             std::optional<Location> location,
                             SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(dialect->createTokenType());
  return success();
}

LogicalResult inferPadOp(std::optional<Location> location, Type operandType,
                         Type paddingValueType,
                         ArrayRef<int64_t> edgePaddingLow,
                         ArrayRef<int64_t> edgePaddingHigh,
                         ArrayRef<int64_t> interiorPadding,
                         SmallVectorImpl<Type>& inferredReturnTypes) {
  auto inputType = operandType.cast<RankedTensorType>();
  auto padType = paddingValueType.cast<RankedTensorType>();

  // pad_i2
  if (padType.getRank() != 0)
    return emitOptionalError(location,
                             "padding value type should be a rank-0 "
                             "tensor, is rank ",
                             padType.getRank());

  int64_t rank = inputType.getRank();
  // pad_c2
  if (static_cast<int64_t>(edgePaddingLow.size()) != rank)
    return emitOptionalError(location, "edge_padding_low length (",
                             edgePaddingLow.size(),
                             ") must match operand rank (", rank, ")");

  auto inputShape = inputType.getShape();
  SmallVector<int64_t> resultShape(rank, ShapedType::kDynamic);
  ArrayRef<int64_t> inputBounds = encodingToBounds(inputType.getEncoding());
  SmallVector<int64_t> resultBounds(inputBounds.size(), ShapedType::kDynamic);

  for (int i = 0, e = inputShape.size(); i < e; i++) {
    int64_t paddingLowVal = edgePaddingLow[i];
    int64_t paddingHighVal = edgePaddingHigh[i];
    int64_t paddingInteriorVal = interiorPadding[i];
    // pad_c3
    if (paddingInteriorVal < 0)
      return emitOptionalError(
          location,
          "Interior padding cannot be negative: ", paddingInteriorVal);

    bool isStaticDim = !isDynamicDimSize(inputShape[i]);
    bool isStaticBound =
        !inputBounds.empty() && !isDynamicDimSize(inputBounds[i]);
    if (isStaticDim || isStaticBound) {
      int64_t operandSizeOrBound = isStaticDim ? inputShape[i] : inputBounds[i];
      int64_t resultSizeOrBound =
          operandSizeOrBound + paddingLowVal + paddingHighVal +
          std::max<int64_t>(operandSizeOrBound - 1, 0ll) * paddingInteriorVal;

      // pad_c4
      if (resultSizeOrBound < 0) {
        auto sizeOrBound = isStaticDim ? "size" : "bound";
        return emitOptionalError(location, "Padding result in negative ",
                                 sizeOrBound, " for dimension ", i);
      }
      (isStaticDim ? resultShape : resultBounds)[i] = resultSizeOrBound;
    }
  }

  // pad_c1
  inferredReturnTypes.push_back(RankedTensorType::get(
      resultShape, inputType.getElementType(),
      boundsToEncoding(inputType.getEncoding(), resultBounds)));

  return success();
}

LogicalResult inferPartitionIdOp(MLIRContext* context, std::optional<Location>,
                                 SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(RankedTensorType::get(
      /*shape=*/{}, IntegerType::get(context, 32, IntegerType::Unsigned)));
  return success();
}

LogicalResult inferRealOp(std::optional<Location>, Value operand,
                          SmallVectorImpl<Type>& inferredReturnTypes) {
  // real_c2
  inferredReturnTypes.push_back(
      createRealType(operand.getType().cast<ShapedType>()));
  return success();
}

LogicalResult inferReduceOp(
    std::optional<Location> location, TypeRange inputTypes,
    TypeRange initValueTypes, DenseIntElementsAttr dimensions,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  SmallVector<ShapedType> inputArgTensorTypes{
      llvm::map_range(inputTypes, [](Type t) { return t.cast<ShapedType>(); })};
  SmallVector<ShapedType> initValueTensorTypes{llvm::map_range(
      initValueTypes, [](Type t) { return t.cast<ShapedType>(); })};

  SmallVector<int64_t> newDimensions;
  Attribute encoding;
  // reduce_c1, reduce_c4, reduce_c5, reduce_i3
  if (failed(verifyReduceOpInputsAndInferShape(location, inputArgTensorTypes,
                                               initValueTensorTypes, dimensions,
                                               newDimensions, encoding)))
    return failure();
  // reduce_c2, reduce_c3, reduce_c7
  for (uint64_t inputIdx = 0; inputIdx < inputTypes.size(); ++inputIdx) {
    ShapedType inputType = inputArgTensorTypes[inputIdx];
    Type elementType = inputType.getElementType();
    if (inputType.hasRank())
      inferredReturnShapes.emplace_back(newDimensions, elementType, encoding);
    else
      inferredReturnShapes.emplace_back(elementType);
  }

  return success();
}

LogicalResult inferReduceWindowOp(
    std::optional<Location> location, ValueRange inputs, ValueRange initValues,
    DenseIntElementsAttr windowDimensions,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> baseDilations,
    std::optional<DenseIntElementsAttr> windowDilations,
    std::optional<DenseIntElementsAttr> padding,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  SmallVector<ShapedType> inputTypes{llvm::map_range(
      inputs.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};
  SmallVector<ShapedType> initValueTypes{llvm::map_range(
      initValues.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};

  SmallVector<int64_t> windowDims;
  SmallVector<WindowDimension> inferredWindow;
  // reduce_window_c1, reduce_window_c2, reduce_window_c4...reduce_window_c12,
  // reduce_window_i4...reduce_window_i7
  if (failed(verifyReduceWindowOpInputsAndInferWindow(
          location, inputTypes, initValueTypes, windowDimensions, windowStrides,
          baseDilations, windowDilations, padding, windowDims, inferredWindow)))
    return failure();

  // reduce_window_c1, reduce_window_c14...reduce_window_c16
  for (size_t i = 0; i < inputTypes.size(); ++i) {
    auto inputRankedType = inputs[i].getType().dyn_cast<RankedTensorType>();
    if (!inputRankedType) {
      inferredReturnShapes.emplace_back(inputTypes[i].getElementType());
    } else {
      auto resultShape =
          inferWindowOutputShape(inputTypes[i].getShape(), inferredWindow);
      auto inputBounds = encodingToBounds(inputRankedType.getEncoding());
      if (inputBounds.empty()) {
        inferredReturnShapes.emplace_back(resultShape,
                                          inputTypes[i].getElementType());
      } else {
        auto resultBounds = inferWindowOutputShape(inputBounds, inferredWindow);
        inferredReturnShapes.emplace_back(
            resultShape, inputTypes[i].getElementType(),
            boundsToEncoding(inputRankedType.getEncoding(), resultBounds));
      }
    }
  }

  return success();
}

LogicalResult inferReplicaIdOp(MLIRContext* context, std::optional<Location>,
                               SmallVectorImpl<Type>& inferredReturnTypes) {
  inferredReturnTypes.push_back(RankedTensorType::get(
      /*shape=*/{}, IntegerType::get(context, 32, IntegerType::Unsigned)));
  return success();
}

LogicalResult inferReverseOp(
    std::optional<Location> location, Type operandType,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  return hlo::inferMostSpecificTypeComponents(location, operandType,
                                              inferredReturnShapes);
}

LogicalResult inferRngOp(
    std::optional<Location> location, Value a, Value b, Value shape,
    bool isRngDistributionUniform,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  if (!isRngDistributionUniform) {
    auto muTy = a.getType().cast<ShapedType>().getElementType();
    auto sigmaTy = b.getType().cast<ShapedType>().getElementType();
    if (!muTy.isa<FloatType>() || !sigmaTy.isa<FloatType>())
      return emitOptionalError(location, "mu and sigma must be floats");
  }

  SmallVector<int64_t> shapeVector;
  auto shapeOperandType = shape.getType().cast<ShapedType>();
  Type elementType = getElementTypeOrSelf(b);

  // Operand `shape` (1D by ODS) may be a constant or not, if `shape` is:
  // 1, not constant and have dynamic dim (tensor<?x>): infer tensor<*x>.
  // 2. not constant nor dynamic (e.g. tensor<3xi64>): infer tensor<?x?x?x>.
  // 3. constant (e.g. dense<[2, 3, 5]>): infer tensor<2x3x5x>.

  // Match to check whether the `shape` operand is a constant.
  DenseIntElementsAttr shapeAttr;
  if (!matchPattern(shape, m_Constant(&shapeAttr))) {
    int size = shapeOperandType.getDimSize(0);
    if (isDynamicDimSize(size)) {
      inferredReturnShapes.emplace_back(elementType);
      return success();
    }
    shapeVector.resize(size, ShapedType::kDynamic);
    inferredReturnShapes.emplace_back(shapeVector, elementType);
    return success();
  }

  // `shape` operand is a constant.
  shapeVector.reserve(shapeAttr.size());
  for (const APInt& fp : shapeAttr.getValues<APInt>())
    shapeVector.push_back(fp.getSExtValue());
  inferredReturnShapes.emplace_back(shapeVector, elementType);
  return success();
}

LogicalResult inferScatterOp(std::optional<Location>, ValueRange inputs,
                             SmallVectorImpl<Type>& inferredReturnTypes) {
  llvm::append_range(inferredReturnTypes, inputs.getTypes());
  return success();
}

LogicalResult inferSelectOp(
    std::optional<Location> location, Value pred, Value onTrue, Value onFalse,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto predType = pred.getType().cast<ShapedType>();
  auto trueType = onTrue.getType().cast<ShapedType>();
  auto falseType = onFalse.getType().cast<ShapedType>();

  // select_c2
  if (!compatibleShapeAndElementType(trueType, falseType))
    return emitOptionalError(
        location, "requires compatible types for non-predicate operands");

  // select_c1
  bool predCannotBeScalar = predType.hasRank() && predType.getRank() != 0;
  if (predCannotBeScalar)
    if (failed(verifyCompatibleShape(predType, trueType)))
      return emitOptionalError(location,
                               "requires the same shape for all operands");

  // select_c2
  SmallVector<Type> inferredReturnTypes;
  return inferMostSpecificTypeComponents(location, {trueType, falseType},
                                         inferredReturnShapes);
}

LogicalResult inferSelectAndScatterOp(
    Value operand, SmallVectorImpl<Type>& inferredReturnTypes) {
  // select_and_scatter_c11
  inferredReturnTypes.push_back(operand.getType());
  return success();
}

LogicalResult inferSendOp(HloDialectInterface* dialect,
                          std::optional<Location> location,
                          bool isDeviceToDevice, bool isDeviceToHost,
                          bool isHostTransfer,
                          SmallVectorImpl<Type>& inferredReturnTypes) {
  // send_c1_i4
  if (!isHostTransfer && !isDeviceToDevice)
    return emitOptionalError(location,
                             "channel_type should be DEVICE_TO_DEVICE when "
                             "is_host_transfer is false");

  // send_c1_i4
  if (isHostTransfer && !isDeviceToHost)
    return emitOptionalError(location,
                             "channel_type should be DEVICE_TO_HOST when "
                             "is_host_transfer is true");

  inferredReturnTypes.push_back(dialect->createTokenType());
  return success();
}

LogicalResult inferSetDimensionSizeOp(
    HloDialectInterface* dialect, std::optional<Location> location,
    Type operandType, Value size, int64_t dimension,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto sizeType = size.getType().dyn_cast<RankedTensorType>();
  if (sizeType && sizeType.getRank() != 0)
    return emitOptionalError(location, "size operand should be of rank-0");
  if (failed(verifyDimInBounds(location, operandType.cast<ShapedType>(),
                               dimension)))
    return failure();

  auto inputType = operandType.dyn_cast<RankedTensorType>();
  if (!inputType) {
    inferredReturnShapes.emplace_back(
        operandType.cast<ShapedType>().getElementType());
    return success();
  }
  int64_t rank = inputType.getRank();
  if (dimension < 0 || dimension >= rank)
    return emitOptionalError(location, "expects dimension to be in range [0, ",
                             rank, "); got: [", dimension, "].");

  auto shape = llvm::to_vector<4>(inputType.getShape());
  llvm::SmallVector<int64_t, 4> bounds(rank, ShapedType::kDynamic);
  ArrayRef<int64_t> inputBounds = encodingToBounds(inputType.getEncoding());
  if (!inputBounds.empty()) bounds = llvm::to_vector<4>(inputBounds);

  if (!hlo::isDynamicDimSize(shape[dimension]))
    bounds[dimension] = shape[dimension];
  shape[dimension] = ShapedType::kDynamic;

  DenseIntElementsAttr sizeAttr;
  if (matchPattern(size, m_Constant(&sizeAttr))) {
    int64_t splat =
        sizeAttr.getSplatValue<IntegerAttr>().getValue().getSExtValue();
    if (splat == bounds[dimension]) {
      shape[dimension] = splat;
      bounds[dimension] = ShapedType::kDynamic;
    }
  }

  if (llvm::all_of(bounds, [&](auto b) { return isDynamicDimSize(b); }))
    inferredReturnShapes.emplace_back(shape, inputType.getElementType());
  else
    inferredReturnShapes.emplace_back(shape, inputType.getElementType(),
                                      dialect->createTypeExtensions(bounds));
  return success();
}

LogicalResult inferSliceOp(std::optional<Location> location, Type operandType,
                           ArrayRef<int64_t> startIndices,
                           ArrayRef<int64_t> limitIndices,
                           ArrayRef<int64_t> strides,
                           SmallVectorImpl<Type>& inferredReturnTypes) {
  auto rankedTy = operandType.dyn_cast<RankedTensorType>();
  if (!rankedTy) {
    // The operand type is unranked, so the best we can infer for the result
    // type is an unranked tensor with the same element type as the operand
    // type.
    inferredReturnTypes.assign({operandType});
    return success();
  }

  // slice_c2
  int64_t rank = rankedTy.getRank();
  if (static_cast<int64_t>(startIndices.size()) != rank)
    return emitOptionalError(
        location, "the number of elements in start_indices (",
        startIndices.size(), ") does not match the rank of the operand (", rank,
        ")");

  ArrayRef<int64_t> inputBounds = encodingToBounds(rankedTy.getEncoding());
  SmallVector<int64_t> shape(rank, ShapedType::kDynamic);
  SmallVector<int64_t> resultBounds(inputBounds.size(), ShapedType::kDynamic);

  for (int64_t i = 0, e = rank; i != e; i++) {
    // slice_c3
    if (startIndices[i] < 0)
      return emitOptionalError(location, "negative start index ",
                               startIndices[i], " in dimension ", i);

    bool isStaticDim = !isDynamicDimSize(rankedTy.getDimSize(i));
    bool isStaticBound =
        !inputBounds.empty() && !isDynamicDimSize(inputBounds[i]);
    if (isStaticDim || isStaticBound) {
      int64_t operandSizeOrBound =
          isStaticDim ? rankedTy.getDimSize(i) : inputBounds[i];
      StringRef sizeOrBound = isStaticDim ? "size" : "bound";
      // slice_c3
      if (limitIndices[i] > operandSizeOrBound)
        return emitOptionalError(location, "limit index ", limitIndices[i],
                                 " is larger than dimension ", sizeOrBound, " ",
                                 operandSizeOrBound, " in dimension ", i);
    }

    // slice_c3
    if (startIndices[i] > limitIndices[i])
      return emitOptionalError(location, "start index ", startIndices[i],
                               " is larger than limit index ", limitIndices[i],
                               " in dimension ", i);
    // slice_c4
    if (strides[i] <= 0)
      return emitOptionalError(location, "stride must be positive but got ",
                               strides[i], " in dimension ", i);

    // slice_c5
    shape[i] = static_cast<int64_t>(
        llvm::divideCeil(limitIndices[i] - startIndices[i], strides[i]));
  }

  // slice_c1
  inferredReturnTypes.push_back(RankedTensorType::get(
      shape, rankedTy.getElementType(),
      boundsToEncoding(rankedTy.getEncoding(), resultBounds)));
  return success();
}

LogicalResult inferSortOp(
    std::optional<Location>, ValueRange inputs,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // sort_c2
  for (auto resultType : inputs.getTypes()) {
    auto rankedResult = resultType.dyn_cast<RankedTensorType>();
    if (rankedResult)
      inferredReturnShapes.emplace_back(rankedResult.getShape(),
                                        rankedResult.getElementType(),
                                        rankedResult.getEncoding());
    else
      inferredReturnShapes.emplace_back(resultType.cast<ShapedType>());
  }
  return success();
}

LogicalResult inferTopKOp(
    std::optional<Location> location, Value operand, int64_t k,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  Builder builder(operand.getContext());
  auto operandType = operand.getType().dyn_cast<RankedTensorType>();
  if (!operandType) {
    inferredReturnShapes.emplace_back(
        operand.getType().cast<ShapedType>().getElementType());
    inferredReturnShapes.emplace_back(builder.getI32Type());
    return success();
  }

  if (operandType.getRank() < 1)
    return emitOptionalError(location, "operand's rank must be at least 1");
  auto operandLastDim = operandType.getRank() - 1;
  if (!operandType.isDynamicDim(operandLastDim) &&
      operandType.getDimSize(operandLastDim) < k)
    return emitOptionalError(location,
                             "operand's last dimension must be at least ", k);

  SmallVector<int64_t> resultShape(operandType.getShape());
  resultShape[operandLastDim] = k;
  SmallVector<int64_t> resultBounds(
      encodingToBounds(operandType.getEncoding()));
  if (!resultBounds.empty())
    resultBounds[operandLastDim] = ShapedType::kDynamic;

  inferredReturnShapes.emplace_back(
      resultShape, operandType.getElementType(),
      hlo::boundsToEncoding(operandType.getEncoding(), resultBounds));
  inferredReturnShapes.emplace_back(
      resultShape, builder.getI32Type(),
      hlo::boundsToEncoding(operandType.getEncoding(), resultBounds));
  return success();
}

LogicalResult inferTransposeOp(std::optional<Location> loc, Value operand,
                               ArrayRef<int64_t> permutation,
                               SmallVectorImpl<Type>& inferredReturnTypes) {
  auto type = operand.getType();
  auto rankedTy = type.dyn_cast<RankedTensorType>();
  if (!rankedTy) {
    inferredReturnTypes.emplace_back(type);
    return success();
  }
  int64_t rank = rankedTy.getRank();
  if (static_cast<int64_t>(permutation.size()) != rank)
    return emitOptionalError(loc, "TransposeOp operand rank ", rank,
                             " does not match permutation size ",
                             permutation.size());

  std::vector<int64_t> range(rank);
  std::iota(range.begin(), range.end(), 0);
  if (!std::is_permutation(range.begin(), range.end(), permutation.begin()))
    return emitOptionalError(loc,
                             "attribute permutation must be a permutation"
                             " of [",
                             range, "] but got ", permutation);

  ArrayRef<int64_t> inputBounds = encodingToBounds(rankedTy.getEncoding());
  SmallVector<int64_t> resultShape;
  SmallVector<int64_t> resultBounds;
  ArrayRef<int64_t> inputShape = rankedTy.getShape();
  for (int64_t dim : permutation) {
    resultShape.push_back(inputShape[dim]);
    if (!inputBounds.empty()) resultBounds.push_back(inputBounds[dim]);
  }

  inferredReturnTypes.push_back(RankedTensorType::get(
      resultShape, rankedTy.getElementType(),
      boundsToEncoding(rankedTy.getEncoding(), resultBounds)));
  return success();
}

LogicalResult inferTriangularSolveOp(
    std::optional<Location> location, Value a, Value b, bool leftSide,
    bool isTransposeAInvalid,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  // ODS enforces that a and b are of same element type: float or complex.
  auto elementType = a.getType().cast<ShapedType>().getElementType();
  auto aType = a.getType().dyn_cast<RankedTensorType>();
  if (!aType) {
    inferredReturnShapes.emplace_back(elementType);
    return success();
  }

  auto aRank = aType.getRank();
  if (aRank < 2)
    return emitOptionalError(
        location, "operand 'a' must have rank >= 2, but got ", aType);

  if (!verifyCompatibleDims(aType.getDimSize(aRank - 2),
                            aType.getDimSize(aRank - 1)))
    return emitOptionalError(location,
                             "two minor dimensions of operand 'a' must ",
                             "be compatible, but got ", aType);

  auto bType = b.getType().dyn_cast<RankedTensorType>();
  if (!bType) {
    inferredReturnShapes.emplace_back(elementType);
    return success();
  }

  auto bRank = bType.getRank();
  if (aRank != bRank)
    return emitOptionalError(location,
                             "operands must have equal rank, but got ", aType,
                             " and ", bType);

  if (!verifyCompatibleDims(aType.getDimSize(aRank - 1),
                            bType.getDimSize(bRank - (leftSide ? 2 : 1))))
    return emitOptionalError(location,
                             "shared dimension of operands 'a' and 'b' must ",
                             "be compatible, but got ", aType, " and ", bType);

  auto aBatchDims = aType.getShape().drop_back(2);
  auto bBatchDims = bType.getShape().drop_back(2);
  if (failed(verifyCompatibleShape(aBatchDims, bBatchDims)))
    return emitOptionalError(location, "batch dimensions of the operands must ",
                             "be compatible, but got ", aType, " and ", bType);

  if (isTransposeAInvalid)
    return emitOptionalError(
        location, "Invalid transpose option value for triangular solve");

  inferredReturnShapes.emplace_back(bType.getShape(), bType.getElementType(),
                                    bType.getEncoding());
  return success();
}

LogicalResult inferTupleOp(MLIRContext* context, std::optional<Location>,
                           ValueRange val,
                           SmallVectorImpl<Type>& inferredReturnTypes) {
  // tuple_c1
  inferredReturnTypes.push_back(TupleType::get(context, val.getTypes()));
  return success();
}

LogicalResult inferUniformDequantizeOp(
    std::optional<Location> location, Value operand,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().cast<ShapedType>();
  // Trait HLO_QuantizedIntTensor in ODS guarantees QuantizedType;
  auto quantType = operandType.getElementType().cast<quant::QuantizedType>();
  auto shape = operandType.cast<ShapedType>().getShape();
  // uniform_dequantize_c1, uniform_dequantize_c2
  inferredReturnShapes.emplace_back(shape, quantType.getExpressedType());
  return success();
}

LogicalResult inferUniformQuantizeOp(
    std::optional<Location> location, Value operand,
    SmallVectorImpl<ShapedTypeComponents>& inferredReturnShapes) {
  auto operandType = operand.getType().cast<ShapedType>();
  // uniform_quantize_c1
  inferredReturnShapes.emplace_back(
      operandType.hasRank() ? operandType.getShape() : ArrayRef<int64_t>{});
  return success();
}

LogicalResult inferWhileOp(std::optional<Location>, ValueRange operand,
                           SmallVectorImpl<Type>& inferredReturnTypes) {
  // while_c3
  for (const auto& resultType : operand.getType())
    inferredReturnTypes.push_back(resultType);
  return success();
}

//===----------------------------------------------------------------------===//
// Verifiers for ops.
//===----------------------------------------------------------------------===//

LogicalResult verifyAllGatherOp(std::optional<Location> location, Value operand,
                                int64_t allGatherDim,
                                DenseIntElementsAttr replicaGroups,
                                int64_t channelId, bool useGlobalDeviceIds,
                                Value result) {
  auto operandType = operand.getType().dyn_cast<RankedTensorType>();
  auto resultType = result.getType().dyn_cast<RankedTensorType>();

  // all_gather_c1
  if (allGatherDim < 0)
    return emitOptionalError(location, "all_gather_dim cannot be negative");

  if (operandType) {
    // all_gather_c1
    if (allGatherDim >= operandType.getRank())
      return emitOptionalError(
          location, "all_gather_dim must be a valid index of operand");

    // TODO(#1745): Sync verification of AllGather with HLO.
    if (operandType.getDimSize(allGatherDim) == 0)
      return emitOptionalError(
          location,
          "dimension size of operand at 'all_gather_dim' cannot be zero");
  }

  // all_gather_i3, all_gather_c2, all_gather_c4
  if (failed(verifyReplicaGroups(location, replicaGroups,
                                 /*allGroupsMustHaveSameSize=*/true,
                                 useGlobalDeviceIds,
                                 /*expectedGroupSize=*/std::nullopt)))
    return failure();

  // all_gather_c5
  if (useGlobalDeviceIds && channelId < 0)
    return emitOptionalError(
        location,
        "channel_id cannot be negative when useGlobalDeviceIds is set");

  // all_gather_c6
  if (operandType && resultType) {
    if (resultType.getRank() != operandType.getRank())
      return emitOptionalError(location,
                               "operand and result must have the same rank");

    for (int64_t i = 0; i < operandType.getRank(); i++) {
      if (i == allGatherDim) continue;
      // all_gather_c6
      if (!verifyCompatibleDims(resultType.getDimSize(i),
                                operandType.getDimSize(i)))
        return emitOptionalError(
            location,
            "operand and result should have the same shape except for the "
            "dimension size at 'all_gather_dim'");
    }

    if (operandType.isDynamicDim(allGatherDim) ||
        resultType.isDynamicDim(allGatherDim))
      return success();

    // all_gather_c6
    if ((resultType.getDimSize(allGatherDim) %
         operandType.getDimSize(allGatherDim)) != 0)
      return emitOptionalError(
          location, "result gather dimension has size ",
          resultType.getDimSize(allGatherDim),
          ", expected to be a multiple of operand gather dimension size ",
          operandType.getDimSize(allGatherDim));
  }

  return success();
}

LogicalResult verifyAllReduceOp(std::optional<Location> location, Value operand,
                                DenseIntElementsAttr replicaGroups,
                                int64_t channelId, bool useGlobalDeviceIds,
                                Region& computation) {
  // TODO(#498): AllReduceOp does not have rank-2 replicaGroups.
  // all_reduce_c1...all_reduce_c3
  if (failed(verifyReplicaGroups(location, replicaGroups,
                                 /*allGroupsMustHaveSameSize=*/false,
                                 useGlobalDeviceIds,
                                 /*expectedGroupSize=*/std::nullopt)))
    return failure();

  // all_reduce_c4
  if (useGlobalDeviceIds && channelId <= 0)
    return emitOptionalError(
        location,
        "channel_id must be positive when useGlobalDeviceIds is set but got: ",
        channelId);

  auto operandType = operand.getType().cast<ShapedType>();
  // all_reduce_c5
  if (failed(verifyReducerShape(
          location, computation.front(), {operandType},
          {RankedTensorType::get({}, operandType.getElementType())},
          /*allowedDimensions=*/{})))
    return failure();

  return success();
}

LogicalResult verifyBitcastConvertOp(std::optional<Location> location,
                                     Value operand, Value result) {
  auto operandShapedType = operand.getType().cast<ShapedType>();
  auto targetShapedType = result.getType().cast<ShapedType>();

  // bitcast_convert_c2
  auto targetElt = targetShapedType.getElementType();
  auto operandElt = operandShapedType.getElementType();
  if (targetElt.isa<ComplexType>() != operandElt.isa<ComplexType>())
    return emitOptionalError(
        location, "cannot convert between real and complex types, but got: ",
        operandShapedType, " and ", targetShapedType);

  auto targetEltBitWidth = potentiallyComplexBitWidth(targetElt);
  auto operandEltBitWidth = potentiallyComplexBitWidth(operandElt);

  auto operandType = operandShapedType.dyn_cast<RankedTensorType>();
  auto targetType = targetShapedType.dyn_cast<RankedTensorType>();
  if (!operandType || !targetType) return success();

  auto targetShape = targetType.getShape();
  auto operandShape = operandType.getShape();
  ArrayRef<int64_t> smallerEltShape, biggerEltShape;
  if (operandEltBitWidth < targetEltBitWidth) {
    smallerEltShape = operandShape;
    biggerEltShape = targetShape;
  } else {
    smallerEltShape = targetShape;
    biggerEltShape = operandShape;
  }

  ArrayRef<int64_t> smallerEltPrefix;
  auto smallerEltBitWidth = std::min(targetEltBitWidth, operandEltBitWidth);
  auto biggerEltBitWidth = std::max(targetEltBitWidth, operandEltBitWidth);
  // bitcast_convert_c1
  if (operandEltBitWidth != targetEltBitWidth) {
    if (smallerEltShape.size() != biggerEltShape.size() + 1) {
      return emitOptionalError(
          location, "rank of smaller element type (", smallerEltShape.size(),
          ") should be 1 more than rank of larger element type (",
          biggerEltShape.size(), "), but ", smallerEltShape.size(),
          " != ", biggerEltShape.size(), " + 1.");
    }
    smallerEltPrefix = smallerEltShape.drop_back();
    if (!isDynamicDimSize(smallerEltShape.back()) &&
        smallerEltShape.back() * smallerEltBitWidth != biggerEltBitWidth) {
      return emitOptionalError(
          location, "requires compatible bit widths. ", "Got: ", operandType,
          " and ", targetType, ", but ", smallerEltBitWidth, " * ",
          smallerEltShape.back(), " != ", biggerEltBitWidth, ".");
    }
  } else {
    smallerEltPrefix = smallerEltShape;
  }

  for (auto it : llvm::zip(smallerEltPrefix, biggerEltShape)) {
    auto targetDim = std::get<0>(it);
    auto operandDim = std::get<1>(it);
    // bitcast_convert_c1
    if (!verifyCompatibleDims(targetDim, operandDim))
      return emitOptionalError(location,
                               "operand and result shapes must match except "
                               "for the innermost dimension of the shape with "
                               "the smaller element type. Got: ",
                               operandType, " and ", targetType, ".");
  }

  return success();
}

LogicalResult verifyBroadcastInDimOp(std::optional<Location> location,
                                     Value operand,
                                     ArrayRef<int64_t> broadcastDimensions,
                                     Value result) {
  auto operandType = operand.getType().dyn_cast<RankedTensorType>();
  if (!operandType) {
    // The following verification checks all depend on knowing the rank of
    // the operand. Bail out now if we don't know the rank of the operand.
    return success();
  }

  // broadcast_in_dim_c2
  auto dimensionsSize = broadcastDimensions.size();
  auto operandRank = operandType.getRank();
  if (static_cast<int64_t>(dimensionsSize) != operandRank)
    return emitOptionalError(location, "broadcast_dimensions size (",
                             dimensionsSize, ") does not match operand rank (",
                             operandRank, ")");

  auto dimensions = llvm::to_vector(broadcastDimensions);
  // broadcast_in_dim_c4
  if (hasDuplicates(dimensions))
    return emitOptionalError(location,
                             "broadcast_dimensions should not have duplicates");

  auto resultType = result.getType().cast<RankedTensorType>();
  auto resultRank = resultType.getRank();
  for (size_t i = 0; i != dimensionsSize; ++i) {
    auto dimIndex = dimensions[i];
    // broadcast_in_dim_c3
    if (dimIndex < 0 || dimIndex >= resultRank)
      return emitOptionalError(location,
                               "broadcast_dimensions contains invalid value ",
                               dimIndex, " for result with rank ", resultRank);

    if (!operandType.isDynamicDim(i)) {
      auto dimSize = operandType.getDimSize(i);
      auto resultDimSize = resultType.getDimSize(dimIndex);
      // broadcast_in_dim_c5
      if (dimSize != 1 && dimSize != resultDimSize)
        return emitOptionalError(
            location, "size of operand dimension ", i, " (", dimSize,
            ") is not equal to 1 or size of result dimension ", dimIndex, " (",
            resultDimSize, ")");
    }
  }

  return success();
}

LogicalResult verifyCollectiveBroadcastOp(std::optional<Location> location,
                                          DenseIntElementsAttr replicaGroups) {
  // collective_permute_i2
  auto replicaGroupType = replicaGroups.getType().cast<RankedTensorType>();
  if (replicaGroupType.getRank() != 2)
    return emitOptionalError(
        location, "replica groups should be a rank 2 tensor,",
        "but instead it is of rank ", replicaGroupType.getRank());

  auto replicaIds = replicaGroups.getValues<int64_t>();
  llvm::SmallSet<int64_t, 8> replicaIdsSeen;
  for (int64_t replicaId : replicaIds) {
    // collective_broadcast_c2
    // We only check that is is not negative, as it is impossible
    // to statically know `num_replicas` or `num_partitions`
    if (replicaId < 0)
      return emitOptionalError(
          location, "replica_groups values must be positive, but was given ",
          replicaId);

    // collective_broadcast_c1
    if (!replicaIdsSeen.insert(replicaId).second)
      return emitOptionalError(location, "replica id #", replicaId,
                               " seen more than once");
  }

  return success();
}

LogicalResult verifyCollectivePermuteOp(
    std::optional<Location> location, DenseIntElementsAttr sourceTargetPairs) {
  auto type = sourceTargetPairs.getType().dyn_cast<RankedTensorType>();
  // collective_permute_i2
  if (type.getRank() != 2)
    return emitOptionalError(location,
                             "expect source_target_pairs attribute to be of "
                             "rank 2, but got rank ",
                             type.getRank());

  // collective_permute_c1
  if (type.getShape()[1] != 2)
    return emitOptionalError(
        location,
        "expect source_target_pairs attribute of shape (N, 2), but got (",
        type.getShape(), ")");

  llvm::DenseSet<int64_t> sources;
  llvm::DenseSet<int64_t> targets;
  for (auto i = sourceTargetPairs.begin(), e = sourceTargetPairs.end(); i != e;
       ++i) {
    auto val = (*i).getSExtValue();
    // collective_permute_c4
    if (val < 0)
      return emitOptionalError(
          location, "replica ids in source_target_pairs must be >= 0.");

    if (i.getIndex() % 2 == 0) {
      bool isUnique = sources.insert(val).second;
      // collective_permute_c2
      if (!isUnique)
        return emitOptionalError(location, "duplicate sources not allowed.");
    } else {
      bool isUnique = targets.insert(val).second;
      // collective_permute_c3
      if (!isUnique)
        return emitOptionalError(location, "duplicate targets not allowed.");
    }
  }
  return success();
}

LogicalResult verifyConvolutionOp(
    std::optional<Location> location, Type lhsType, Type rhsType,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> padding,
    std::optional<DenseIntElementsAttr> lhsDilation,
    std::optional<DenseIntElementsAttr> rhsDilation,
    std::optional<DenseElementsAttr> windowReversal,
    int64_t inputBatchDimension, int64_t inputFeatureDimension,
    ArrayRef<int64_t> inputSpatialDimensions,
    int64_t kernelInputFeatureDimension, int64_t kernelOutputFeatureDimension,
    ArrayRef<int64_t> kernelSpatialDimensions, int64_t outputBatchDimension,
    int64_t outputFeatureDimension, ArrayRef<int64_t> outputSpatialDimensions,
    int64_t featureGroupCount, int64_t batchGroupCount,
    std::optional<ArrayAttr> precisionConfig, Type resultType) {
  SmallVector<ShapedTypeComponents> inferredReturnShapes;
  if (failed(inferConvolutionOp(
          location, lhsType, rhsType, windowStrides, padding, lhsDilation,
          rhsDilation, windowReversal, inputBatchDimension,
          inputFeatureDimension, inputSpatialDimensions,
          kernelInputFeatureDimension, kernelOutputFeatureDimension,
          kernelSpatialDimensions, outputBatchDimension, outputFeatureDimension,
          outputSpatialDimensions, featureGroupCount, batchGroupCount,
          precisionConfig, inferredReturnShapes)))
    return failure();

  auto inferredShape = inferredReturnShapes[0];
  auto shapedResultType = resultType.cast<ShapedType>();
  if (inferredShape.hasRank() && shapedResultType.hasRank() &&
      failed(verifyCompatibleShape(inferredShape.getDims(),
                                   shapedResultType.getShape())))
    return emitOptionalError(location, "inferred shape '",
                             dimSizesToString(inferredShape.getDims()), "' ",
                             "is incompatible with return type of operation ",
                             shapedResultType, "");

  return success();
}

LogicalResult verifyDotOp(std::optional<Location> location, Value lhs,
                          Value rhs, std::optional<ArrayAttr> precisionConfig,
                          Value result) {
  SmallVector<ShapedTypeComponents> inferredReturnShapes;
  if (failed(inferDotOp(location, lhs, rhs, precisionConfig,
                        inferredReturnShapes)))
    return failure();

  auto inferredShape = inferredReturnShapes[0];
  auto resultType = result.getType().cast<ShapedType>();
  if (inferredShape.hasRank() && resultType.hasRank() &&
      failed(verifyCompatibleShape(inferredShape.getDims(),
                                   resultType.getShape())))
    return emitOptionalError(
        location, "inferred shape '", dimSizesToString(inferredShape.getDims()),
        "' ", "is incompatible with return type of operation ", resultType, "");
  return success();
}

LogicalResult verifyDotGeneralOp(std::optional<Location> location, Value lhs,
                                 Value rhs,
                                 ArrayRef<int64_t> lhsBatchingDimensions,
                                 ArrayRef<int64_t> rhsBatchingDimensions,
                                 ArrayRef<int64_t> lhsContractingDimensions,
                                 ArrayRef<int64_t> rhsContractingDimensions,
                                 std::optional<ArrayAttr> precisionConfig,
                                 Value result) {
  SmallVector<ShapedTypeComponents> inferredReturnShapes;
  if (failed(inferDotGeneralOp(
          location, lhs.getType(), rhs.getType(), lhsBatchingDimensions,
          rhsBatchingDimensions, lhsContractingDimensions,
          rhsContractingDimensions, precisionConfig, inferredReturnShapes)))
    return failure();

  auto inferredShape = inferredReturnShapes[0];
  auto resultType = result.getType().cast<ShapedType>();
  if (inferredShape.hasRank() && resultType.hasRank() &&
      failed(verifyCompatibleShape(inferredShape.getDims(),
                                   resultType.getShape())))
    return emitOptionalError(
        location, "inferred shape '", dimSizesToString(inferredShape.getDims()),
        "' ", "is incompatible with return type of operation ", resultType, "");
  return success();
}

LogicalResult verifyDynamicBroadcastInDimOp(
    std::optional<Location> location, Value operand, Value outputDimensions,
    ArrayRef<int64_t> broadcastDimensions,
    std::optional<ArrayRef<int64_t>> knownExpandingDimensions,
    std::optional<ArrayRef<int64_t>> knownNonexpandingDimensions,
    Value result) {
  auto operandType = operand.getType().dyn_cast<RankedTensorType>();
  auto resultType = result.getType().dyn_cast<RankedTensorType>();

  // If either the operand or result are unranked, there is very little
  // to verify statically.
  if (!operandType || !resultType) return success();

  auto outputDimensionsType =
      outputDimensions.getType().cast<RankedTensorType>();
  auto outputDimensionsSize = outputDimensionsType.getDimSize(0);
  auto operandRank = operandType.getRank();
  auto resultRank = resultType.getRank();

  // Verify broadcast_dimensions.
  auto bcastDimensions = broadcastDimensions;
  int64_t bcastDimensionsSize = bcastDimensions.size();
  if (bcastDimensionsSize != operandRank)
    return emitOptionalError(
        location, "broadcast_dimensions size (", bcastDimensionsSize,
        ") does not match operand rank (", operandRank, ")");

  if (resultRank < operandRank)
    return emitOptionalError(location, "result rank (", resultRank,
                             ") is less than operand rank (", operandRank, ")");

  for (int i = 0; i != bcastDimensionsSize; ++i) {
    auto dimIndex = bcastDimensions[i];
    if (dimIndex < 0 || dimIndex >= resultRank)
      return emitOptionalError(location,
                               "broadcast_dimensions contains invalid value ",
                               dimIndex, " for result with rank ", resultRank);

    auto dimSize = operandType.getDimSize(i);
    auto resultDimSize = resultType.getDimSize(dimIndex);
    // Note: verifyCompatibleShapes doesn't consider size-1 broadcasting, so
    // we add a manual check for this.
    if (dimSize != 1 && failed(verifyCompatibleShape(dimSize, resultDimSize)))
      return emitOptionalError(location, "size of operand dimension ", i, " (",
                               dimSize,
                               ") is not compatible "
                               "with size of result dimension ",
                               dimIndex, " (", resultDimSize, ")");
  }

  if (outputDimensionsSize != resultRank)
    return emitOptionalError(location, "result rank (", resultRank,
                             ") is not equal to number of output dimensions (",
                             outputDimensionsSize, ")");

  // Verify that the known expanding and non-expanding dimensions are a subset
  // of the operand's dimensions.
  int64_t numKnownExpansionBehavior = 0;
  DenseSet<int64_t> knownExpansionBehavior;
  auto collectExpansionBehaviorDims =
      [&](const std::optional<ArrayRef<int64_t>>& attr) {
        if (!attr) return;
        for (const auto& i : attr.value()) {
          numKnownExpansionBehavior++;
          knownExpansionBehavior.insert(i);
        }
      };
  collectExpansionBehaviorDims(knownExpandingDimensions);
  collectExpansionBehaviorDims(knownNonexpandingDimensions);
  if (knownExpansionBehavior.size() != numKnownExpansionBehavior)
    return emitOptionalError(
        location,
        "duplicate expansion hint for at least one operand dimension");
  for (int64_t i : knownExpansionBehavior)
    if (i < 0 || i >= operandRank)
      return emitOptionalError(location, "hint for expanding dimension ", i,
                               " does not refer to a "
                               "valid operand dimension");

  if (!isCompatibleForHloTypeInference(outputDimensions, resultType))
    return emitOptionalError(
        location,
        "output_dimensions are incompatible with return type of operation ",
        resultType);

  return success();
}

LogicalResult verifyDynamicIotaOp(std::optional<Location> location,
                                  Value outputShape, int64_t iotaDimension,
                                  Value result) {
  auto shape = result.getType().cast<ShapedType>();
  if (!isCompatibleForHloTypeInference(outputShape, shape))
    return emitOptionalError(
        location, "output_shape is incompatible with return type of operation ",
        result.getType());

  if (!shape.hasRank()) return success();

  if (iotaDimension >= shape.getRank() || iotaDimension < 0)
    return emitOptionalError(
        location,
        "iota dimension cannot go beyond the output rank or be negative.");

  return success();
}

LogicalResult verifyDynamicPadOp(std::optional<Location> location,
                                 Value operand, Value paddingValue,
                                 Value edgePaddingLow, Value edgePaddingHigh,
                                 Value interiorPadding, Value result) {
  auto inputType = operand.getType().dyn_cast<RankedTensorType>();
  // If operand is unranked, there is very little to verify statically.
  if (!inputType) return success();
  int inputRank = inputType.getRank();

  auto padType = paddingValue.getType().cast<RankedTensorType>();
  if (padType.getRank() != 0)
    return emitOptionalError(location, "padding value type should be a rank-0");

  auto paddingLowType = edgePaddingLow.getType().cast<RankedTensorType>();
  if (paddingLowType.getNumElements() != inputRank)
    return emitOptionalError(location, "edge_padding_low length(",
                             paddingLowType.getNumElements(),
                             ") must match operand rank(", inputRank, ").");

  auto paddingHighType = edgePaddingHigh.getType().cast<RankedTensorType>();
  if (paddingHighType.getNumElements() != inputRank)
    return emitOptionalError(location, "edge_padding_high length(",
                             paddingHighType.getNumElements(),
                             ") must match operand rank(", inputRank, ").");

  auto interiorPaddingType = interiorPadding.getType().cast<RankedTensorType>();
  if (interiorPaddingType.getNumElements() != inputRank)
    return emitOptionalError(location, "edge_padding_interior length(",
                             interiorPaddingType.getNumElements(),
                             ") must match operand rank(", inputRank, ").");

  auto outputType = result.getType().dyn_cast<RankedTensorType>();
  // If result is unranked, there is very little to verify statically.
  if (!outputType) return success();
  int outputRank = outputType.getRank();
  if (inputRank != outputRank)
    return emitOptionalError(location, "operand rank(", inputRank,
                             ") must match result(", outputRank, ").");

  return success();
}

LogicalResult verifyDynamicReshapeOp(std::optional<Location> location,
                                     Value outputShape, Value result) {
  auto resultType = result.getType().cast<ShapedType>();
  auto outputShapeType = outputShape.getType().cast<ShapedType>();
  if (resultType.hasRank() && outputShapeType.hasStaticShape() &&
      outputShapeType.getDimSize(0) != resultType.getRank())
    return emitOptionalError(location,
                             "output should have a rank equal to the number of "
                             "elements in output_shape");
  if (!isCompatibleForHloTypeInference(outputShape, resultType))
    return emitOptionalError(
        location, "output_shape is incompatible with return type of operation ",
        resultType);
  return success();
}

LogicalResult verifyInfeedOp(HloDialectInterface* dialect,
                             std::optional<Location> location,
                             std::optional<ArrayAttr> layout,
                             ValueRange results) {
  auto resultTypes = results.getType();
  // infeed_c1
  if (resultTypes.empty())
    return emitOptionalError(
        location, "result is expected to be at least of size 1, but got ",
        resultTypes.size());

  // infeed_c2
  for (auto resultType : results.drop_back().getTypes())
    if (!resultType.isa<TensorType>())
      return emitOptionalError(
          location,
          "all elements of result types, except the last element, are expected "
          "to be of tensor type, but got ",
          resultType);

  // infeed_c3
  if (!dialect->isTokenType(results.back().getType()))
    return emitOptionalError(location,
                             "last element of result types is expected to "
                             "be of token type, but got ",
                             results.back().getType());

  if (!layout.has_value()) return success();
  if (!layout.value())
    return emitOptionalError(location,
                             "layout-attribute expected to be of array-type.");

  if (layout.value().size() != resultTypes.size() - 1)
    return emitOptionalError(location, "layout-attribute size must be ",
                             resultTypes.size() - 1,
                             " (which is the number of "
                             "op-results - 1 (for token result)), but got ",
                             layout.value().size());

  for (auto childLayout : layout.value()) {
    mlir::ArrayAttr childLayoutArr = childLayout.dyn_cast<mlir::ArrayAttr>();
    if (!childLayoutArr)
      return emitOptionalError(location,
                               "layout-attribute expected to have "
                               "elements of type array, but got ",
                               childLayout);

    for (auto i : childLayoutArr) {
      mlir::IntegerAttr attr = i.dyn_cast<mlir::IntegerAttr>();
      if (!attr)
        return emitOptionalError(location,
                                 "layout-attribute's leaf elements are "
                                 "expected to be of type integer, but got ",
                                 i);
    }
  }

  return success();
}

LogicalResult verifyIotaOp(std::optional<Location> location,
                           int64_t iotaDimension, Value result) {
  auto shape = result.getType().cast<ShapedType>();
  if (!shape.hasRank()) return success();
  if (shape.getRank() == 0)
    return emitOptionalError(location, "does not support scalars.");

  if (iotaDimension >= shape.getRank() || iotaDimension < 0)
    return emitOptionalError(
        location,
        "iota dimension cannot go beyond the output rank or be negative.");
  return success();
}

// Verifies that operand rank matches start_indices/limit_indices/strides size
LogicalResult verifyRealDynamicSliceOp(std::optional<Location> location,
                                       Value operand, Value startIndices,
                                       Value limitIndices, Value strides) {
  auto inputType = operand.getType().dyn_cast<RankedTensorType>();
  // If operand is unranked, there is very little to verify statically.
  if (!inputType) return success();
  int inputRank = inputType.getRank();

  auto startType = startIndices.getType().cast<RankedTensorType>();
  auto limitType = limitIndices.getType().cast<RankedTensorType>();
  auto stridesType = strides.getType().cast<RankedTensorType>();

  if (inputRank != startType.getNumElements())
    return emitOptionalError(
        location, "has mismatched number of operand rank (", inputRank,
        ") and start_indices size (", startType.getNumElements(), ")");

  if (inputRank != limitType.getNumElements())
    return emitOptionalError(
        location, "has mismatched number of operand rank (", inputRank,
        ") and limit_indices size (", limitType.getNumElements(), ")");

  if (inputRank != stridesType.getNumElements())
    return emitOptionalError(
        location, "has mismatched number of operand rank (", inputRank,
        ") and strides size (", stridesType.getNumElements(), ")");
  return success();
}

LogicalResult verifyRecvOp(HloDialectInterface* dialect,
                           std::optional<Location> location,
                           bool isDeviceToDevice, bool isHostToDevice,
                           bool isHostTransfer, ValueRange results) {
  // recv_c1_i3
  if (!isHostTransfer && !isDeviceToDevice)
    return emitOptionalError(location,
                             "channel_type should be DEVICE_TO_DEVICE when "
                             "is_host_transfer is false");

  // recv_c1_i3
  if (isHostTransfer && !isHostToDevice)
    return emitOptionalError(location,
                             "channel_type should be HOST_TO_DEVICE when "
                             "is_host_transfer is true");

  // recv_c2
  if (results.empty())
    return emitOptionalError(
        location, "result is expected to be at least of size 1, but got ",
        results.size());

  // recv_c3
  for (auto resultType : results.drop_back().getTypes())
    if (!resultType.isa<TensorType>())
      return emitOptionalError(
          location,
          "everything but the last element of result types is expected to be "
          "of tensor type, but got ",
          resultType);

  // recv_c4
  if (!dialect->isTokenType(results.back().getType()))
    return emitOptionalError(location,
                             "last element of result types is expected to "
                             "be of token type, but got ",
                             results.back().getType());

  return success();
}

LogicalResult verifyReduceOp(std::optional<Location> location,
                             ValueRange inputs, ValueRange initValues,
                             DenseIntElementsAttr dimensions, Region& body) {
  SmallVector<ShapedType> inputTypes{llvm::map_range(
      inputs.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};
  SmallVector<ShapedType> initValueTypes{llvm::map_range(
      initValues.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};

  SmallVector<int64_t> newDimensions;
  Attribute encoding;
  // reduce_c1, reduce_c4, reduce_c5, reduce_i3
  if (failed(verifyReduceOpInputsAndInferShape(location, inputTypes,
                                               initValueTypes, dimensions,
                                               newDimensions, encoding)))
    return failure();

  // reduce_c2, reduce_c6
  if (failed(verifyReducerShape(location, body.front(), inputTypes,
                                initValueTypes, newDimensions)))
    return failure();
  return success();
}

LogicalResult verifyReducePrecisionOp(std::optional<Location> location,
                                      int32_t exponentBits,
                                      int32_t mantissaBits) {
  // reduce_precision_c2
  if (exponentBits < 1)
    return emitOptionalError(location, "exponent_bits must be at least 1.");
  // reduce_precision_c3
  if (mantissaBits < 0)
    return emitOptionalError(location, "mantissa_bits must be at least 0.");
  return success();
}

LogicalResult verifyReduceScatterOp(std::optional<Location> location,
                                    Value operand, int64_t scatterDimension,
                                    DenseIntElementsAttr replicaGroups,
                                    int64_t channelId, bool useGlobalDeviceIds,
                                    Region& computation, Value result) {
  if (failed(verifyReplicaGroups(location, replicaGroups,
                                 /*allGroupsMustHaveSameSize=*/true,
                                 useGlobalDeviceIds,
                                 /*expectedGroupSize=*/std::nullopt)))
    return failure();
  auto operandType = operand.getType().cast<ShapedType>();
  // reduce_scatter_c7
  if (failed(verifyReducerShape(
          location, computation.front(), {operandType},
          {RankedTensorType::get({}, operandType.getElementType())},
          /*allowedDimensions=*/{})))
    return failure();

  auto resultType = result.getType().cast<ShapedType>();
  if (!operandType.hasRank() || !resultType.hasRank()) return success();
  // reduce_scatter_c8
  if (operandType.getRank() != resultType.getRank())
    return emitOptionalError(location,
                             "operand and result should have same rank");

  // reduce_scatter_c2
  if (scatterDimension < 0)
    return emitOptionalError(location, "expects scatter_dimension >= 0");

  // reduce_scatter_c2
  if (scatterDimension >= operandType.getRank())
    return emitOptionalError(
        location, "scatter dim should be less than operand/result rank");

  // reduce_scatter_c6
  if (useGlobalDeviceIds && channelId <= 0)
    return emitOptionalError(
        location,
        "channel_id must be positive when useGlobalDeviceIds is set but got: ",
        channelId);

  if (operandType.isDynamicDim(scatterDimension) ||
      resultType.isDynamicDim(scatterDimension))
    return success();

  auto operandScatterDimSize = operandType.getDimSize(scatterDimension);
  auto resultScatterDimSize = resultType.getDimSize(scatterDimension);
  // TODO(#1746): Sync verification of ReduceScatter with HLO.
  if (resultScatterDimSize == 0)
    return emitOptionalError(
        location, "result dimension size at scatter_dimension cannot be zero");

  // TODO(#1746): Sync verification of ReduceScatter with HLO.
  if (operandScatterDimSize == 0)
    return emitOptionalError(
        location, "operand dimension size at scatter_dimension cannot be zero");

  // reduce_scatter_c8
  if (isStaticDimSize(operandScatterDimSize) &&
      isStaticDimSize(resultScatterDimSize) &&
      operandScatterDimSize % resultScatterDimSize != 0)
    return emitOptionalError(
        location, "operand scatter dimension has size ", operandScatterDimSize,
        ", expected to be a multiple of result scatter dimension size ",
        resultScatterDimSize);

  // reduce_scatter_c8
  for (auto index : llvm::seq<int64_t>(0, operandType.getRank())) {
    if (index == scatterDimension) continue;
    if (!verifyCompatibleDims(operandType.getDimSize(index),
                              resultType.getDimSize(index)))
      return emitOptionalError(
          location, "non scatter dimensions should be same for operand (",
          operandType.getDimSize(index), ") and result (",
          resultType.getDimSize(index), ")");
  }
  return success();
}

LogicalResult verifyReduceWindowOp(
    std::optional<Location> location, ValueRange inputs, ValueRange initValues,
    DenseIntElementsAttr windowDimensions,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> baseDilations,
    std::optional<DenseIntElementsAttr> windowDilations,
    std::optional<DenseIntElementsAttr> padding, Region& body) {
  SmallVector<ShapedType> inputTypes{llvm::map_range(
      inputs.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};
  SmallVector<ShapedType> initValueTypes{llvm::map_range(
      initValues.getTypes(), [](Type t) { return t.cast<ShapedType>(); })};

  SmallVector<int64_t> windowDims;
  SmallVector<WindowDimension> inferredWindow;
  // reduce_window_c1, reduce_window_c2, reduce_window_c4...reduce_window_c12,
  // reduce_window_i4...reduce_window_i7
  if (failed(verifyReduceWindowOpInputsAndInferWindow(
          location, inputTypes, initValueTypes, windowDimensions, windowStrides,
          baseDilations, windowDilations, padding, windowDims, inferredWindow)))
    return failure();

  // reduce_window_c3, reduce_window_c13, reduce_window_i2
  if (failed(verifyReducerShape(location, body.front(), inputTypes,
                                initValueTypes, windowDims)))
    return failure();

  return success();
}

LogicalResult verifyReshapeOp(std::optional<Location> location, Value operand,
                              Value result) {
  // If the operand type is dynamically shaped there is nothing to verify.
  auto operandTy = operand.getType().dyn_cast<RankedTensorType>();
  if (!operandTy || !operandTy.hasStaticShape()) return success();

  // If the operand type is statically shaped (not required) the number of
  // elements must match that of the result type.
  auto resultTy = result.getType().cast<RankedTensorType>();
  assert(resultTy && resultTy.hasStaticShape() &&
         "result type must be statically shaped");
  int64_t numResultElements = resultTy.getNumElements();
  int64_t numOperandElements = operandTy.getNumElements();
  if (numResultElements != numOperandElements)
    return emitOptionalError(location, "number of output elements (",
                             numResultElements,
                             ") doesn't match expected number of elements (",
                             numOperandElements, ")");

  return success();
}

LogicalResult verifyReverseOp(std::optional<Location> location, Value operand,
                              ArrayRef<int64_t> dimensions) {
  llvm::SmallDenseSet<int64_t> uniqueDims(dimensions.begin(), dimensions.end());
  // reverse_c2
  if (uniqueDims.size() != dimensions.size())
    return emitOptionalError(location,
                             "dimensions should be unique. Got: ", dimensions);
  auto operandTy = operand.getType().dyn_cast<RankedTensorType>();
  for (int64_t dim : dimensions) {
    // reverse_c3
    if (dim < 0)
      return emitOptionalError(
          location,
          "all dimensions should be non-negative. Got dimension: ", dim, ".");
    if (operandTy && dim >= operandTy.getRank())
      return emitOptionalError(
          location, "all dimensions should be between [0, ",
          operandTy.getRank(), "). Got dimension: ", dim, ".");
  }
  return success();
}

LogicalResult verifyRngBitGeneratorOp(std::optional<Location> location,
                                      Value initialState, Value outputState) {
  auto initialShape = initialState.getType().dyn_cast<RankedTensorType>();
  auto outputShape = outputState.getType().dyn_cast<RankedTensorType>();
  if (failed(verifyCompatibleShape(initialShape.getShape(),
                                   outputShape.getShape())))
    return emitOptionalError(
        location,
        "output state shape must be compatible with initial state shape. Got: ",
        initialShape, " and ", outputShape);
  return success();
}

LogicalResult verifyScatterOp(std::optional<Location> location,
                              ValueRange inputs, Value scatterIndices,
                              ValueRange updates,
                              ArrayRef<int64_t> updateWindowDims,
                              ArrayRef<int64_t> insertedWindowDims,
                              ArrayRef<int64_t> scatterDimsToOperandDims,
                              int64_t indexVectorDim,
                              Region& updateComputation) {
  // Get the first operand and update, since variadic Scatter is not yet
  // implemented
  auto numOperands = inputs.size();
  auto scatterIndicesType = scatterIndices.getType().cast<ShapedType>();

  SmallVector<ShapedType, 1> operandTypes = llvm::to_vector(llvm::map_range(
      inputs.getTypes(), [](Type type) { return type.cast<ShapedType>(); }));
  SmallVector<ShapedType, 1> updatesTypes = llvm::to_vector(llvm::map_range(
      updates.getTypes(), [](Type type) { return type.cast<ShapedType>(); }));
  bool scatterIndicesTypeRanked = scatterIndicesType.isa<RankedTensorType>();

  // scatter_c1
  for (auto operandType : operandTypes)
    if (failed(verifyCompatibleShape(operandTypes[0].getShape(),
                                     operandType.getShape())))
      return emitOptionalError(location,
                               "Not all inputs have compatible shapes.");

  // scatter_c3
  for (auto updateType : updatesTypes)
    if (failed(verifyCompatibleShape(updatesTypes[0].getShape(),
                                     updateType.getShape())))
      return emitOptionalError(location,
                               "Not all updates have compatible shapes.");

  // scatter_c14
  if (scatterIndicesTypeRanked) {
    if (indexVectorDim > scatterIndicesType.getRank() || indexVectorDim < 0)
      return emitOptionalError(
          location,
          "expects scatter index leaf dimension to be within [0, "
          "rank(scatter_indices) + 1. rank(scatter_indices) is ",
          scatterIndicesType.getRank(), " and scatter index leaf dimension is ",
          indexVectorDim, ".");
  }

  SmallVector<ShapedType> inputTypes, initValueTypes;
  for (int64_t i = 0; i < static_cast<int64_t>(numOperands); i++) {
    inputTypes.push_back(operandTypes[i]);
    initValueTypes.push_back(
        RankedTensorType::get({}, updatesTypes[i].getElementType()));
  }
  // scatter_c6, scatter_c15
  if (failed(verifyReducerShape(location, updateComputation.front(), inputTypes,
                                initValueTypes,
                                /*allowedDimensions=*/{})))
    return failure();

  // rank-of('updates[i]') == size-of('update_window_dims') +
  // rank-of('scatter_indices') - 1, where 'scatter_indices' is expanded by a
  // trailing 1 dimension if 'index_vector_dim' == rank-of('scatter_indices')
  // for all values of `i`.
  SmallVector<int64_t> expandedScatterIndicesShape;
  if (scatterIndicesTypeRanked) {
    expandedScatterIndicesShape =
        llvm::to_vector(scatterIndicesType.getShape());
    if (static_cast<int64_t>(expandedScatterIndicesShape.size()) ==
        indexVectorDim)
      expandedScatterIndicesShape.push_back(1);
  }

  // scatter_c4
  for (int64_t i = 0; i < static_cast<int64_t>(numOperands); i++) {
    if (scatterIndicesTypeRanked && updatesTypes[i].isa<RankedTensorType>()) {
      int64_t expectedUpdatesRank =
          expandedScatterIndicesShape.size() - 1 + updateWindowDims.size();
      if (updatesTypes[i].getRank() != expectedUpdatesRank)
        return emitOptionalError(
            location, "expects updates tensor must be of rank ",
            expectedUpdatesRank,
            " ( == rank-of('scatter_indices') - 1 + "
            "size-of('update_window_dims'), where 'scatter_indices' is "
            "expanded by a trailing 1 dimension if 'index_vector_dim' == "
            "rank-of('scatter_indices')), but got ",
            updatesTypes[i].getRank(), ".");
    }
  }

  // scatter_c2, scatter_c7...scatter_c13
  for (int64_t i = 0; i < static_cast<int64_t>(numOperands); i++) {
    if (failed(validateScatterDimensionNumbers(
            operandTypes[i], expandedScatterIndicesShape, updatesTypes[i],
            operandTypes[i].isa<RankedTensorType>(), scatterIndicesTypeRanked,
            updatesTypes[i].isa<RankedTensorType>(), updateWindowDims,
            insertedWindowDims, scatterDimsToOperandDims, indexVectorDim,
            location)))
      return failure();
  }

  for (int64_t i = 0; i < static_cast<int64_t>(numOperands); i++) {
    if (updatesTypes[i].isa<RankedTensorType>()) {
      auto updatesShape = updatesTypes[i].getShape();
      if (operandTypes[i].isa<RankedTensorType>()) {
        auto operandShape = operandTypes[i].getShape();

        int64_t insertedDimsSeen = 0;
        SmallVector<int64_t> maxUpdateSliceSizes;
        const auto dimensionsSize = operandTypes[i].getRank();
        maxUpdateSliceSizes.reserve(dimensionsSize);
        for (int i = 0; i < dimensionsSize; ++i) {
          if (insertedDimsSeen <
                  static_cast<int64_t>(insertedWindowDims.size()) &&
              insertedWindowDims[insertedDimsSeen] == i)
            ++insertedDimsSeen;
          else
            maxUpdateSliceSizes.push_back(operandShape[i]);
        }

        for (int64_t i = 0; i < static_cast<int64_t>(updateWindowDims.size());
             ++i) {
          auto updateWindowDim = updateWindowDims[i];

          if (isDynamicDimSize(updatesShape[updateWindowDim]) ||
              isDynamicDimSize(maxUpdateSliceSizes[i]))
            continue;

          // scatter_c4
          if (updatesShape[updateWindowDim] > maxUpdateSliceSizes[i]) {
            return emitOptionalError(
                location,
                "expects bounds of the window dimensions of updates to not "
                "exceed the bounds of the corresponding dimensions of operand. "
                "For dimension ",
                updateWindowDim, ", updates bound is ",
                updatesShape[updateWindowDim], ", operand bound is ",
                maxUpdateSliceSizes[i], ".");
          }
        }
      }

      if (scatterIndicesTypeRanked) {
        int64_t scatterDimsSeen = 0;
        for (int64_t i = 0; i < static_cast<int64_t>(updatesShape.size());
             ++i) {
          bool isUpdateWindowDim = std::binary_search(
              updateWindowDims.begin(), updateWindowDims.end(), i);

          if (isUpdateWindowDim) continue;
          if (scatterDimsSeen == indexVectorDim) ++scatterDimsSeen;

          // scatter_c4
          if (!verifyCompatibleDims(
                  updatesShape[i],
                  expandedScatterIndicesShape[scatterDimsSeen]))
            return emitOptionalError(
                location,
                "expects bounds of the scatter dimensions of updates to be "
                "same as the bounds of the corresponding dimensions of scatter "
                "indices. For scatter dimension ",
                i, ", updates bound is ", updatesShape[i],
                " , scatter_indices bound is ",
                expandedScatterIndicesShape[scatterDimsSeen], ".");

          ++scatterDimsSeen;
        }
      }
    }
  }

  return success();
}

//  We intend to verify the following properties:
//   P1. Check if the select function has a proper shape of (T,T) -> PRED, where
//        T is a 0-D tensor with element-type same as 'operand' element-type.
//   P2. Verify scatter-computation type.
//   P3. size-of(window_dimension) == rank-of(input),
//         where input is an element of 'inputs'.
//   P4. Verify and collect the window attributes.
//   P5. Check if the result type of window operation matches the source type.
LogicalResult verifySelectAndScatterOp(
    std::optional<Location> location, Value operand, Value source,
    Value initValue, std::optional<DenseIntElementsAttr> windowDimensions,
    std::optional<DenseIntElementsAttr> windowStrides,
    std::optional<DenseIntElementsAttr> padding, Region& select,
    Region& scatter) {
  auto operandType = operand.getType().cast<ShapedType>();
  auto initValueType = initValue.getType().cast<ShapedType>();
  auto sourceType = source.getType().cast<ShapedType>();

  Block& selectBlock = select.front();
  // select_and_scatter_c9
  if (selectBlock.getArguments().size() != 2)
    return emitOptionalError(
        location, "expects the select-region to take 2 parameters, but takes ",
        selectBlock.getArguments().size());

  Type expectedSelectArgType =
      RankedTensorType::get({}, operandType.getElementType());
  for (const auto& selectArgIt : llvm::enumerate(selectBlock.getArguments()))
    // select_and_scatter_c9
    if (!compatibleShapeAndElementType(expectedSelectArgType,
                                       selectArgIt.value().getType(),
                                       /*ignoreFpPrecision=*/true))
      return emitOptionalError(
          location, "expects the type of select-region's parameter at index ",
          selectArgIt.index(), " to be ", expectedSelectArgType, ", but got ",
          selectArgIt.value().getType());

  auto selectResult = selectBlock.getTerminator()->getOperands();
  // select_and_scatter_c9
  if (selectResult.size() != 1)
    return emitOptionalError(
        location, "expects select-region to return single value, but got: ",
        selectResult.size());

  auto selectResultType = selectResult[0].getType().dyn_cast<ShapedType>();
  // select_and_scatter_c9
  if (!selectResultType || !selectResultType.getElementType().isInteger(1) ||
      (selectResultType.hasRank() &&
       selectResultType.cast<RankedTensorType>().getRank() != 0))
    return emitOptionalError(
        location,
        "expects the return-type of select-region to be tensor<i1>, but got: ",
        selectResult[0].getType());

  // select_and_scatter_c10
  if (failed(verifyReducerShape(
          location, scatter.front(),
          {RankedTensorType::get({}, sourceType.getElementType())},
          {initValueType},
          /*allowedDimensions=*/{})))
    return failure();

  // TODO: add missing tests of convert1DAttribute( for SelectAndScatterOp.
  auto windowDimsOrErr =
      convert1DAttribute(windowDimensions, location, "window_dimensions");
  if (failed(windowDimsOrErr)) return failure();
  if (operandType.hasRank()) {
    // select_and_scatter_c4
    if (operandType.getRank() !=
        static_cast<int64_t>((*windowDimsOrErr).size()))
      return emitOptionalError(
          location,
          "expects window-dimensions size == operand rank, but got "
          "window-dimensions size: ",
          (*windowDimsOrErr).size(), " and operand-type: ", operandType,
          " with rank = ", operandType.getRank(), ".");
  }
  // select_and_scatter_c8, select_and_scatter_i6
  auto paddingOrErr = convertPaddingAttribute(padding, location);
  if (failed(paddingOrErr)) return failure();

  // TODO: add missing tests of convert1DAttribute( for SelectAndScatterOp.
  auto windowStridesOrErr =
      convert1DAttribute(windowStrides, location, "window_strides");
  if (failed(windowStridesOrErr)) return failure();
  // select_and_scatter_c5, select_and_scatter_c7
  auto windowOrErr = verifyWindowAttributesAndInferWindowDimensions(
      *windowDimsOrErr, *windowStridesOrErr, *paddingOrErr,
      /*lhsDilation=*/{}, /*rhsDilation=*/{}, /*windowReversal*/ {}, location);
  if (failed(windowOrErr)) return failure();

  ShapedType windowResultType;
  if (!operandType.hasRank())
    windowResultType = UnrankedTensorType::get(operandType.getElementType());
  else
    windowResultType = RankedTensorType::get(
        inferWindowOutputShape(operandType.getShape(), *windowOrErr),
        operandType.getElementType());

  // select_and_scatter_c1, select_and_scatter_c2
  if (!compatibleShapeAndElementType(windowResultType, sourceType,
                                     /*ignoreFpPrecision=*/true))
    return emitOptionalError(location, "expects source-type to be ",
                             windowResultType, ", but got", sourceType);

  return success();
}

LogicalResult verifySortOp(std::optional<Location> location, ValueRange inputs,
                           int64_t dimension, Region& comparator) {
  auto operandTypes = inputs.getTypes();
  for (auto operandType : operandTypes) {
    auto operandShapedType = operandType.cast<ShapedType>();
    if (operandShapedType.hasRank()) {
      int64_t cmpDim = dimension;
      int64_t rank = operandShapedType.getRank();
      // sort_c4
      if (cmpDim < -rank || cmpDim >= rank)
        return emitOptionalError(
            location, "dimension attribute value must be in range [-", rank,
            ", ", rank, "), but found ", cmpDim);
      else
        break;  // ODS SameOperandsAndResultShape asserts inputs have same shape
    }
  }

  Block& block = comparator.front();
  // sort_c5
  size_t numOperands = operandTypes.size();
  if (block.getNumArguments() != 2 * numOperands)
    return emitOptionalError(location, "comparator block should have ",
                             2 * numOperands, " arguments");
  // sort_c5
  for (const auto& indexedOperandType : llvm::enumerate(operandTypes)) {
    int index = indexedOperandType.index();
    Type elementType =
        indexedOperandType.value().cast<ShapedType>().getElementType();
    Type shapedType = RankedTensorType::get({}, elementType);
    for (int i : {2 * index, 2 * index + 1}) {
      Type argType = block.getArgument(i).getType();
      if (argType != shapedType)
        return emitOptionalError(location, "comparator block argument #", i,
                                 " should be of type ", shapedType, " but got ",
                                 argType);
    }
  }

  // sort_c5
  auto comparatorResult = block.getTerminator()->getOperands();
  if (comparatorResult.size() != 1)
    return emitOptionalError(location,
                             "comparator must return single output but got ",
                             comparatorResult.size());
  // sort_c5
  auto comparatorResultType = comparatorResult[0].getType().cast<ShapedType>();
  if ((comparatorResultType.hasRank() && comparatorResultType.getRank() != 0) ||
      !comparatorResultType.getElementType().isInteger(1))
    return emitOptionalError(location,
                             "comparator must return tensor<i1> but got ",
                             comparatorResult[0].getType());
  return success();
}

LogicalResult verifyWhileOp(std::optional<Location> location,
                            ValueRange operand, Region& cond, Region& body) {
  auto operandTypes = operand.getTypes();
  auto condArgsTypes = cond.front().getArgumentTypes();
  auto bodyArgsTypes = body.front().getArgumentTypes();
  // while_c1
  if (!isCompatibleForHloTypeInference(operandTypes, condArgsTypes))
    return emitOptionalError(location,
                             "expect operands to be compatible with condition "
                             "block arguments but got ",
                             operandTypes, " vs ", condArgsTypes);
  // while_c2
  if (!isCompatibleForHloTypeInference(operandTypes, bodyArgsTypes))
    return emitOptionalError(
        location,
        "expect operands to be compatible with body block arguments but got ",
        operandTypes, " vs ", bodyArgsTypes);
  // while_c2
  auto bodyReturnTypes = body.front().getTerminator()->getOperandTypes();
  if (!isCompatibleForHloTypeInference(operandTypes, bodyReturnTypes))
    return emitOptionalError(location,
                             "expect operands to be compatible with body block "
                             "return types but got ",
                             operandTypes, " vs ", bodyReturnTypes);
  // while_c1
  auto condReturnTypes = cond.front().back().getOperandTypes();
  if (condReturnTypes.size() != 1)
    return emitOptionalError(
        location, "expect condition body returns a single value but got ",
        condReturnTypes.size());
  // while_c1
  auto operandType = condReturnTypes[0].cast<ShapedType>();
  if ((operandType.hasRank() && operandType.getRank() != 0) ||
      !operandType.getElementType().isInteger(1))
    return emitOptionalError(
        location,
        "expect condition block return a zero-ranked tensor of i1 but got ",
        condReturnTypes[0]);

  return success();
}

}  // end namespace hlo
}  // end namespace mlir
