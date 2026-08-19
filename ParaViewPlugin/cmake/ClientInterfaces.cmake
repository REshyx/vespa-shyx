# Auto-start, dock windows, and context menus for VESPAPlugin.
# Accumulates vespa_plugin_qt_auto_interfaces / vespa_plugin_qt_auto_sources
# and shyx_ai_assistant_interfaces / shyx_ai_assistant_sources.

set(vespa_plugin_qt_auto_interfaces)
set(vespa_plugin_qt_auto_sources)
set(shyx_ai_assistant_interfaces)
set(shyx_ai_assistant_sources)

if (NOT PARAVIEW_USE_QT)
  return()
endif()

paraview_plugin_add_auto_start(
  CLASS_NAME "pqPulseGlyphAnimationManager"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES pulse_glyph_autostart_interfaces
  SOURCES pulse_glyph_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${pulse_glyph_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${pulse_glyph_autostart_sources}
  Representations/pqPulseGlyphAnimationManager.cxx
  Representations/pqPulseGlyphAnimationManager.h)

paraview_plugin_add_auto_start(
  CLASS_NAME "pqAnimatedStreamlineAnimationManager"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES animated_streamline_autostart_interfaces
  SOURCES animated_streamline_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${animated_streamline_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${animated_streamline_autostart_sources}
  Representations/pqAnimatedStreamlineAnimationManager.cxx
  Representations/pqAnimatedStreamlineAnimationManager.h)

paraview_plugin_add_auto_start(
  CLASS_NAME "pqSHYXSphereSelectionAutoStart"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES sphere_selection_autostart_interfaces
  SOURCES sphere_selection_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${sphere_selection_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${sphere_selection_autostart_sources}
  selection/SphereSelection/pqSHYXSphereSelectionAutoStart.cxx
  selection/SphereSelection/pqSHYXSphereSelectionAutoStart.h
  selection/SphereSelection/pqSHYXSphereSelectionViewFrameActions.cxx
  selection/SphereSelection/pqSHYXSphereSelectionViewFrameActions.h
  selection/SphereSelection/pqSHYXSphereSelectionController.cxx
  selection/SphereSelection/pqSHYXSphereSelectionController.h)

paraview_plugin_add_auto_start(
  CLASS_NAME "pqSHYXGrowSelectionWithSimilarAutoStart"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES grow_selection_similar_autostart_interfaces
  SOURCES grow_selection_similar_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${grow_selection_similar_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${grow_selection_similar_autostart_sources}
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarAutoStart.cxx
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarAutoStart.h
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarViewFrameActions.cxx
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarViewFrameActions.h
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarController.cxx
  selection/GrowSelectionWithSimilar/pqSHYXGrowSelectionWithSimilarController.h)

paraview_plugin_add_auto_start(
  CLASS_NAME "pqSHYXVascularCategoryAutoStart"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES vascular_category_autostart_interfaces
  SOURCES vascular_category_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${vascular_category_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${vascular_category_autostart_sources}
  VascularCategory/pqSHYXVascularCategoryAutoStart.cxx
  VascularCategory/pqSHYXVascularCategoryAutoStart.h)

paraview_plugin_add_auto_start(
  CLASS_NAME "pqSHYXAIAssistantAutoStart"
  STARTUP onStartup
  SHUTDOWN onShutdown
  INTERFACES shyx_ai_autostart_interfaces
  SOURCES shyx_ai_autostart_sources)
list(APPEND vespa_plugin_qt_auto_interfaces ${shyx_ai_autostart_interfaces})
list(APPEND vespa_plugin_qt_auto_sources
  ${shyx_ai_autostart_sources}
  AIAssistant/pqSHYXAIAssistantAutoStart.cxx
  AIAssistant/pqSHYXAIAssistantAutoStart.h)

list(APPEND vespa_plugin_qt_auto_interfaces pqSHYXSelectBlockContextMenu)
list(APPEND vespa_plugin_qt_auto_sources
  selection/SelectBlock/pqSHYXSelectBlockContextMenu.cxx
  selection/SelectBlock/pqSHYXSelectBlockContextMenu.h)

list(APPEND vespa_plugin_qt_auto_interfaces pqSHYXSelectSimilarContextMenu)
list(APPEND vespa_plugin_qt_auto_sources
  selection/SelectSimilar/pqSHYXSelectSimilarContextMenu.cxx
  selection/SelectSimilar/pqSHYXSelectSimilarContextMenu.h)

paraview_plugin_add_dock_window(
  CLASS_NAME pqSHYXAIAssistantPanel
  DOCK_AREA Right
  INTERFACES shyx_ai_assistant_interfaces
  SOURCES shyx_ai_assistant_sources)
list(APPEND shyx_ai_assistant_sources
  AIAssistant/pqSHYXAIAssistantPanel.cxx
  AIAssistant/pqSHYXAIAssistantPanel.h
  AIAssistant/pqSHYXCurlRequest.cxx
  AIAssistant/pqSHYXCurlRequest.h
  AIAssistant/pqSHYXAIChatView.cxx
  AIAssistant/pqSHYXAIChatView.h
  AIAssistant/pqSHYXAIImageAnnotator.cxx
  AIAssistant/pqSHYXAIImageAnnotator.h
  AIAssistant/pqSHYXAIAgentTools.cxx
  AIAssistant/pqSHYXAIAgentTools.h
  AIAssistant/pqSHYXPythonSyntaxHighlighter.cxx
  AIAssistant/pqSHYXPythonSyntaxHighlighter.h
  AIAssistant/pqSHYXAIOutputLog.cxx
  AIAssistant/pqSHYXAIOutputLog.h)
