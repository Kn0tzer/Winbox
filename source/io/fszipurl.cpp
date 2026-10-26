#include "boxedwine.h"
#include "fszipurl.h"

// See fszipurl.h — HTTP-Range-backed minizip IO for the M7 lazy rootfs.

bool fsZipUrlIsSpec(BString path) {
    return path.startsWith("bw64url:");
}

#if defined(BOXEDWINE_ZLIB) && defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

// zlib_filefunc64_def etc. come via unzip.h (already included by fszipurl.h
// inside extern "C"); ioapi.h alone doesn't parse without zlib.h's macros.

// Synchronous same-origin Range fetch from a guest pthread, built on
// fetch() ONLY. Chrome ~151/152 rejects the XHR paths in this COEP worker
// context (both a hand-rolled sync XHR and emscripten_fetch — whose internal
// transport is XHR — start returning network errors after which lazy reads
// fail; bisected on Chrome-for-Testing 152, passes on 149/150). Mechanism:
// the calling pthread queues a task to the browser main runtime thread with
// emscripten_proxy_sync_with_ctx and blocks; on the main thread an EM_JS kicks
// an async fetch(Range) whose .then copies the bytes into the (SAB-backed)
// destination, stores the byte count, and finishes the proxy ctx, waking the
// caller. fetch() is exactly what the launcher's eager path uses — the one
// transport every Chrome keeps working.
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

namespace {
struct FetchJob {
    const char* url;
    U64 start;
    U64 endIncl;
    U8* dst;
    int want;
    int result; // bytes copied or negative status/-1
};
}

extern "C" {
// Called from JS on the main thread when the fetch settles.
EMSCRIPTEN_KEEPALIVE void bw64FetchDone(double ctxAddr, double jobAddr, int result) {
    FetchJob* job = (FetchJob*)(uintptr_t)jobAddr;
    job->result = result;
    emscripten_proxy_finish((em_proxying_ctx*)(uintptr_t)ctxAddr);
}
}

// Stall timeout for the fetch below (Celeste hang, runtime.md lazy-fetch
// row): a fetch whose body stalls used to never settle, so bw64FetchDone
// was never called and the guest pthread blocked in
// emscripten_proxy_sync_with_ctx hung forever, silently. The watchdog is a
// STALL deadline, not a total deadline: it starts at issuance and resets
// whenever progress arrives (response headers, then every streamed chunk),
// so a slow-but-progressing fetch is never aborted. 60000 ms of total
// silence is the trip point — healthy same-origin range fetches of one
// 2MB block settle in well under a second, and any link still delivering
// bytes resets the clock; only a dead connection (or a wedged server)
// produces a full minute of silence. Where the launcher's prefetch wrapper
// buffers the body before this promise resolves, no progress is visible
// here and the window acts as a per-block deadline instead; the wrapper
// carries its own 120 s backstop watchdog (wine64-launcher.js) so its
// pending map cannot wedge either. On a stall the watchdog logs once
// (greppable: url + offset + bytes so far), aborts the fetch, and settles
// the job itself with BW64_FETCH_STALLED (-2, mirrored in C++ below) via
// the settled-guarded settle(), so the C++ side wakes and fetchRaw's retry
// path takes over — never a silent hang. BW64_FETCH_STALL_MS on the global
// object overrides the window (unit-test seam).
EM_JS(void, bw64StartFetch, (double ctxAddr, double jobAddr, const char* url, double start, double endIncl, double dstAddr, int want), {
    var u = UTF8ToString(url);
    var range = 'bytes=' + start + '-' + endIncl;
    var stallMs = (typeof globalThis !== 'undefined' && Number(globalThis.BW64_FETCH_STALL_MS) > 0)
        ? Number(globalThis.BW64_FETCH_STALL_MS) : 60000;
    var ctrl = (typeof AbortController === 'function') ? new AbortController() : null;
    var settled = false;
    var received = 0;
    var timer = null;
    function settle(result) {
        if (settled) return;
        settled = true;
        if (timer !== null) { clearTimeout(timer); timer = null; }
        _bw64FetchDone(ctxAddr, jobAddr, result);
    }
    function arm() {
        if (timer !== null) clearTimeout(timer);
        timer = setTimeout(function () {
            console.error('fszipurl: fetch STALL url=' + u + ' off=' + start +
                          ' len=' + (endIncl - start + 1) + ' bytesSoFar=' + received +
                          ' stallMs=' + stallMs);
            if (ctrl) { try { ctrl.abort(); } catch (e) {} }
            settle(-2); // BW64_FETCH_STALLED
        }, stallMs);
    }
    arm();
    var init = { headers: { 'Range': range }, cache: 'no-store' };
    if (ctrl) init.signal = ctrl.signal;
    fetch(u, init).then(function (r) {
        if (r.status != 206 && !(r.status == 200 && start == 0)) {
            throw { bwStatus: r.status };
        }
        arm(); // headers arrived: that is progress, restart the stall clock
        if (r.body && typeof r.body.getReader === 'function') {
            var reader = r.body.getReader();
            var chunks = [];
            var total = 0;
            var pump = function () {
                return reader.read().then(function (step) {
                    if (step.done) {
                        var all = new Uint8Array(total);
                        var at = 0;
                        for (var i = 0; i < chunks.length; i++) { all.set(chunks[i], at); at += chunks[i].length; }
                        return all.buffer;
                    }
                    chunks.push(step.value);
                    total += step.value.length;
                    received += step.value.length;
                    arm(); // bytes arrived: restart the stall clock
                    return pump();
                });
            };
            return pump();
        }
        return r.arrayBuffer();
    }).then(function (ab) {
        var src = new Uint8Array(ab);
        var n = src.length < want ? src.length : want;
        if (typeof growMemViews === 'function') growMemViews();
        HEAPU8.set(src.subarray(0, n), Number(dstAddr));
        settle(n);
    }).catch(function (e) {
        if (settled) return; // stall path already settled the job
        var code = (e && e.bwStatus) ? -e.bwStatus : -1;
        settle(code);
    });
});

static EMSCRIPTEN_KEEPALIVE void bw64FetchOnMain(em_proxying_ctx* ctx, void* arg) {
    FetchJob* job = (FetchJob*)arg;
    bw64StartFetch((double)(uintptr_t)ctx, (double)(uintptr_t)job, job->url,
                   (double)job->start, (double)job->endIncl,
                   (double)(uintptr_t)job->dst, job->want);
    // Do NOT finish here — bw64FetchDone finishes when the fetch settles.
}

static int bw64SyncFetchRange(const char* url, double start, double endIncl, U8* dst, int want) {
    FetchJob job;
    job.url = url;
    job.start = (U64)start;
    job.endIncl = (U64)endIncl;
    job.dst = dst;
    job.want = want;
    job.result = -1;
    if (!emscripten_proxy_sync_with_ctx(emscripten_proxy_get_system_queue(),
                                        emscripten_main_runtime_thread_id(),
                                        bw64FetchOnMain, &job)) {
        return -1;
    }
    return job.result;
}

namespace {

// Result code settled by the bw64StartFetch stall watchdog when a fetch
// makes no progress for the whole stall window (see the EM_JS comment).
// Distinct from -1 (generic failure) and -<status> so fetchRaw can log a
// stall as a stall and retry it like other transient failures.
constexpr int BW64_FETCH_STALLED = -2;

// Defence-in-depth deadline for threads waiting on fetchCv for a block
// another thread is fetching. The fetcher is now stall-bounded end to end:
// each bw64SyncFetchRange settles within the JS stall window (60 s of
// silence) and fetchRaw retries a chunk at most 3 times, so a live
// fetcher holds inFlight for at most ~3 minutes per block. 240 s gives
// margin over that worst case; a waiter still blocked past it means the
// fetcher thread is dead, and the read fails loudly instead of hanging
// the guest silently.
constexpr std::chrono::seconds FETCH_WAIT_DEADLINE{240};

constexpr U64 BLOCK_SIZE = 2 * 1024 * 1024;

struct UrlPart {
    BString url;
    U64 size;
    U64 start; // cumulative offset of this part in the virtual file
};

struct RangedFile {
    std::vector<UrlPart> parts; // immutable after mount (fsZipUrlOpen)
    U64 total = 0;              // immutable after mount
    std::atomic<bool> error{false};
    std::atomic<bool> permanentError{false};
    std::atomic<U64> fetched{0};      // stats: bytes downloaded so far
    std::atomic<U64> lastLogged{0};
    std::mutex m;
    std::condition_variable fetchCv; // signaled when an in-flight block fetch finishes
    // LRU block cache: 2MB blocks, capped at MAX_CACHED_BLOCKS (512MB).
    // Locking discipline: m is held for cache lookup/insertion/eviction and for
    // the memcpy out of a cached block, but NEVER across the synchronous
    // network fetch. A cache miss releases m while fetching, so one thread's
    // 50-200ms fetch never blocks other threads' cache hits or fetches of
    // different blocks. inFlight tracks blocks with a fetch in progress;
    // threads that miss on the same block wait on fetchCv instead of
    // duplicating the fetch. Eviction is transparent: a later read of an
    // evicted block just re-fetches it.
    static constexpr size_t MAX_CACHED_BLOCKS = 256; // 256 x 2MB = 512MB
    struct BlockEntry {
        std::unique_ptr<U8[]> data;
        std::list<U64>::iterator lruIt; // position in lruOrder, front = MRU
    };
    std::map<U64, BlockEntry> blocks;
    std::list<U64> lruOrder; // front = most recently used
    std::set<U64> inFlight;  // block indices with a fetch in progress (under m)
    U64 evicted = 0;         // stats: blocks evicted so far (under m)
    U64 lastEvictLogged = 0; // (under m)

    // Fetch [off, off+len) directly from the underlying parts into dst.
    // Lock-free by design (parts/total are immutable after mount, stats are
    // atomic): called WITHOUT holding m so concurrent block fetches proceed
    // in parallel.
    bool fetchRaw(U64 off, U64 len, U8* dst) {
        while (len) {
            const UrlPart* p = nullptr;
            for (auto& part : parts) {
                if (off >= part.start && off < part.start + part.size) { p = &part; break; }
            }
            if (!p) return false;
            U64 inPart = off - p->start;
            U64 chunk = std::min(len, p->size - inPart);
            // Bounded retry: a stall-aborted or transiently failed chunk is
            // re-fetched (each attempt is itself stall-bounded in JS), so a
            // single dead connection cannot fail the whole block read.
            // Permanent statuses are not retried — they fail fast below and
            // latch permanentError as before.
            int got = -1;
            for (int attempt = 1; attempt <= 3; attempt++) {
                got = bw64SyncFetchRange(p->url.c_str(), (double)inPart,
                                         (double)(inPart + chunk - 1),
                                         dst, (int)chunk);
                if (got == (int)chunk) break;
                if (got == -404 || got == -403) break;
                if (attempt < 3) {
                    klog_fmt("fszipurl: range fetch retry %d/3 url=%s off=%llu len=%llu -> %d",
                             attempt + 1, p->url.c_str(),
                             (unsigned long long)inPart,
                             (unsigned long long)chunk, got);
                }
            }
            if (got != (int)chunk) {
                if (got == BW64_FETCH_STALLED) {
                    klog_fmt("fszipurl: range fetch STALLED url=%s off=%llu len=%llu (no progress before the stall deadline, retries exhausted)",
                             p->url.c_str(), (unsigned long long)inPart,
                             (unsigned long long)chunk);
                } else {
                    klog_fmt("fszipurl: range fetch FAILED url=%s off=%llu len=%llu -> %d",
                             p->url.c_str(), (unsigned long long)inPart,
                             (unsigned long long)chunk, got);
                }
                if (got == -404 || got == -403) { permanentError.store(true, std::memory_order_relaxed); }
                return false;
            }
            fetched.fetch_add(chunk, std::memory_order_relaxed);
            off += chunk;
            dst += chunk;
            len -= chunk;
        }
        U64 f = fetched.load(std::memory_order_relaxed);
        U64 ll = lastLogged.load(std::memory_order_relaxed);
        if (f - ll >= 16 * 1024 * 1024) {
            lastLogged.store(f, std::memory_order_relaxed);
            klog_fmt("fszipurl: %llu MB fetched lazily so far",
                     (unsigned long long)(f >> 20));
        }
        return true;
    }

    // Look up block idx in the cache, fetching it on miss.
    // Enter with m held via lk; m may be TEMPORARILY RELEASED during the
    // network fetch and is re-acquired before return. On non-null return the
    // block is present in the cache and m is held, so the caller can safely
    // memcpy from the returned pointer (eviction also requires m).
    const U8* getBlock(U64 idx, std::unique_lock<std::mutex>& lk) {
        if (permanentError.load(std::memory_order_relaxed)) return nullptr;
        const auto waitDeadline = std::chrono::steady_clock::now() + FETCH_WAIT_DEADLINE;
        for (;;) {
            auto it = blocks.find(idx);
            if (it != blocks.end()) {
                // Hit: refresh recency (splice keeps the stored iterator valid).
                lruOrder.splice(lruOrder.begin(), lruOrder, it->second.lruIt);
                return it->second.data.get();
            }
            if (inFlight.find(idx) == inFlight.end())
                break; // miss and nobody fetching: we become the fetcher
            // Another thread is fetching this block; wait for it to finish,
            // then re-check the cache (or become the fetcher if it failed).
            // The wait is deadline-bounded (FETCH_WAIT_DEADLINE): the
            // fetcher's fetches settle within the JS stall window plus
            // bounded retries, so still waiting past the deadline means
            // the fetcher thread died. Fail this read loudly instead of
            // hanging forever. inFlight is left untouched — it is owned by
            // the fetcher — so no duplicate fetch is started and a later
            // read re-enters this wait (or becomes fetcher once the entry
            // is cleared).
            if (fetchCv.wait_until(lk, waitDeadline) == std::cv_status::timeout &&
                blocks.find(idx) == blocks.end() &&
                inFlight.find(idx) != inFlight.end()) {
                klog_fmt("fszipurl: STALL waiting for in-flight block %llu (fetcher made no completion past the wait deadline); failing this read",
                         (unsigned long long)idx);
                return nullptr;
            }
        }
        U64 off = idx * BLOCK_SIZE;
        if (off >= total) {
            return nullptr;
        }
        U64 len = std::min(BLOCK_SIZE, total - off);
        inFlight.insert(idx);
        lk.unlock();
        // ---- synchronous network fetch WITHOUT holding m ----
        // Exception safety: if the allocation or fetch throws, we must
        // re-lock, remove from inFlight, and wake waiters. Otherwise
        // waiters deadlock on a stale inFlight entry.
        std::unique_ptr<U8[]> buf;
        bool ok = false;
        bool threw = false;
        try {
            buf = std::make_unique<U8[]>(BLOCK_SIZE);
            ok = fetchRaw(off, len, buf.get());
        } catch (...) {
            threw = true;
        }
        // ---- re-acquire before touching cache state ----
        lk.lock();
        inFlight.erase(idx);
        if (threw) {
            fetchCv.notify_all();
            throw;
        }
        const U8* result = nullptr;
        if (ok) try {
            // inFlight gave us exclusive fetch rights, so the block should
            // not already be cached; check anyway for robustness.
            auto it = blocks.find(idx);
            if (it != blocks.end()) {
                lruOrder.splice(lruOrder.begin(), lruOrder, it->second.lruIt);
                result = it->second.data.get();
            } else {
                lruOrder.push_front(idx);
                BlockEntry e;
                e.data = std::move(buf);
                e.lruIt = lruOrder.begin();
                result = e.data.get();
                blocks[idx] = std::move(e);
                while (blocks.size() > MAX_CACHED_BLOCKS) {
                    U64 victim = lruOrder.back();
                    lruOrder.pop_back();
                    blocks.erase(victim);
                    evicted++;
                }
                if (evicted - lastEvictLogged >= 64) {
                    lastEvictLogged = evicted;
                    klog_fmt("fszipurl: LRU evicted %llu blocks (%llu MB total), cache capped at 512MB",
                             (unsigned long long)evicted,
                             (unsigned long long)((evicted * BLOCK_SIZE) >> 20));
                }
            }
        } catch (...) {
            // Don't leave waiters hanging on inFlight if insertion throws
            // (e.g. OOM during map node allocation).
            fetchCv.notify_all();
            throw;
        }
        // Wake waiters on success or failure: on failure they re-check, find
        // the block still missing, and retry the fetch themselves.
        fetchCv.notify_all();
        return result;
    }

    U32 readAt(U64& pos, void* out, U32 size) {
        U8* dst = (U8*)out;
        U32 done = 0;
        std::unique_lock<std::mutex> lk(m);
        // Clear sticky error on entry: a previous transient failure should not
        // poison subsequent reads. The flag is re-set below on actual failure.
        if (done == 0) error.store(false, std::memory_order_relaxed);
        while (size) {
            if (pos >= total) {
                break; // EOF
            }
            U64 idx = pos / BLOCK_SIZE;
            U64 inBlock = pos % BLOCK_SIZE;
            U64 blockLen = std::min(BLOCK_SIZE, total - idx * BLOCK_SIZE);
            // getBlock may release m during the network fetch, then
            // re-acquire it. The memcpy below runs with m held, so the
            // block cannot be evicted under us.
            const U8* b = getBlock(idx, lk);
            if (!b) {
                error.store(true, std::memory_order_relaxed);
                break;
            }
            U32 chunk = (U32)std::min<U64>(size, blockLen - inBlock);
            memcpy(dst, b + inBlock, chunk);
            pos += chunk;
            dst += chunk;
            size -= chunk;
            done += chunk;
        }
        return done;
    }
};

// One RangedFile (block cache) is shared per spec across every unzOpen of it
// (FsZip::init + the static helpers each open the zip; without sharing, each
// re-fetched the central directory). Streams carry their own cursor.
struct RangedStream {
    std::shared_ptr<RangedFile> file;
    U64 pos = 0;
};
std::mutex g_registryMutex;
std::map<std::string, std::shared_ptr<RangedFile>> g_registry;

voidpf urlOpen64(voidpf opaque, const void* filename, int mode) {
    (void)filename; (void)mode;
    RangedStream* st = new RangedStream();
    st->file = *(std::shared_ptr<RangedFile>*)opaque;
    return st;
}
uLong urlRead(voidpf opaque, voidpf stream, void* buf, uLong size) {
    (void)opaque;
    RangedStream* st = (RangedStream*)stream;
    return st->file->readAt(st->pos, buf, (U32)size);
}
uLong urlWrite(voidpf opaque, voidpf stream, const void* buf, uLong size) {
    (void)opaque; (void)stream; (void)buf; (void)size;
    return 0; // read-only
}
ZPOS64_T urlTell64(voidpf opaque, voidpf stream) {
    (void)opaque;
    return ((RangedStream*)stream)->pos;
}
long urlSeek64(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
    (void)opaque;
    RangedStream* st = (RangedStream*)stream;
    U64 base = (origin == ZLIB_FILEFUNC_SEEK_CUR) ? st->pos
             : (origin == ZLIB_FILEFUNC_SEEK_END) ? st->file->total : 0;
    st->pos = base + offset;
    return 0;
}
int urlClose(voidpf opaque, voidpf stream) {
    (void)opaque;
    delete (RangedStream*)stream; // the shared RangedFile lives in the registry
    return 0;
}
int urlError(voidpf opaque, voidpf stream) {
    (void)opaque;
    return ((RangedStream*)stream)->file->error ? 1 : 0;
}

} // namespace

unzFile fsZipUrlOpen(BString spec) {
    // bw64url:<totalBytes>;<url1>|<size1>;<url2>|<size2>;...
    BString body = spec.substr(8);
    std::vector<BString> fields;
    body.split(';', fields);
    if (fields.size() < 2) {
        klog_fmt("fszipurl: bad spec (need total;url|size...): %s", spec.c_str());
        return nullptr;
    }
    std::lock_guard<std::mutex> reg(g_registryMutex);
    auto& slot = g_registry[std::string(spec.c_str())];
    if (slot) {
        zlib_filefunc64_def fns = {};
        fns.zopen64_file = urlOpen64;
        fns.zread_file = urlRead;
        fns.zwrite_file = urlWrite;
        fns.ztell64_file = urlTell64;
        fns.zseek64_file = urlSeek64;
        fns.zclose_file = urlClose;
        fns.zerror_file = urlError;
        fns.opaque = &slot;
        return unzOpen2_64(spec.c_str(), &fns);
    }
    auto f = std::make_shared<RangedFile>();
    f->total = (U64)atoll(fields[0].c_str());
    U64 cum = 0;
    for (size_t i = 1; i < fields.size(); i++) {
        std::vector<BString> us;
        fields[i].split('|', us);
        if (us.size() != 2) {
            klog_fmt("fszipurl: bad url|size field: %s", fields[i].c_str());
            return nullptr;
        }
        UrlPart p;
        p.url = us[0];
        p.size = (U64)atoll(us[1].c_str());
        p.start = cum;
        cum += p.size;
        f->parts.push_back(p);
    }
    if (cum != f->total) {
        klog_fmt("fszipurl: part sizes (%llu) != total (%llu)",
                 (unsigned long long)cum, (unsigned long long)f->total);
        return nullptr;
    }
    slot = f; // registered BEFORE open so opaque points at stable storage
    zlib_filefunc64_def fns = {};
    fns.zopen64_file = urlOpen64;
    fns.zread_file = urlRead;
    fns.zwrite_file = urlWrite;
    fns.ztell64_file = urlTell64;
    fns.zseek64_file = urlSeek64;
    fns.zclose_file = urlClose;
    fns.zerror_file = urlError;
    fns.opaque = &slot;
    unzFile z = unzOpen2_64(spec.c_str(), &fns);
    if (z) {
        klog_fmt("fszipurl: mounted %llu MB zip lazily (%d part(s), 2MB blocks)",
                 (unsigned long long)(f->total >> 20), (int)f->parts.size());
    } else {
        g_registry.erase(std::string(spec.c_str()));
    }
    return z;
}

#elif defined(BOXEDWINE_ZLIB)

unzFile fsZipUrlOpen(BString spec) {
    klog_fmt("fszipurl: bw64url specs need the emscripten build: %s", spec.c_str());
    return nullptr;
}

#endif
