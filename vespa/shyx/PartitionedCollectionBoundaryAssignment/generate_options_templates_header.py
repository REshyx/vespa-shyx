from pathlib import Path

base = Path(__file__).resolve().parent
out = base / "vtkSHYXBoundaryAssignmentOptionsTemplates.h"

with out.open("w", encoding="utf-8", newline="\n") as f:
    f.write(
        "// Auto-synced from options_template_single_*.txt — keep in sync with those files.\n"
    )
    f.write("#ifndef vtkSHYXBoundaryAssignmentOptionsTemplates_h\n")
    f.write("#define vtkSHYXBoundaryAssignmentOptionsTemplates_h\n\n")
    f.write("namespace shyxBoundaryAssignmentOptionsTemplates\n{\n")
    for name, var in (
        ("options_template_single_inlet.txt", "kSingleInlet"),
        ("options_template_single_outlet.txt", "kSingleOutlet"),
    ):
        text = (base / name).read_text(encoding="utf-8")
        if not text.endswith("\n"):
            text += "\n"
        if ")SHYXOPT" in text:
            raise SystemExit(f"delimiter conflict in {name}")
        f.write(f"static constexpr const char* {var} = R\"SHYXOPT(\n")
        f.write(text)
        f.write(")SHYXOPT\";\n\n")
    f.write("} // namespace shyxBoundaryAssignmentOptionsTemplates\n\n")
    f.write("#endif\n")

print("wrote", out, "size", out.stat().st_size)
