# FrnlCPP upstream parity ledger

## Authority and scope

- Canonical FrnlTS revision: a9339b9e92f9ef375ac164e274842644dca60d8a
- Foundation GutCPP revision: 443ad9397c081dd596e784c50cff14c1ecad95c6
- FrnlCPP baseline main: b6feb7ff8111c4ccbeb8e53722b935247e68b2e1
- Native minimum: C++17. Semantic core is header-only and uses only the C++ standard library.
- Runtime dependencies on GutCPP, Qt, SDL, React, Floks, Skipo or KrnlCPP: none.
  No GutCPP APIs are consumed in this release, so the outstanding Ettin tokenizer is irrelevant.
- Existing event regression suite remains in include/frnl/event_dispatch.hpp and tests/*.
- This is a **partial headless native implementation**, not a claim of complete FrnlTS exported API parity.
  Platform-specific React bindings are intentionally not reimplemented.

## Native public API inventory

| FrnlTS source at pinned SHA | Native API / implementation | Contract coverage | Fixture |
| --- | --- | --- | --- |
| src/runtime/core.ts (LifecyclePhase, canTransition, createLifecycle) | include/frnl/lifecycle.hpp: LifecyclePhase, can_transition, Lifecycle, LifecycleSubscription | Implemented: nine phases, legal edges, no-op self-transition, RAII snapshot listeners | tests/lifecycle_test.cpp |
| src/runtime/core.ts (contracts, capabilities, fault, registry helpers) | — | Missing: typed contracts, capabilities, structured faults | — |
| src/runtime/component-definition.ts | semantic_runtime.hpp: ComponentDefinition, StateDefinition, PhaseDurations, register_component | Partial: identity, named states, semantic event mapping and phase durations; no Zod schema | tests/semantic_runtime_test.cpp |
| src/runtime/events.ts | event_dispatch.hpp: EventDispatcher; semantic_runtime.hpp: invoke, semantic_input, on_event | Partial: state-scoped resolution, nested/reentrant listener snapshots, legacy handlers, scoped invocation; no schema-backed payloads | tests/event_dispatch_test.cpp, tests/listener_growth_nested_dispatch_test.cpp, tests/semantic_runtime_test.cpp |
| src/runtime/store.ts | semantic_runtime.hpp: component tree, local/page data, properties and state; slots.hpp: SlotModel | Implemented headless slot occupant, properties, repeatable collection changes and occupant state observation; no Zustand store | tests/semantic_runtime_test.cpp, tests/slots_test.cpp |
| src/runtime/themes.ts | semantic_runtime.hpp: register_theme, apply_theme, clear_theme, visual_parameters | Partial: state/definition theme map, inherited nearest ancestor, override precedence | tests/semantic_runtime_test.cpp |
| src/runtime/orchestration.ts | semantic_runtime.hpp: register_flow, execute_flow, FlowStep | Partial: deterministic acyclic dependency ordering, callbacks, nested-error context; no asynchronous orchestration engine | tests/semantic_runtime_test.cpp |
| src/runtime/libraries.ts | semantic_runtime.hpp: register_library, call_library | Partial: host function injection without library lifecycle/namespace policies | tests/semantic_runtime_test.cpp |
| src/runtime/semantic-input.ts | semantic_runtime.hpp: semantic_input; input_adapters.hpp: InputAdapter | Partial: input binding, validation and transformation before state event routing; natural-language inference and domain schemas stay with host/GutCPP | tests/semantic_runtime_test.cpp, tests/input_adapters_test.cpp |
| src/runtime/registry.ts | include/frnl/registry.hpp: Registry, Contract, Capability | Implemented core: versioned entries, downgrade rejection, typed resolve, capability requirements; no JS dynamic registry objects | tests/registry_test.cpp |
| src/runtime/dependencies.ts | include/frnl/registry.hpp: DependencyGraph; FlowStep dependencies | Implemented: capability provider lookup, missing dependency diagnostics, deterministic topological order and cycle rejection | tests/registry_test.cpp, tests/semantic_runtime_test.cpp |
| src/runtime/validation.ts | register_component / register_flow preconditions; diagnostics | Partial: native fail-fast validation and missing handler diagnostics; no Zod parity | tests/semantic_runtime_test.cpp |
| src/runtime/resources.ts | — | Missing | — |
| src/runtime/routing.ts | — | Missing | — |
| src/runtime/accessibility.ts | ComponentDefinition presentation_role/presentation_label | Partial metadata only, no accessibility resolution engine | tests/consumer/main.cpp |
| src/runtime/presentation.ts | ComponentDefinition presentation metadata, Update | Partial metadata and renderer-neutral notifications only | tests/semantic_runtime_test.cpp |
| src/runtime/frontend-api.ts | — | Missing: complete facade methods | — |
| src/runtime/facade.ts | — | Missing | — |
| src/runtime/measurement.ts | — | Missing: geometry and measurement adapters | — |
| src/runtime/ui-session.ts | — | Missing: UI session abstraction | — |
| src/react/* and React hooks | — | Deliberately omitted: renderer-neutral native project, no fake React | — |
| packages/cli/* and packages/validate/* | CMake/CTest + static native validators | Partial: no source generators or JS/TS validators | CMakeLists.txt, CI |

## Semantics and known differences

1. Nodes cannot be registered with an unknown parent; paths verify exact ancestry. A parent cannot be removed while it retains children. Existing PR #1 also supplies a separate ComponentScope type. It must be separately reconciled before merging.
2. State transition Release uses the current state's finite release duration, then enters the target state's finite Attack and looping Sustain. A host calls advance()/advance_all() using a monotonic delta. This tracks phase time, not the upstream property-curve evaluator.
3. State-aware event handlers are invoked synchronously and may reenter. Listener snapshots are captured before handler calls; listeners added during a nested invocation observe the nested call, not the already-started parent dispatch. Exceptions propagate without fabricated success, and state mutations are not rolled back.
4. Theme visuals resolve as base state parameters, then inherited theme values, then explicit per-component overrides. No renderer or widget toolkit is embedded.
5. Values in renderer properties use a bounded variant; local/page state and event values use std::any. Persistence is explicitly supplied by the host and is not automatically serializable.
6. Flows reject unknown dependencies and cycles before registration, execute the topological order, and nest thrown errors with step identity. They are synchronous; asynchronous steps must be delegated through a host.
7. No UI thread, timer, event loop or device is created or owned. HostPorts supplies a UI scheduler, monotonic clock and persistence callbacks. The host owns thread-affinity and device adapters.

## Evidence and release gate

- Baseline regression: CMakeLists.txt registered 18 event dispatch tests on main (including listener disposal and nested growth). PR #3's previously unmerged nested-listener-growth regression has been incorporated into the new branch without altering PR #3.
- Initial Wave 2 branch compile gate **failed** on GCC 15.2.0 with -Werror=missing-field-initializers for SemanticRuntime::Node; corrected by explicitly initializing all Node fields in a follow-up commit. Do not certify against the failed SHA.
- Required evidence: GCC and Clang Debug/Release, GCC and Clang AddressSanitizer/UndefinedBehaviorSanitizer, original reentrancy tests, new semantic/lifecycle tests, installed external consumer and exact-head main verification.
- CI entry point: .github/workflows/event-dispatch-ci.yml.
- External consumer: tests/consumer, linking exported FrnlCPP::semantic through find_package(FrnlCPP CONFIG REQUIRED).
- PRs #1 and #3 remain independent competing branches; do not force-push or overwrite them.
- The library is not fully source-parity-complete while the missing rows above remain. Do not mark the global C++ parity goal complete solely on this PR.
- Additional fixtures: tests/registry_test.cpp, tests/input_adapters_test.cpp, tests/slots_test.cpp. The external input/slot adapters hold references to SemanticRuntime, so the embedding host must destroy these adapters before the underlying runtime.
