add_subdirectory(tests)

# Static lib for dock (used by plugin and tests)
set(kritacomfyuiremote_static_SOURCES
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
target_link_libraries(kritacomfyuiremote_static kritaimage kritaui)

# §13.170: Optional Qt WebSockets for ETN workflow_published (same client_id as prompt API)
set(_comfy_ws_libs "")
if(BUILD_WITH_QT6)
  find_package(Qt6 QUIET COMPONENTS WebSockets)
  if(TARGET Qt6::WebSockets)
    list(APPEND _comfy_ws_libs Qt6::WebSockets)
  endif()
else()
  find_package(Qt5 QUIET COMPONENTS WebSockets)
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
target_link_libraries(kritacomfyuiremote kritacomfyuiremote_static)
install(TARGETS kritacomfyuiremote DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
install(FILES ComfyUIRemote.action DESTINATION ${KDE_INSTALL_DATADIR}/krita/actions)
