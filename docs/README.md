# httpmorph Documentation

This directory contains the documentation for httpmorph, built with [Sphinx](https://www.sphinx-doc.org/).

## Building the Documentation Locally

### Prerequisites

Install the documentation dependencies:

```bash
pip install -r requirements.txt
```

### Build HTML Documentation

```bash
cd docs
make html
```

The built documentation will be in `build/html/`. Open `build/html/index.html` in your browser.

### Other Build Targets

```bash
make clean      # Remove built documentation
make help       # Show all available targets
```

## Documentation Structure

- `source/` - Documentation source files (reStructuredText)
  - `index.rst` - Main documentation page
  - `quickstart.rst` - Quick start guide
  - `api.rst` - API reference
  - `conf.py` - Sphinx configuration

## ReadTheDocs

The documentation is automatically built and hosted on ReadTheDocs when changes are pushed to the repository.

Configuration: `.readthedocs.yaml` in the project root

## Contributing to Documentation

Documentation contributions are welcome! Please:

1. Edit the `.rst` files in `source/`
2. Build locally to verify your changes: `make html`
3. Submit a pull request

## Current Status

This is an initial documentation release. Comprehensive documentation including:

- Detailed API reference
- Advanced usage guides
- Examples and tutorials
- Browser fingerprinting details
- Performance tuning guides
- HTTP/2 configuration

...will be added in future updates.
