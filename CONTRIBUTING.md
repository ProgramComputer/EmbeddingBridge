# Contributing to Embedding Bridge

We love your input! We want to make contributing to Embedding Bridge as easy and transparent as possible.

## Development Process

1. Fork the repo and create your branch from `main`.
2. Make your changes.
3. If you've added code, add tests.
4. Ensure the test suite passes.
5. Make sure your code follows the style guidelines.
6. Issue a pull request.

## Code Style

- Use 8-character indentation
- Maximum line length of 80 characters
- Clear and descriptive variable names
- Functions should do one thing and do it well
- Comprehensive error handling
- Comments should explain WHY, not WHAT

## Pull Request Process

1. Update the README.md with details of changes if needed.
2. Update the documentation if you're introducing new features.
3. The PR will be merged once you have the sign-off of at least one maintainer.

## Helper Scripts

The `scripts/` directory contains helper scripts to automate building and installing native dependencies:

- **build_aws.sh**: clones and builds all AWS C SDK libraries into `vendor/aws/install`.
- **build_arrow.sh**: downloads and builds Apache Arrow C++ (and optional GLib bindings via `--with-glib`), installing to `/usr/local` or the `ARROW_INSTALL_DIR` defined in this repository.
- **build_aws.md**: a human-readable guide describing the manual steps performed by `build_aws.sh`.

The Makefile provides targets that wrap these scripts:
```bash
make build-aws         # builds all AWS C libraries
make build-arrow       # builds Arrow C++ library only
make build-arrow-glib  # builds Arrow C++ with GLib bindings
```

Be sure to run these targets before running `make`, building tests, or contributing code that depends on AWS or Arrow.

## Debugging

To enable detailed debug output when running Embedding Bridge commands, set the following environment variables in your shell:

```sh
EB_DEBUG=1 EB_DEBUG_LEVEL=5 <your-command>
```

- `EB_DEBUG=1` enables debug output.
- `EB_DEBUG_LEVEL` controls verbosity:
  - `0` (none, no debug output)
  - `1` (error)
  - `2` (warn)
  - `3` (info)
  - `4` (debug)
  - `5` (trace, very verbose)

This can help diagnose issues during development or when submitting bug reports.

## License

By contributing, you agree that your contributions will be licensed under the GNU General Public License v2.0.

## Questions?

Feel free to open an issue for any questions or concerns. 