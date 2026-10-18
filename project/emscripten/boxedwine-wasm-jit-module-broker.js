/*
 *  Copyright (C) 2012-2026  The Boxedwine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * WASM JIT module broker -- worker-local implementation.
 *
 * WHY THIS FILE EXISTS
 * -------------------
 * The multi-threaded WASM JIT publishes a generated module's table index into
 * the shared DecodedOp, but a WebAssembly table is per-worker, so every worker
 * must be able to turn the published bytes into a slot in *its own* table. The
 * C++ side owns that contract (shared byte registry keyed by slot, monotonic
 * slot allocation via Atomics, lazy install). This file supplies the JS-side
 * API the C++ code calls through EM_JS:
 *
 *   bwWasmJitBrokerGetOrCompile  compile the module bytes, or reuse the copy
 *                                this worker already compiled
 *   bwWasmJitBrokerReleaseMemory drop cached modules/instances for an owner
 *   bwWasmJitBrokerIsOom         classify an engine OOM so the C++ can back off
 *   bwWasmJitMt*                 per-worker merged-group instance bookkeeping
 *
 * SCOPE / LIMITATION (deliberate, documented)
 * -------------------------------------------
 * Upstream's broker additionally *publishes* each compiled module to the other
 * workers through the main thread (postMessage + Atomics.wait handshake) and
 * preloads modules for a pthread before its first slice. That mechanism is not
 * implemented here: every worker compiles the module bytes it needs for itself
 * and reuses them from this worker's own cache. Correctness is unaffected --
 * the C++ registry is authoritative and lazy installs already handle a worker
 * that has never seen a slot -- but the duplicate compile work per worker is
 * not shared. Because nothing is published, no worker can ever be waiting on a
 * publication, so the publication-blocking hooks report "not blocked".
 * Compile sharing is the only thing lost; see the lane report for the
 * performance implication and the follow-up work item.
 *
 * This file is added with --pre-js so it is present on the main thread and in
 * every pthread worker. It must not assume `Module` exists at load time; every
 * entry point below is called long after runtime initialization.
 */

(function() {
    'use strict';

    // Lookup sources, mirroring WasmJitMtBrokerLookupResult in
    // jitWasmCodeGen.cpp.
    var LOOKUP_LOCAL_COMPILE = 1;

    var isWorker = (typeof ENVIRONMENT_IS_PTHREAD !== 'undefined' && ENVIRONMENT_IS_PTHREAD);

    // moduleId is allocated by the C++ per (memory, incarnation) and is unique
    // for the lifetime of that owner, so this key cannot alias a module from a
    // previous session of the same guest address space.
    function moduleKey(moduleId, memoryId, memoryIncarnation, moduleClass) {
        return (moduleId >>> 0) + ':' + (memoryId >>> 0) + ':' +
            (memoryIncarnation >>> 0) + ':' + (moduleClass >>> 0);
    }

    function ownerKey(memoryId, memoryIncarnation) {
        return (memoryId >>> 0) + ':' + (memoryIncarnation >>> 0);
    }

    // moduleKey -> { module, owner }
    var localModules = new Map();
    // instanceKey -> owner (group instances live in globalThis.bwJitMtGroupInstances,
    // maintained by the C++ EM_JS helpers; this map tracks their owner so a
    // release can purge them).
    var localGroupOwners = new Map();

    globalThis.bwWasmJitBrokerLocalStats = {
        compiles: 0,
        cacheHits: 0,
        purgedModules: 0,
        purgedInstances: 0,
        oomRejections: 0,
        worker: isWorker
    };

    function isWasmOom(error) {
        if (!error) {
            return false;
        }
        if (error.bwWasmJitBrokerOom === true) {
            return true;
        }
        var text = String(error.name || '') + ' ' + String(error.message || error);
        return text.indexOf('out of memory') !== -1 ||
            text.indexOf('Out of memory') !== -1 ||
            text.indexOf('OOM') !== -1;
    }

    function markOom(error) {
        if (isWasmOom(error)) {
            try {
                error.bwWasmJitBrokerOom = true;
            } catch (e) {
                // Frozen error objects: the C++ classifier also matches text.
            }
            globalThis.bwWasmJitBrokerLocalStats.oomRejections++;
            return true;
        }
        return false;
    }

    // Compile (or reuse) the module described by `bytes`.
    //
    // Returns { module, source }. `source` is always LOCAL_COMPILE: this worker
    // compiled the bytes itself. A worker-local cache hit is still reported as
    // LOCAL_COMPILE so the C++ "brokerHits" counter keeps meaning "served by a
    // cross-worker publication", which never happens in this implementation;
    // local reuse is counted in bwWasmJitBrokerLocalStats.cacheHits instead.
    // A module that fails to compile is never cached, and
    // WebAssembly.CompileError is rethrown so the C++ records an install
    // failure for that block and keeps interpreting it (see
    // JitWasmCodeGen::commitJIT). Memory exhaustion is marked with
    // bwWasmJitBrokerOom first so the C++ can pause compilation instead of
    // retrying a doomed block.
    globalThis.bwWasmJitBrokerGetOrCompile = function(
            moduleId, memoryId, memoryIncarnation, bytes, enabled, moduleClass,
            representedBlockCount) {
        var stats = globalThis.bwWasmJitBrokerLocalStats;
        var key = moduleKey(moduleId, memoryId, memoryIncarnation, moduleClass);
        var cached = localModules.get(key);
        if (cached) {
            stats.cacheHits++;
            return { module: cached.module, source: LOOKUP_LOCAL_COMPILE };
        }
        var compiled;
        try {
            compiled = new WebAssembly.Module(bytes);
        } catch (error) {
            markOom(error);
            throw error;
        }
        stats.compiles++;
        localModules.set(key, {
            module: compiled,
            owner: ownerKey(memoryId, memoryIncarnation)
        });
        return { module: compiled, source: LOOKUP_LOCAL_COMPILE };
    };

    // Release every cached module (and merged-group instance) owned by a guest
    // address space. `retireOwner` means the whole owner is going away; otherwise
    // only the listed moduleIds are dropped. Table slots are NOT touched: they
    // are per-worker and the C++ already unpublished them before asking.
    globalThis.bwWasmJitBrokerReleaseMemory = function(
            memoryId, memoryIncarnation, moduleIds, tableSlots, retireOwner,
            groupInstanceKeys) {
        var stats = globalThis.bwWasmJitBrokerLocalStats;
        var owner = ownerKey(memoryId, memoryIncarnation);
        if (retireOwner) {
            localModules.forEach(function(entry, key) {
                if (entry.owner === owner) {
                    localModules.delete(key);
                    stats.purgedModules++;
                }
            });
            localGroupOwners.forEach(function(entryOwner, instanceKey) {
                if (entryOwner === owner) {
                    localGroupOwners.delete(instanceKey);
                    if (globalThis.bwJitMtGroupInstances) {
                        globalThis.bwJitMtGroupInstances.delete(instanceKey);
                    }
                    stats.purgedInstances++;
                }
            });
            return;
        }
        if (moduleIds && moduleIds.length) {
            for (var i = 0; i < moduleIds.length; ++i) {
                var id = moduleIds[i] >>> 0;
                localModules.forEach(function(entry, key) {
                    if (entry.owner === owner && key.indexOf(id + ':') === 0) {
                        localModules.delete(key);
                        stats.purgedModules++;
                    }
                });
            }
        }
        if (groupInstanceKeys && groupInstanceKeys.length) {
            for (var g = 0; g < groupInstanceKeys.length; ++g) {
                var instanceKey = groupInstanceKeys[g];
                localGroupOwners.delete(instanceKey);
                if (globalThis.bwJitMtGroupInstances) {
                    globalThis.bwJitMtGroupInstances.delete(instanceKey);
                }
                stats.purgedInstances++;
            }
        }
    };

    globalThis.bwWasmJitBrokerIsOom = isWasmOom;

    // --- Merged-group instance bookkeeping (worker-local) -------------------
    globalThis.bwWasmJitMtGroupInstanceKey = function(
            moduleId, groupKind, groupIdentity, memoryId, memoryIncarnation) {
        if (moduleId) {
            return 'm' + (moduleId >>> 0);
        }
        return (groupKind >>> 0) + '|' + String(groupIdentity) + '|' +
            (memoryId >>> 0).toString(16) + '|' +
            (memoryIncarnation >>> 0).toString(16);
    };

    globalThis.bwWasmJitMtRememberGroupInstance = function(
            instanceKey, memoryId, memoryIncarnation) {
        localGroupOwners.set(instanceKey, ownerKey(memoryId, memoryIncarnation));
    };

    globalThis.bwWasmJitMtForgetGroupInstance = function(instanceKey) {
        localGroupOwners.delete(instanceKey);
    };

    // Nothing is ever published, so a fresh group construction can never be
    // blocked by an in-flight publication from this worker.
    globalThis.bwWasmJitMtCanConstructFreshGroup = function(memoryId, memoryIncarnation) {
        return true;
    };

    globalThis.bwWasmJitMtBlockFreshGroupConstruction = function(memoryId, memoryIncarnation) {
        // no-op: see CanConstructFreshGroup
    };

    // The C++ counter is only reachable when the build exports it; otherwise
    // count locally so worker-side group activity is still observable.
    globalThis.bwWasmJitMtRecordGroupWorkerEvent = function(event) {
        if (typeof Module !== 'undefined' &&
                typeof Module._wasm_jit_mt_record_group_worker_event === 'function') {
            Module._wasm_jit_mt_record_group_worker_event(event >>> 0);
            return;
        }
        if (!globalThis.bwWasmJitMtLocalGroupEvents) {
            globalThis.bwWasmJitMtLocalGroupEvents = { localCompiles: 0, brokerHits: 0, reuses: 0 };
        }
        if ((event >>> 0) === 1) {
            globalThis.bwWasmJitMtLocalGroupEvents.localCompiles++;
        } else if ((event >>> 0) === 2) {
            globalThis.bwWasmJitMtLocalGroupEvents.brokerHits++;
        } else if ((event >>> 0) === 3) {
            globalThis.bwWasmJitMtLocalGroupEvents.reuses++;
        }
    };
})();