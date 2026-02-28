cmake_minimum_required(VERSION 3.20)
project(clashtui-cpp VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ftxui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(clashtui-cpp
    src/main.cpp
    src/app.cpp
    src/api/mihomo_client.cpp
    src/ui/main_screen.cpp
    src/ui/proxy_panel.cpp
    src/ui/subscription_panel.cpp
    src/ui/log_panel.cpp
    src/ui/install_wizard.cpp
    src/ui/config_panel.cpp
    src/ui/status_bar.cpp
    src/core/config.cpp
    src/core/installer.cpp
    src/core/subscription.cpp
    src/core/profile_manager.cpp
    src/core/updater.cpp
    src/daemon/daemon.cpp
    src/daemon/process_manager.cpp
    src/daemon/ipc_client.cpp
)

target_include_directories(clashtui-cpp PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(clashtui-cpp PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Embed version for self-update checks
target_compile_definitions(clashtui-cpp PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

install(TARGETS clashtui-cpp DESTINATION bin)

# ── Tests ────────────────────────────────────────────────────
enable_testing()
find_package(GTest CONFIG REQUIRED)

# Collect library sources (everything except main.cpp)
set(LIB_SOURCES
    src/app.cpp
    src/api/mihomo_client.cpp
    src/ui/main_screen.cpp
    src/ui/proxy_panel.cpp
    src/ui/subscription_panel.cpp
    src/ui/log_panel.cpp
    src/ui/install_wizard.cpp
    src/ui/config_panel.cpp
    src/ui/status_bar.cpp
    src/core/config.cpp
    src/core/installer.cpp
    src/core/subscription.cpp
    src/core/profile_manager.cpp
    src/core/updater.cpp
    src/daemon/daemon.cpp
    src/daemon/process_manager.cpp
    src/daemon/ipc_client.cpp
)

add_executable(clashtui-tests
    tests/test_config.cpp
    tests/test_i18n.cpp
    tests/test_status_bar.cpp
    tests/test_installer.cpp
    tests/test_mihomo_client.cpp
    tests/test_profile_manager.cpp
    tests/test_process_manager.cpp
    tests/test_daemon_ipc.cpp
    tests/test_updater.cpp
    ${LIB_SOURCES}
)

target_include_directories(clashtui-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(clashtui-tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(clashtui-tests PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

include(GoogleTest)
gtest_discover_tests(clashtui-tests)

# ── E2E Tests (requires running mihomo at 127.0.0.1:9090) ───
add_executable(clashtui-e2e
    tests/test_e2e.cpp
    ${LIB_SOURCES}
)

target_include_directories(clashtui-e2e PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(clashtui-e2e PRIVATE
    GTest::gtest
    GTest::gtest_main
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(clashtui-e2e PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

gtest_discover_tests(clashtui-e2e)
