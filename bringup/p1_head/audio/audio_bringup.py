#!/usr/bin/env python3
"""P1 head audio bringup utility (Phase 2).

Validates Python-side audio I/O against the Cubilux DAC + Plugable USB mic
chain documented in roz_docs/implementations/p1_head/audio/wiring.md.

Subcommands:
    list                          List audio devices and defaults.
    record FILE [-d s] [--device] Record mono 16-bit PCM to a WAV file.
    play   FILE       [--device]  Play a WAV file.
    loopback   [-d s] [--input --output]
                                  Record then immediately play back.

Devices may be selected by index (from `list`) or by case-insensitive name
substring (e.g. --device cubilux, --input plugable).
"""
import argparse
import os
import sys
import time

import numpy as np
import sounddevice as sd
import soundfile as sf


def cmd_list(args):
    print(sd.query_devices())
    print()
    in_idx, out_idx = sd.default.device
    print(f"Default input  : [{in_idx}] {sd.query_devices(in_idx)['name']}")
    print(f"Default output : [{out_idx}] {sd.query_devices(out_idx)['name']}")


def find_device(spec, kind):
    if spec is None:
        return sd.default.device[0 if kind == "input" else 1]
    try:
        return int(spec)
    except ValueError:
        pass
    channels_key = "max_input_channels" if kind == "input" else "max_output_channels"
    matches = [
        (i, d["name"])
        for i, d in enumerate(sd.query_devices())
        if d[channels_key] > 0 and spec.lower() in d["name"].lower()
    ]
    if not matches:
        sys.exit(f"No {kind} device matching '{spec}'. Run `list` to see options.")
    if len(matches) > 1:
        names = ", ".join(f"[{i}] {n}" for i, n in matches)
        sys.exit(f"Ambiguous {kind} match for '{spec}': {names}")
    return matches[0][0]


def negotiate_rate(device, kind, requested):
    """Return a sample rate the device accepts, falling back from requested.

    Opening a USB audio device by PortAudio index uses ALSA `hw:` directly,
    which only allows the device's native rates. We probe `requested` first,
    then a list of common rates, and warn if we had to fall back.
    """
    check = sd.check_input_settings if kind == "input" else sd.check_output_settings
    candidates = [requested, 48000, 44100, 32000, 16000]
    tried = set()
    for rate in candidates:
        if rate in tried:
            continue
        tried.add(rate)
        try:
            check(device=device, samplerate=rate, channels=1, dtype="int16")
        except sd.PortAudioError:
            continue
        if rate != requested:
            print(
                f"NOTE: device does not support {requested} Hz; using {rate} Hz. "
                f"Resample in software if {requested} Hz is needed downstream."
            )
        return rate
    sys.exit(f"No supported sample rate for device {device} (tried {sorted(tried)}).")


def report_peak(audio):
    peak = int(np.abs(audio).max())
    full = 32767
    pct = peak / full * 100
    bar = "#" * int(pct / 2)
    print(f"Peak: {peak:>5}/{full} ({pct:5.1f}%) |{bar:<50}|")


def cmd_record(args):
    dev = find_device(args.device, "input")
    name = sd.query_devices(dev)["name"]
    rate = negotiate_rate(dev, "input", args.rate)
    print(f"Recording {args.duration}s @ {rate} Hz mono from [{dev}] {name}...")
    audio = sd.rec(
        int(args.duration * rate),
        samplerate=rate,
        channels=1,
        dtype="int16",
        device=dev,
    )
    sd.wait()
    sf.write(args.file, audio, rate, subtype="PCM_16")
    print(f"Wrote {args.file}")
    report_peak(audio)


def cmd_play(args):
    dev = find_device(args.device, "output")
    audio, rate = sf.read(args.file, dtype="int16")
    name = sd.query_devices(dev)["name"]
    duration = len(audio) / rate
    print(f"Playing {args.file} ({duration:.2f}s @ {rate} Hz) on [{dev}] {name}...")
    sd.play(audio, samplerate=rate, device=dev)
    sd.wait()


def resample_int16(audio, src_rate, dst_rate):
    if src_rate == dst_rate:
        return audio
    from math import gcd
    from scipy.signal import resample_poly
    g = gcd(src_rate, dst_rate)
    out = resample_poly(audio.astype(np.float32), dst_rate // g, src_rate // g)
    return np.clip(out, -32768, 32767).astype(np.int16)


def cmd_say(args):
    from piper.voice import PiperVoice  # late import; pulls in onnxruntime

    if not os.path.exists(args.model):
        sys.exit(f"Voice model not found at {args.model}. See README for download steps.")

    out_dev = find_device(args.device, "output")
    out_name = sd.query_devices(out_dev)["name"]

    print(f"Loading voice {os.path.basename(args.model)}...")
    voice = PiperVoice.load(args.model)

    print(f"Synthesizing {args.text!r}...")
    chunks = list(voice.synthesize(args.text))
    if not chunks:
        sys.exit("No audio chunks returned from voice.synthesize().")
    first = chunks[0]
    if isinstance(first, (bytes, bytearray)):
        # piper-tts <=1.2 streamed raw bytes
        src_rate = voice.config.sample_rate
        audio_bytes = b"".join(chunks)
    elif hasattr(first, "audio_int16_bytes"):
        # piper-tts >=1.3 yields AudioChunk objects
        src_rate = first.sample_rate
        audio_bytes = b"".join(c.audio_int16_bytes for c in chunks)
    else:
        sys.exit(
            f"Unexpected synthesize() chunk type {type(first).__name__}; "
            "piper-tts API may have changed again."
        )
    audio = np.frombuffer(audio_bytes, dtype=np.int16)
    print(f"Got {len(audio)} samples at {src_rate} Hz native.")

    target_rate = negotiate_rate(out_dev, "output", src_rate)
    audio = resample_int16(audio, src_rate, target_rate)

    duration = len(audio) / target_rate
    print(f"Playing {duration:.2f}s on [{out_dev}] {out_name} @ {target_rate} Hz...")
    sd.play(audio, samplerate=target_rate, device=out_dev)
    sd.wait()


def cmd_loopback(args):
    in_dev = find_device(args.input, "input")
    out_dev = find_device(args.output, "output")
    in_name = sd.query_devices(in_dev)["name"]
    out_name = sd.query_devices(out_dev)["name"]
    rate = negotiate_rate(in_dev, "input", args.rate)
    sd.check_output_settings(device=out_dev, samplerate=rate, channels=1, dtype="int16")
    print(f"Recording {args.duration}s @ {rate} Hz from [{in_dev}] {in_name}...")
    audio = sd.rec(
        int(args.duration * rate),
        samplerate=rate,
        channels=1,
        dtype="int16",
        device=in_dev,
    )
    sd.wait()
    report_peak(audio)
    print(f"Playing back via [{out_dev}] {out_name}...")
    sd.play(audio, samplerate=rate, device=out_dev)
    sd.wait()


def cmd_interactive(args):
    """Long-running REPL: keeps Piper voice resident and audio devices warm.

    Demonstrates the eventual roz_server topology -- after the one-time cold
    start (voice load + onnxruntime session init), `say` operations run at
    inference speed (sub-200 ms for short utterances on Jetson Orin Nano CPU).
    """
    from piper.voice import PiperVoice

    if not os.path.exists(args.model):
        sys.exit(f"Voice model not found at {args.model}.")

    in_dev = find_device(args.input, "input")
    out_dev = find_device(args.output, "output")
    in_name = sd.query_devices(in_dev)["name"]
    out_name = sd.query_devices(out_dev)["name"]

    print(f"Loading voice {os.path.basename(args.model)}...")
    t0 = time.monotonic()
    voice = PiperVoice.load(args.model)
    load_ms = (time.monotonic() - t0) * 1000

    print("Warming up onnxruntime + resample...")
    t0 = time.monotonic()
    list(voice.synthesize("a"))  # discard; triggers ORT graph optimization
    resample_int16(np.zeros(100, dtype=np.int16), 22050, 48000)  # warm scipy
    warmup_ms = (time.monotonic() - t0) * 1000

    print(f"Ready (voice {load_ms:.0f} ms, warmup {warmup_ms:.0f} ms).")
    print(f"  Input  : [{in_dev}] {in_name}")
    print(f"  Output : [{out_dev}] {out_name}")
    print("Commands: say <text> | record <sec> <file> | play <file> | quit")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        cmd, _, rest = line.partition(" ")
        cmd = cmd.lower()

        if cmd in ("quit", "exit", "q"):
            break

        if cmd == "say":
            if not rest:
                print("usage: say <text>")
                continue
            t0 = time.monotonic()
            chunks = list(voice.synthesize(rest))
            if not chunks:
                print("(no audio)")
                continue
            first = chunks[0]
            if isinstance(first, (bytes, bytearray)):
                src_rate = voice.config.sample_rate
                audio_bytes = b"".join(chunks)
            else:
                src_rate = first.sample_rate
                audio_bytes = b"".join(c.audio_int16_bytes for c in chunks)
            audio = np.frombuffer(audio_bytes, dtype=np.int16)
            target_rate = negotiate_rate(out_dev, "output", src_rate)
            audio = resample_int16(audio, src_rate, target_rate)
            synth_ms = (time.monotonic() - t0) * 1000
            print(
                f"  synth+resample: {synth_ms:.0f} ms "
                f"({len(audio)/target_rate:.2f}s audio)"
            )
            sd.play(audio, samplerate=target_rate, device=out_dev)
            sd.wait()

        elif cmd == "record":
            parts = rest.split(maxsplit=1)
            if len(parts) != 2:
                print("usage: record <sec> <file>")
                continue
            try:
                duration = float(parts[0])
            except ValueError:
                print("usage: record <sec> <file>")
                continue
            target_rate = negotiate_rate(in_dev, "input", args.rate)
            print(f"Recording {duration}s @ {target_rate} Hz...")
            audio = sd.rec(
                int(duration * target_rate),
                samplerate=target_rate,
                channels=1,
                dtype="int16",
                device=in_dev,
            )
            sd.wait()
            sf.write(parts[1], audio, target_rate, subtype="PCM_16")
            print(f"  wrote {parts[1]}")
            report_peak(audio)

        elif cmd == "play":
            if not rest:
                print("usage: play <file>")
                continue
            try:
                audio, rate = sf.read(rest, dtype="int16")
            except Exception as e:
                print(f"  error: {e}")
                continue
            target_rate = negotiate_rate(out_dev, "output", rate)
            audio = resample_int16(audio, rate, target_rate)
            sd.play(audio, samplerate=target_rate, device=out_dev)
            sd.wait()

        else:
            print(f"unknown command: {cmd!r}. Try: say, record, play, quit")

    print("bye.")


def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("list", help="List audio devices")
    sp.set_defaults(func=cmd_list)

    sp = sub.add_parser("record", help="Record audio to a WAV file")
    sp.add_argument("file")
    sp.add_argument("-d", "--duration", type=float, default=5.0)
    sp.add_argument("-r", "--rate", type=int, default=48000)
    sp.add_argument("--device", help="Input device (index or name substring)")
    sp.set_defaults(func=cmd_record)

    sp = sub.add_parser("play", help="Play a WAV file")
    sp.add_argument("file")
    sp.add_argument("--device", help="Output device (index or name substring)")
    sp.set_defaults(func=cmd_play)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_model = os.environ.get(
        "PIPER_MODEL",
        os.path.join(script_dir, "piper-voices", "en_US-lessac-medium.onnx"),
    )
    sp = sub.add_parser("say", help="Synthesize text via Piper TTS and play it")
    sp.add_argument("text")
    sp.add_argument(
        "--model",
        default=default_model,
        help=f"Path to a Piper voice .onnx file (default: {default_model})",
    )
    sp.add_argument("--device", help="Output device (index or name substring)")
    sp.set_defaults(func=cmd_say)

    sp = sub.add_parser("loopback", help="Record then play back")
    sp.add_argument("-d", "--duration", type=float, default=5.0)
    sp.add_argument("-r", "--rate", type=int, default=48000)
    sp.add_argument("--input", help="Input device (index or name substring)")
    sp.add_argument("--output", help="Output device (index or name substring)")
    sp.set_defaults(func=cmd_loopback)

    sp = sub.add_parser("interactive", help="Long-running REPL with voice + devices warm")
    sp.add_argument(
        "--model",
        default=default_model,
        help=f"Path to a Piper voice .onnx file (default: {default_model})",
    )
    sp.add_argument("--input", help="Input device (index or name substring)")
    sp.add_argument("--output", help="Output device (index or name substring)")
    sp.add_argument("-r", "--rate", type=int, default=48000)
    sp.set_defaults(func=cmd_interactive)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
