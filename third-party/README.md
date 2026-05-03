# Third-Party Dependencies

This directory is reserved for intentionally limited vendored or submodule dependencies.

Expected v1 candidates:

- Boost.Asio/Beast for sockets and HTTP.
- Boost.JSON for JSON parsing and encoding.
- GoogleTest for tests.

Do not add general-purpose dependencies here without updating `docs/architecture.md` and the root `CMakeLists.txt`.
