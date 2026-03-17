set(MASTER_SOURCES
  hash_ring_manager.cpp
  inode_store.cpp
  inode_tree.cpp
  master_server.cpp
  master_service_impl.cpp
  mount_table.cpp
  path_resolver.cpp
  worker_manager.cpp
)

if(FLUXCACHE_ENABLE_RAFT)
  list(APPEND MASTER_SOURCES
    ha/raft_state_machine.cpp
    ha/raft_state_manager.cpp
    ha/raft_node.cpp
    ${FLUXCACHE_NURAFT_EXAMPLES_DIR}/in_memory_log_store.cxx
  )
endif()

add_library(fluxcache_master STATIC ${MASTER_SOURCES})
target_include_directories(fluxcache_master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_master PUBLIC fluxcache_common fluxcache_proto
  fluxcache_ufs ${FLUXCACHE_ROCKSDB_TARGET})

if(FLUXCACHE_ENABLE_RAFT)
  target_link_libraries(fluxcache_master PUBLIC ${FLUXCACHE_NURAFT_TARGET})
  target_include_directories(fluxcache_master PUBLIC
    ${FLUXCACHE_NURAFT_EXAMPLES_DIR}
    ${nuraft_SOURCE_DIR}/include/libnuraft
  )
  target_compile_definitions(fluxcache_master PUBLIC FLUXCACHE_ENABLE_RAFT=1)
endif()

add_executable(fluxcache-master main.cpp)
target_link_libraries(fluxcache-master PRIVATE fluxcache_master)
