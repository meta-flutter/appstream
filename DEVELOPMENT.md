# Development

## Native build hook

The C++ FFI library (`libappstream.so`) is built by the Dart native-assets
build hook at `hook/build.dart`, driven automatically by `dart pub get` /
`flutter pub get` / `dart build`. The hook has three paths, selected by
`user_defines`:

- **Skip** (`skip_native_build`) — the hook does nothing. Use this when the
  embedding build system compiles `libappstream.so` itself (e.g. a bitbake
  recipe that drives `CMakeLists.txt` via `cmake.bbclass`) and only needs the
  Dart side from the hook.
- **Host / local development** (no user_defines) — `native_toolchain_c`'s
  `CBuilder` compiles the sources in `src/` directly with the auto-detected
  host toolchain. No configuration required.
- **Cross-compile** (`cmake_toolchain_file` / `cross_compile_env`) — when an
  embedding build system (e.g. a Yocto/OE SDK-driven build without bitbake)
  supplies a toolchain, the hook drives the project's `CMakeLists.txt`
  instead, so the cross compiler, sysroot, and flags are honored. See below.

Both paths produce `libappstream.so` and register the same code asset, so the
output is interchangeable. At runtime the library is located by filesystem
search (see `lib/src/bindings.dart`); the registered asset is what gets the
`.so` copied into the application bundle's `lib/` directory.

## Cross-compiling for a target (Yocto / OE SDK)

The Dart hooks runner executes build hooks under a semi-hermetic environment
allow-list (<https://dart.dev/tools/hooks#environment-variables>). Toolchain
variables exported in the parent shell — `CC`, `CXX`, `CFLAGS`, `CXXFLAGS`,
`--sysroot`, `OECORE_TARGET_SYSROOT`, `OECORE_TARGET_ARCH`,
`OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX`, `PKG_CONFIG_*`, etc. — are stripped
before the hook process starts and never reach the compiler it spawns. They
are therefore **dropped on a cross build** unless passed through a channel that
survives the boundary.

That channel is `user_defines`: values set there cross the hermetic boundary
and participate in cache-key invalidation (changing one re-runs the build). The
hook reads these, all keyed by this package's pubspec `name`
(`appstream_dart`):

| user_define | Type | Effect |
|---|---|---|
| `skip_native_build` | any (presence) | Hook does nothing. For build systems that compile `libappstream.so` themselves (e.g. a bitbake recipe using `cmake.bbclass`). |
| `cmake_toolchain_file` | string | Passed to CMake as `-DCMAKE_TOOLCHAIN_FILE=<path>`. |
| `cross_compile_env` | map of `{String: String}` | Re-exported into the `cmake` subprocess environment. |

Setting either of the latter two switches the hook to the CMake-driven cross path. The
`cross_compile_env` map is necessary in addition to the toolchain file because
a generated OE toolchain file (e.g. `OEToolchainConfig.cmake`) reads `CFLAGS` /
`CXXFLAGS` / `OECORE_TARGET_SYSROOT` / `OECORE_TARGET_ARCH` /
`OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX` directly via `$ENV{...}` at configure
time — passing only the toolchain file is not enough.

Supply these via a `pubspec_overrides.yaml` next to the consuming app's
`pubspec.yaml`:

```yaml
hooks:
  user_defines:
    appstream_dart:
      cmake_toolchain_file: /path/to/sysroots-components/.../OEToolchainConfig.cmake
      cross_compile_env:
        CC: "aarch64-poky-linux-gcc ... --sysroot=/path/to/sysroot"
        CXX: "aarch64-poky-linux-g++ ... --sysroot=/path/to/sysroot"
        CFLAGS: " -O2 -g -pipe ..."
        CXXFLAGS: " -O2 -g -pipe ..."
        LDFLAGS: " ..."
        OECORE_TARGET_SYSROOT: /path/to/sysroot
        OECORE_TARGET_ARCH: aarch64
        OE_CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX: ""
        PKG_CONFIG_SYSROOT_DIR: /path/to/sysroot
        PKG_CONFIG_PATH: /path/to/sysroot/usr/lib/pkgconfig
```

In an OE recipe, a `do_configure:prepend()` typically writes this file,
expanding the recipe's own `${CC}` / `${CFLAGS}` / `${STAGING_DIR_TARGET}` /
`${TARGET_ARCH}` etc. into the map.

Notes:

- The user_defines key is the pubspec **`name`** (`appstream_dart`), not the
  CMake project / library name (`appstream`).
- If a given Dart SDK does not honor `hooks: user_defines:` from
  `pubspec_overrides.yaml`, merge the same `user_defines` into the consuming
  app's `pubspec.yaml` directly (e.g. via `yq -i`) instead.

## CMake options

The root `CMakeLists.txt` exposes two knobs the build hook relies on; they are
also useful for manual builds:

| Option | Default | Purpose |
|---|---|---|
| `APPSTREAM_LIB_OUTPUT_DIR` | `${CMAKE_SOURCE_DIR}/lib` | Where `libappstream.so` is written. The cross path points it at a per-build output directory so nothing is written back into the source tree. |
| `APPSTREAM_BUILD_TESTS` | `ON` | Build the C++ unit tests. The cross path sets this `OFF` — the test binary is not needed for a target build and the GoogleTest `FetchContent` fallback would require network access. |

Example manual cross-style configure (host toolchain, tests off, redirected
output):

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAPPSTREAM_BUILD_TESTS=OFF \
  -DAPPSTREAM_LIB_OUTPUT_DIR=/tmp/out
cmake --build build --parallel
```

For the full host build/test workflow and the test-suite layout, see
[docs/BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md).