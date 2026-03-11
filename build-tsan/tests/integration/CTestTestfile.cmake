# CMake generated Testfile for 
# Source directory: /Users/gongxun/workspace/code/FluxCache/tests/integration
# Build directory: /Users/gongxun/workspace/code/FluxCache/build-tsan/tests/integration
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[read_path_e2e_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/integration/read_path_e2e_test")
set_tests_properties([=[read_path_e2e_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;7;add_test;/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;0;")
add_test([=[write_path_e2e_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/integration/write_path_e2e_test")
set_tests_properties([=[write_path_e2e_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;15;add_test;/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;0;")
add_test([=[cli_smoke_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/integration/cli_smoke_test")
set_tests_properties([=[cli_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;23;add_test;/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;0;")
add_test([=[gc_reconciliation_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/integration/gc_reconciliation_test")
set_tests_properties([=[gc_reconciliation_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;31;add_test;/Users/gongxun/workspace/code/FluxCache/tests/integration/CMakeLists.txt;0;")
