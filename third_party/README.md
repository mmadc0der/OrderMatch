# Third-party dependencies

The `third_party/` tree holds intentionally limited vendored or Git submodule dependencies.

Expected v1 candidates:

- Boost.Asio/Beast for sockets and HTTP.
- Boost.JSON for JSON parsing and encoding.
- GoogleTest for tests (submodule: `third_party/googletest`).

Do not add general-purpose dependencies under `third_party/` without updating `docs/architecture.md` and the root `CMakeLists.txt`.
