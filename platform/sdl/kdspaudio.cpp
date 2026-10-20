/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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

#include "boxedwine.h"
#include "kdspaudio.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include "../../source/kernel/devs/oss.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define DSP_BUFFER_SIZE (1024*32)

class KDspAudioSdl : public KDspAudio, public std::enable_shared_from_this<KDspAudioSdl> {
public:
	KDspAudioSdl() {
		this->want.format = AUDIO_U8;
		this->want.channels = 1;
		this->want.freq = 11025;
#ifdef __EMSCRIPTEN__
		this->want.samples = 4096; //Must be pow of 2
#else
		this->want.samples = 5512;
#endif
		this->got.channels = 1;
		this->got.freq = 11025;
#ifdef __EMSCRIPTEN__
		this->got.samples = 4096; //Must be pow of 2
#else
		this->got.samples = 5512;
#endif
	}

	virtual ~KDspAudioSdl() {
		if (this->cvtBuf) {
			SDL_free(this->cvtBuf);
		}
		if (this->stream) {
			SDL_FreeAudioStream(this->stream);
		}
		if (this->deviceId) {
			SDL_CloseAudioDevice(this->deviceId);
			this->deviceId = 0;
		}
	}

	void openAudio(U32 format, U32 freq, U32 channels) override;
	void soundEnabled() override;
	bool isOpen() override { return this->open; }
	void closeAudio() override;
	U32 writeAudio(U8* data, U32 len) override;
	U32 getFragmentSize() override {return this->dspFragSize;}
	void setFragmentSize(U32 size) override;
	U32 getBufferSize() override {return this->getGuestQueuedAudioSizeWant();}
	U32 getBufferCapacity() override { return DSP_BUFFER_SIZE;}
	bool isBwPump() override {
#ifdef __EMSCRIPTEN__
		return this->bwPump;
#else
		return false;
#endif
	}

	U32 bytesPerSampleWant() {
		return SDL_AUDIO_BITSIZE(this->want.format) / 8;
	}

	U32 bytesPerSampleGot() {
		return SDL_AUDIO_BITSIZE(this->got.format) / 8;
	}

	U32 bytesPerSecondWant() {
		return this->want.freq * this->want.channels * bytesPerSampleWant();
	}

	U32 bytesPerSecondGot() {
		return this->got.freq * this->got.channels * bytesPerSampleGot();
	}

	U32 getQueuedAudioSizeWant() {
		if (!KSystem::soundEnabled) {
			return (U32)this->audioBuffer.size();
		}
		if (!this->deviceId) {
			return 0;
		}
		U32 gotBytesPerSecond = bytesPerSecondGot();
		if (!gotBytesPerSecond) {
			return 0;
		}
		return (U32)(((U64)SDL_GetQueuedAudioSize(this->deviceId) * bytesPerSecondWant()) / gotBytesPerSecond);
	}

	U32 getGuestQueuedAudioSizeWant() {
#ifdef __EMSCRIPTEN__
		return this->getEstimatedRealQueuedWant();
#else
		return this->getQueuedAudioSizeWant();
#endif
	}

	U32 getSdlFormat(U32 format) {
		switch (format) {
		case AFMT_MU_LAW:
		case AFMT_A_LAW:
		case AFMT_IMA_ADPCM:
		case AFMT_U8:
			return AUDIO_U8;
		case AFMT_S16_LE:
			return AUDIO_S16LSB;
		case AFMT_S16_BE:
			return AUDIO_S16MSB;
		case AFMT_S8:
			return AUDIO_S8;
		case AFMT_U16_LE:
			return AUDIO_U16LSB;
		case AFMT_U16_BE:
			return AUDIO_U16MSB;
		case AFMT_MPEG:
			return AUDIO_U8;
        case AFMT_FLOAT:
                return AUDIO_F32LSB;
		default:
			kpanic_fmt("KNativeAudioSdl Unknow audio format %d", format);
			return 0;
		}
	}
	SDL_AudioSpec want = { 0 };
	SDL_AudioSpec got = { 0 };
	SDL_AudioCVT cvt = { 0 };
	SDL_AudioStream* stream = nullptr;
	U32 openedFormat = 0;
	int cvtBufLen = 0;
	unsigned char* cvtBuf = nullptr;
	std::vector<U8> streamBuffer;
	bool sameFormat = false;
	U32 dspFragSize = 4096;
	bool open = false;
#ifdef __EMSCRIPTEN__
	bool bwPump = false; // KDspAudioSdl-owned Web Audio bridge active
#endif
	std::deque<U8> audioBuffer; // only used when KSystem::soundEnabled is false
	SDL_AudioDeviceID deviceId = 0;
	U32 realQueuedWant = 0;
	U32 lastRealQueuedTime = 0;
	std::vector<U8> silenceBuffer;

	U32 getEstimatedRealQueuedWant() {
		U32 now = KSystem::getMilliesSinceStart();
		if (!this->lastRealQueuedTime) {
			this->lastRealQueuedTime = now;
			return this->realQueuedWant;
		}
		U32 elapsed = now - this->lastRealQueuedTime;
		if (elapsed) {
			U32 consumed = (U32)(((U64)bytesPerSecondWant() * elapsed) / 1000);
			this->realQueuedWant = consumed >= this->realQueuedWant ? 0 : this->realQueuedWant - consumed;
			this->lastRealQueuedTime = now;
		}
		return this->realQueuedWant;
	}

	void addRealQueuedWant(U32 bytes) {
		U32 queued = getEstimatedRealQueuedWant();
		this->realQueuedWant = std::min((U32)DSP_BUFFER_SIZE, queued + bytes);
	}

	U32 topUpSilence() {
#ifdef __EMSCRIPTEN__
		if (!this->deviceId) {
			return 0;
		}
		if (this->bwPump) {
			return 0; // our pump drains every byte; priming only adds latency
		}
		U32 gotBytesPerSecond = bytesPerSecondGot();
		U32 frameSize = bytesPerSampleGot() * this->got.channels;
		if (!gotBytesPerSecond || !frameSize) {
			return 0;
		}
		U32 queued = SDL_GetQueuedAudioSize(this->deviceId);
		U32 target = (gotBytesPerSecond * 96 / 1000) & ~(frameSize - 1);
		if (queued >= target) {
			return 0;
		}
		U32 bytes = (target - queued) & ~(frameSize - 1);
		if (!bytes) {
			return 0;
		}
		if (this->silenceBuffer.size() < bytes) {
			this->silenceBuffer.resize(bytes);
		}
		SDL_memset(this->silenceBuffer.data(), this->got.silence, bytes);
		SDL_QueueAudio(this->deviceId, this->silenceBuffer.data(), bytes);
		return bytes;
#else
		return 0;
#endif
	}
};

#ifdef __EMSCRIPTEN__
// KDspAudioSdl-owned Web Audio bridge. SDL's emscripten audio driver pump
// (ScriptProcessorNode.onaudioprocess -> HandleAudioProcess) never delivers
// audio to the destination in this build, so writeAudio pushes F32 frames
// into a ring buffer in wasm heap (shared across threads), consumed by our
// own ScriptProcessorNode. This avoids SDL's buffer queue entirely, and
// avoids JS Module state (openAudio and writeAudio may run on different
// threads with different Module objects).
#define BW_RING_FRAMES (1 << 20)
#define BW_RING_MASK (BW_RING_FRAMES - 1)
#define BW_RING_CHANNELS 2
static float bwRing[BW_RING_FRAMES * BW_RING_CHANNELS];
static std::atomic<Uint32> bwRingWrite{0}; // producer (writeAudio), unbounded frame index
static std::atomic<Uint32> bwRingRead{0};  // consumer (bridge onaudioprocess), unbounded frame index
static Uint32 bwRingChannels = 1;
static Uint32 bwInputChannels = 1;
static Uint32 bwSourceRate = 48000;
static bool bwBridgeArmed = false;
static bool pumpLogged = false; // one-time diagnostic: peak of first pushed audio

static void bwAudioEnsureBridge(Uint32 sourceRate, Uint32 sourceChannels) {
	bwRingChannels = std::min(sourceChannels, (Uint32)BW_RING_CHANNELS);
	bwInputChannels = sourceChannels;
	bwSourceRate = sourceRate;
	// Idempotent per AudioContext; recreates if SDL made a new context.
	EM_ASM({
		var SDL2 = Module['SDL2'] || (Module['SDL2'] = {});
		var ctx = SDL2.audioContext;
		if (!ctx) return;
		if (ctx.state === 'suspended' && navigator.userActivation && navigator.userActivation.hasBeenActive) ctx.resume().catch(function() {});
		var outputChannels = $4;
		var buffer = HEAPF32.buffer;
		var sameConfig = SDL2.bwNodeCtx === ctx && SDL2.bwNodeChannels === outputChannels && SDL2.bwNodeRate === $5;
		if (SDL2.bwNode && sameConfig) return;
		if (SDL2.bwNode) { try { SDL2.bwNode.disconnect(); } catch (e) {} }
		var ring = new Float32Array(buffer, $0, $3 * outputChannels);
		var wIdx = new Uint32Array(buffer, $1, 1);
		var rIdx = new Uint32Array(buffer, $2, 1);
		var mask = $3 - 1;
		var step = $5 / ctx.sampleRate;
		var phase = 0;
		var node = ctx.createScriptProcessor(4096, 0, outputChannels);
		node.onaudioprocess = function(e) {
			var outputs = [];
			for (var c = 0; c < outputChannels; c++) {
				outputs[c] = e.outputBuffer.getChannelData(c);
				outputs[c].fill(0);
			}
			var w = Atomics.load(wIdx, 0) >>> 0;
			var r = Atomics.load(rIdx, 0) >>> 0;
			var used = (w - r) >>> 0;
			if (used > $3) used = $3;
			for (var i = 0; i < outputs[0].length; i++) {
				var base = Math.floor(phase);
				if (base >= used) { phase += step; continue; }
				var next = base + 1 < used ? base + 1 : base;
				var fraction = phase - base;
				for (var c = 0; c < outputChannels; c++) {
					var a = ring[((r + base) & mask) * outputChannels + c];
					var b = ring[((r + next) & mask) * outputChannels + c];
					outputs[c][i] = a + (b - a) * fraction;
				}
				phase += step;
			}
			var consumed = Math.floor(phase);
			if (consumed > used) { consumed = used; phase = 0; }
			else phase -= consumed;
			Atomics.store(rIdx, 0, (r + consumed) >>> 0);
		};
		node.connect(ctx.destination);
		SDL2.bwNode = node;
		SDL2.bwNodeCtx = ctx;
		SDL2.bwNodeChannels = outputChannels;
		SDL2.bwNodeRate = $5;
		SDL2.bwAudioWorklet = false;
	}, bwRing, (int)&bwRingWrite, (int)&bwRingRead, BW_RING_FRAMES, bwRingChannels, bwSourceRate);
	bwBridgeArmed = true;
}

// Frames currently in the ring (for closeAudio drain-wait).
static Uint32 bwAudioRingUsed() {
	Uint32 w = bwRingWrite.load(std::memory_order_acquire);
	Uint32 r = bwRingRead.load(std::memory_order_acquire);
	Uint32 used = w - r;
	return used > BW_RING_FRAMES ? BW_RING_FRAMES : used;
}

// Push interleaved F32 frames into the ring. Single producer. Channels above
// stereo are downmixed to two outputs by averaging even and odd source lanes.
static void bwAudioPushJs(const Uint8* bytes, Uint32 byteLen) {
	Uint32 sourceChannels = bwInputChannels;
	Uint32 outputChannels = bwRingChannels;
	if (!sourceChannels || !outputChannels) return;
	Uint32 frames = byteLen / (4 * sourceChannels);
	if (!frames || !bwBridgeArmed) return;
	const float* samples = (const float*)bytes;
	Uint32 w = bwRingWrite.load(std::memory_order_relaxed);
	Uint32 r;
	Uint32 free;
	for (;;) {
		r = bwRingRead.load(std::memory_order_acquire);
		Uint32 used = w - r;
		if (used > BW_RING_FRAMES) used = BW_RING_FRAMES;
		free = BW_RING_FRAMES - used - 1;
		if (frames <= free) break;
		Uint32 drop = frames - free;
		if (bwRingRead.compare_exchange_weak(r, r + drop, std::memory_order_acq_rel)) {
			r += drop;
			break;
		}
	}
	for (Uint32 frame = 0; frame < frames; frame++) {
		Uint32 ringIndex = ((w + frame) & BW_RING_MASK) * outputChannels;
		if (sourceChannels <= 2) {
			for (Uint32 channel = 0; channel < outputChannels; channel++)
				bwRing[ringIndex + channel] = samples[frame * sourceChannels + channel];
		} else {
			for (Uint32 channel = 0; channel < outputChannels; channel++) {
				float sum = 0;
				Uint32 count = 0;
				for (Uint32 source = channel; source < sourceChannels; source += 2) {
					sum += samples[frame * sourceChannels + source];
					count++;
				}
				if (!count) {
					for (Uint32 source = 0; source < sourceChannels; source++) {
						sum += samples[frame * sourceChannels + source];
						count++;
					}
				}
				bwRing[ringIndex + channel] = sum / count;
			}
		}
	}
	bwRingWrite.store(w + frames, std::memory_order_release);
	if (!pumpLogged) {
		pumpLogged = true;
		float peak = 0;
		for (Uint32 i = 0; i < frames * sourceChannels; i++) {
			float a = samples[i] < 0 ? -samples[i] : samples[i];
			if (a > peak) peak = a;
		}
		klog_fmt("bwPump: first push %u frames peak=%.4f", frames, peak);
	}
}
#endif
// Voices whose closeAudio was called while audio was still queued to SDL.
// A timer polls these and finalizes the device close when the queue drains.
static std::list<std::shared_ptr<KDspAudioSdl>> pendingCloses;
static BOXEDWINE_MUTEX pendingClosesMutex;
static SDL_TimerID drainTimer = 0;

static Uint32 SDLCALL drainTimerCb(Uint32 interval, void* /*param*/) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pendingClosesMutex);
	auto it = pendingCloses.begin();
	while (it != pendingCloses.end()) {
		std::shared_ptr<KDspAudioSdl> v = *it;
#ifdef __EMSCRIPTEN__
		bool drained = !v->deviceId || (SDL_GetQueuedAudioSize(v->deviceId) == 0 && (!v->bwPump || bwAudioRingUsed() == 0));
#else
		bool drained = !v->deviceId || SDL_GetQueuedAudioSize(v->deviceId) == 0;
#endif
		if (drained) {
			if (v->deviceId) {
				SDL_CloseAudioDevice(v->deviceId);
				v->deviceId = 0;
			}
			it = pendingCloses.erase(it);
		} else {
			++it;
		}
	}
	if (pendingCloses.empty() && drainTimer) {
		interval = 0; // stop the timer
		drainTimer = 0;
	}
	return interval;
}

static void ensureDrainTimer() {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pendingClosesMutex);
	if (!drainTimer) {
		if (!SDL_WasInit(SDL_INIT_TIMER)) {
			SDL_InitSubSystem(SDL_INIT_TIMER);
		}
		drainTimer = SDL_AddTimer(50, drainTimerCb, nullptr);
	}
}


void KDspAudioSdl::soundEnabled() {
	if (this->open) {
		openAudio(this->openedFormat, this->want.freq, this->want.channels);
	}
}

void KDspAudioSdl::openAudio(U32 format, U32 freq, U32 channels) {
	this->want.callback = nullptr; // SDL_QueueAudio mode
	this->want.userdata = nullptr;
	this->want.format = getSdlFormat(format);
	this->want.freq = freq;
	this->want.channels = channels;
	this->openedFormat = format;

	if (!KSystem::soundEnabled) {
		this->open = true;
		return;
	}

	{
		BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pendingClosesMutex);
		// Drop any pending-close entry; we're reusing this voice.
		std::shared_ptr<KDspAudioSdl> self = shared_from_this();
		for (auto it = pendingCloses.begin(); it != pendingCloses.end();) {
			if (*it == self) {
				it = pendingCloses.erase(it);
			} else {
				++it;
			}
		}
		// Close any prior device on this voice (reopening drops any still-queued audio).
		if (this->deviceId) {
			SDL_CloseAudioDevice(this->deviceId);
			this->deviceId = 0;
		}
		if (this->stream) {
			SDL_FreeAudioStream(this->stream);
			this->stream = nullptr;
		}
	}

    SDL_AudioSpec requested = this->want;
#ifdef __MACH__
    if (requested.freq < 44100) {
        requested.freq = 44100;
    }
#endif
	SDL_AudioDeviceID newId = SDL_OpenAudioDevice(nullptr, 0, &requested, &this->got, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (newId == 0) {
		klog_fmt("Failed to open audio: %s", SDL_GetError());
		return;
	}
	this->deviceId = newId;

	if (this->want.freq != this->got.freq || this->want.channels != this->got.channels || this->want.format != this->got.format) {
		this->sameFormat = false;
		this->stream = SDL_NewAudioStream(this->want.format, this->want.channels, this->want.freq, this->got.format, this->got.channels, this->got.freq);
		if (!this->stream) {
			SDL_BuildAudioCVT(&this->cvt, this->want.format, this->want.channels, this->want.freq, this->got.format, this->got.channels, this->got.freq);
		}
	} else {
		this->sameFormat = true;
	}

	this->open = true;	
	SDL_PauseAudioDevice(this->deviceId, 0);
	klog_fmt("openAudio: freq=%d(got %d) format=%x(got %x) channels=%d(got %d)", this->want.freq, this->got.freq, this->want.format, this->got.format, this->want.channels, this->got.channels);
#ifdef __EMSCRIPTEN__
	// SDL's emscripten driver pump does not deliver audio in this build;
	// feed our own Web Audio bridge instead.
	bwRingRead.store(bwRingWrite.load(std::memory_order_acquire), std::memory_order_release);
	bwAudioEnsureBridge(this->got.freq, this->got.channels);
	this->bwPump = true;
	klog_fmt("bwPump: bridge armed");
#endif
}

void KDspAudioSdl::closeAudio() {
	if (!KSystem::soundEnabled) {
		this->open = false;
		return;
	}
	if (!this->open) {
		return;
	}

	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pendingClosesMutex);
	this->open = false;
	if (!this->deviceId) {
		return;
	}
#ifdef __EMSCRIPTEN__
	bool stillQueued = this->bwPump ? (bwAudioRingUsed() > 0) : (SDL_GetQueuedAudioSize(this->deviceId) > 0);
#else
	bool stillQueued = SDL_GetQueuedAudioSize(this->deviceId) > 0;
#endif
	if (stillQueued) {
		// Leave the device open so the queue can drain; the poll timer will finalize.
		ensureDrainTimer();
		pendingCloses.push_back(shared_from_this());
	} else {
		SDL_CloseAudioDevice(this->deviceId);
		this->deviceId = 0;
	}
}

void KDspAudioSdl::setFragmentSize(U32 size) {
#ifdef __EMSCRIPTEN__
	size = std::clamp(size, (U32)256, (U32)4096);
#else
	size = std::clamp(size, (U32)512, (U32)16384);
#endif
	U32 blockSize = bytesPerSampleWant() * want.channels;
	if (blockSize) {
		size &= ~(blockSize - 1);
	}
	this->dspFragSize = size ? size : blockSize;
}

U32 KDspAudioSdl::writeAudio(U8* data, U32 len) {	
	if (!KSystem::soundEnabled) {
		static U32 timeSinceLastWrite;

		U32 wantBytesPerSecond = want.freq * want.channels * bytesPerSampleWant();
		U32 delay = wantBytesPerSecond / 8;
		U32 elapsedTime = KSystem::getMilliesSinceStart() - timeSinceLastWrite;
		U32 bytesToRemove = wantBytesPerSecond * elapsedTime / 1000;

		bytesToRemove = std::min((U32)this->audioBuffer.size(), bytesToRemove);
		this->audioBuffer.erase(this->audioBuffer.begin(), this->audioBuffer.begin() + bytesToRemove);

		if (this->audioBuffer.size() > delay) {
			return -K_EWOULDBLOCK;
		}
		U32 blockSize = bytesPerSampleWant() * want.channels;
		len = std::min(len, ((delay - (U32)this->audioBuffer.size()) & ~(blockSize - 1)));
		audioBuffer.insert(this->audioBuffer.end(), data, data + len);
		timeSinceLastWrite = KSystem::getMilliesSinceStart();
		return len;
	}

	if (!this->open || !this->deviceId) {
		return 0;
	}
#ifdef __EMSCRIPTEN__
	{
		static int wlog = 0;
		if (wlog < 3) {
			wlog++;
			klog_fmt("bwPump: writeAudio bwPump=%d gotFmt=%x", (int)this->bwPump, this->got.format);
		}
		// Check S16 input peak (want.format is S16) on every write until non-zero seen
		static int nzLogged = 0;
		if (!nzLogged && this->want.format == AUDIO_S16LSB) {
			int peak16 = 0;
			const Sint16* s16 = (const Sint16*)data;
			Uint32 n16 = len / 2;
			for (Uint32 i = 0; i < n16; i++) {
				int a = s16[i] < 0 ? -s16[i] : s16[i];
				if (a > peak16) peak16 = a;
				if (peak16 > 100) break;
			}
			if (peak16 > 100) {
				nzLogged = 1;
				klog_fmt("bwPump: S16 input first non-zero peak=%d (n=%u)", peak16, n16);
			}
		}
	}
#endif

#ifdef __EMSCRIPTEN__
	// bwPump: JS Web Audio bridge handles backpressure via its own ring buffer.
	// The host-side wall-clock queue estimate is unreliable under emulation
	// (native writer thread vs emulator clock) and causes spurious EWOULDBLOCK,
	// stalling winealsa feeder thread so WHDR_DONE never fires. Always accept.
	if (!this->bwPump) {
#endif
	U32 queued = this->getGuestQueuedAudioSizeWant();
	if (queued >= DSP_BUFFER_SIZE) {
		return -K_EWOULDBLOCK;
	}
	U32 blockSize = bytesPerSampleWant() * want.channels;
	len = std::min(len, (DSP_BUFFER_SIZE - queued) & ~(blockSize - 1));
	if (!len) {
		return -K_EWOULDBLOCK;
	}
#ifdef __EMSCRIPTEN__
	}
#endif

	if (!this->sameFormat && this->stream) {
		if (SDL_AudioStreamPut(this->stream, data, (int)len) < 0) {
			return 0;
		}
		int available = SDL_AudioStreamAvailable(this->stream);
		if (available > 0) {
			if ((int)this->streamBuffer.size() < available) {
				this->streamBuffer.resize(available);
			}
			int got = SDL_AudioStreamGet(this->stream, this->streamBuffer.data(), available);
			if (got > 0) {
#ifdef __EMSCRIPTEN__
				if (this->bwPump && this->got.format == AUDIO_F32LSB) bwAudioPushJs(this->streamBuffer.data(), (Uint32)got);
				else
#endif
				SDL_QueueAudio(this->deviceId, this->streamBuffer.data(), got);
			}
		}
	} else if (!this->sameFormat) {
		int needed = (int)len * this->cvt.len_mult;
		if (this->cvtBufLen && this->cvtBufLen < needed) {
			SDL_free(this->cvtBuf);
			this->cvtBuf = nullptr;
			this->cvtBufLen = 0;
		}
		if (!this->cvtBufLen) {
			this->cvtBufLen = needed;
			this->cvtBuf = (Uint8*)SDL_malloc(this->cvtBufLen);
		}
		this->cvt.len = (int)len;
		this->cvt.buf = this->cvtBuf;
		memcpy(this->cvt.buf, data, len);
		SDL_ConvertAudio(&this->cvt);
#ifdef __EMSCRIPTEN__
		if (this->bwPump && this->got.format == AUDIO_F32LSB) bwAudioPushJs(this->cvt.buf, (Uint32)this->cvt.len_cvt);
		else
#endif
		SDL_QueueAudio(this->deviceId, this->cvt.buf, this->cvt.len_cvt);
	} else {
#ifdef __EMSCRIPTEN__
		if (this->bwPump && this->got.format == AUDIO_F32LSB) bwAudioPushJs(data, len);
		else
#endif
		SDL_QueueAudio(this->deviceId, data, len);
	}
	addRealQueuedWant(len);
	topUpSilence();
	return len;
}

static U32 nextId = 0;
BOXEDWINE_MUTEX KDspAudio::mutex;
BHashTable<U32, KDspAudioWeakPtr> KDspAudio::openAudios;

KDspAudioPtr KDspAudio::createDspAudio() {
	KDspAudioPtr result = std::make_shared<KDspAudioSdl>();
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
	result->id = nextId++;
	KDspAudioWeakPtr weak = result;
	openAudios.set(result->id, weak);
	return result;
}

KDspAudio::~KDspAudio() {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
	openAudios.remove(this->id);
}

void KDspAudio::iterateOpenAudio(std::function<void(KDspAudioPtr&)> callback) {
	std::vector<KDspAudioPtr> snapshot;
	{
		BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
		for (auto& it : openAudios) {
			KDspAudioPtr audio = it.value.lock();
			if (audio) snapshot.push_back(std::move(audio));
		}
	}
	// Opening a voice can create/release another voice. Its destructor also
	// takes the registry mutex, so neither callbacks nor final references may
	// be released while that mutex is held.
	for (auto& audio : snapshot) callback(audio);
}

void KDspAudio::shutdown() {
	if (!KSystem::soundEnabled) {
		return;
	}	
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pendingClosesMutex);
	if (drainTimer) {
		SDL_RemoveTimer(drainTimer);
		drainTimer = 0;
	}
	for (auto& v : pendingCloses) {
		if (v->deviceId) {
			SDL_CloseAudioDevice(v->deviceId);
			v->deviceId = 0;
		}
	}
	pendingCloses.clear();
}
