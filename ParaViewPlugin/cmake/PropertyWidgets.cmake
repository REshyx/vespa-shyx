# Custom PropertyGroup panel_widget classes for VESPAPlugin.
# Accumulates vespa_plugin_widget_interfaces / vespa_plugin_widget_sources.

set(vespa_plugin_widget_interfaces)
set(vespa_plugin_widget_sources)

macro(vespa_plugin_group_widget)
  cmake_parse_arguments(_vespa_pw "" "TYPE;CLASS_NAME" "FILES" ${ARGN})
  paraview_plugin_add_property_widget(
    KIND GROUP_WIDGET
    TYPE "${_vespa_pw_TYPE}"
    CLASS_NAME "${_vespa_pw_CLASS_NAME}"
    INTERFACES _vespa_pw_ifaces
    SOURCES _vespa_pw_srcs)
  list(APPEND vespa_plugin_widget_interfaces ${_vespa_pw_ifaces})
  list(APPEND vespa_plugin_widget_sources ${_vespa_pw_srcs} ${_vespa_pw_FILES})
  unset(_vespa_pw_ifaces)
  unset(_vespa_pw_srcs)
  unset(_vespa_pw_TYPE)
  unset(_vespa_pw_CLASS_NAME)
  unset(_vespa_pw_FILES)
endmacro()

vespa_plugin_group_widget(
  TYPE "array_curve_mapper_panel"
  CLASS_NAME pqArrayCurveMapperPanel
  FILES
    widgets/TransferCurve/pqArrayCurveMapperPanel.cxx
    widgets/TransferCurve/pqArrayCurveMapperPanel.h
    widgets/TransferCurve/pqSHYXTransferCurveWidget.cxx
    widgets/TransferCurve/pqSHYXTransferCurveWidget.h
    widgets/TransferCurve/vtkSHYXTransferChartXY.cxx
    widgets/TransferCurve/vtkSHYXTransferChartXY.h
    widgets/TransferCurve/vtkSHYXGrayHistogramPiecewiseFunctionItem.cxx
    widgets/TransferCurve/vtkSHYXGrayHistogramPiecewiseFunctionItem.h
    widgets/TransferCurve/vtkSHYXCompositeControlPointsItem.cxx
    widgets/TransferCurve/vtkSHYXCompositeControlPointsItem.h
    widgets/TransferCurve/vtkSHYXInputRangeHandlesItem.cxx
    widgets/TransferCurve/vtkSHYXInputRangeHandlesItem.h
    widgets/TransferCurve/vtkSHYXOutputRangeHandlesItem.cxx
    widgets/TransferCurve/vtkSHYXOutputRangeHandlesItem.h)

vespa_plugin_group_widget(
  TYPE "shyx_obb_interactive_box"
  CLASS_NAME pqSHYXOBBInteractiveBoxWidget
  FILES
    widgets/pqSHYXOBBInteractiveBoxWidget.cxx
    widgets/pqSHYXOBBInteractiveBoxWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_endclipper_plane_handles"
  CLASS_NAME pqSHYXEndClipperPlaneHandlesWidget
  FILES
    widgets/pqSHYXEndClipperPlaneHandlesWidget.cxx
    widgets/pqSHYXEndClipperPlaneHandlesWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_selection_plane_clipper"
  CLASS_NAME pqSHYXSelectionPlaneClipperWidget
  FILES
    widgets/pqSHYXSelectionPlaneClipperWidget.cxx
    widgets/pqSHYXSelectionPlaneClipperWidget.h)

vespa_plugin_group_widget(
  TYPE "vespa_shyx_vascular_stent_cylinder"
  CLASS_NAME pqSHYXVascularStentCylinderWidget
  FILES
    widgets/pqSHYXVascularStentCylinderWidget.cxx
    widgets/pqSHYXVascularStentCylinderWidget.h)

vespa_plugin_group_widget(
  TYPE "vespa_shyx_enhanced_ruler"
  CLASS_NAME pqSHYXEnhancedRulerWidget
  FILES
    widgets/pqSHYXEnhancedRulerWidget.cxx
    widgets/pqSHYXEnhancedRulerWidget.h)

vespa_plugin_group_widget(
  TYPE "vespa_shyx_endpoint_stent"
  CLASS_NAME pqSHYXEndpointStentWidget
  FILES
    widgets/pqSHYXEndpointStentWidget.cxx
    widgets/pqSHYXEndpointStentWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_partitioned_block_names"
  CLASS_NAME pqSHYXPartitionedBlockNamesWidget
  FILES
    widgets/pqSHYXPartitionedBlockNamesWidget.cxx
    widgets/pqSHYXPartitionedBlockNamesWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_selection_patch_table"
  CLASS_NAME pqSHYXSelectionPatchTableWidget
  FILES
    widgets/pqSHYXSelectionPatchTableWidget.cxx
    widgets/pqSHYXSelectionPatchTableWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_boundary_assignment_info"
  CLASS_NAME pqSHYXBoundaryAssignmentInfoWidget
  FILES
    widgets/pqSHYXBoundaryAssignmentInfoWidget.cxx
    widgets/pqSHYXBoundaryAssignmentInfoWidget.h)

vespa_plugin_group_widget(
  TYPE "shyx_remesh_uncapped_histogram"
  CLASS_NAME pqSHYXRemeshUncappedHistogramPanel
  FILES
    widgets/pqSHYXRemeshUncappedHistogramPanel.cxx
    widgets/pqSHYXRemeshUncappedHistogramPanel.h)

if (VESPA_USE_VMTK)
  vespa_plugin_group_widget(
    TYPE "shyx_openings_table"
    CLASS_NAME pqSHYXOpeningTable
    FILES
      widgets/pqSHYXOpeningTable.cxx
      widgets/pqSHYXOpeningTable.h)
endif()

if (VESPA_USE_SNAPPYHEXMESH)
  vespa_plugin_group_widget(
    TYPE "shyx_snappy_inside_points"
    CLASS_NAME pqSHYXSnappyInsidePointsWidget
    FILES
      widgets/SnappyHexMesh/pqSHYXSnappyInsidePointsWidget.cxx
      widgets/SnappyHexMesh/pqSHYXSnappyInsidePointsWidget.h)
  vespa_plugin_group_widget(
    TYPE "shyx_snappy_case_folder"
    CLASS_NAME pqSHYXSnappyCaseFolderWidget
    FILES
      widgets/SnappyHexMesh/pqSHYXSnappyCaseFolderWidget.cxx
      widgets/SnappyHexMesh/pqSHYXSnappyCaseFolderWidget.h)
  vespa_plugin_group_widget(
    TYPE "shyx_snappy_patch_table"
    CLASS_NAME pqSHYXSnappyPatchTableWidget
    FILES
      widgets/SnappyHexMesh/pqSHYXSnappyPatchTableWidget.cxx
      widgets/SnappyHexMesh/pqSHYXSnappyPatchTableWidget.h)
  vespa_plugin_group_widget(
    TYPE "shyx_snappy_castellated"
    CLASS_NAME pqSHYXSnappyCastellatedWidget
    FILES
      widgets/SnappyHexMesh/pqSHYXSnappyCastellatedWidget.cxx
      widgets/SnappyHexMesh/pqSHYXSnappyCastellatedWidget.h)
endif()
