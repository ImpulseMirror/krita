target_compile_definitions(kritacomfyuiremote_static PRIVATE COMFYUI_ENABLE_TEST_HOOKS=1)

kis_add_tests(
    ComfyUIRemoteDockTest.cpp
    ComfyPortP51Test.cpp
    ComfyPortM8Test.cpp
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)

kis_add_test(
    ComfyPortP52Test.cpp
    ComfyMockHttpServer.cpp
    TEST_NAME ComfyPortP52Test
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)

kis_add_tests(
    ComfyWorkflowEngineGoldenTest.cpp
    ComfyWorkflowNormalize.cpp
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)
target_compile_definitions(ComfyWorkflowEngineGoldenTest PRIVATE
    "COMFYUI_TEST_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/golden\"")
