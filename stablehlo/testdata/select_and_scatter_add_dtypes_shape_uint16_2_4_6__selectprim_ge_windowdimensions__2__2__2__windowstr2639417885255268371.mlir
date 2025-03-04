// RUN: stablehlo-opt -inline %s | stablehlo-translate --interpret
// RUN: stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt > %t.0
// RUN: stablehlo-opt %s > %t.1
// RUN: diff %t.0 %t.1

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0:2 = call @inputs() : () -> (tensor<1x3x5xui16>, tensor<2x4x6xui16>)
    %1 = call @expected() : () -> tensor<2x4x6xui16>
    %2 = stablehlo.constant dense<0> : tensor<ui16>
    %3 = stablehlo.pad %0#1, %2, low = [0, 0, 0], high = [0, 0, 0], interior = [0, 0, 0] : (tensor<2x4x6xui16>, tensor<ui16>) -> tensor<2x4x6xui16>
    %4 = stablehlo.constant dense<0> : tensor<ui16>
    %5 = "stablehlo.select_and_scatter"(%3, %0#0, %4) ({
    ^bb0(%arg0: tensor<ui16>, %arg1: tensor<ui16>):
      %8 = stablehlo.compare  GE, %arg0, %arg1,  UNSIGNED : (tensor<ui16>, tensor<ui16>) -> tensor<i1>
      stablehlo.return %8 : tensor<i1>
    }, {
    ^bb0(%arg0: tensor<ui16>, %arg1: tensor<ui16>):
      %8 = stablehlo.add %arg0, %arg1 : tensor<ui16>
      stablehlo.return %8 : tensor<ui16>
    }) {window_dimensions = dense<2> : tensor<3xi64>} : (tensor<2x4x6xui16>, tensor<1x3x5xui16>, tensor<ui16>) -> tensor<2x4x6xui16>
    %6 = "stablehlo.slice"(%5) {limit_indices = array<i64: 2, 4, 6>, start_indices = array<i64: 0, 0, 0>, strides = array<i64: 1, 1, 1>} : (tensor<2x4x6xui16>) -> tensor<2x4x6xui16>
    %7 = stablehlo.custom_call @check.eq(%6, %1) : (tensor<2x4x6xui16>, tensor<2x4x6xui16>) -> tensor<i1>
    return %7 : tensor<i1>
  }
  func.func private @inputs() -> (tensor<1x3x5xui16>, tensor<2x4x6xui16>) {
    %0 = stablehlo.constant dense<[[[0, 1, 2, 0, 2], [0, 0, 4, 1, 1], [1, 1, 6, 3, 1]]]> : tensor<1x3x5xui16>
    %1 = stablehlo.constant dense<[[[8, 2, 2, 1, 0, 1], [0, 1, 3, 2, 1, 4], [0, 2, 4, 4, 6, 2], [3, 2, 2, 0, 2, 0]], [[0, 3, 2, 3, 1, 0], [4, 0, 1, 2, 3, 4], [3, 1, 1, 3, 5, 1], [1, 0, 3, 1, 4, 0]]]> : tensor<2x4x6xui16>
    return %0, %1 : tensor<1x3x5xui16>, tensor<2x4x6xui16>
  }
  func.func private @expected() -> tensor<2x4x6xui16> {
    %0 = stablehlo.constant dense<[[[0, 0, 0, 0, 0, 0], [0, 0, 3, 0, 0, 2], [0, 0, 11, 0, 6, 0], [1, 0, 0, 0, 0, 0]], [[0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0]]]> : tensor<2x4x6xui16>
    return %0 : tensor<2x4x6xui16>
  }
}

