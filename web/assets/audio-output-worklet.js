class BWAudioOutputProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const config = options.processorOptions;
    this.ringFrames = config.ringFrames;
    this.channels = config.channels;
    this.mask = this.ringFrames - 1;
    this.sourceRate = config.sourceRate;
    this.step = this.sourceRate / sampleRate;
    this.phase = 0;
    this.totalOutputFrames = 0;
    this.totalNonzeroFrames = 0;
    this.reportOutputFrames = 0;
    this.reportNonzeroFrames = 0;
    this.totalFramesRead = 0;
    this.totalUnderrunFrames = 0;
    this.reportUnderrunFrames = 0;
    this.reportInputPeak = 0;
    this.reportOutputPeak = 0;
    this.failed = false;
    this.ring = new Float32Array(config.buffer, config.ringOffset, this.ringFrames * this.channels);
    this.writeIndex = new Uint32Array(config.buffer, config.writeOffset, 1);
    this.readIndex = new Uint32Array(config.buffer, config.readOffset, 1);
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    if (!output?.length) return true;
    for (const channel of output) channel.fill(0);
    const frameCount = output[0].length;
    let write = 0;
    let read = 0;
    let available = 0;
    try {
      write = Atomics.load(this.writeIndex, 0) >>> 0;
      read = Atomics.load(this.readIndex, 0) >>> 0;
      available = (write - read) >>> 0;
      if (available > this.ringFrames) available = this.ringFrames;
      if (this.step > 0 && Number.isFinite(this.step)) {
        for (let frame = 0; frame < frameCount; frame++) {
          const base = Math.floor(this.phase);
          if (base < available) {
            const next = base + 1 < available ? base + 1 : base;
            const fraction = this.phase - base;
            for (let channel = 0; channel < output.length; channel++) {
              const sourceChannel = Math.min(channel, this.channels - 1);
              const indexA = (((read + base) & this.mask) * this.channels) + sourceChannel;
              const indexB = (((read + next) & this.mask) * this.channels) + sourceChannel;
              const a = this.ring[indexA];
              const b = this.ring[indexB];
              this.reportInputPeak = Math.max(this.reportInputPeak, Math.abs(a), Math.abs(b));
              output[channel][frame] = a + (b - a) * fraction;
            }
          } else {
            this.totalUnderrunFrames++;
            this.reportUnderrunFrames++;
          }
          this.phase += this.step;
        }
        let consumed = Math.floor(this.phase);
        if (consumed > available) {
          consumed = available;
          this.phase = 0;
        } else {
          this.phase -= consumed;
        }
        Atomics.store(this.readIndex, 0, (read + consumed) >>> 0);
        this.totalFramesRead += consumed;
      }
    } catch {
      this.failed = true;
      for (const channel of output) channel.fill(0);
    }

    let nonzeroFrames = 0;
    for (let frame = 0; frame < frameCount; frame++) {
      let audible = false;
      for (let channel = 0; channel < output.length; channel++) {
        const sample = Math.abs(output[channel][frame]);
        this.reportOutputPeak = Math.max(this.reportOutputPeak, sample);
        if (sample > 0.001) audible = true;
      }
      if (audible) nonzeroFrames++;
    }
    this.totalOutputFrames += frameCount;
    this.totalNonzeroFrames += nonzeroFrames;
    this.reportOutputFrames += frameCount;
    this.reportNonzeroFrames += nonzeroFrames;
    if (this.reportOutputFrames >= sampleRate * 5) {
      this.port.postMessage({
        type: 'bw-audio-metrics',
        outputFrames: this.reportOutputFrames,
        nonzeroOutputFrames: this.reportNonzeroFrames,
        totalOutputFrames: this.totalOutputFrames,
        totalNonzeroOutputFrames: this.totalNonzeroFrames,
        framesWritten: Atomics.load(this.writeIndex, 0) >>> 0,
        framesRead: this.totalFramesRead,
        underrunFrames: this.reportUnderrunFrames,
        totalUnderrunFrames: this.totalUnderrunFrames,
        availableFrames: available,
        rateIn: this.sourceRate,
        rateOut: sampleRate,
        inputPeak: this.reportInputPeak,
        outputPeak: this.reportOutputPeak,
        workletError: this.failed,
      });
      this.reportOutputFrames = 0;
      this.reportNonzeroFrames = 0;
      this.reportUnderrunFrames = 0;
      this.reportInputPeak = 0;
      this.reportOutputPeak = 0;
    }
    return true;
  }
}

registerProcessor('bw-audio-output', BWAudioOutputProcessor);
