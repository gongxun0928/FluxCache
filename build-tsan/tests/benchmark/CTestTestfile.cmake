# CMake generated Testfile for 
# Source directory: /Users/gongxun/workspace/code/FluxCache/tests/benchmark
# Build directory: /Users/gongxun/workspace/code/FluxCache/build-tsan/tests/benchmark
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[sequential_read_bench]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/benchmark/sequential_read_bench")
set_tests_properties([=[sequential_read_bench]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/benchmark/CMakeLists.txt;6;add_test;/Users/gongxun/workspace/code/FluxCache/tests/benchmark/CMakeLists.txt;0;")
add_test([=[page_store_bench]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/benchmark/page_store_bench")
set_tests_properties([=[page_store_bench]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/benchmark/CMakeLists.txt;11;add_test;/Users/gongxun/workspace/code/FluxCache/tests/benchmark/CMakeLists.txt;0;")
