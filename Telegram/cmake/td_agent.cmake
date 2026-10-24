# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_agent OBJECT)
init_non_host_target(td_agent)
add_library(tdesktop::td_agent ALIAS td_agent)

nice_target_sources(td_agent ${src_loc}
PRIVATE
    agent/core/agent_adapter.h
    agent/core/agent_anthropic.cpp
    agent/core/agent_anthropic.h
    agent/core/agent_anthropic_stream.cpp
    agent/core/agent_anthropic_stream.h
    agent/core/agent_card.cpp
    agent/core/agent_card.h
    agent/core/agent_chat_messages.cpp
    agent/core/agent_chat_messages.h
    agent/core/agent_chat_state.cpp
    agent/core/agent_chat_state.h
    agent/core/agent_chatgpt_auth.cpp
    agent/core/agent_chatgpt_auth.h
    agent/core/agent_claude_engine.cpp
    agent/core/agent_claude_engine.h
    agent/core/agent_claude_process.h
    agent/core/agent_claude_protocol.cpp
    agent/core/agent_claude_protocol.h
    agent/core/agent_context.cpp
    agent/core/agent_context.h
    agent/core/agent_documents.cpp
    agent/core/agent_documents.h
    agent/core/agent_engine.h
    agent/core/agent_exclusion_list.cpp
    agent/core/agent_exclusion_list.h
    agent/core/agent_feed.cpp
    agent/core/agent_feed.h
    agent/core/agent_journal.cpp
    agent/core/agent_journal.h
    agent/core/agent_loop.cpp
    agent/core/agent_loop.h
    agent/core/agent_media.cpp
    agent/core/agent_media.h
    agent/core/agent_memory.cpp
    agent/core/agent_memory.h
    agent/core/agent_mtproto.cpp
    agent/core/agent_mtproto.h
    agent/core/agent_navigation.cpp
    agent/core/agent_navigation.h
    agent/core/agent_openai.cpp
    agent/core/agent_openai.h
    agent/core/agent_openai_responses.cpp
    agent/core/agent_openai_responses.h
    agent/core/agent_policy.cpp
    agent/core/agent_policy.h
    agent/core/agent_prompts.cpp
    agent/core/agent_prompts.h
    agent/core/agent_providers.cpp
    agent/core/agent_providers.h
    agent/core/agent_record.cpp
    agent/core/agent_record.h
    agent/core/agent_request.cpp
    agent/core/agent_request.h
    agent/core/agent_scope.h
    agent/core/agent_spending.cpp
    agent/core/agent_spending.h
    agent/core/agent_sse.cpp
    agent/core/agent_sse.h
    agent/core/agent_task.cpp
    agent/core/agent_task.h
    agent/core/agent_tl_codec.cpp
    agent/core/agent_tl_codec.h
    agent/core/agent_tl_schema.cpp
    agent/core/agent_tl_schema.h
    agent/core/agent_tool_catalog.cpp
    agent/core/agent_tool_catalog.h
    agent/core/agent_transport.h
    agent/core/agent_types.cpp
    agent/core/agent_types.h
)

target_include_directories(td_agent
PUBLIC
    ${src_loc}
)

target_link_libraries(td_agent
PUBLIC
    desktop-app::lib_base
)

add_executable(td_agent_test)
init_non_host_target(td_agent_test "(agent)")

nice_target_sources(td_agent_test ${src_loc}
PRIVATE
    agent/core/tests/agent_anthropic_tests.cpp
    agent/core/tests/agent_card_tests.cpp
    agent/core/tests/agent_chat_messages_tests.cpp
    agent/core/tests/agent_chat_state_tests.cpp
    agent/core/tests/agent_chatgpt_tests.cpp
    agent/core/tests/agent_claude_tests.cpp
    agent/core/tests/agent_context_tests.cpp
    agent/core/tests/agent_documents_tests.cpp
    agent/core/tests/agent_exclusion_tests.cpp
    agent/core/tests/agent_feed_tests.cpp
    agent/core/tests/agent_journal_tests.cpp
    agent/core/tests/agent_loop_tests.cpp
    agent/core/tests/agent_media_tests.cpp
    agent/core/tests/agent_memory_tests.cpp
    agent/core/tests/agent_mtproto_tests.cpp
    agent/core/tests/agent_navigation_tests.cpp
    agent/core/tests/agent_openai_tests.cpp
    agent/core/tests/agent_policy_tests.cpp
    agent/core/tests/agent_prompts_tests.cpp
    agent/core/tests/agent_providers_tests.cpp
    agent/core/tests/agent_record_tests.cpp
    agent/core/tests/agent_request_tests.cpp
    agent/core/tests/agent_responses_tests.cpp
    agent/core/tests/agent_spending_tests.cpp
    agent/core/tests/agent_sse_tests.cpp
    agent/core/tests/agent_task_tests.cpp
    agent/core/tests/agent_tests.h
    agent/core/tests/agent_tests_main.cpp
    agent/core/tests/agent_tl_tests.cpp
    agent/core/tests/agent_tool_catalog_tests.cpp
)

target_include_directories(td_agent_test
PRIVATE
    ${src_loc}
)

target_compile_definitions(td_agent_test
PRIVATE
    AGENT_TEST_FIXTURES_PATH="${src_loc}/agent/core/tests/fixtures"
    AGENT_TEST_API_TL_PATH="${src_loc}/mtproto/scheme/api.tl"
)

target_link_libraries(td_agent_test
PRIVATE
    tdesktop::td_agent
    desktop-app::lib_base
)

set_target_properties(td_agent_test PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Upstream's harness copies loop variables (GCC -Werror in CI); drop once fixed.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(${src_loc}/test/test_menu.cpp
    PROPERTIES
        COMPILE_OPTIONS -Wno-error=range-loop-construct
    )
endif()

# Static QtNetwork pulls in QDnsLookup on Windows; Qt links it with Dnsapi.
if (WIN32)
    target_link_libraries(td_agent PUBLIC Dnsapi)
endif()
