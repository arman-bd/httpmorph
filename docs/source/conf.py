# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import os
import sys

sys.path.insert(0, os.path.abspath("../.."))

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "httpmorph"
copyright = "2025, Arman Hossain"
author = "Arman Hossain"

# Read version from package metadata (single source of truth: pyproject.toml)
try:
    from importlib.metadata import version as _get_version
except ImportError:
    # Python < 3.8
    from importlib_metadata import version as _get_version

release = _get_version("httpmorph")

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.viewcode",
    "sphinx.ext.napoleon",
    "myst_parser",
]

templates_path = ["_templates"]
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]

# Theme options for Read the Docs theme
html_theme_options = {
    "display_version": True,
    "prev_next_buttons_location": "bottom",
    "style_external_links": False,
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
}

# Add author contact information
html_context = {
    "display_github": True,
    "github_user": "arman-bd",
    "github_repo": "httpmorph",
    "github_version": "main",
    "conf_py_path": "/docs/source/",
}

# Additional metadata
html_show_sourcelink = True
html_show_sphinx = True
html_show_copyright = True

# -- Options for autodoc -----------------------------------------------------
autodoc_member_order = "bysource"
autodoc_typehints = "description"
