// The OBS plugin's local bridge: a WebSocket on 127.0.0.1 (47820 unless changed in the plugin's
// settings). We send {"type":"control",...} and read {"type":"state",...}, which the plugin
// sends whenever anything changes and on request.
import WebSocket from "ws";

// the SDK's logger once the plugin is running inside Stream Deck; the console otherwise
let log = console;
export function setLogger(l) { log = l; }

class Bridge {
	constructor() {
		this.port = 47820;
		this.ws = null;
		this.state = null;          // the last state the plugin sent
		this.listeners = new Set(); // (state|null) => void
		this.timer = null;
		this.connected = false;
	}

	start() {
		this.connect();
	}

	setPort(port) {
		const p = Number(port) || 47820;
		if (p !== this.port) {
			this.port = p;
			this.close();
			this.connect();
		}
	}

	connect() {
		if (this.ws) return;
		const url = `ws://127.0.0.1:${this.port}`;
		let ws;
		try {
			ws = new WebSocket(url, { handshakeTimeout: 3000 });
		} catch (e) {
			this.retry();
			return;
		}
		this.ws = ws;
		ws.on("open", () => {
			this.connected = true;
			log.info(`bridge: connected to ${url}`);
			// no frames, no audio: keys only
			this.send({ type: "subscribe", frames: false });
			this.send({ type: "state_please" });
			this.emit();
		});
		ws.on("message", (data, isBinary) => {
			if (isBinary) return; // frames / audio for ClipHound, not for us
			let o;
			try { o = JSON.parse(data.toString()); } catch { return; }
			if (o.type === "state") {
				this.state = o;
				this.emit();
			}
		});
		const gone = () => {
			if (this.ws !== ws) return;
			this.ws = null;
			if (this.connected) log.info("bridge: OBS plugin went away");
			this.connected = false;
			this.state = null;
			this.emit();
			this.retry();
		};
		ws.on("close", gone);
		ws.on("error", gone);
	}

	retry() {
		if (this.timer) return;
		this.timer = setTimeout(() => { this.timer = null; this.connect(); }, 3000);
	}

	close() {
		const ws = this.ws;
		this.ws = null;
		if (ws) { try { ws.close(); } catch {} }
	}

	send(o) {
		if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
		try { this.ws.send(JSON.stringify(o)); return true; } catch { return false; }
	}

	control(cmd, extra = {}) {
		return this.send({ type: "control", cmd, ...extra });
	}

	onChange(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
	emit() { for (const fn of this.listeners) { try { fn(this.state); } catch (e) { log.error(String(e)); } } }
}

export const bridge = new Bridge();
