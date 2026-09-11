# AYUI Flow Contract

Status: **Stage 8 executable graph pipeline implemented** (2026-09-11).

UI Flow describes application-level UI orchestration. It does not replace a
`*.ui.json` layout: a layout owns one Widget tree, while a `*.uiflow.json`
document decides which layouts are present, where they are layered, how their
lifetimes are scoped, and which signals may change the presentation.

Stage 1 delivered `UIFlowDocument`, validation, JSON round-trip, project
references, and legacy migration. Stage 2 added the optional `AYApplicationUI`
runtime target: application-owned orchestration, real Widget-tree mounting,
Context/Slot arbitration, Scope cleanup, typed signals/actions, parallel state
regions, and an extension boundary for action-graph execution. Stage 3 adds
World lifecycle binding, serializable generic Scene signal components, and an
explicit signal entry point for physics, script, task, or custom interaction
systems. Stage 4 adds the independent AYEditor Flow authoring window and a
logical live preview backed by the production runtime. Stage 5 closes the
production execution path with asynchronous graph interruption, precise input
fall-through, Screen animation handoff, transactional Flow reload, replayable
diagnostics, reduced-motion behavior, and a real Widget-tree editor preview.
Stage 6 adds a deployable Screen/layout/animation asset closure. Stage 7 connects
declarative Widget events to Flow Signals and defines host-extensible Graph
node/pin metadata for typed authoring and strict opt-in validation. Stage 8 adds
the production command-graph executor in AYApplication and makes the editor
preview execute that same pipeline with typed ports and links.

## Ownership and dependency boundary

The dependency direction is fixed:

```text
AYUI
  UIFlow data types + validation + .uiflow.json serializer
      ↑
AYApplication / game host
  persistent UIRuntime, signal sources, action executors, World/owner lifetime
      ↑
AYEditor
  project descriptor, migration, Flow authoring, validation and preview
```

- AYUI contains no Scene, World, Entity, gameplay, `EngineAsset`, or editor
  semantics. Host-defined action-node types remain string IDs.
- AYApplication or a game composition root owns the persistent runtime. It may
  outlive every World and is responsible for mounting layouts into UIManager.
- Scene systems publish ordinary signals such as `player.enteredRegion`; they
  do not directly find or mutate Widgets.
- AYEditor reads and writes the same contract consumed by runtime. Preview may
  provide mock signal payloads and action executors, but cannot define a second
  wire format.

`UIFlowLayerDefinition` is a logical presentation layer and must not be confused
with `IRenderBackend::LayerHandle` or Widget `LayerCachePolicy`. The former
controls composition/input semantics; the latter is a renderer pixel cache.

## Core concepts

| Concept | Purpose |
|---|---|
| Screen | A named `*.ui.json` layout plus layer, slot, lifetime, animations, parameters and Widget-event mappings. |
| Layer | Global visual order and lower-layer input policy. |
| Slot | A replacement channel within a Layer, for example `hud.primary` or `modal`. |
| Scope | Lifetime boundary: `application`, `world`, `owner`, or `transient`. |
| Context | A reusable set of slot presentations. Contexts are suitable for location, gameplay mode, cutscene, inventory, and accessibility overlays. |
| Entry | Initial contexts and an optional startup action graph. It can be selected before a World exists. |
| Signal | A typed observation from UI, Scene, game, or platform code. |
| Action | A typed host capability which a graph may request. |
| Region | An independent state-machine region. Multiple regions run in parallel, avoiding a Cartesian product of HUD/story/modal states. |
| State | A flat-ID hierarchical state with optional parent, initial child, contexts and enter/exit graphs. |
| Transition | A signal-triggered state change with guard, priority, action graph and interruption policy. |
| Graph | Extensible nodes and pin links. Unknown node types and recursive JSON properties round-trip without AYUI understanding their behavior. |

The runtime uses a Context stack per Slot. Higher-priority Contexts
override lower-priority assignments; removing an override reveals the previous
assignment when `restorePrevious` is true. A World change removes World-scoped
instances only. Application-scoped menus and loading screens remain alive, and
Owner-scoped UI can follow a specific entity or gameplay owner.

## Stage 2 runtime

`AYApplicationUI` is built only when the `AYUI` target exists. Core
`AYApplication` remains usable by headless/server configurations without a UI
dependency. The principal types are:

- `UIFlowRuntime`: persistent logical state owned by the Application layer.
- `IUIFlowScreenHost`: narrow presentation adapter used by headless tests,
  native views, or Widget hosts.
- `UIManagerFlowScreenHost`: production adapter which mounts each Screen with
  an independent `UILayoutLoader` under ordered Flow Layer widgets.
- `UIFlowRuntimeModule`: unscaled Presentation-phase subsystem which publishes
  `UIFlowRuntime*` as `kHostServiceUIFlowRuntime`.

The normal startup order is: initialize and size `UIManager`, load its host
root, construct `UIManagerFlowScreenHost`, add `UIFlowRuntimeModule` to the
Application module graph, then let GameLoop initialize the subsystem. The
adapter owns the Flow subtree and attaches it as an externally-owned child of
the current manager root. Replacing that root therefore detaches rather than
destroys mounted Screens; the next host update reattaches the same subtree.
Per-Screen file hot reload remains independent and is handled by the adapter.

`load()` validates the complete contract before replacing a running document.
`start()` selects an explicit Entry or `defaultEntry`, enters every Region's
initial leaf, then reconciles the desired Screens. Reconciliation is
presentation-transactional: all new layouts mount first; old layouts unmount
only after every new mount succeeds. A failed load therefore preserves the
previous visible presentation.

Slot candidates are sorted by Context priority and then activation serial.
`capacity` keeps the highest N distinct Screen/scope identities. A highest
priority `Hide` assignment suppresses the whole Slot. When an override leaves,
`restorePrevious=true` reveals the prior candidate; `false` advances a restore
floor so older candidates remain suppressed until activated again. Layer order
and order within a Layer are forwarded to the Screen host.

Application Scope is permanent. `beginScope`/`endScope` switch World or Owner
keys, remove Context activations bound to the ending key, and reconcile only
the affected Screens. World-scoped Screens remain dormant until a World key is
active; application Screens survive World changes; transient identity is tied
to a Context activation. A manual Context returns a stable handle for explicit
deactivation.

Signals are schema-checked, default-filled, and serialized
through a re-entrant queue with a 1024-event safety limit. Each parallel Region
chooses its highest-priority matching Transition independently. Guards are
delegated to a host evaluator. Enter/exit/transition Graphs are emitted through
`setGraphRequestHandler` or `setAsyncGraphRequestHandler`; node execution stays
host/plugin-defined, so AYUI does not acquire gameplay semantics. The synchronous
handler remains the compatibility path. With the asynchronous handler, a
transition advances through exit, transition, and enter graphs only after the
host completes each returned execution ID via `completeGraphExecution()`.
Handlers must return before completing that ID; completion is intentionally a
later callback so the runtime cannot be re-entered while installing the pending
execution record.

`queue`, `coalesce`, `ignoreIfRunning`, `cancelPrevious`, and `reversePrevious`
are enforced per Region. Queue preserves every deferred transition; coalesce
retains only the newest; ignore drops it; cancel/reverse call the host interrupt
handler and replace the old pipeline. Logical State and mounted presentation
commit before asynchronous graph work begins, matching the existing
non-rollback graph contract. Runtime trace entries and the bounded replay Signal
log expose the exact decision sequence for diagnostics and deterministic replay.
If the host rejects a graph start, the failed Region's deferred transitions are
discarded so stale work cannot leak into a later, unrelated pipeline.

The Action registry validates declared inputs and applies defaults before
calling the registered host capability. Unknown actions, missing handlers,
wrong types, and handler exceptions return `UIFlowActionResult::failure`.
Screen-host, guard, graph, and signal-listener exceptions are contained at the
runtime boundary.

A Screen can map a semantic handler name used by its layout to a declared Flow
Signal. `UILayoutLoader` first resolves explicitly registered controller/global
handlers, then invokes its dynamic declarative-event resolver. The production
Screen host installs that resolver per mounted Screen, so a layout such as
`"events":{"onClick":"startGame"}` can emit the Signal mapped by
`{"handler":"startGame","signal":"ui.startGame"}` without game code finding
the Button by ID. Command events currently carry no dynamic Widget value;
therefore every required payload field on such a Signal must have a default.

For Widget input, Layer order is painter order. `blockLower` (or
`blocksLowerInput`) captures the transparent Layer surface. `passThrough`
ignores a viewport-sized Screen root's blank background while preserving hits
on interactive descendants. `consumeHandled` first offers the event to the top
target and retries lower siblings only when it remains unhandled. The retry is
bounded by explicit container capabilities, so ordinary overlapping controls do
not accidentally acquire click-through behavior and `blockLower` remains a hard
boundary.

`UIManagerFlowScreenHost` validates named enter/exit clips before mounting,
plays enter clips immediately, and retains an unmounted Screen until its exit
clip completes. The same `AnimationTimeline` honors the process-wide
`AnimationSettings` reduced-motion provider; when reduced motion is active the
handoff completes synchronously without maintaining a retiring Screen.
`reload()` validates and reconciles a replacement Flow transactionally while
preserving compatible manual Context handles and active Region state. It rejects
reload during an in-flight asynchronous graph rather than creating a mixed
document pipeline.

## Stage 3 Scene bridge

`UIFlowSceneBridge` is an optional Application integration layer. It subscribes
to the existing Scene lifecycle events, opens and closes the runtime World
Scope, activates the Context mapped to the stable World key, and can emit the
declared `scene.currentChanged`, `scene.beginPlay`, and `scene.endPlay`
notifications. Lifecycle notifications are optional: an undeclared lifecycle
Signal is skipped so a Flow is not forced to model events it does not use.

`UIFlowSceneBridgeModule` runs in `FramePhase::World`, after Entity processing,
and publishes the non-owning `UIFlowSceneBridge*` host service. When
`GameWorldRouter` is installed, the bridge is ordered after it and resolves a
transition from `pendingWorldId()` before falling back to `currentWorldId()`.
Projects without a router can supply `worldKeyResolver`; otherwise the bridge
uses the Scene path, Scene name, and finally a process-local identity fallback.
Every configured World key and Context ID is validated when the bridge starts.

The optional authored interaction source consists of two ordinary, scene-
serializable Entity components:

- `SceneSignalVolumeComponent` declares an axis-aligned region, enter/exit
  Signal IDs, source ID, optional participant tag, offset, enabled state, and
  `emitOncePerWorld` policy.
- `SceneSignalParticipantComponent` marks an Entity position as a participant
  and provides an optional exact-match tag.

The Stage 3 detector applies Transform position and absolute scale to the
volume but intentionally ignores rotation. It performs an O(volumes ×
participants) scan suitable for authored UI regions and deterministic tests;
large or rotated trigger sets should use a physics/broadphase system and call
`emitSceneSignal()` through the same bridge. No Scene component stores a Widget,
Screen, Context, or gameplay-specific type. Standard payload fields include
`world`, `scene`, `path`, `mode`, `sourceId`, `sourceEntity`, `entity`, and
`participantTag`; additional declared fields can be supplied by the caller.

A typical composition root adds `UIFlowRuntimeModule` first, then adds
`UIFlowSceneBridgeModule` with World-to-Context mappings. The module dependency
graph guarantees runtime and Entity services are available; Scene code still
publishes generic signals and never searches for or mutates Widgets.

## Version 1 JSON shape

All references are stable IDs. Asset paths are portable project paths and use
forward slashes.

```json
{
  "schemaVersion": 1,
  "id": "game_ui",
  "defaultEntry": "Boot",
  "layers": [
    { "id": "screen", "order": 0, "input": "consumeHandled", "maxActiveScreens": 1 },
    { "id": "hud", "order": 100, "input": "passThrough" },
    { "id": "modal", "order": 1000, "input": "blockLower", "blocksLowerInput": true }
  ],
  "slots": [
    { "id": "screen.main", "layer": "screen", "capacity": 1, "restorePrevious": true },
    { "id": "hud.primary", "layer": "hud", "capacity": 1, "restorePrevious": true }
  ],
  "screens": [
    {
      "id": "main_menu",
      "layout": "ui/main_menu.ui.json",
      "layer": "screen",
      "slot": "screen.main",
      "scope": "application",
      "enterAnimation": "enter",
      "exitAnimation": "exit",
      "parameters": { "title": "Aliyat" },
      "events": [
        { "handler": "startGame", "signal": "ui.startGame" }
      ]
    },
    {
      "id": "gameplay_hud",
      "layout": "ui/gameplay_hud.ui.json",
      "layer": "hud",
      "slot": "hud.primary",
      "scope": "world"
    }
  ],
  "contexts": [
    {
      "id": "MainMenu",
      "priority": 0,
      "slots": [
        { "slot": "screen.main", "operation": "present", "screen": "main_menu" }
      ]
    },
    {
      "id": "Gameplay",
      "priority": 0,
      "slots": [
        { "slot": "hud.primary", "operation": "present", "screen": "gameplay_hud" }
      ]
    }
  ],
  "entries": [
    { "id": "Boot", "contexts": ["MainMenu"], "actionGraph": "boot" }
  ],
  "signals": [
    {
      "id": "ui.startGame",
      "payload": [
        { "id": "world", "type": "string", "required": true },
        { "id": "fadeMs", "type": "number", "default": 250.0 }
      ]
    },
    {
      "id": "player.enteredRegion",
      "payload": [
        { "id": "region", "type": "string", "required": true },
        { "id": "entity", "type": "entity" }
      ]
    }
  ],
  "actions": [
    {
      "id": "world.load",
      "inputs": [{ "id": "world", "type": "asset", "required": true }]
    }
  ],
  "regions": [
    {
      "id": "application",
      "initialState": "MainMenu",
      "states": [
        { "id": "MainMenu", "contexts": ["MainMenu"] },
        { "id": "Loading" },
        { "id": "Gameplay", "contexts": ["Gameplay"] }
      ]
    },
    {
      "id": "story",
      "initialState": "Idle",
      "states": [{ "id": "Idle" }, { "id": "Cutscene" }]
    }
  ],
  "transitions": [
    {
      "id": "start_game",
      "region": "application",
      "from": "MainMenu",
      "to": "Loading",
      "trigger": "ui.startGame",
      "guard": "payload.world != ''",
      "actionGraph": "load_world",
      "priority": 10,
      "interrupt": "cancelPrevious"
    }
  ],
  "graphs": [
    {
      "id": "load_world",
      "nodes": [
        { "id": "fade", "type": "ui.playAnimation", "properties": { "clip": "exit" } },
        {
          "id": "load",
          "type": "game.world.load",
          "properties": {
            "world": "$payload.world",
            "options": { "streaming": true, "tags": ["gameplay"] }
          }
        }
      ],
      "links": [
        { "fromNode": "fade", "fromPin": "completed", "toNode": "load", "toPin": "execute" }
      ]
    }
  ]
}
```

Supported field types are `bool`, `integer`, `number`, `string`, `entity`, and
`asset`. Their defaults are scalar and type-checked. Screen parameters and graph
node properties preserve any JSON value, including nested objects and arrays.

`UIFlowGraphNodeRegistry` is authoring metadata, not an executor registry. A
host registers node display/category data, typed input/output pins and property
fields. `validateUIFlowGraphNodes()` can then reject unknown node types,
unavailable pins, wrong pin directions, incompatible value links and invalid
required properties. Base `validateUIFlow()` deliberately remains forward
compatible and preserves unknown extension nodes, while runtime execution stays
owned by AYApplication/game plugins.

`findUIFlowGraphPin()` and `areUIFlowGraphPinsCompatible()` are public so an
authoring host, executor, and connection UI use exactly the same direction,
execution/value-kind, and value-type rules. AYApplication's
`UIFlowGraphExecutor` pairs those definitions with host handlers. It executes
command nodes serially and deterministically, routes execution outputs, copies
typed values from already completed producers, and permits one node to return
`Running` before a later `completeNode()` continuation. Execution cycles,
unknown node types, invalid links, duplicate execution IDs, and handler
exceptions are rejected at the runtime boundary. Completed results are checked
against the registered execution-output and value-output pins before routing,
so a host handler cannot inject a mismatched runtime value. Cancel/reverse
interruption removes pending node continuations; gameplay behavior remains
outside AYUI.

Validation rejects unsupported schema versions, duplicate/empty IDs, broken
Layer/Slot/Screen/Context/Entry/Signal/Graph references, incompatible Screen
and Slot layers, hierarchy cycles, invalid initial children, missing graph
nodes, invalid Screen event mappings, and typed defaults with the wrong value
type. Behavioral validation of a host-defined graph node is the host executor's
responsibility; the registry validates only its authoring contract.

## Project descriptor and legacy migration

New projects reference one Flow at project scope and may associate Worlds with
named Contexts:

```json
{
  "schemaVersion": 1,
  "id": "sample",
  "paths": { "assets": "Assets" },
  "ui": {
    "flow": "ui/game.uiflow.json",
    "entry": "Boot"
  },
  "worlds": [
    {
      "id": "village",
      "scene": "worlds/village.ayscene",
      "uiContext": "Gameplay"
    }
  ]
}
```

The top-level `ui.entry` is optional and falls back to the Flow's
`defaultEntry`. `worlds[].uiContext` is optional: regions and Scene signals can
drive all presentation without a one-to-one World binding.

Legacy descriptors remain readable:

```json
{
  "worlds": [
    { "id": "village", "scene": "worlds/village.ayscene", "ui": "ui/hud.ui.json" }
  ]
}
```

AYEditor resolves each legacy `world.ui` into an in-memory compatibility Flow:

- one `legacy.world` Layer and `legacy.world.primary` Slot;
- one World-scoped Screen `legacy.world.<world-id>.screen`;
- one Context `legacy.world.<world-id>.context` which presents that Screen;
- one World-to-Context binding.

The migration is deterministic and validated but is not written back
automatically. A project-level `ui.flow` cannot be mixed with legacy
`world.ui`; `world.uiContext` without `ui.flow` is also rejected. Explicit
conversion in a later editor stage can generate a real asset after user review.

## Staged delivery

1. **Data contract — complete:** model, schema v1, serializer, validation,
   project descriptor reference, legacy migration, pure tests and documentation.
2. **Persistent runtime — complete:** optional application-owned runtime,
   transactional layout mounting, Layer/Slot arbitration, Context stack, Scope
   cleanup, typed signal bus/action registry, parallel state transitions,
   Widget host adapter and Host service publication.
3. **Scene bridge — complete:** World Scope/Context lifecycle binding, optional
   lifecycle Signals, serializable generic signal volume/participant components,
   explicit physics/script signal entry point, Host service and module ordering.
4. **Flow Editor — complete:** independent AYDevice tool window, project asset
   creation/opening, graph/state/region canvas, Screen/Context/Layer/Transition
   inspectors, reference-safe history, diagnostics, simulated signals, mock
   action execution and production-runtime logical live preview.
5. **Production gates — complete:** async interruption-policy execution, lower-target
   input retry, enter/exit animation handoff, save/reload migration, replay
   diagnostics, accessibility/reduced-motion behavior, and a clipped real
   Widget-tree preview in the Flow Editor. Runtime, AYUI, editor-contract, and
   full editor test gates cover the integration.
6. **Asset closure — complete:** the production asset audit resolves every
   Screen below the project asset root, rejects absolute/root-escaping paths,
   loads each unique layout once, validates referenced enter/exit clips and
   their Widget track targets, and emits a sorted layout dependency closure
   with reverse Screen references. The Flow Editor displays these asset
   diagnostics before preview; project validation and its command-line tool
   expose the same closure for packaging.
7. **Interaction authoring — complete:** Screen-local handler-to-Signal
   mappings bridge real declarative Widget events into the production runtime;
   asset validation checks that every mapped handler exists in the referenced
   layout. A generic Graph node registry supplies typed node/pin choices and
   strict diagnostics without putting gameplay node semantics into AYUI.
8. **Executable graphs — complete:** AYApplication binds registered node
   contracts to deterministic handlers, propagates typed values, resumes async
   nodes and honors runtime interruption. AYEditor's production preview runs
   the same executor; graph cards expose typed ports, curved links and
   compatibility-filtered connection targets.
