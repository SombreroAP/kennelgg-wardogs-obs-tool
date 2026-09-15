#include "discord-ipc.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <algorithm>
#include <cstring>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace DiscordIpc {

static const char *kClientId = "1542623631111098378"; // the Kennel Ops application

std::optional<User> currentUser(int timeoutMs)
{
#ifdef _WIN32
	// one frame: 4 bytes opcode, 4 bytes length, JSON; opcode 0 is the handshake, 1 a frame, 2 close
	QByteArray body = QJsonDocument(QJsonObject{{"v", 1}, {"client_id", kClientId}}).toJson(QJsonDocument::Compact);
	QByteArray frame;
	uint32_t op = 0, len = (uint32_t)body.size();
	frame.append((const char *)&op, 4);
	frame.append((const char *)&len, 4);
	frame.append(body);
	for (int n = 0; n < 10; n++) {
		std::string path = "\\\\?\\pipe\\discord-ipc-" + std::to_string(n);
		HANDLE h =
			CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		DWORD wrote = 0;
		bool ok = WriteFile(h, frame.constData(), (DWORD)frame.size(), &wrote, nullptr) &&
			  wrote == (DWORD)frame.size();
		std::optional<User> out;
		QByteArray buf;
		QElapsedTimer t;
		t.start();
		while (ok && !out && t.elapsed() < timeoutMs) {
			DWORD avail = 0;
			if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
				break;
			if (avail == 0) {
				Sleep(20);
				continue;
			}
			char tmp[4096];
			DWORD got = 0;
			if (!ReadFile(h, tmp, std::min<DWORD>(avail, (DWORD)sizeof tmp), &got, nullptr) || got == 0)
				break;
			buf.append(tmp, (int)got);
			while (buf.size() >= 8) {
				uint32_t fop = 0, flen = 0;
				memcpy(&fop, buf.constData(), 4);
				memcpy(&flen, buf.constData() + 4, 4);
				if (buf.size() < 8 + (int)flen)
					break;
				QJsonObject o = QJsonDocument::fromJson(buf.mid(8, (int)flen)).object();
				buf.remove(0, 8 + (int)flen);
				if (o.value("evt").toString() == "READY") {
					QJsonObject u = o.value("data").toObject().value("user").toObject();
					User usr;
					usr.id = u.value("id").toString();
					usr.username = u.value("username").toString();
					if (!usr.username.isEmpty())
						out = usr;
				} else if (fop == 2 || o.value("evt").toString() == "ERROR") {
					ok = false; // Discord closed the pipe or refused the handshake
				}
			}
		}
		CloseHandle(h);
		if (out)
			return out;
	}
#else
	(void)timeoutMs;
#endif
	return std::nullopt;
}

} // namespace DiscordIpc
