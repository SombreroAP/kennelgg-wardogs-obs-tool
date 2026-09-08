"""ClipHound first-run setup. Asks the questions, checks OBS, writes config.yaml.

  python setup.py            interactive
  python setup.py --check    just re-test the current config (OBS reachable, source found, feed region)

Works for one-PC and two-PC layouts: you pick whichever OBS input carries the gameplay,
a capture card, NDI feed or Game Capture, from the list OBS reports.
"""
import os
import sys

import yaml

CFG = "config.yaml"


def ask(prompt, default=None, secret=False):
    d = f" [{default}]" if default not in (None, "") else ""
    v = input(f"{prompt}{d}: ").strip()
    return v or (default or "")


def main():
    cfg = yaml.safe_load(open(CFG, encoding="utf-8"))
    check_only = "--check" in sys.argv
    print("=== ClipHound setup ===\n")

    if not check_only:
        print("OBS > Tools > WebSocket Server Settings: enable the server, port 4455, set a password.")
        cfg["obs"]["host"] = ask("OBS host (127.0.0.1 if OBS runs on this PC)", cfg["obs"].get("host") or "127.0.0.1")
        cfg["obs"]["port"] = int(ask("OBS websocket port", cfg["obs"].get("port") or 4455))
        cfg["obs"]["password"] = ask("OBS websocket password", cfg["obs"].get("password"))

    # --- connect and list inputs ---------------------------------------------------------
    try:
        import obsws_python as obs
        cl = obs.ReqClient(host=cfg["obs"]["host"], port=cfg["obs"]["port"], password=cfg["obs"]["password"], timeout=4)
        ver = cl.get_version()
        print(f"\nConnected: OBS {ver.obs_version}, websocket {ver.obs_web_socket_version}")
    except Exception as e:
        print(f"\nCannot reach OBS at {cfg['obs']['host']}:{cfg['obs']['port']} - {e}")
        print("Check OBS is open, the websocket server is enabled and the password matches, then run setup again.")
        _save(cfg)
        return
    inputs = [i for i in cl.get_input_list().inputs if not i["inputKind"].startswith(("wasapi", "coreaudio", "pulse"))]
    print("\nVideo inputs OBS reports (pick the one that shows the GAMEPLAY - on a two-PC setup that is")
    print("the capture card or NDI input, on one PC it is the Game Capture):")
    cur = cfg["capture"].get("obs_source")
    for n, i in enumerate(inputs, 1):
        mark = "  <- current" if i["inputName"] == cur else ""
        print(f"  {n:2}. {i['inputName']}   ({i['inputKind']}){mark}")
    if not check_only:
        default = str(next((n for n, i in enumerate(inputs, 1) if i["inputName"] == cur), "")) or ""
        pick = ask("Number of the gameplay input", default)
        if pick.isdigit() and 1 <= int(pick) <= len(inputs):
            cfg["capture"]["obs_source"] = inputs[int(pick) - 1]["inputName"]
        elif pick:
            cfg["capture"]["obs_source"] = pick
        cfg["capture"]["backend"] = "obs"

    # --- rest of the questions --------------------------------------------------------------
    if not check_only:
        cfg["detection"]["player_name"] = ask("Your in-game name as it appears in the kill feed", cfg["detection"].get("player_name"))
        cfg["obs"]["library"] = ask("Clip library folder for renamed replays (blank = keep OBS's recording folder)", cfg["obs"].get("library"))
        tw = ask("Enable Twitch clips now? y/n", "y" if cfg["twitch"].get("enabled") else "n").lower().startswith("y")
        cfg["twitch"]["enabled"] = tw
        if tw:
            print("Twitch app from https://dev.twitch.tv/console/apps (logged in as the clipping account, e.g. InfoKennel),")
            print("redirect URL http://localhost:3000. Then run:  python get_token.py")
            cfg["twitch"]["client_id"] = ask("Twitch client id", cfg["twitch"].get("client_id"))
            cfg["twitch"]["client_secret"] = ask("Twitch client secret", cfg["twitch"].get("client_secret"))
    _save(cfg)

    # --- feed region check ---------------------------------------------------------------------
    try:
        from capture_obs import ObsRoiCapture
        from ocr import read_rows, binarize
        import cv2
        cap = ObsRoiCapture(cfg["capture"], cfg["obs"])
        roi = cap.grab()
        cv2.imwrite("roi.png", roi)
        cv2.imwrite("roi_proc.png", binarize(roi)[1])
        rows = read_rows(roi)
        print(f"\nFeed region saved to roi.png ({roi.shape[1]}x{roi.shape[0]} px). {len(rows)} text row(s) visible right now.")
        print("Open roi.png: the kill-feed strip must sit inside it. If the game is not running yet, run")
        print("  python setup.py --check   later with a match on screen.")
        st = cl.get_replay_buffer_status()
        print("Replay buffer:", "running" if st.output_active else "NOT running - OBS > Settings > Output > Replay Buffer (90-120 s), then Start Replay Buffer")
    except SystemExit as e:
        print(e)
    except Exception as e:
        print(f"Feed check skipped: {e}")


def _save(cfg):
    yaml.safe_dump(cfg, open(CFG, "w", encoding="utf-8"), sort_keys=False, allow_unicode=True)
    print(f"Saved {CFG}")


if __name__ == "__main__":
    main()
