// RUN: stablehlo-opt -inline %s | stablehlo-translate --interpret
// RUN: stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt > %t.0
// RUN: stablehlo-opt %s > %t.1
// RUN: diff %t.0 %t.1

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0 = call @inputs() : () -> tensor<4x6xi16>
    %1 = call @expected() : () -> tensor<3x5xi16>
    %2 = stablehlo.constant dense<1> : tensor<i16>
    %3 = "stablehlo.reduce_window"(%0, %2) ({
    ^bb0(%arg0: tensor<i16>, %arg1: tensor<i16>):
      %5 = stablehlo.add %arg0, %arg1 : tensor<i16>
      stablehlo.return %5 : tensor<i16>
    }) {window_dimensions = dense<2> : tensor<2xi64>} : (tensor<4x6xi16>, tensor<i16>) -> tensor<3x5xi16>
    %4 = stablehlo.custom_call @check.eq(%3, %1) : (tensor<3x5xi16>, tensor<3x5xi16>) -> tensor<i1>
    return %4 : tensor<i1>
  }
  func.func private @inputs() -> tensor<4x6xi16> {
    %0 = stablehlo.constant dense<[[1, -3, -3, 5, 3, 2], [0, -1, 3, -4, -2, -2], [0, 2, 1, 0, -8, 0], [-2, 0, 4, 2, 0, -2]]> : tensor<4x6xi16>
    return %0 : tensor<4x6xi16>
  }
  func.func private @expected() -> tensor<3x5xi16> {
    %0 = stablehlo.constant dense<[[-2, -3, 2, 3, 2], [2, 6, 1, -13, -11], [1, 8, 8, -5, -9]]> : tensor<3x5xi16>
    return %0 : tensor<3x5xi16>
  }
}

