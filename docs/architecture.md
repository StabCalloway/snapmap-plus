# Architecture

A contributor-orientation map of how the two DLLs fit together. For the *what it does*
feature list see [`capabilities.md`](capabilities.md); for the deliberately-faithful quirks see
[`fidelity.md`](fidelity.md).

## The two DLLs and the boundary between them

The clone is a **backend** (`XINPUT1_3.dll`, built from `src/backend/`) and a **frontend**
(`snapmap-plus-ui.dll`, built from `src/ui/`).

- The backend loads first. DOOM loads `XINPUT1_3.dll` at startup (it sits in the game root and
  forwards the real XInput exports through to System32). Once running, the **backend** does
  `LoadLibraryA(".\\snapmap-plus\\snapmap-plus-ui.dll")` and then `CreateThread(sh_ui_init, ...)` to
  bring the frontend window up on its own thread.
- The frontend never touches the engine directly. Every engine read or write the UI needs goes
  through a shared **interface object** that the backend creates and hands to the frontend's
  init thread.

This split is the version-portability story: all the build-specific engine offsets and
signature-resolved engine calls live **behind the interface, in the backend**. The frontend
holds no raw engine addresses, so a DOOM update only forces a re-derive on the backend side.

## The frontend: a WebView2 (HTML) window

The frontend (`src/ui/webview/snapmap_plus_ui_webview.cpp`) hosts the Snapmap+ UI as HTML/CSS/JS in a
Microsoft Edge **WebView2** control inside a plain Win32 window. Its `sh_ui_init` entry (export
ordinal 10, the same entry the backend calls) creates the window, brings up WebView2, loads the UI
(`mockup.html`, compiled into the DLL), wires the JS <-> native bridge, stores the backend **interface**
pointer, then enters the think-loop and never returns. The UI's structure — the tabs, the entity list,
the entity-state editor, the timeline editor, prefabs — lives in the HTML; the C++ host is a thin bridge
that turns JS messages into interface-slot calls and posts results back to the page. Full detail:
[`webview-ui.md`](webview-ui.md).

Theme selection is available before the first navigation: the host reads the registered `theme` setting,
adds `class="dark"` to the embedded document root when needed, and only lets the native window become
visible after a successful `NavigationCompleted`. A returning dark-theme user therefore never sees a
light or blank first frame.

The host keeps a normal `WS_OVERLAPPEDWINDOW` so Windows still owns resizing, Aero Snap, minimize,
maximize, and taskbar behavior, then consumes `WM_NCCALCSIZE` so the HTML menubar replaces the visible
caption. A one-pixel `DwmExtendFrameIntoClientArea` margin preserves DWM's rounded Windows 11 corners and
drop shadow on that captionless client, matching snapmap-midi without switching to a behavior-poor
frameless-window style.

## The asset browser is a live installed-data view

The asset browser does not ship or build a second asset library. Selecting a category posts that one
kind through the existing interface, the host pages newline-delimited names from the backend, and the
page keeps a two-category least-recently-used cache -- the minimum that lets the tab and modal retain
their independent current categories without accumulating every list visited. Scalar counts remain
for the rail. Materials and Sounds request one extra qualifier list for atlas-only rows and soundbank
names respectively.

The backend parses the game's installed resource indexes only when the first catalog or plain-image
preview needs them. It retains interned names plus resource-file offsets and releases the raw index
documents; record classes from the broader base-game box that cannot serve a list or preview route
are discarded while parsing. Wwise event/bank metadata and the `.vmtr` name union have separate
first-use gates. The Wwise XML is streamed for only bank and event tags, and VMTR names occupy an
exact string pool rather than fixed-width slots. Event, bank-row, and decl-less-material pointer
tables are also shrunk to their final deduplicated counts.

A preview then seeks to the selected payload, reads and decodes that payload in memory, and publishes
only the resulting thumbnail. Mega2 page lookup follows the same rule: one 4-byte page id and one
16-byte offset/size entry are read for a selected cell instead of copying whole shard tables. The
worker sleeps between requests, allocates decode scratch only for an atlas-backed preview, and
releases it after an idle interval. Encoded previews are consumed by the page rather than retained
on both sides of the interface. Image selections carry their catalog kind through the existing
append-stable request slot, so a direct Image bypasses VMTR and cannot be captured by a same-named
Material record. The large game files remain the source of truth and are never copied into the
overlay.

## The prefab preview reconstructs an installed-data scene

Prefab Details does not capture the game's renderer or build a screenshot. The host reads only the
currently selected prefab JSON and sends it to the page, where `prefab_viewport.js` reconstructs entity
transforms from the prefab-local `spawnPosition`, `spawnOrientation`, and scale values. The serialized
orientation is a sparse patch over identity: each `mat[n]` is one complete local axis, matching idTech's
column-major `idMat3`, and an omitted component retains its identity value. Scale is sparse too, but its
base is the inherited entityDef's `renderModelInfo.scale` rather than always `{1,1,1}`. Direct
`renderModelInfo.model` names are already in the file; otherwise the backend first uses the pure,
read-locked entityDef lookup without loading or creating an engine object. A file-only fallback follows
installed `snapEditorEntityDef` / `entityDef` inheritance and composes the first derived occurrence of
each scale component. That fallback also understands
`spawnerEntityPair.entityStatic`, so pickup spawners resolve to the armor, health, ammo, or equipment mesh
they represent instead of becoming generic boxes.

Geometry crosses four append-only interface slots: resolve inherit to model (`+0x308`), enqueue an
installed mesh request (`+0x310`), consume one completion (`+0x318`), and resolve the model plus inherited
scale defaults (`+0x320`). The original model-only slot remains intact for paired-version compatibility.
A single bounded worker uses the
asset browser's lazy installed-resource index to seek the requested BMODEL or MD6 payload. It decodes only
positions, packed normals, and indices, with hard source/vertex/index/surface limits. BMODEL's fixed
32-byte per-surface metadata is consumed between surfaces; treating that block as the next material header
was the reason formerly working single-surface geometry degraded every multi-surface prop to a proxy. The frontend host
moves one completion per think-loop tick through a WebView2 shared buffer, avoiding base64 expansion; the
page uploads it and immediately releases the shared buffer.

The page classifies saved entities before drawing them. Props and resolved pickup spawners use their real
installed mesh; blockers prefer the visible `renderModels` shell rather than its editor trigger shell;
SnapMap logic, action/listener I/O, and filter entities use the installed hexagon, circle, and diamond
editor meshes. Hexagons retain their full editor size while I/O circles and filter diamonds use the
editor's half scale. The common saved `isVisible: false` state is not treated as a trigger classification;
class/inheritance semantics keep ordinary props, pickups, and logic nodes solid. Actual invisible triggers
are faint outlined helpers and do not control automatic framing. Decals
remain thin helper planes because their appearance is texture data, not geometry. Only truly unsupported,
over-budget, missing, or transport-incompatible solid geometry falls back to a procedural box. Block and
trigger fallback boxes match the installed unit meshes' bottom origin instead of centering around the spawn
point. Neutral lighting uses an inverse-transpose normal matrix, so strongly non-uniform block dimensions
do not skew their shading. The floor
keeps Cartesian square coordinates but extends beyond the scene and fades through a circular radial mask.

This is a read-only hook into files the player already installed, not a shipped asset library. Snapmap+
does not persist or package payload bytes, textures, materials, skeletons, animations, or game renderer
state. WebGL draws only when input, geometry, theme, or the observed preview bounds change. Its device-pixel
ratio, triangle count, and GPU cache are bounded, so resizing the native window or the shared pane divider
does not create a continuous render loop.

## The 30 Hz manual think-loop

The frontend runs its own pump (the same shape as OG `FUN_180015c04`), once per frame at roughly 30 Hz,
under a loop mutex — draining the backend work-queue rather than relying on any UI toolkit's event loop:

```
lock(loop_mutex)
    (*(interface + 0x1a0))()      // drain the backend work-queue: run queued {handler, args}
    apply deferred UI-driven writes (snapshotted in the JS message callback)
unlock
pump the window's messages
Sleep(33ms)                       // ~30 Hz
```

This is **load-bearing**, not a stylistic choice. Heavy engine work (the SnapStack apply chain,
Save-to-Decl, timeline commits) is snapshotted off the re-entrant JS message callback and applied here,
on the think-loop thread; the manual pump plus the `+0x1a0` work-queue drain *are* the frontend's
main-thread execution point (a UI-thread or RPC-thread engine call deadlocks the engine's command-system
lock). Replicate the pump.

## Engine allocations inherit a heap scope — mind the lifetime

Any engine object we build through idlib containers is allocated from **whatever heap is currently on top
of `idMemLocal`'s heap-scope stack**, because idlib always asks `Mem_Alloc` for heap id `-1` ("current
scope"). The engine keeps three heaps — global/process, persist, and **map** — and the map heap is
`HeapDestroy`d at map load. So the lifetime of an engine allocation is decided by **when it runs**, not by
what is allocated, and while the SnapMap editor is up the ambient scope is the *map* heap.

Practical rule: **anything we build that must outlive the current map has to be allocated inside an
explicit `idMemLocal::PushHeap(0)` / `PopHeap()` pair.** Everything else is fine as-is — an object created
and destroyed within one call cannot outlive its heap, and objects that genuinely belong to the map
*should* die with it.

Today exactly one thing we build outlives its call: the prefab staged into `editor+0x209a8` by
`ae_mkcmd_one`. Every other engine ctor in the backend is paired with its dtor in the same function. That
one site is scope-pushed; see [`backend-changes.md`](backend-changes.md) for the failure it caused before
it was, and doom-re `docs/truth/engine/memory-heaps-and-allocator.md` for the engine-side derivation.

Two properties of the mechanism worth knowing before using it:

- It is **main-thread-only.** `PushHeap`, `PopHeap` and `Mem_Alloc`'s `-1` lookup share a
  `GetCurrentThreadId()` gate; off the engine's main thread all three are silently inert.
- The scope stack is **global, not per-thread**, so a push briefly changes the ambient heap for other
  threads. That is a leak risk, never corruption — each block records its own heap in its header and
  `Mem_Free` reads it back, so a block is always freed into the heap it came from.

`PopHeap` **fatals on underflow**, so pushes and pops must be balanced across early returns and exceptions.

## The interface vtable (the matched-pair ABI)

The shared interface object is defined once, in `src/common/snapmap_plus_iface.h`, and **both DLLs
include that header** — it is a matched pair. The backend writes the vtable and fields; the
frontend reads them at the same offsets.

- The backend builds it (`operator_new(0x60)`), installs the vtable — the **77 original-faithful
  slots** (`+0x00..+0x260`) plus the **clone-extension slots** appended after them (`+0x268..+0x318`
  today, `sizeof(sh_iface_vtbl) == 0x320`: the atomic class+inherit apply, the class/inherit
  enumerators, the dev-layer query, the wire-edit generation counter, the synchronous `apply_sync`,
  the timeline inherit-normalize, push/clear-stack, the generic configuration getter/setter, and the
  asset-browser group — preview request/publish, request-by-name, the material atlas rect, the
  catalog pager, sound preview/session, and prefab model resolution/mesh transport) — initializes the mutex at `+0x08`, and hangs a
  sub-object off `+0x58` that holds the SnapStack subcommand map and the main-thread work-queue.
- **Extension slots are append-only**: a new capability gets the next slot after the current end;
  original-block offsets never move. This is also a real failure mode, not a formality — a frontend
  calling an extension slot that an older backend never installed would call through garbage. That is
  why `build.ps1` builds both DLLs from the same header in one pass by default (its `-BackendOnly`
  switch skips only the frontend — the safe direction, since an older frontend never reads past a
  newer backend's vtable), and why the frontend null-probes an extension slot (falling back or
  skipping the feature) rather than assuming it.
- The frontend calls vtable slots for everything it needs from the engine: entity
  count/validity, classname/inherit/displayname read and write, serialize/deserialize an
  entity, apply an edit (`+0xd0`), enqueue and drain the work-queue (`+0x90` / `+0x1a0`),
  register/unregister SnapStack subcommands (`+0x188` / `+0x190`), enumerate decls, manage the
  selection, show toasts (`+0x1b8`), and read/write registered settings as JSON fragments
  (`config_get_json` `+0x2B0` / `config_set_json` `+0x2B8`).

Because this vtable is the *clone's own* ABI — not a DOOM structure — it is self-consistent and
not DOOM-build-dependent. The only hardcoded offsets that cross the DLL line are these vtable
slot offsets and the `WIN[...]` field offsets. **They must stay pinned identically in both
DLLs**; the two are a matched set. The build-specific *engine* offsets sit behind the vtable in
the backend, where they are re-derived per build.

## Persistent configuration

Two files, owned by different sides on purpose. The backend owns `config.json` — the registered
settings, validated and versioned. The **frontend host** owns `pinned.json`, the asset browser's
shortlist, and deliberately keeps it out of the settings registry: `config.json` is all-or-nothing,
so any parse failure resets the whole document to defaults, which is an acceptable trade for a
handful of validated scalars and not for unbounded data a user grows themselves. The host moves those
bytes and parses none of them; shape and validation live in the UI, the only side that knows what a
pin means. See [`capabilities.md`](capabilities.md#persistent-settings).

The backend is the sole owner of `%LOCALAPPDATA%\snapmap-plus\config.json`; the installer does not
generate, parse, or replace it. `sh_config_init` runs after the common per-user directories are available
and creates this version-1 document when the file is absent:

```text
config init -> immutable user-overrides snapshot -> resource-shadow install
            -> command-system install -> decl-server snapshot
            -> one private main-thread registration command
```

The snapshot makes the user-file layer stable for that DOOM process; the setting is changed for a later
launch, not as a live resource-loader switch.

## Existing shadows versus genuinely new decls

The two mechanisms deliberately share one user setting and one data root, but solve different engine
problems:

| Path | Trigger | Result |
|---|---|---|
| Ordinary file shadow | DOOM requests an already-registered source path | The resource loader receives the user's bytes instead of the packaged bytes. |
| Dynamic decl server | Startup enumerates `overrides/generated/decls/<type>/...*.decl` | An absent logical identity is inserted into DOOM's live decl registry. |

The dynamic path is not a second resource hook. Discovery and bounded structural validation happen on the
backend bootstrap thread, producing an immutable in-memory launch snapshot. Snapmap+ then registers one
private engine command and queues it through `BufferCommandText`; DOOM drains it at its own command-exec
point on the main thread. The handler resolves the short decl type through the registry's `+0x58` virtual
method and inserts missing identities through its native `+0x70` AddDeclFromText method. It calls the
signature-resolved decl finder before and after insertion, so existing identities are classified as
`SHADOWED` and newly published ones as `REGISTERED`.

The registry anchor, type lookup, add-from-text method, and decl finder are independently signature-resolved.
The anchor must be a clean scan because Snapmap+ decodes its RIP-relative registry slot; the two live vtable
entries must exactly match their resolved method addresses. Any missing or ambiguous boundary refuses the
service before mutation. A per-file engine exception aborts the remaining batch rather than continuing from
uncertain registry state.

Files are case-insensitively collision-checked, capped at 512 files, 1 MiB each, and 16 MiB total, and reparse
points, traversal, malformed paths, embedded NULs, and unbalanced text are refused. DOOM's parser remains the
semantic authority. The service intentionally has no watcher, refresh, retry, or unload path: changing a decl
requires a cold restart. It registers textual decl identities only; it does not make referenced binary assets
portable or distribute dependencies with a map.

```json
{
  "schema_version": 1,
  "settings": {
    "theme": "light",
    "entities.show_hidden": false,
    "entities.selection_mode": "off",
    "overrides.user_enabled": true
  }
}
```

Deleting the file deliberately is therefore a clean reset: the next startup, or the next setting write
in a running session, recreates it. Missing or deleted configuration restores `overrides.user_enabled` to
enabled. Manual config edits are consumed at the next startup; a successful `sh_user_overrides 0` or
`sh_user_overrides 1` write goes through the existing setter and so recreates a deleted file. The one descriptor
table in `src/backend/config.c` declares each setting's key, JSON type, default, validator/normalizer, and
backend/frontend read/write permissions. In addition to `theme`, the registry has the
`entities.show_hidden` boolean, `entities.selection_mode` enum (`off`, `follow`, or `select_in_3d`), and
`overrides.user_enabled` boolean (true by default); the schema version and generic backend↔frontend ABI
are unchanged. Adding a setting means adding a descriptor and its behavior/tests; the wire contract remains
generic.

Values cross the matched-pair ABI as complete UTF-8 JSON fragments. `config_get_json` at `+0x2B0`
supports a size query and reports status flags; `config_set_json` at `+0x2B8` validates the registered
key/value and returns rejected, persisted, or session-only. The WebView host exposes those calls to the
page as generic `configGet` / `configSet` messages carrying `valueJson`. This accommodates future
booleans, numbers, strings, arrays, and objects without growing the ABI once per setting; the generic
bridge already permits a future frontend control for user overrides, though none exists today.

The parser accepts an optional UTF-8 BOM, caps the file at 64 KiB, rejects malformed UTF-8, malformed
JSON, excessive nesting, and duplicate object keys, and requires the supported schema version. For a
supported document it repairs missing or invalid registered values to their defaults while preserving
unknown members under both the root and `settings`. A malformed, structurally invalid, or oversized file
is moved to a timestamped `config.<timestamp>[.<collision>].corrupt.json` backup and replaced with
defaults; the UI warns once for that startup. A document with a newer schema version is instead left
byte-for-byte untouched: the current process uses defaults and refuses to overwrite preferences it does
not understand.

Writes are serialized by an in-process lock and a local-session named mutex. A setter rereads the file
while holding that mutex so it does not discard an external writer's unknown values, writes and flushes a
same-directory temporary file, then atomically replaces `config.json`. Existing-file replacements use
paired temporary/rollback names; if a process stops in Windows' documented partial-replacement state,
the next startup recognizes the pair and restores the prior file before applying missing-file reset
semantics. Creation, read, write, flush, backup, replacement, or mutex failures leave the last good
on-disk file intact where possible and switch the affected value to session-only memory with a visible
warning. `overrides.user_enabled` is the exception to that general session-only behavior: its immutable
launch snapshot has already been captured, so a failed `sh_user_overrides` write reports that it was not
saved, leaves this launch unchanged, and establishes no next-launch change. The two-DLL overlay and
installer payload are unchanged; update/uninstall/reinstall preserve this runtime-owned file.
