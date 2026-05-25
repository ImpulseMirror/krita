# Static lib for dock (used by plugin and tests)
set(kritacomfyuiremote_static_SOURCES
    ComfyControlLayer.cpp
    ComfyControlLayerRowWidget.cpp
    ComfyControlLayerListWidget.cpp
    ComfyOpenPose.cpp
    ComfyRegionProcess.cpp
    ComfyRegionLink.cpp
    ComfyRegionPromptWidget.cpp
    ComfyPromptResizeHandle.cpp
    ComfyResources.cpp
    ComfyStyleCollection.cpp
    ComfyLocalization.cpp
    ComfyFileLibrary.cpp
    ComfyTheme.cpp
    ComfySwitchWidget.cpp
    ComfyQueueButton.cpp
    ComfyWorkflowEngine.cpp
    ComfyUIRemoteDockControl.cpp
    ComfyUIRemoteDockControlGenerate.cpp
    ComfyUIIntervalSlider.cpp
    ComfyUIPoseLayers.cpp
    ComfyUIUtils.cpp
    ComfyUIWorkflows.cpp
    ComfyUIRemoteDock.cpp
    ComfyUIRemoteDockConnection.cpp
    ComfyUIRemoteDockGenerate.cpp
    ComfyUIRemoteDockHistory.cpp
    ComfyUIRemoteDockInpaint.cpp
    ComfyUIRemoteDockLive.cpp
    ComfyUIRemoteDockPresets.cpp
    ComfyUIRemoteDockRegions.cpp
    ComfyUIRemoteDockSettings.cpp
    ComfyUIRemoteDockUpscale.cpp
    ComfyUIRemoteDockWebWorkflow.cpp
)
kis_add_library(kritacomfyuiremote_static STATIC ${kritacomfyuiremote_static_SOURCES})
target_link_libraries(kritacomfyuiremote_static PRIVATE kritaimage kritaui)
target_compile_definitions(kritacomfyuiremote_static PRIVATE
    "COMFYUI_PLUGIN_SOURCE_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

# §13.37: optional ZIP extract after verified download (KF Archive)
find_package(KF${KF_MAJOR} ${MIN_FRAMEWORKS_VERSION} QUIET COMPONENTS Archive)
if(KF${KF_MAJOR}Archive_FOUND)
    target_link_libraries(kritacomfyuiremote_static PRIVATE KF${KF_MAJOR}::Archive)
    target_compile_definitions(kritacomfyuiremote_static PRIVATE COMFYUI_HAVE_KARCHIVE=1)
endif()

# §13.170: Optional Qt WebSockets for ETN workflow_published (same client_id as prompt API)
# Use find_package(Qt5WebSockets) / Qt6WebSockets — not find_package(Qt5 COMPONENTS WebSockets),
# or a missing WebSockets module can clear the aggregate Qt5 package and break feature_summary.
set(_comfy_ws_libs "")
if(BUILD_WITH_QT6)
  find_package(Qt6WebSockets QUIET)
  if(TARGET Qt6::WebSockets)
    list(APPEND _comfy_ws_libs Qt6::WebSockets)
  endif()
else()
  find_package(Qt5WebSockets QUIET)
  if(TARGET Qt5::WebSockets)
    list(APPEND _comfy_ws_libs Qt5::WebSockets)
  endif()
endif()
if(_comfy_ws_libs)
  target_link_libraries(kritacomfyuiremote_static PRIVATE ${_comfy_ws_libs})
  target_compile_definitions(kritacomfyuiremote_static PRIVATE COMFYUI_HAVE_QT_WEBSOCKETS=1)
endif()

# Plugin module
set(kritacomfyuiremote_SOURCES ComfyUIRemotePlugin.cpp)
kis_add_library(kritacomfyuiremote MODULE ${kritacomfyuiremote_SOURCES})
# MODULE needs kritaui (Qt includes) for MOC/ComfyUIRemotePlugin.h; static lib alone does not propagate them.
# Link order: static first, then resolve symbols from kritaimage/kritaui (Unix linkers are order-sensitive).
target_link_libraries(kritacomfyuiremote PRIVATE kritacomfyuiremote_static kritaimage kritaui)
install(TARGETS kritacomfyuiremote DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/ DESTINATION ${KRITA_PLUGIN_INSTALL_DIR}/data)
install(FILES ComfyUIRemote.action DESTINATION ${KDE_INSTALL_DATADIR}/krita/actions)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
