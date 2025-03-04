// RUN: stablehlo-opt -inline %s | stablehlo-translate --interpret
// RUN: stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt > %t.0
// RUN: stablehlo-opt %s > %t.1
// RUN: diff %t.0 %t.1

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0:2 = call @inputs() : () -> (tensor<1x3x5xi1>, tensor<2x4x6xi1>)
    %1 = call @expected() : () -> tensor<2x4x6xi1>
    %2 = stablehlo.constant dense<false> : tensor<i1>
    %3 = stablehlo.pad %0#1, %2, low = [0, 0, 0], high = [0, 0, 0], interior = [0, 0, 0] : (tensor<2x4x6xi1>, tensor<i1>) -> tensor<2x4x6xi1>
    %4 = stablehlo.constant dense<false> : tensor<i1>
    %5 = "stablehlo.select_and_scatter"(%3, %0#0, %4) ({
    ^bb0(%arg0: tensor<i1>, %arg1: tensor<i1>):
      %8 = stablehlo.compare  GE, %arg0, %arg1,  UNSIGNED : (tensor<i1>, tensor<i1>) -> tensor<i1>
      stablehlo.return %8 : tensor<i1>
    }, {
    ^bb0(%arg0: tensor<i1>, %arg1: tensor<i1>):
      %8 = stablehlo.or %arg0, %arg1 : tensor<i1>
      stablehlo.return %8 : tensor<i1>
    }) {window_dimensions = dense<2> : tensor<3xi64>} : (tensor<2x4x6xi1>, tensor<1x3x5xi1>, tensor<i1>) -> tensor<2x4x6xi1>
    %6 = "stablehlo.slice"(%5) {limit_indices = array<i64: 2, 4, 6>, start_indices = array<i64: 0, 0, 0>, strides = array<i64: 1, 1, 1>} : (tensor<2x4x6xi1>) -> tensor<2x4x6xi1>
    %7 = stablehlo.custom_call @check.eq(%6, %1) : (tensor<2x4x6xi1>, tensor<2x4x6xi1>) -> tensor<i1>
    return %7 : tensor<i1>
  }
  func.func private @inputs() -> (tensor<1x3x5xi1>, tensor<2x4x6xi1>) {
    %0 = stablehlo.constant dense<true> : tensor<1x3x5xi1>
    %1 = stablehlo.constant dense<true> : tensor<2x4x6xi1>
    return %0, %1 : tensor<1x3x5xi1>, tensor<2x4x6xi1>
  }
  func.func private @expected() -> tensor<2x4x6xi1> {
    %0 = stablehlo.constant dense<[[[true, true, true, true, true, false], [true, true, true, true, true, false], [true, true, true, true, true, false], [false, false, false, false, false, false]], [[false, false, false, false, false, false], [false, false, false, false, false, false], [false, false, false, false, false, false], [false, false, false, false, false, false]]]> : tensor<2x4x6xi1>
    return %0 : tensor<2x4x6xi1>
  }
}

