"""GModDemoRender — local speech synthesis with OmniVoice (k2-fsa/OmniVoice, Apache-2.0).

The program writes this file next to its Python environment and runs it:

    python gmdr_voice.py --check [--device auto|cuda|cpu]
        load the model (downloads it on first run) and print "GMDR_READY <device>"
    python gmdr_voice.py --job job.json
        synthesize every item of the job and write 24 kHz mono 16-bit WAV files

job.json:
    {"device": "auto", "num_step": 32,
     "items": [{"text": "...", "language": "de", "out": "C:/.../0001.wav",
                "ref_audio": "ref.wav", "ref_text": "...",     # voice cloning, or
                "instruct": "male, low pitch",                  # a designed voice
                "speed": 1.0}]}

Progress goes to stdout, one line per event, so the program can follow it:
    GMDR_LOADING / GMDR_READY <device> / GMDR_DONE <i> / GMDR_FAIL <i> <message> / GMDR_ERROR <message>
"""
import argparse
import json
import os
import sys
import traceback
import wave


def say(*parts):
    print(" ".join(str(p) for p in parts), flush=True)


def load_model(device_pref):
    import torch
    from omnivoice import OmniVoice

    if device_pref == "cpu" or not torch.cuda.is_available():
        device, dtype = "cpu", torch.float32
    else:
        device, dtype = "cuda:0", torch.float16
    say("GMDR_LOADING")
    model = OmniVoice.from_pretrained("k2-fsa/OmniVoice", device_map=device, dtype=dtype)
    return model, device


def write_wav(path, samples, rate=24000):
    import numpy as np

    pcm = np.clip(np.asarray(samples, dtype=np.float32).reshape(-1), -1.0, 1.0)
    pcm = (pcm * 32767.0).astype("<i2")
    tmp = path + ".part"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    os.replace(tmp, path)


def run_job(job_path):
    with open(job_path, encoding="utf-8") as f:
        job = json.load(f)
    model, device = load_model(job.get("device", "auto"))
    say("GMDR_READY", device)
    num_step = int(job.get("num_step", 32))
    failed = 0
    for i, item in enumerate(job.get("items", [])):
        try:
            kw = {"text": item["text"], "num_step": num_step}
            if item.get("language"):
                kw["language_id"] = item["language"]
            if item.get("ref_audio"):
                kw["ref_audio"] = item["ref_audio"]
                if item.get("ref_text"):
                    kw["ref_text"] = item["ref_text"]
            elif item.get("instruct"):
                kw["instruct"] = item["instruct"]
            if item.get("speed") and abs(float(item["speed"]) - 1.0) > 1e-3:
                kw["speed"] = float(item["speed"])
            audio = model.generate(**kw)
            write_wav(item["out"], audio[0])
            say("GMDR_DONE", i)
        except Exception as e:  # one bad line should not stop the rest
            failed += 1
            say("GMDR_FAIL", i, str(e).replace("\n", " ")[:300])
    return 0 if failed == 0 else 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--job")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--device", default="auto")
    args = ap.parse_args()
    try:
        if args.check:
            _, device = load_model(args.device)
            say("GMDR_READY", device)
            return 0
        if args.job:
            return run_job(args.job)
        ap.print_help()
        return 2
    except Exception as e:
        say("GMDR_ERROR", str(e).replace("\n", " ")[:500])
        traceback.print_exc(file=sys.stdout)
        return 1


if __name__ == "__main__":
    sys.exit(main())
