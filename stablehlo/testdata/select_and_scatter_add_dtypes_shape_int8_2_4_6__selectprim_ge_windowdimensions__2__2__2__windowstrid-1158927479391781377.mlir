// RUN: stablehlo-opt -inline %s | stablehlo-translate --interpret
// RUN: stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt > %t.0
// RUN: stablehlo-opt %s > %t.1
// RUN: diff %t.0 %t.1

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0:2 = call @inputs() : () -> (tensor<1x3x5xi8>, tensor<2x4x6xi8>)
    %1 = call @expected() : () -> tensor<2x4x6xi8>
    %2 = stablehlo.constant dense<-128> : tensor<i8>
    %3 = stablehlo.pad %0#1, %2, low = [0, 0, 0], high = [0, 0, 0], interior = [0, 0, 0] : (tensor<2x4x6xi8>, tensor<i8>) -> tensor<2x4x6xi8>
    %4 = stablehlo.constant dense<0> : tensor<i8>
    %5 = "stablehlo.select_and_scatter"(%3, %0#0, %4) ({
    ^bb0(%arg0: tensor<i8>, %arg1: tensor<i8>):
      %8 = stablehlo.compare  GE, %arg0, %arg1,  SIGNED : (tensor<i8>, tensor<i8>) -> tensor<i1>
      stablehlo.return %8 : tensor<i1>
    }, {
    ^bb0(%arg0: tensor<i8>, %arg1: tensor<i8>):
      %8 = stablehlo.add %arg0, %arg1 : tensor<i8>
      stablehlo.return %8 : tensor<i8>
    }) {window_dimensions = dense<2> : tensor<3xi64>} : (tensor<2x4x6xi8>, tensor<1x3x5xi8>, tensor<i8>) -> tensor<2x4x6xi8>
    %6 = "stablehlo.slice"(%5) {limit_indices = array<i64: 2, 4, 6>, start_indices = array<i64: 0, 0, 0>, strides = array<i64: 1, 1, 1>} : (tensor<2x4x6xi8>) -> tensor<2x4x6xi8>
    %7 = stablehlo.custom_call @check.eq(%6, %1) : (tensor<2x4x6xi8>, tensor<2x4x6xi8>) -> tensor<i1>
    return %7 : tensor<i1>
  }
  func.func private @inputs() -> (tensor<1x3x5xi8>, tensor<2x4x6xi8>) {
    %0 = stablehlo.constant dense<[[[3, 3, -1, -1, -3], [2, 3, 2, -3, 2], [0, -5, 2, 0, -3]]]> : tensor<1x3x5xi8>
    %1 = stablehlo.constant dense<[[[7, 1, 1, 6, 0, -5], [0, -6, 2, -2, 0, 1], [2, 0, 1, -4, 0, -2], [-4, -1, 6, -2, -1, 2]], [[0, 0, 5, 2, -3, -1], [0, 0, -4, 3, 1, -5], [-1, 1, -4, 1, -2, 1], [0, 0, 0, -2, -3, 1]]]> : tensor<2x4x6xi8>
    return %0, %1 : tensor<1x3x5xi8>, tensor<2x4x6xi8>
  }
  func.func private @expected() -> tensor<2x4x6xi8> {
    %0 = stablehlo.constant dense<[[[3, 0, 0, -2, 0, 0], [0, 0, 3, 0, 0, -1], [2, 0, 0, 0, 0, 0], [0, 0, -3, 0, 0, -3]], [[0, 0, 3, 0, 0, 0], [0, 0, 0, -1, 0, 0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0]]]> : tensor<2x4x6xi8>
    return %0 : tensor<2x4x6xi8>
  }
}

