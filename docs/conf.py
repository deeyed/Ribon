from pathlib import Path

project = "Ribon"
author = "Ribon contributors"
copyright = "2026, Ribon contributors"

root_dir = Path(__file__).resolve().parents[1]

extensions = [
    "myst_parser",
    "breathe",
]

source_suffix = {
    ".md": "markdown",
    ".rst": "restructuredtext",
}

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "furo"
html_static_path = ["_static"]

breathe_projects = {
    "Ribon": str(root_dir / "build" / "docs" / "doxygen" / "xml"),
}
breathe_default_project = "Ribon"

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
]
