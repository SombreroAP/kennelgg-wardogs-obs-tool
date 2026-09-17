import streamDeck, { SingletonAction } from "@elgato/streamdeck";
import { bridge, setLogger } from "./bridge.js";
setLogger(streamDeck.logger);

const OFFLINE = "OBS?";   // title when the plugin is not there

/** Keys that mirror a piece of state: title and on/off image follow it. */
class KennelAction extends SingletonAction {
	constructor() {
		super();
		this.unsub = null;
	}
	onWillAppear(ev) {
		if (!this.unsub) this.unsub = bridge.onChange(() => this.refreshAll());
		this.refresh(ev.action, bridge.state);
	}
	onWillDisappear() {
		// keep the subscription: other instances of the action may still be visible; refreshAll is cheap
	}
	refreshAll() {
		for (const a of this.actions) this.refresh(a, bridge.state);
	}
	async refresh(a, state) {
		try {
			if (!state) {
				await a.setTitle(OFFLINE);
				if (a.setState) await a.setState(0);
				return;
			}
			await this.paint(a, state);
		} catch (e) {
			streamDeck.logger.error(`refresh: ${e}`);
		}
	}
	async paint(a, state) {}
	async onKeyDown(ev) {
		if (!bridge.connected) {
			await ev.action.showAlert();
			return;
		}
		if (!(await this.press(ev))) await ev.action.showAlert();
	}
	async press(ev) { return false; }
}

class PovAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.pov";
	target(a, settings) {
		// settings.who: "auto" (the live one) or a squad mate's name
		return (settings && settings.who) || "auto";
	}
	async paint(a, state) {
		const settings = await a.getSettings();
		const who = this.target(a, settings);
		let f = null;
		if (who === "auto")
			f = state.friends.find((x) => x.live) || state.friends.find((x) => x.index === state.activeIndex) || null;
		else
			f = state.friends.find((x) => x.name.toLowerCase() === who.toLowerCase()) || null;
		const shown = state.applied && f && f.index === state.activeIndex;
		let title = f ? f.name : (who === "auto" ? "nobody" : who);
		if (f && f.live) title += " ●";
		if (who === "auto" && !f) title = "no squad";
		await a.setTitle(title.length > 12 ? title.slice(0, 11) + "…" : title);
		await a.setState(shown ? 1 : 0);
	}
	async press(ev) {
		const settings = await ev.action.getSettings();
		const who = this.target(ev.action, settings);
		return bridge.control("show", who === "auto" ? { name: "auto" } : { name: who });
	}
	async onSendToPlugin(ev) {
		// the inspector asks for the squad list
		if (ev.payload && ev.payload.event === "squad")
			await streamDeck.ui.current?.sendToPropertyInspector({
				event: "squad",
				friends: bridge.state ? bridge.state.friends : [],
				connected: bridge.connected,
			});
	}
}

class CycleAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.cycle";
	async paint(a, state) {
		const n = state.friends.filter((f) => !f.off).length;
		await a.setTitle(n ? `next\n${n} up` : "no squad");
	}
	async press() { return bridge.control("cycle"); }
}

class ClipAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.clip";
	async paint(a) { await a.setTitle("clip"); }
	async press(ev) { const ok = bridge.control("clip"); if (ok) await ev.action.showOk(); return ok; }
}

class ReplayAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.replay";
	async paint(a, state) { await a.setTitle(state.replayPlaying ? "playing" : "replay"); }
	async press() { return bridge.control("replay"); }
}

class ClipReplayAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.clipreplay";
	async paint(a) { await a.setTitle("clip +\nreplay"); }
	async press(ev) { const ok = bridge.control("clip_replay"); if (ok) await ev.action.showOk(); return ok; }
}

class VoiceAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.voice";
	async paint(a, state) {
		await a.setTitle(state.voice ? "voice\non" : "voice\noff");
		await a.setState(state.voice ? 1 : 0);
	}
	async press() { return bridge.control("voice_toggle"); }
}

class DualAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.dual";
	async paint(a, state) {
		await a.setTitle(state.dual ? "dual\non" : "dual\noff");
		await a.setState(state.dual ? 1 : 0);
	}
	async press() { return bridge.control("dual_toggle"); }
}

class MeAction extends KennelAction {
	manifestId = "com.kennelgg.wardogs.me";
	async paint(a, state) { await a.setTitle(state.applied ? "back\nto me" : "my POV"); }
	async press() { return bridge.control("me"); }
}

streamDeck.actions.registerAction(new PovAction());
streamDeck.actions.registerAction(new CycleAction());
streamDeck.actions.registerAction(new ClipAction());
streamDeck.actions.registerAction(new ReplayAction());
streamDeck.actions.registerAction(new ClipReplayAction());
streamDeck.actions.registerAction(new VoiceAction());
streamDeck.actions.registerAction(new DualAction());
streamDeck.actions.registerAction(new MeAction());

// the bridge port lives in the plugin's global settings (47820 unless the OBS plugin was changed)
streamDeck.settings.getGlobalSettings().then((g) => bridge.setPort(g && g.port)).catch(() => {});
streamDeck.settings.onDidReceiveGlobalSettings((ev) => bridge.setPort(ev.settings && ev.settings.port));

bridge.start();
streamDeck.connect();
