cmake_minimum_required(VERSION 3.16)
project(sip_answer_engine VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAE_USE_SQLITE "Persist call detail records to SQLite when libsqlite3 is available" ON)

add_executable(sip-answer-engine
  src/main.cpp
  src/config.cpp
  src/call_manager.cpp
  src/http_server.cpp
  src/sip_udp_engine.cpp
  src/log_store.cpp
  src/timer_scheduler.cpp
  src/rtp_session.cpp
)

target_include_directories(sip-answer-engine PRIVATE src)
target_compile_options(sip-answer-engine PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(sip-answer-engine PRIVATE pthread)

if(SAE_USE_SQLITE)
  find_package(SQLite3)
  if(SQLite3_FOUND)
    target_compile_definitions(sip-answer-engine PRIVATE SAE_HAVE_SQLITE=1)
    target_link_libraries(sip-answer-engine PRIVATE SQLite::SQLite3)
  else()
    message(WARNING "SQLite3 not found; call detail records will be stored as JSON Lines only")
  endif()
endif()

install(TARGETS sip-answer-engine RUNTIME DESTINATION bin)
install(FILES config.yaml DESTINATION etc/sip-answer-engine)
