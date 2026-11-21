# FrnlCPP

A **renderer-neutral C++17 semantic frontend kernel** based on the FrnlTS contracts. Native renderers, game engines, desktop event loops and embedded hosts can attach their own presentation systems without a React compatibility layer.

## Current API

- `<frnl/semantic_runtime.hpp>`: component definitions and ancestry, state transitions and Attack/Sustain/Release timing, state-dependent events, component/page data, theme inheritance, visual properties, synchronous flow DAGs, host scheduler/clock/persistence interfaces, observer subscriptions and diagnostics.
- `<frnl/lifecycle.hpp>`: nine-state Frnl lifecycle machine with safe listener subscriptions and legal transitions.
- `<frnl/registry.hpp>`: versioned services, capabilities, capability dependency graph and deterministic cycle/requirement diagnostics.
- `<frnl/input_adapters.hpp>`: external host input binding, transformation and validation into semantic dispatch.
- `<frnl/slots.hpp>`: slot occupants and repeatable collections with their own property/state event boundary.
- `<frnl/event_dispatch.hpp>`: the original event dispatcher and existing reentrancy regressions.

The project is header-only. It uses only the C++ standard library and has no required graphics, browser, Qt, SDL, KrnlCPP, SkipoCPP, FloksCPP or GutCPP runtime dependency.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake -S . -B sanitize -DFRNLCPP_SANITIZERS=ON
cmake --build sanitize --parallel
ctest --test-dir sanitize --output-on-failure
```

GitHub Actions runs strict GCC/Clang Debug and Release, GCC/Clang AddressSanitizer + UndefinedBehaviorSanitizer, and external consumer builds.

## Installed consumer

```sh
cmake -S . -B install-build -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$PWD/prefix"
cmake --install install-build
cmake -S tests/consumer -B consumer-build -DCMAKE_PREFIX_PATH="$PWD/prefix"
cmake --build consumer-build
./consumer-build/frnlcpp_consumer
```

External CMake project:

```cmake
find_package(FrnlCPP CONFIG REQUIRED)
target_link_libraries(my_native_app PRIVATE FrnlCPP::semantic)
```

## Runtime example

```cpp
#include <frnl/semantic_runtime.hpp>
frnl::SemanticRuntime runtime;
frnl::ComponentDefinition definition;
definition.name = "Button";
definition.states["idle"].events["click"] = "on_click";
runtime.register_component("button", definition, "idle");
runtime.register_handler("on_click",
  [](frnl::SemanticRuntime&, const std::string&, const std::any&) {
    return std::any(std::string("ok"));
  });
const auto value = std::any_cast<std::string>(runtime.invoke("button", "click"));
```

In an embedding application, hold a `SemanticRuntime` for the lifetime of its `InputAdapter` and `SlotModel`; the latter do not own the runtime. `Subscription` and lifecycle tokens may outlive the emitter safely.

## Portability and parity

The semantic core has no graphics backend. HostPorts delegates the UI thread scheduler, monotonic clock and persistence to the application; device adapters and AI-driven natural language interpretation remain external. **Do not equate this kernel with complete FrnlTS source export parity**: asynchronous resources, measurement, entire frontend facade, UI sessions, TypeScript CLI and specialized React hooks are not included.

See [docs/upstream-parity.md](docs/upstream-parity.md) for pinned authoritative revisions, implementation mapping, exact fixtures and known gaps.
