#include "ui/dock.h"
#include "ui/settings-dialog.h"
#include "ui/wizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QMenu>
#include <QTimer>
#include <QMessageBox>
#include <QScreen>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <obs-module.h>
#include <plugin-support.h>
#include <QFileInfo>
#include <obs-frontend-api.h>

/// Show a window fully inside the screen OBS is on: centred on OBS but never with its title bar off the top or
/// its edges past the screen (a tall OBS window pushed dialogs above the screen, making them impossible to grab).
static void showOnScreen(QWidget *w)
{
	QWidget *main = (QWidget *)obs_frontend_get_main_window();
	QScreen *scr = main && main->screen() ? main->screen() : QGuiApplication::primaryScreen();
	QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
	// a never-shown window reports a default 640x480, not what its layout needs: start from the
	// layout's own size, at least a comfortable size, and only then clamp to the screen
	QSize sz = w->sizeHint().expandedTo(QSize(1000, 820)).expandedTo(w->size());
	sz.setWidth(std::min(sz.width(), avail.width() - 40));
	sz.setHeight(std::min(sz.height(), avail.height() - 40));
	QPoint c = main ? main->frameGeometry().center() : avail.center();
	int x = std::clamp(c.x() - sz.width() / 2, avail.left() + 20,
			   std::max(avail.left() + 20, avail.right() - sz.width() - 20));
	int y = std::clamp(c.y() - sz.height() / 2, avail.top() + 20,
			   std::max(avail.top() + 20, avail.bottom() - sz.height() - 20));
	w->resize(sz);
	w->move(x, y);
	w->show();
	w->raise();
	w->activateWindow();
	// the first paint used geometry from before the resize (everything squeezed to a few pixels
	// until the user resized the window by hand): run the layout again once the window is up
	QTimer::singleShot(0, w, [w]() {
		if (w->layout())
			w->layout()->activate();
		w->updateGeometry();
		QSize s = w->size();
		w->resize(s + QSize(1, 1));
		w->resize(s);
	});
}

Dock::Dock(Engine *engine, QWidget *parent) : QWidget(parent), e_(engine)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(8, 8, 8, 8);
	state_ = new QLabel(this);
	QFont f = state_->font();
	if (f.pointSizeF() > 0)
		f.setPointSizeF(f.pointSizeF() + 2);
	else if (f.pixelSize() > 0)
		f.setPixelSize(f.pixelSize() + 3);
	f.setBold(true);
	state_->setFont(f);
	v->addWidget(state_);

	detector_ = new QLabel(this);
	detector_->setTextFormat(Qt::RichText);
	v->addWidget(detector_);

	near_ = new QLabel(this);
	near_->setWordWrap(true);
	{
		QFont nf = near_->font();
		if (nf.pointSizeF() > 0)
			nf.setPointSizeF(nf.pointSizeF() - 0.5);
		else if (nf.pixelSize() > 2)
			nf.setPixelSize(nf.pixelSize() - 1);
		near_->setFont(nf);
	}
	near_->hide();
	v->addWidget(near_);

	auto *row = new QHBoxLayout();
	row->addWidget(new QLabel("Squad mate", this));
	active_ = new QComboBox(this);
	row->addWidget(active_, 1);
	closest_ = new QCheckBox("Closest", this);
	closest_->setToolTip(
		"Show whoever the game's NEARBY list says is closest, instead of the squad mate chosen here.\n"
		"Needs ClipHound running and each squad mate's in-game name (Settings → Switch).");
	row->addWidget(closest_);
	v->addLayout(row);
	connect(closest_, &QCheckBox::toggled, this, [this](bool on) {
		if (filling_ || on == e_->cfg.nearEnabled)
			return;
		if (on && !e_->appConnected()) {
			// the NEARBY list is read by ClipHound: without it this tick box does nothing
			QMessageBox m((QWidget *)obs_frontend_get_main_window());
			m.setWindowTitle("Kennel WARDOGS");
			m.setIcon(QMessageBox::Information);
			m.setText("Closest needs ClipHound running.");
			m.setInformativeText(
				"ClipHound reads the NEARBY list in the corner of your game and tells the plugin who is nearest. It is not running, so Closest stays off.\n\nStart it now, then tick Closest again once the dock says it is connected. It also starts with OBS when \"Start ClipHound with OBS\" is ticked under Settings → Clips.");
			auto *start = m.addButton("Start ClipHound", QMessageBox::AcceptRole);
			m.addButton(QMessageBox::Cancel);
			m.exec();
			// either way Closest stays off: it cannot work without ClipHound. Tick it again once
			// the dock shows ClipHound connected.
			closest_->blockSignals(true);
			closest_->setChecked(false);
			closest_->blockSignals(false);
			if (m.clickedButton() == start)
				e_->launchApp();
			return;
		}
		e_->cfg.nearEnabled = on;
		e_->cfg.save();
		e_->reloadConfig(); // same as ticking it in Settings: pushes the names and areas to ClipHound
		e_->log(on ? "Following the closest squad mate (NEARBY list)."
			   : "Following the squad mate you picked.");
		refresh();
	});
	connect(active_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (!filling_ && i >= 0)
			e_->setActive(i);
	});

	auto *btns = new QHBoxLayout();
	show_ = new QPushButton("Show friend's POV", this);
	back_ = new QPushButton("Back to me", this);
	btns->addWidget(show_);
	btns->addWidget(back_);
	v->addLayout(btns);
	connect(show_, &QPushButton::clicked, this, [this]() { e_->applyNow(true, "button"); });
	connect(back_, &QPushButton::clicked, this, [this]() { e_->applyNow(false, "button"); });

	auto *btns2 = new QHBoxLayout();
	dual_ = new QPushButton("Dual POV", this);
	dual_->setCheckable(true);
	dual_->setToolTip("Your crew mate's feed in a small window over your POV (set up on the Dual POV tab).");
	btns->addWidget(dual_);
	connect(dual_, &QPushButton::clicked, this, [this](bool on) { e_->setDual(on, "dock"); });
	pause_ = new QPushButton("Pause", this);
	auto *settings = new QPushButton("Settings...", this);
	auto *wiz = new QPushButton("Setup", this);
	auto *logs = new QPushButton("Logs", this);
	btns2->addWidget(pause_);
	btns2->addWidget(wiz);
	btns2->addWidget(settings);
	btns2->addWidget(logs);
	connect(logs, &QPushButton::clicked, this, &Dock::openLogs);
	connect(wiz, &QPushButton::clicked, this, &Dock::openWizard);
	v->addLayout(btns2);
	connect(pause_, &QPushButton::clicked, this, [this]() { e_->setEnabled(!e_->cfg.enabled); });
	connect(settings, &QPushButton::clicked, this, &Dock::openSettings);

	app_ = new QLabel(this);
	clip_ = new QLabel(this);
	for (auto *l : {app_, clip_}) {
		l->setWordWrap(true);
		{
			QFont f = l->font();
			if (f.pointSizeF() > 0)
				f.setPointSizeF(f.pointSizeF() - 0.5);
			else if (f.pixelSize() > 2)
				f.setPixelSize(f.pixelSize() - 1);
			l->setFont(f);
		}
	}
	// ClipHound control, styled like OBS's replay-buffer control: [Start/Stop ClipHound] [Save clip ▾]
	auto *appRow = new QHBoxLayout();
	appBtn_ = new QPushButton("Start ClipHound", this);
	appRow->addWidget(appBtn_, 1);
	saveBtn_ = new QToolButton(this);
	saveBtn_->setText("Save clip");
	saveBtn_->setToolTip("Save a clip now");
	saveBtn_->setPopupMode(QToolButton::MenuButtonPopup);
	auto *saveMenu = new QMenu(saveBtn_);
	saveMenu->addAction("Save clip now", this, [this]() { e_->clipNow("manual", {"manual"}, "dock"); });
	saveMenu->addAction("Save clip tagged 'highlight'", this,
			    [this]() { e_->clipNow("highlight", {"highlight", "manual"}, "dock"); });
	saveMenu->addAction("Save clip tagged 'funny'", this,
			    [this]() { e_->clipNow("funny", {"funny", "manual"}, "dock"); });
	saveMenu->addAction("Save clip tagged 'fail'", this,
			    [this]() { e_->clipNow("fail", {"fail", "manual"}, "dock"); });
	saveBtn_->setMenu(saveMenu);
	connect(saveBtn_, &QToolButton::clicked, this, [this]() { e_->clipNow("manual", {"manual"}, "dock"); });
	appRow->addWidget(saveBtn_);
	v->addLayout(appRow);
	connect(appBtn_, &QPushButton::clicked, this, [this]() {
		QString st = e_->appState();
		if (st == "connected" || st == "starting")
			e_->stopApp();
		else
			e_->launchApp();
		refresh();
	});
	clipNow_ = nullptr;
	v->addWidget(clip_);
	v->addWidget(app_);
	events_ = new QListWidget(this);
	events_->setMaximumHeight(120);
	events_->setSelectionMode(QAbstractItemView::NoSelection);
	events_->setFocusPolicy(Qt::NoFocus);
	v->addWidget(events_);
	last_ = new QLabel(this);
	last_->setWordWrap(true);

	v->addWidget(last_);
	v->addStretch(1);

	connect(e_, &Engine::stateChanged, this, &Dock::refresh);
	connect(e_, &Engine::frameUpdated, this, [this]() {
		Match m = e_->lastGame();
		bool down = m.score >= e_->cfg.threshold;
		detector_->setText(QString("Downed state detector: <b style=\"color:%1\">%2</b>")
					   .arg(down ? "#ce6050" : "#4cbe5a", m.score < 0 ? "no template"
									      : down      ? "Downed"
											  : "Alive"));
		if (e_->revivingRecent())
			state_->setText(QString::fromStdString(e_->stateText()) +
					QString(" (%1%)").arg((int)(std::max(0.0, e_->reviveProgress()) * 100)));
	});
	connect(e_, &Engine::logged, this, [this](const QString &s) { last_->setText(s); });
	refresh();
}

void Dock::refresh()
{
	QStringList names;
	for (auto &f : e_->cfg.friends)
		names << QString::fromStdString(f.name);
	QStringList shown;
	for (int i = 0; i < active_->count(); i++)
		shown << active_->itemText(i);
	filling_ = true;
	if (shown != names) { // rebuilding while the user has the list open would close it
		active_->clear();
		active_->addItems(names);
	}
	if (!names.isEmpty())
		active_->setCurrentIndex(std::clamp(e_->cfg.activeFriend, 0, (int)names.size() - 1));
	filling_ = false;
	state_->setText(QString::fromStdString(e_->stateText()));
	state_->setStyleSheet(e_->applied() ? "color: #ce6050;" : "");
	pause_->setText(e_->cfg.enabled ? "Pause" : "Resume");
	QString st = e_->appState();
	if (st == "connected") {
		appBtn_->setText("Stop ClipHound");
		appBtn_->setStyleSheet("QPushButton { border-left: 4px solid #4cbe5a; }");
		app_->setText("ClipHound: " + (e_->appStatus().isEmpty() ? QString("connected") : e_->appStatus()));
	} else if (st == "starting") {
		appBtn_->setText("Stop ClipHound");
		appBtn_->setStyleSheet("QPushButton { border-left: 4px solid #c99a3b; }");
		app_->setText("ClipHound: starting...");
	} else if (st == "crashed") {
		appBtn_->setText("Start ClipHound");
		appBtn_->setStyleSheet("QPushButton { border-left: 4px solid #ce6050; }");
		app_->setText("ClipHound: exited right after starting - Settings → Logs");
	} else {
		appBtn_->setText("Start ClipHound");
		appBtn_->setStyleSheet("");
		app_->setText(e_->cfg.bridgeEnabled ? "ClipHound: not running" : "ClipHound: bridge off");
	}
	events_->clear();
	QStringList ev = e_->recentEvents();
	for (int i = ev.size() - 1; i >= 0 && ev.size() - i <= 8; i--)
		events_->addItem(ev[i]);
	if (events_->count() == 0)
		events_->addItem("events from the kill feed and the POV swap appear here");
	if (near_) {
		near_->setVisible(e_->cfg.nearEnabled);
		near_->setText("Nearby: " + e_->nearbyStatus());
		near_->setStyleSheet(e_->cfg.nearEnabled && !e_->appConnected() ? "color: #ce6050;" : "");
	}
	if (closest_) {
		closest_->setChecked(e_->cfg.nearEnabled);
		active_->setEnabled(!e_->cfg.nearEnabled);
		active_->setToolTip(e_->cfg.nearEnabled
					    ? "Set automatically to whoever is closest; untick Closest to choose."
					    : "");
	}
	QString lp = e_->clips.lastPath();
	QString rb = (e_->cfg.clipUseReplay && !obs_frontend_replay_buffer_active())
			     ? "REPLAY BUFFER OFF (OBS Settings → Output)  ·  "
			     : "";
	clip_->setText(rb + (lp.isEmpty() ? "no clips yet" : "last: " + QFileInfo(lp).fileName()));
	if (dual_) {
		dual_->blockSignals(true);
		dual_->setChecked(e_->dualOn());
		dual_->blockSignals(false);
		dual_->setStyleSheet(e_->dualOn() ? "QPushButton { border-left: 4px solid #4cbe5a; }" : "");
	}
	show_->setEnabled(!e_->applied());
	back_->setEnabled(e_->applied());
}

void Dock::openWizard()
{
	if (wizard_) {
		wizard_->raise();
		wizard_->activateWindow();
		return;
	}
	auto *w = new SetupWizard(e_, (QWidget *)obs_frontend_get_main_window());
	w->setAttribute(Qt::WA_DeleteOnClose);
	wizard_ = w;
	showOnScreen(w);
}

static QString tailOf(const QString &path, int lines)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	QStringList all = QString::fromUtf8(f.readAll()).split('\n');
	if (all.size() > lines)
		all = all.mid(all.size() - lines);
	return all.join('\n');
}

void Dock::openLogs()
{
	auto *d = new QDialog((QWidget *)obs_frontend_get_main_window());
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle("Kennel WARDOGS - logs");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	d->resize(820, 600);
	auto *v = new QVBoxLayout(d);
	auto *txt = new QPlainTextEdit(d);
	txt->setReadOnly(true);
	txt->setLineWrapMode(QPlainTextEdit::NoWrap);
	QString appDir = QString::fromStdString(e_->cfg.appPath).isEmpty()
				 ? QString("C:/ProgramData/Kennel WARDOGS/ClipHound")
				 : QFileInfo(QString::fromStdString(e_->cfg.appPath)).absolutePath();
	QString body = QString("=== Kennel WARDOGS plugin %1 ===\n").arg(PLUGIN_VERSION);
	body += QString("state: %1 | game source: %2 | squad mate: %3 | replay buffer: %4 | ClipHound: %5 | clip hotkeys: %6\n\n")
			.arg(QString::fromStdString(e_->stateText()), QString::fromStdString(e_->cfg.gameSource),
			     e_->cfg.active() ? QString::fromStdString(e_->cfg.active()->name) : "(none)",
			     obs_frontend_replay_buffer_active() ? "running" : "NOT running",
			     e_->appConnected() ? "connected" : "not connected",
			     QString::number(e_->cfg.clipHotkeys.size()));
	body += e_->recentLog().join('\n');
	body += "\n\n=== ClipHound (" + appDir + "/cliphound.log, last 200 lines) ===\n";
	QString ch = tailOf(appDir + "/cliphound.log", 200);
	body += ch.isEmpty() ? "(no log file - is ClipHound running from that folder?)" : ch;
	txt->setPlainText(body);
	v->addWidget(txt, 1);
	auto *row = new QHBoxLayout();
	auto *copy = new QPushButton("Copy all", d);
	auto *obsLogs = new QPushButton("Open OBS log folder", d);
	auto *appFolder = new QPushButton("Open ClipHound folder", d);
	auto *cfgFolder = new QPushButton("Open plugin config folder", d);
	for (auto *b : {copy, obsLogs, appFolder, cfgFolder})
		row->addWidget(b);
	row->addStretch(1);
	v->addLayout(row);
	connect(copy, &QPushButton::clicked, d, [txt, copy]() {
		QApplication::clipboard()->setText(txt->toPlainText());
		copy->setText("Copied");
	});
	connect(obsLogs, &QPushButton::clicked, d, []() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
			"/obs-studio/logs"));
	});
	connect(appFolder, &QPushButton::clicked, d,
		[appDir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(appDir)); });
	connect(cfgFolder, &QPushButton::clicked, d,
		[]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Config::configDir()))); });
	showOnScreen(d);
}

void Dock::openSettings()
{
	if (settings_) {
		settings_->raise();
		settings_->activateWindow();
		return;
	}
	auto *dlg = new SettingsDialog(e_, (QWidget *)obs_frontend_get_main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	settings_ = dlg;
	showOnScreen(dlg);
}
