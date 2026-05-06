# Optional regeneration helper for custom_interpolated_corrected_curvatures.h from CGAL 6.1 upstream.

from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "custom_interpolated_corrected_curvatures.impl.tmp.h"
DST = ROOT / "custom_interpolated_corrected_curvatures.h"

CGAL_ICC_URL = (
    "https://raw.githubusercontent.com/CGAL/cgal/v6.1/"
    "Polygon_mesh_processing/include/CGAL/Polygon_mesh_processing/interpolated_corrected_curvatures.h"
)


def fetch_upstream_if_missing() -> None:
    if SRC.exists():
        return
    with urlopen(CGAL_ICC_URL, timeout=60) as r:
        SRC.write_bytes(r.read())

def main() -> None:
    fetch_upstream_if_missing()
    text = SRC.read_text(encoding="utf-8")
    text = text.replace(
        "#include <CGAL/license/Polygon_mesh_processing/interpolated_corrected_curvatures.h>\n\n",
        "",
    )
    text = text.replace(
        "#ifndef CGAL_POLYGON_MESH_PROCESSING_INTERPOLATED_CORRECTED_CURVATURES_H\n"
        "#define CGAL_POLYGON_MESH_PROCESSING_INTERPOLATED_CORRECTED_CURVATURES_H\n",
        "#ifndef VESPA_SHYX_CUSTOM_INTERPOLATED_CORRECTED_CURVATURES_H\n"
        "#define VESPA_SHYX_CUSTOM_INTERPOLATED_CORRECTED_CURVATURES_H\n"
        "\n"
        "// Fork of CGAL 6.1.x Polygon_mesh_processing/interpolated_corrected_curvatures.h\n"
        "// for local experimentation. Original copyright GeometryFactory / CGAL.\n"
        "// Renamed API: custom_interpolated_corrected_curvatures,\n"
        "// Custom_principal_curvatures_and_directions (see namespace vespa_shyx).\n"
        "\n",
    )
    text = text.replace(
        "#endif // CGAL_POLYGON_MESH_PROCESSING_INTERPOLATED_CORRECTED_CURVATURES_H",
        "#endif // VESPA_SHYX_CUSTOM_INTERPOLATED_CORRECTED_CURVATURES_H",
    )
    text = text.replace(
        "#include <CGAL/boost/graph/named_params_helper.h>\n#include <Eigen/Eigenvalues>",
        "#include <CGAL/boost/graph/named_params_helper.h>\n#include <CGAL/boost/graph/helpers.h>\n"
        "#include <CGAL/boost/graph/iterator.h>\n#include <CGAL/number_utils.h>\n#include <Eigen/Eigenvalues>",
    )

    text = text.replace(
        "namespace CGAL {\n\nnamespace Polygon_mesh_processing {",
        "namespace vespa_shyx {\n\n"
        "using CGAL::Polygon_mesh_processing::average_edge_length;\n"
        "using CGAL::Polygon_mesh_processing::compute_vertex_normals;\n"
        "using CGAL::faces;\n"
        "using CGAL::faces_around_face;\n"
        "using CGAL::faces_around_target;\n"
        "using CGAL::halfedge;\n"
        "using CGAL::is_negative;\n"
        "using CGAL::is_zero;\n"
        "using CGAL::vertices;\n"
        "using CGAL::vertices_around_face;\n\n",
    )
    text = text.replace(
        "} // namespace Polygon_mesh_processing\n} // namespace CGAL",
        "} // namespace vespa_shyx",
    )

    text = text.replace(
        "Principal_curvatures_and_directions",
        "Custom_principal_curvatures_and_directions",
    )
    text = text.replace(
        "void interpolated_corrected_curvatures(",
        "void custom_interpolated_corrected_curvatures(",
    )
    text = text.replace(
        "void interpolated_corrected_curvatures_one_vertex(",
        "void custom_interpolated_corrected_curvatures_one_vertex(",
    )
    text = text.replace(
        "Interpolated_corrected_curvatures_computer",
        "Custom_interpolated_corrected_curvatures_computer",
    )
    text = text.replace(
        "internal::interpolated_corrected_curvatures_one_vertex",
        "internal::custom_interpolated_corrected_curvatures_one_vertex",
    )

    text = text.replace(
        "parameters::choose_parameter",
        "CGAL::Polygon_mesh_processing::parameters::choose_parameter",
    )
    text = text.replace(
        "parameters::get_parameter",
        "CGAL::Polygon_mesh_processing::parameters::get_parameter",
    )
    text = text.replace(
        "parameters::is_default_parameter",
        "CGAL::Polygon_mesh_processing::parameters::is_default_parameter",
    )
    text = text.replace(
        "parameters::default_values()",
        "CGAL::Polygon_mesh_processing::parameters::default_values()",
    )

    text = text.replace("internal_np::", "CGAL::internal_np::")
    text = text.replace("typename GetGeomTraits<", "typename CGAL::GetGeomTraits<")
    text = text.replace("typename GetVertexPointMap<", "typename CGAL::GetVertexPointMap<")
    text = text.replace("Constant_property_map<", "CGAL::Constant_property_map<")
    text = text.replace("dynamic_vertex_property_t<", "CGAL::dynamic_vertex_property_t<")
    text = text.replace("get_const_property_map(", "CGAL::get_const_property_map(")

    DST.write_text(text, encoding="utf-8")

if __name__ == "__main__":
    main()
