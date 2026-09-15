#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include <functional>

/// One HTTPS GET. On Windows it goes through WinHTTP, the system's own stack, because the Qt that
/// OBS ships has no TLS backend on some PCs and every QNetworkAccessManager request then fails with
/// "TLS initialization failed". Elsewhere it is a plain Qt request.
namespace Http {
struct Result {
	bool ok = false;
	int status = 0; // HTTP status, 0 when the request never got an answer
	QByteArray body;
	QString error; // "" when ok
};
/// Blocking: call it off the UI thread.
Result get(const QString &url, int timeoutMs, const QString &userAgent);
/// get() on its own thread; `done` runs on ctx's thread afterwards (not at all if ctx is gone).
void getAsync(QObject *ctx, const QString &url, int timeoutMs, const QString &userAgent,
	      std::function<void(Result)> done);
} // namespace Http
