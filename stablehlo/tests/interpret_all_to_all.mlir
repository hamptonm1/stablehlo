// RUN: stablehlo-translate --interpret -split-input-file %s

module @cross_replica {
  func.func public @all_to_all(%operand : tensor<2x4xi64>) -> tensor<4x2xi64> {
    %result = "stablehlo.all_to_all"(%operand) {
      split_dimension = 1 : i64,
      concat_dimension = 0 : i64,
      split_count = 2 : i64,
      replica_groups = dense<[[0, 1]]> : tensor<1x2xi64>
    } : (tensor<2x4xi64>) -> tensor<4x2xi64>
    return %result : tensor<4x2xi64>
  }
  func.func public @main() {
    %inputs0 = stablehlo.constant dense<[[1, 2, 3, 4],
                                         [5, 6, 7, 8]]> : tensor<2x4xi64>
    %inputs1 = stablehlo.constant dense<[[9, 10, 11, 12],
                                         [13, 14, 15, 16]]> : tensor<2x4xi64>
    %results:2 = "interpreter.run_parallel"(%inputs0, %inputs1) {
      programs=[[@all_to_all], [@all_to_all]]
    } : (tensor<2x4xi64>, tensor<2x4xi64>) -> (tensor<4x2xi64>, tensor<4x2xi64>)
    check.expect_eq_const %results#0, dense<[[1, 2],
                                             [5, 6],
                                             [9, 10],
                                             [13, 14]]> : tensor<4x2xi64>
    check.expect_eq_const %results#1, dense<[[3, 4],
                                             [7, 8],
                                             [11, 12],
                                             [15, 16]]> : tensor<4x2xi64>
    func.return
  }
}

// -----

module @cross_partition {
  func.func public @all_to_all(%operand : tensor<2x4xi64>) -> tensor<4x2xi64> {
    %result = "stablehlo.all_to_all"(%operand) {
      split_dimension = 1 : i64,
      concat_dimension = 0 : i64,
      split_count = 2 : i64,
      replica_groups = dense<[[0, 1]]> : tensor<1x2xi64>,
      channel_handle = #stablehlo.channel_handle<handle = 1, type = 0>
    } : (tensor<2x4xi64>) -> tensor<4x2xi64>
    return %result : tensor<4x2xi64>
  }
  func.func public @main() {
    %inputs0 = stablehlo.constant dense<[[1, 2, 3, 4],
                                         [5, 6, 7, 8]]> : tensor<2x4xi64>
    %inputs1 = stablehlo.constant dense<[[9, 10, 11, 12],
                                         [13, 14, 15, 16]]> : tensor<2x4xi64>
    %results:2 = "interpreter.run_parallel"(%inputs0, %inputs1) {
      programs=[[@all_to_all, @all_to_all]]
    } : (tensor<2x4xi64>, tensor<2x4xi64>) -> (tensor<4x2xi64>, tensor<4x2xi64>)
    check.expect_eq_const %results#0, dense<[[1, 2],
                                             [5, 6],
                                             [9, 10],
                                             [13, 14]]> : tensor<4x2xi64>
    check.expect_eq_const %results#1, dense<[[3, 4],
                                             [7, 8],
                                             [11, 12],
                                             [15, 16]]> : tensor<4x2xi64>
    func.return
  }
}
