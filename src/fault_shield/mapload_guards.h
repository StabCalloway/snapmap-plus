/* mapload_guards.h -- see mapload_guards.c.
 *
 * Two INDEPENDENT, individually-installable guards for defects in the GAME's own code on the map-load /
 * entity-spawn path. Each detours one engine function and repairs a missing precondition BEFORE the
 * engine reaches the deref that faults; each is a no-op on a healthy path. Install once at startup after
 * the module base is known. Both are idempotent (one-shot latch) and return 1 if armed, 0 on failure.
 *
 * Comment out either install call in the backend bootstrap to disable that guard alone -- the same
 * convention the render-node guard uses.
 */
#ifndef SHIELD_MAPLOAD_GUARDS_H
#define SHIELD_MAPLOAD_GUARDS_H

#include <stdint.h>

/* Guard 1 -- the event/trigger linker's unvalidated list walk (use-after-free during map load).
 * Detours the bidirectional AddUnique and, before it runs, resets any link list whose element buffer is
 * unreadable or whose count/capacity are inconsistent to the engine's OWN empty-list state. */
int sh_evwire_guard_install(const uint8_t *module_base);

/* Guard 2 -- idInteractable::Spawn's unchecked subsystem pointer.
 * Detours Spawn and, when the subsystem pointer is absent AND the interactable has tags to bind, zeroes
 * the tag count so the engine takes its own already-exercised "nothing to bind" path instead of
 * dereferencing the missing subsystem. */
int sh_interactable_guard_install(const uint8_t *module_base);

#endif /* SHIELD_MAPLOAD_GUARDS_H */
