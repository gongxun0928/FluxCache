add_executable(master_test master_test.cpp)
target_link_libraries(master_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
add_test(NAME master_test COMMAND master_test)

add_executable(mount_table_test mount_table_test.cpp)
target_link_libraries(mount_table_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
add_test(NAME mount_table_test COMMAND mount_table_test)

add_executable(inode_tree_test inode_tree_test.cpp)
target_link_libraries(inode_tree_test PRIVATE fluxcache_master GTest::gtest_main)
add_test(NAME inode_tree_test COMMAND inode_tree_test)

add_executable(worker_manager_test worker_manager_test.cpp)
target_link_libraries(worker_manager_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
add_test(NAME worker_manager_test COMMAND worker_manager_test)

add_executable(sync_from_ufs_test sync_from_ufs_test.cpp)
target_link_libraries(sync_from_ufs_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
add_test(NAME sync_from_ufs_test COMMAND sync_from_ufs_test)

add_executable(get_file_info_test get_file_info_test.cpp)
target_link_libraries(get_file_info_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
add_test(NAME get_file_info_test COMMAND get_file_info_test)

add_executable(create_complete_file_test create_complete_file_test.cpp)
target_link_libraries(create_complete_file_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
add_test(NAME create_complete_file_test COMMAND create_complete_file_test)

add_executable(mount_table_persistence_test mount_table_persistence_test.cpp)
target_link_libraries(mount_table_persistence_test PRIVATE fluxcache_master GTest::gtest_main)
add_test(NAME mount_table_persistence_test COMMAND mount_table_persistence_test)

add_executable(prewarm_test prewarm_test.cpp)
target_link_libraries(prewarm_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
add_test(NAME prewarm_test COMMAND prewarm_test)

if(FLUXCACHE_ENABLE_RAFT)
  add_executable(raft_state_machine_test raft_state_machine_test.cpp)
  target_link_libraries(raft_state_machine_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
  add_test(NAME raft_state_machine_test COMMAND raft_state_machine_test)

  add_executable(raft_state_manager_test raft_state_manager_test.cpp)
  target_link_libraries(raft_state_manager_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
  add_test(NAME raft_state_manager_test COMMAND raft_state_manager_test)

  add_executable(raft_node_test raft_node_test.cpp)
  target_link_libraries(raft_node_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
  add_test(NAME raft_node_test COMMAND raft_node_test)

  add_executable(raft_cluster_test raft_cluster_test.cpp)
  target_link_libraries(raft_cluster_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
  add_test(NAME raft_cluster_test COMMAND raft_cluster_test)

  add_executable(raft_write_path_test raft_write_path_test.cpp)
  target_link_libraries(raft_write_path_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
  add_test(NAME raft_write_path_test COMMAND raft_write_path_test)

  add_executable(raft_topology_test raft_topology_test.cpp)
  target_link_libraries(raft_topology_test PRIVATE fluxcache_master fluxcache_proto fluxcache_ufs GTest::gtest_main)
  add_test(NAME raft_topology_test COMMAND raft_topology_test)
endif()
