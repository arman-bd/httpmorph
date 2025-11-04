"""
httpmorph test suite
"""

# Read version from package metadata (single source of truth: pyproject.toml)
try:
    from importlib.metadata import version as _get_version
except ImportError:
    # Python < 3.8
    from importlib_metadata import version as _get_version

__version__ = _get_version("httpmorph")
