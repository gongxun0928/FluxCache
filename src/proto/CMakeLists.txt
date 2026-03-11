# Phase 1 minimal RPC contract (P1-03)
# Generates C++ from common.proto, master.proto, worker.proto

set(PROTO_IMPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# common.proto (messages only, no service)
add_custom_command(
  OUTPUT "${PROTO_GEN_DIR}/common.pb.cc" "${PROTO_GEN_DIR}/common.pb.h"
  COMMAND protobuf::protoc
  ARGS --cpp_out="${PROTO_GEN_DIR}" -I"${PROTO_IMPORT_DIR}"
       "${CMAKE_CURRENT_SOURCE_DIR}/common.proto"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/common.proto"
  COMMENT "Generating C++ from common.proto"
)

# master.proto (messages + service)
add_custom_command(
  OUTPUT "${PROTO_GEN_DIR}/master.pb.cc" "${PROTO_GEN_DIR}/master.pb.h"
          "${PROTO_GEN_DIR}/master.grpc.pb.cc" "${PROTO_GEN_DIR}/master.grpc.pb.h"
  COMMAND protobuf::protoc
  ARGS --cpp_out="${PROTO_GEN_DIR}"
       --grpc_out="${PROTO_GEN_DIR}"
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       -I"${PROTO_IMPORT_DIR}"
       "${CMAKE_CURRENT_SOURCE_DIR}/master.proto"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/master.proto"
          "${CMAKE_CURRENT_SOURCE_DIR}/common.proto"
  COMMENT "Generating C++ and gRPC from master.proto"
)

# worker.proto (messages + service)
add_custom_command(
  OUTPUT "${PROTO_GEN_DIR}/worker.pb.cc" "${PROTO_GEN_DIR}/worker.pb.h"
          "${PROTO_GEN_DIR}/worker.grpc.pb.cc" "${PROTO_GEN_DIR}/worker.grpc.pb.h"
  COMMAND protobuf::protoc
  ARGS --cpp_out="${PROTO_GEN_DIR}"
       --grpc_out="${PROTO_GEN_DIR}"
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       -I"${PROTO_IMPORT_DIR}"
       "${CMAKE_CURRENT_SOURCE_DIR}/worker.proto"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/worker.proto"
          "${CMAKE_CURRENT_SOURCE_DIR}/common.proto"
  COMMENT "Generating C++ and gRPC from worker.proto"
)

add_library(fluxcache_proto STATIC
  "${PROTO_GEN_DIR}/common.pb.cc"
  "${PROTO_GEN_DIR}/master.pb.cc"
  "${PROTO_GEN_DIR}/master.grpc.pb.cc"
  "${PROTO_GEN_DIR}/worker.pb.cc"
  "${PROTO_GEN_DIR}/worker.grpc.pb.cc"
)

target_include_directories(fluxcache_proto PUBLIC
  ${PROTO_GEN_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(fluxcache_proto PUBLIC
  fluxcache_common
  protobuf::libprotobuf
  gRPC::grpc++
)
