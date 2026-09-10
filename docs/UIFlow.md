# AYUI Flow Contract

Status: **Stage 1 data contract implemented** (2026-09-11).

UI Flow describes application-level UI orchestration. It does not replace a
`*.ui.json` layout: a layout owns one Widget tree, while a `*.uiflow.json`
document decides which layouts are present, where they are layered, how their
lifetimes are scoped, and which signals may change the presentation.

Stage 1 is intentionally data-only. `UIFlowDocument`, validation, JSON
round-trip, project references, and legacy migration are implemented. Runtime
mounting, signal dispatch, action execution, transitions, and the visual Flow
Editor belong to later stages.

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
| Screen | A named `*.ui.json` layout plus layer, slot, lifetime, animations and parameters. |
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

The planned runtime uses a Context stack per Slot. Higher-priority Contexts
override lower-priority assignments; removing an override reveals the previous
assignment when `restorePrevious` is true. A World change removes World-scoped
instances only. Application-scoped menus and loading screens remain alive, and
Owner-scoped UI can follow a specific entity or gameplay owner.

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
      "parameters": { "title": "Aliyat" }
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

Validation rejects unsupported schema versions, duplicate/empty IDs, broken
Layer/Slot/Screen/Context/Entry/Signal/Graph references, incompatible Screen
and Slot layers, hierarchy cycles, invalid initial children, missing graph
nodes, and typed defaults with the wrong value type. Behavioral validation of
a host-defined graph node is the node registry's responsibility in Stage 2.

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
2. **Persistent runtime:** application-owned UIRuntime, layout mounting, slot
   arbitration, Context stack, scope cleanup, signal bus and action registry.
3. **Scene bridge:** generic Scene signal sources and `SceneSignalVolume`-style
   components; World lifecycle binding without AYUI dependencies.
4. **Flow Editor:** graph/state/region canvas, Screen/Context/Layer inspectors,
   diagnostics, simulated signals, mock action execution and live preview.
5. **Production gates:** async interruption semantics, save/reload migration,
   replay diagnostics, accessibility/reduced-motion behavior and full visual
   integration tests.
