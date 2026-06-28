target_compile_definitions(kritacomfyuiremote_static PRIVATE COMFYUI_ENABLE_TEST_HOOKS=1)

kis_add_tests(
    ComfyUIRemoteDockTest.cpp
    ComfyPortP51Test.cpp
    ComfyPortM8Test.cpp
    ComfyInpaintRegressionTest.cpp
    ComfyHistoryThumbnailRegressionTest.cpp
    ComfyHistoryListLayoutRegressionTest.cpp
    ComfyUpscaleApplyRegressionTest.cpp
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)
target_compile_definitions(ComfyUpscaleApplyRegressionTest PRIVATE
    "COMFYUI_TEST_PLUGIN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/..\"")

kis_add_test(
    ComfyPortP52Test.cpp
    ComfyMockHttpServer.cpp
    TEST_NAME ComfyPortP52Test
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)

kis_add_test(
    ComfyUploadPipelineTest.cpp
    ComfyMockHttpServer.cpp
    TEST_NAME ComfyUploadPipelineTest
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)

kis_add_test(
    ComfyPromptClientTest.cpp
    ComfyMockHttpServer.cpp
    TEST_NAME ComfyPromptClientTest
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)

kis_add_test(
    ComfyWorkflowEngineGoldenTest.cpp
    ComfyWorkflowNormalize.cpp
    TEST_NAME ComfyWorkflowEngineGoldenTest
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk
)
target_compile_definitions(ComfyWorkflowEngineGoldenTest PRIVATE
    "COMFYUI_TEST_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/golden\"")

kis_add_test(
    ComfyInpaintIntegrationTest.cpp
    TEST_NAME ComfyInpaintIntegrationTest
    LINK_LIBRARIES kritacomfyuiremote_static kritaui kritaimage kritatestsdk Qt5::Network
)
# Skips at runtime unless COMFY_INTEGRATION_TEST=1. Run: scripts/run_comfy_integration_test.sh
