#include "http.h"
#include <QPointer>
#include <QUrl>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#endif

namespace Http {

#ifdef _WIN32
static QString lastError(const char *where)
{
	DWORD e = GetLastError();
	return QString("%1 failed (WinHTTP error %2)").arg(where).arg(e);
}

Result request(const QString &method, const QString &url, const QByteArray &body, const QString &headers, int timeoutMs,
	       const QString &userAgent)
{
	Result out;
	QUrl u(url);
	if (!u.isValid() || u.host().isEmpty()) {
		out.error = "bad address";
		return out;
	}
	bool secure = u.scheme().compare("https", Qt::CaseInsensitive) == 0;
	std::wstring host = u.host().toStdWString();
	QString pathQ = u.path(QUrl::FullyEncoded);
	if (pathQ.isEmpty())
		pathQ = "/";
	if (u.hasQuery())
		pathQ += "?" + u.query(QUrl::FullyEncoded);
	std::wstring path = pathQ.toStdWString();
	INTERNET_PORT port = (INTERNET_PORT)(u.port(secure ? 443 : 80));

	HINTERNET session = WinHttpOpen(userAgent.toStdWString().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		out.error = lastError("WinHttpOpen");
		return out;
	}
	WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
	HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
	std::wstring meth = method.toStdWString();
	HINTERNET req = conn ? WinHttpOpenRequest(conn, meth.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
						  WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)
			     : nullptr;
	if (!req) {
		out.error = lastError(conn ? "WinHttpOpenRequest" : "WinHttpConnect");
		if (conn)
			WinHttpCloseHandle(conn);
		WinHttpCloseHandle(session);
		return out;
	}
	std::wstring hdrs =
		(QString("Cache-Control: no-cache\r\nAccept: application/json, */*\r\n") + headers).toStdWString();
	if (!WinHttpSendRequest(req, hdrs.c_str(), (DWORD)-1L,
				body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.constData(), (DWORD)body.size(),
				(DWORD)body.size(), 0) ||
	    !WinHttpReceiveResponse(req, nullptr)) {
		out.error = lastError("the request");
	} else {
		DWORD status = 0, sz = sizeof status;
		WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
		out.status = (int)status;
		for (;;) {
			DWORD avail = 0;
			if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
				break;
			QByteArray chunk((int)avail, Qt::Uninitialized);
			DWORD got = 0;
			if (!WinHttpReadData(req, chunk.data(), avail, &got) || got == 0)
				break;
			out.body.append(chunk.constData(), (int)got);
			if (out.body.size() > 8 * 1024 * 1024)
				break;
		}
		if (status >= 200 && status < 300)
			out.ok = true;
		else
			out.error = QString("HTTP %1").arg(status);
	}
	WinHttpCloseHandle(req);
	WinHttpCloseHandle(conn);
	WinHttpCloseHandle(session);
	return out;
}
#else
Result request(const QString &method, const QString &url, const QByteArray &body, const QString &headers, int timeoutMs,
	       const QString &userAgent)
{
	Result out;
	QNetworkAccessManager nam;
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	for (const QString &line : headers.split("\r\n", Qt::SkipEmptyParts)) {
		int c = line.indexOf(':');
		if (c > 0)
			req.setRawHeader(line.left(c).trimmed().toUtf8(), line.mid(c + 1).trimmed().toUtf8());
	}
	QNetworkReply *r = nam.sendCustomRequest(req, method.toUtf8(), body);
	QEventLoop loop;
	QTimer::singleShot(timeoutMs, &loop, [r]() {
		if (r->isRunning())
			r->abort();
	});
	QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	out.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (r->error() == QNetworkReply::NoError) {
		out.ok = true;
		out.body = r->readAll();
	} else
		out.error = r->errorString();
	r->deleteLater();
	return out;
}
#endif

Result get(const QString &url, int timeoutMs, const QString &userAgent)
{
	return request("GET", url, QByteArray(), QString(), timeoutMs, userAgent);
}

void requestAsync(QObject *ctx, const QString &method, const QString &url, const QByteArray &body,
		  const QString &headers, int timeoutMs, const QString &userAgent, std::function<void(Result)> done)
{
	QPointer<QObject> alive(ctx);
	std::thread([alive, method, url, body, headers, timeoutMs, userAgent, done]() {
		Result r = request(method, url, body, headers, timeoutMs, userAgent);
		if (!alive)
			return;
		QMetaObject::invokeMethod(
			alive.data(),
			[alive, done, r]() {
				if (alive)
					done(r);
			},
			Qt::QueuedConnection);
	}).detach();
}

void getAsync(QObject *ctx, const QString &url, int timeoutMs, const QString &userAgent,
	      std::function<void(Result)> done)
{
	QPointer<QObject> alive(ctx);
	std::thread([alive, url, timeoutMs, userAgent, done]() {
		Result r = get(url, timeoutMs, userAgent);
		if (!alive)
			return;
		QMetaObject::invokeMethod(
			alive.data(),
			[alive, done, r]() {
				if (alive)
					done(r);
			},
			Qt::QueuedConnection);
	}).detach();
}

} // namespace Http
