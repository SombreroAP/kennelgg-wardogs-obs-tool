"""Listen to the streamer's microphone (16 kHz mono PCM, sent by the OBS plugin over the bridge).

Two jobs:
  1. Name manual clips. When the plugin saves a clip the streamer asked for, the words said
     from ~8 s before the moment to ~4 s after become the clip's title ("Insane Triple Through
     Smoke"), and the whole sentence goes into the clip's .json. Whisper (faster-whisper, base
     model, int8, CPU) does that; Vosk is the fallback when Whisper is not available.
  2. Commands. Vosk listens all the time (it is light: ~10 % of one core) for the wake word
     followed by a command, and the plugin does the rest: "kennel replay", "kennel clip",
     "kennel show bouga", "kennel back", "kennel dual", "kennel highlights".

Nothing is written to disk and nothing leaves the PC: both models run locally. They are
downloaded once into ProgramData\\Kennel.gg\\ClipHound\\models the first time voice is on.
"""
import json
import os
import re
import sys
import threading
import time
import zipfile

import numpy as np

RATE = 16000
RING_S = 30                       # seconds of microphone kept for naming
BEFORE_S, AFTER_S = 8.0, 4.0      # the window around a manual clip that names it
VOICE_AFTER_S = 7.0               # "kennel clip that" ... then the sentence that names it
MAX_TITLE_WORDS = 7

VOSK_URL = "https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
VOSK_DIR = "vosk-model-small-en-us-0.15"
WHISPER_MODEL = "base"            # ~75 MB int8; "small" is better and 3x slower

COMMANDS = [
    # (regex after the wake word, cmd, has-name)
    (r"^(?:instant )?replay\b", "replay", False),
    (r"^clip(?: that| this| it)?\b", "clip", False),
    (r"^(?:show|switch to|swap to|go to|watch)\s+(.+)$", "show", True),
    (r"^(?:back|me|my pov|my p o v|mine)\b", "me", False),
    (r"^dual (?:on|up)\b", "dual_on", False),
    (r"^dual off\b", "dual_off", False),
    (r"^dual\b", "dual", False),
    (r"^(?:play )?(?:highlights?|compilation|montage)\b", "highlights", False),
]


def models_dir() -> str:
    base = os.path.dirname(os.path.abspath(sys.argv[0]))
    d = os.path.join(base, "models")
    os.makedirs(d, exist_ok=True)
    return d


class Voice:
    def __init__(self, bridge, cfg: dict):
        self.b = bridge
        self.cfg = cfg
        self.enabled = False
        self.wake = "kennel"
        self.commands = True
        self.names = True
        self.squad: list[str] = []
        self._ring = np.zeros(RATE * RING_S, np.int16)
        self._ring_pos = 0
        self._ring_epoch = 0.0          # wall clock of the newest sample in the ring
        self._lock = threading.Lock()
        self._q: list[bytes] = []
        self._q_cv = threading.Condition()
        self._vosk = None
        self._vosk_state = "off"
        self._whisper = None
        self._whisper_state = "off"
        self._worker = None
        self._loader = None
        self._last_cmd = 0.0
        self._last_clip_cmd = 0.0   # when "kennel clip" was last heard: the name comes after it
        self._status = ""

    # ----- from the plugin -----
    def configure(self, o: dict):
        self.enabled = bool(o.get("enabled", True))
        self.wake = str(o.get("wake") or "kennel").strip().lower()
        self.commands = bool(o.get("commands", True))
        self.names = bool(o.get("names", True))
        self.squad = [str(n) for n in (o.get("squad") or [])]
        if self.enabled and self._loader is None:
            self._loader = threading.Thread(target=self._load_models, daemon=True)
            self._loader.start()
        if self.enabled and self._worker is None:
            self._worker = threading.Thread(target=self._run, daemon=True)
            self._worker.start()
        self._say("listening" if self.enabled else "off")

    def feed(self, pcm: bytes):
        """Called from the bridge thread with ~100 ms of 16 kHz mono int16."""
        if not self.enabled or not pcm:
            return
        a = np.frombuffer(pcm, np.int16)
        with self._lock:
            n = len(a)
            end = self._ring_pos + n
            if end <= len(self._ring):
                self._ring[self._ring_pos:end] = a
            else:
                k = len(self._ring) - self._ring_pos
                self._ring[self._ring_pos:] = a[:k]
                self._ring[:n - k] = a[k:]
            self._ring_pos = end % len(self._ring)
            self._ring_epoch = time.time()
        with self._q_cv:
            self._q.append(pcm)
            self._q_cv.notify()

    def name_clip(self, path: str, epoch: float):
        """The plugin saved a manual clip at `epoch`: name it from what was said around then."""
        if not self.enabled or not self.names:
            return
        threading.Thread(target=self._name, args=(path, epoch), daemon=True).start()

    # ----- inside -----
    def _say(self, text: str):
        if text != self._status:
            self._status = text
            print(f"[voice] {text}")
            self.b.send({"type": "voice_status", "text": text})

    def _load_models(self):
        d = models_dir()
        # Vosk: the always-on listener
        try:
            import vosk
            vosk.SetLogLevel(-1)
            vd = os.path.join(d, VOSK_DIR)
            if not os.path.isdir(vd):
                self._say("downloading the command model (40 MB)")
                import requests
                z = vd + ".zip"
                with requests.get(VOSK_URL, stream=True, timeout=60) as r:
                    r.raise_for_status()
                    with open(z, "wb") as fh:
                        for chunk in r.iter_content(1 << 20):
                            fh.write(chunk)
                with zipfile.ZipFile(z) as zf:
                    zf.extractall(d)
                os.remove(z)
            self._vosk = vosk.Model(vd)
            self._vosk_state = "ready"
        except Exception as e:
            self._vosk_state = f"unavailable ({e})"
            print(f"[voice] vosk: {e}")
        # Whisper: the namer
        try:
            from faster_whisper import WhisperModel
            self._say("loading the naming model" if os.path.isdir(os.path.join(d, "whisper")) else
                      "downloading the naming model (75 MB)")
            self._whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8",
                                         download_root=os.path.join(d, "whisper"))
            self._whisper_state = "ready"
        except Exception as e:
            self._whisper_state = f"unavailable ({e})"
            print(f"[voice] whisper: {e}")
        if self._vosk is None and self._whisper is None:
            self._say("no speech model could be loaded; see cliphound.log")
        elif self._vosk is None:
            self._say("naming ready; commands unavailable")
        elif self._whisper is None:
            self._say("commands ready; clips named with the small model")
        else:
            self._say("listening: commands and clip names ready")

    def _run(self):
        """Feed the microphone to Vosk and act on the wake word."""
        rec = None
        while True:
            with self._q_cv:
                while not self._q:
                    self._q_cv.wait(1.0)
                    if not self._q:
                        continue
                chunk = b"".join(self._q)
                self._q.clear()
            if not self.commands or self._vosk is None:
                continue
            if rec is None:
                from vosk import KaldiRecognizer
                rec = KaldiRecognizer(self._vosk, RATE)
                rec.SetWords(False)
            try:
                if rec.AcceptWaveform(chunk):
                    text = json.loads(rec.Result()).get("text", "")
                    if text:
                        self._heard(text)
                else:
                    # a command should not wait for a pause in the talking: look at the partial too
                    part = json.loads(rec.PartialResult()).get("partial", "")
                    if part and self.wake in part and self._heard(part, partial=True):
                        rec.Reset()
            except Exception as e:
                print(f"[voice] recogniser: {e}")
                rec = None

    def _heard(self, text: str, partial: bool = False) -> bool:
        t = " " + re.sub(r"[^a-z0-9' ]", " ", text.lower()) + " "
        t = re.sub(r"\s+", " ", t)
        i = t.rfind(" " + self.wake + " ")
        if i < 0:
            return False
        after = t[i + len(self.wake) + 2:].strip()
        if not after:
            return False
        for pat, cmd, has_name in COMMANDS:
            m = re.match(pat, after)
            if not m:
                continue
            name = self.digits(m.group(1).strip()) if has_name else ""
            if has_name and partial and len(name) < 3:
                return False           # the name is still being said
            now = time.time()
            if now - self._last_cmd < 2.0:
                return True            # the same command, heard twice (partial then final)
            self._last_cmd = now
            if cmd == "clip":
                self._last_clip_cmd = now
            print(f"[voice] command: {cmd} {name!r}  <- {text!r}")
            self.b.send({"type": "voice", "cmd": cmd, "name": name, "heard": text})
            return True
        return False

    def _window(self, t0: float, t1: float) -> np.ndarray:
        """Microphone samples between two wall-clock times, from the ring."""
        with self._lock:
            newest = self._ring_epoch
            ring = self._ring.copy()
            pos = self._ring_pos
        # the ring ends at `newest`; sample s is (len - s) samples before it
        lin = np.concatenate([ring[pos:], ring[:pos]])       # oldest .. newest
        n = len(lin)
        a = int(n - (newest - t0) * RATE)
        z = int(n - (newest - t1) * RATE)
        a, z = max(0, a), max(0, min(n, z))
        return lin[a:z] if z > a else lin[:0]

    def _name(self, path: str, epoch: float):
        # asked by voice ("kennel clip that ..."): the sentence that follows names it, so the
        # window runs from the command onwards and waits for it. Asked from the dock or a
        # hotkey: what was being said around the moment
        by_voice = abs(epoch - self._last_clip_cmd) < 4.0
        t0, t1 = (self._last_clip_cmd - 1.0, self._last_clip_cmd + VOICE_AFTER_S) if by_voice \
            else (epoch - BEFORE_S, epoch + AFTER_S)
        wait = t1 - time.time() + 0.3
        if wait > 0:
            time.sleep(min(wait, VOICE_AFTER_S + 1))
        audio = self._window(t0, t1)
        if len(audio) < RATE:
            self.b.send({"type": "clip_name", "path": path, "title": "", "text": ""})
            return
        text = ""
        try:
            if self._whisper is not None:
                f = audio.astype(np.float32) / 32768.0
                segs, _ = self._whisper.transcribe(f, language="en", beam_size=2, vad_filter=True,
                                                   condition_on_previous_text=False)
                text = " ".join(s.text.strip() for s in segs).strip()
            elif self._vosk is not None:
                from vosk import KaldiRecognizer
                rec = KaldiRecognizer(self._vosk, RATE)
                rec.AcceptWaveform(audio.tobytes())
                text = json.loads(rec.FinalResult()).get("text", "")
        except Exception as e:
            print(f"[voice] naming: {e}")
        title = self.title_from(text, self.wake)
        print(f"[voice] clip name: {title!r}  <- {text!r}")
        self.b.send({"type": "clip_name", "path": path, "title": title, "text": text})

    @staticmethod
    def digits(name: str) -> str:
        """'bouga three four' -> 'bouga34': names carry numbers, recognisers say them as words."""
        nums = {"zero": "0", "oh": "0", "one": "1", "two": "2", "three": "3", "four": "4", "five": "5",
                "six": "6", "seven": "7", "eight": "8", "nine": "9"}
        out = []
        for w in name.split():
            if w in nums and out and (out[-1][-1].isdigit() or len(out[-1]) > 0):
                if out[-1][-1].isdigit():
                    out[-1] += nums[w]
                else:
                    out[-1] += nums[w]
            elif w in nums:
                out.append(nums[w])
            else:
                out.append(w)
        return " ".join(out)

    @staticmethod
    def title_from(text: str, wake: str = "kennel") -> str:
        """A few words fit for a file name: the command words and filler dropped, Title Case."""
        t = text.lower()
        t = re.sub(r"[^a-z0-9' ]", " ", t)
        # "kennel clip that <what it was>": what follows the ask is the title, whatever came before
        ask = re.compile(r"\b" + re.escape(wake) + r"\b\s*(clip|replay|show|back|dual|highlights)?(\s+(that|this|it))?\s*")
        m = None
        for m in ask.finditer(t):
            pass
        if m and t[m.end():].strip():
            t = t[m.end():]
        else:
            t = ask.sub(" ", t)
        t = re.sub(r"\b(clip|save|record)\s+(that|this|it)\b", " ", t)
        filler = {"uh", "um", "like", "yeah", "okay", "ok", "oh", "so", "just", "bro", "dude", "man", "guys", "chat",
                  "holy", "wow", "lets", "let's", "please"}
        words = [w for w in t.split() if w not in filler]
        # leading noise ("that was", "oh my god") goes; the little words inside a phrase stay
        lead = {"that", "this", "it", "was", "is", "my", "god", "what", "no", "way", "there", "we", "and", "the", "a"}
        while words and words[0] in lead:
            words.pop(0)
        if not words:
            return ""
        words = words[:MAX_TITLE_WORDS] if len(words) > MAX_TITLE_WORDS else words
        return " ".join(w.capitalize() for w in words)[:48].strip()
