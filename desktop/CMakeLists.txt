cmake_minimum_required(VERSION 3.21)
project(Lumen VERSION 0.6.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Test Pdf PdfWidgets)

set(LUMEN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(LUMEN_EDITOR_SOURCES
  ${LUMEN_SRC}/editor/lumen_document.cpp
  ${LUMEN_SRC}/editor/lumen_document_commands.cpp
  ${LUMEN_SRC}/editor/lumen_clipboard.cpp
  ${LUMEN_SRC}/editor/lumen_editor_clipboard.cpp
  ${LUMEN_SRC}/editor/lumen_editor.cpp
  ${LUMEN_SRC}/editor/lumen_temporal.cpp
  ${LUMEN_SRC}/editor/lumen_image_block.cpp
  ${LUMEN_SRC}/editor/lumen_block_references.cpp
  ${LUMEN_SRC}/editor/lumen_table_controls.cpp
  ${LUMEN_SRC}/editor/lumen_syntax.cpp
  ${LUMEN_SRC}/editor/lumen_math.cpp)
set(LUMEN_MODULES
  ${LUMEN_EDITOR_SOURCES}
  ${LUMEN_SRC}/editor/lumen_media.cpp
  ${LUMEN_SRC}/objects/lumen_properties.cpp
  ${LUMEN_SRC}/objects/lumen_tasks.cpp
  ${LUMEN_SRC}/views/lumen_views.cpp
  ${LUMEN_SRC}/views/lumen_calculations.cpp
  ${LUMEN_SRC}/views/lumen_calendar.cpp
  ${LUMEN_SRC}/views/lumen_graph.cpp)

set(LUMEN_APP_SOURCES
  ${LUMEN_SRC}/app/main_window.cpp
  ${LUMEN_SRC}/ui/main_window_ui.cpp
  ${LUMEN_SRC}/views/main_window_views.cpp
  ${LUMEN_SRC}/views/main_window_collections.cpp
  ${LUMEN_SRC}/views/main_window_dashboards.cpp
  ${LUMEN_SRC}/views/main_window_templates.cpp
  ${LUMEN_SRC}/views/main_window_aliases.cpp
  ${LUMEN_SRC}/sync/main_window_sync.cpp)

function(lumen_target target)
  target_include_directories(${target} PRIVATE
    "${LUMEN_SRC}"
    "${LUMEN_SRC}/app"
    "${LUMEN_SRC}/editor"
    "${LUMEN_SRC}/objects"
    "${LUMEN_SRC}/views"
    "${LUMEN_SRC}/ui"
    "${LUMEN_SRC}/sync")
  target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Pdf Qt6::PdfWidgets)
  qt_add_resources(${target} fonts PREFIX "/" FILES assets/fonts/Inter-Regular.otf assets/fonts/Inter-Medium.otf assets/fonts/Inter-SemiBold.otf assets/fonts/Inter-Bold.otf)
  file(GLOB ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
  qt_add_resources(${target} icons PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons" FILES ${ICONS})
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

qt_add_executable(lumen src/app/main.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
lumen_target(lumen)
install(TARGETS lumen RUNTIME DESTINATION bin)
install(FILES lumen.desktop DESTINATION share/applications)

option(LUMEN_BUILD_TESTS "Build native acceptance and component tests" OFF)
if(LUMEN_BUILD_TESTS)
  enable_testing()

  foreach(test acceptance integration)
    qt_add_executable(lumen-${test} tests/${test}.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
    lumen_target(lumen-${test})
    target_link_libraries(lumen-${test} PRIVATE Qt6::Test)
  endforeach()

  qt_add_executable(lumen-views tests/views.cpp ${LUMEN_SRC}/views/lumen_views.cpp ${LUMEN_SRC}/views/lumen_calculations.cpp)
  # Collections UI flows run against an in-process fake API (no server, no
  # network): the fake is a loopback QTcpServer speaking just enough HTTP.
  qt_add_executable(lumen-collections tests/collections_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  qt_add_executable(lumen-sync tests/sync_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  # Find in page + replace: editor-level coverage plus the MainWindow save
  # path against the same in-process fake API.
  qt_add_executable(lumen-find-replace tests/find_replace_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  # Type dashboards (D4): section tabs on the type page (All, Recent,
  # Untagged, No backlinks, Collections, saved queries) against the fake API,
  # which mirrors the server's type_id/backlinked_count list filters.
  qt_add_executable(lumen-dashboards tests/dashboards_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  # Templates (D5): save/unsave flags, the create picker, from-template
  # creation, the type default template, the journal default and the
  # Templates dashboard section, against the fake API extended with the
  # templates slice of the contract.
  qt_add_executable(lumen-templates tests/templates_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  # Aliases (D6): the per-object alias editor, the object-page display, the
  # search surfaces (palette via GET /v1/search, @-dropdown, view search)
  # and the matched-alias insertion rule, against the fake API extended
  # with the aliases contract slice (normalization, limits, matched_alias).
  qt_add_executable(lumen-aliases tests/aliases_test.cpp ${LUMEN_APP_SOURCES} ${LUMEN_MODULES})
  # View calculations (D7): the op-per-type engine and the LumenViews
  # rendering/customize flows (table footer, summary strip, per-group lines,
  # add/edit/remove), without a server.
  qt_add_executable(lumen-calculations tests/calculations_test.cpp
    ${LUMEN_SRC}/views/lumen_calculations.cpp ${LUMEN_SRC}/views/lumen_views.cpp)
  qt_add_executable(lumen-calendar tests/calendar.cpp ${LUMEN_SRC}/views/lumen_calendar.cpp ${LUMEN_SRC}/views/lumen_views.cpp ${LUMEN_SRC}/views/lumen_calculations.cpp)
  qt_add_executable(lumen-properties tests/properties.cpp ${LUMEN_SRC}/objects/lumen_properties.cpp)
  qt_add_executable(lumen-editor-media tests/editor_media_test.cpp ${LUMEN_EDITOR_SOURCES} ${LUMEN_SRC}/editor/lumen_media.cpp)
  qt_add_executable(lumen-graph tests/graph.cpp ${LUMEN_SRC}/views/lumen_graph.cpp)
  qt_add_executable(lumen-code-math tests/code_math.cpp ${LUMEN_EDITOR_SOURCES})
  qt_add_executable(lumen-tasks tests/tasks_test.cpp ${LUMEN_SRC}/objects/lumen_tasks.cpp)
  qt_add_executable(lumen-document tests/document_test.cpp ${LUMEN_EDITOR_SOURCES})
  qt_add_executable(lumen-editor-blocks tests/editor_blocks_test.cpp ${LUMEN_EDITOR_SOURCES} ${LUMEN_SRC}/editor/lumen_media.cpp)

  qt_add_executable(lumen-api-protocol tests/api_protocol_test.cpp)
  qt_add_executable(lumen-table-controls tests/table_controls_test.cpp ${LUMEN_EDITOR_SOURCES})

  foreach(test api-protocol table-controls document views collections sync find-replace dashboards templates aliases calculations calendar properties editor-media graph code-math tasks editor-blocks)
    lumen_target(lumen-${test})
    target_link_libraries(lumen-${test} PRIVATE Qt6::Test)
    add_test(NAME ${test} COMMAND lumen-${test})
  endforeach()
  set_tests_properties(api-protocol table-controls document views collections sync find-replace dashboards templates aliases calculations calendar properties editor-media graph code-math tasks editor-blocks
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
