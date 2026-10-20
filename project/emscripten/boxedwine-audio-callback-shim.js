/*
 * Keep SDL's ScriptProcessor callback from propagating browser audio buffer
 * mismatches into the emulator runtime. SDL assumes each output buffer has
 * the requested ScriptProcessor size and throws from its audio callback when
 * that assumption stops holding (for example while a device is reopened).
 */
(function(global) {
    'use strict';

    if (!global || global.__boxedwineAudioCallbackShimInstalled) return;
    var AudioContextType = global.AudioContext || global.webkitAudioContext;
    if (!AudioContextType || !AudioContextType.prototype ||
            typeof AudioContextType.prototype.createScriptProcessor !== 'function') return;

    var prototype = AudioContextType.prototype;
    var createScriptProcessor = prototype.createScriptProcessor;

    function silenceOutput(event) {
        try {
            var output = event && event.outputBuffer;
            if (!output) return;
            for (var channel = 0; channel < output.numberOfChannels; ++channel) {
                output.getChannelData(channel).fill(0);
            }
        } catch (_) {
            // Audio callbacks must never throw into the browser event loop.
        }
    }

    function findHandlerDescriptor(node) {
        var current = node;
        while (current) {
            var descriptor = Object.getOwnPropertyDescriptor(current, 'onaudioprocess');
            if (descriptor) return descriptor;
            current = Object.getPrototypeOf(current);
        }
        return null;
    }

    prototype.createScriptProcessor = function(bufferSize) {
        var node = createScriptProcessor.apply(this, arguments);
        var expectedSize = Number(bufferSize) >>> 0;
        if (!expectedSize && node) expectedSize = Number(node.bufferSize) >>> 0;
        if (!node || !expectedSize) return node;

        var descriptor = findHandlerDescriptor(node);
        if (!descriptor || typeof descriptor.set !== 'function') return node;

        var handler = typeof descriptor.get === 'function'
            ? descriptor.get.call(node) : null;
        Object.defineProperty(node, 'onaudioprocess', {
            configurable: true,
            enumerable: descriptor.enumerable,
            get: function() { return handler; },
            set: function(callback) {
                handler = callback;
                var guarded = typeof callback === 'function' ? function(event) {
                    try {
                        if (event && event.outputBuffer &&
                                event.outputBuffer.length !== expectedSize) {
                            silenceOutput(event);
                            return;
                        }
                        callback.call(node, event);
                    } catch (_) {
                        silenceOutput(event);
                    }
                } : callback;
                descriptor.set.call(node, guarded);
            }
        });
        return node;
    };

    global.__boxedwineAudioCallbackShimInstalled = true;
})(globalThis);
