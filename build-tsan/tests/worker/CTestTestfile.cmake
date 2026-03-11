# CMake generated Testfile for 
# Source directory: /Users/gongxun/workspace/code/FluxCache/tests/worker
# Build directory: /Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[worker_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/worker_test")
set_tests_properties([=[worker_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;4;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[memory_tier_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/memory_tier_test")
set_tests_properties([=[memory_tier_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;9;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[ssd_tier_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/ssd_tier_test")
set_tests_properties([=[ssd_tier_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;14;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[hdd_tier_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/hdd_tier_test")
set_tests_properties([=[hdd_tier_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;19;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[tier_manager_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/tier_manager_test")
set_tests_properties([=[tier_manager_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;24;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[page_store_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/page_store_test")
set_tests_properties([=[page_store_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;29;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[read_pages_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/read_pages_test")
set_tests_properties([=[read_pages_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;35;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[write_pages_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/write_pages_test")
set_tests_properties([=[write_pages_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;41;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[lru_policy_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/lru_policy_test")
set_tests_properties([=[lru_policy_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;47;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[lfu_policy_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/lfu_policy_test")
set_tests_properties([=[lfu_policy_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;53;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[meta_store_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/meta_store_test")
set_tests_properties([=[meta_store_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;59;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
add_test([=[tier_promotion_test]=] "/Users/gongxun/workspace/code/FluxCache/build-tsan/tests/worker/tier_promotion_test")
set_tests_properties([=[tier_promotion_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;65;add_test;/Users/gongxun/workspace/code/FluxCache/tests/worker/CMakeLists.txt;0;")
