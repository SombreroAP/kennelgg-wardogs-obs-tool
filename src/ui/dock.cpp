#include "ui/dock.h"
#include "ui/clips-dialog.h"
#include "ui/settings-dialog.h"
#include "ui/wizard.h"
#include "ui/squad.h"
#include <QVBoxLayout>
#include <functional>
#include <QHBoxLayout>
#include <QDialog>
#include <QMenu>
#include <QTimer>
#include <QMessageBox>
#include <QStyle>
#include <QPixmap>
#include <QInputDialog>
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

/// The dock's look: the brand's graphite, olive, amber and bone, scoped to this widget so OBS's own
/// theme is left alone. Buttons share one height and one edge; the status line is a pill whose
/// colour is the state; section labels are the condensed face in small caps.
static const char *kDockStyle = R"(
#kennelDock { background: #1c1f1d; }
#kennelDock QLabel { color: #e6e2d6; }
#kennelDock QLabel#eyebrow { color: #c99a3b; font-family: "Saira Condensed"; font-size: 12pt; font-weight: 700;
	letter-spacing: 2px; padding: 8px 0 2px 0; border-bottom: 1px solid #343835; margin-bottom: 2px; }
#kennelDock QLabel#wordmark { color: #ece7db; font-family: "Saira Condensed"; font-size: 17pt; font-weight: 700;
	letter-spacing: 1px; }
#kennelDock QLabel#version { color: #7c8076; font-family: "IBM Plex Mono"; font-size: 8pt; }
#kennelDock QLabel#statePill { padding: 5px 10px; border-radius: 3px; border: 1px solid #3a3e3b; background: #242725;
	font-weight: 600; }
#kennelDock QLabel#statePill[mode="watching"] { border-color: #6f7c45; color: #cbd3a4; }
#kennelDock QLabel#statePill[mode="showing"] { border-color: #ce6050; background: #3a2521; color: #f2c9c1; }
#kennelDock QLabel#statePill[mode="off"] { border-color: #3a3e3b; color: #9a9e93; }
#kennelDock QPushButton { min-height: 24px; padding: 2px 10px; border: 1px solid #3a3e3b; border-radius: 3px;
	background: #262927; color: #e6e2d6; }
#kennelDock QPushButton:hover { background: #2f3330; border-color: #4a4f4b; }
#kennelDock QPushButton:pressed { background: #202321; }
#kennelDock QPushButton:checked { border-color: #c99a3b; background: #2e2a1f; }
#kennelDock QPushButton:disabled { color: #6c7068; border-color: #2e3230; }
#kennelDock QPushButton#liveChip { border-color: #6f7c45; background: #232a1e; color: #d9e0b6; font-weight: 600; }
#kennelDock QPushButton#liveChip:checked { border-color: #ce6050; background: #3a2521; color: #f2c9c1; }
#kennelDock QLabel#lockedChip { min-height: 24px; padding: 2px 10px; border: 1px dashed #3a3e3b; border-radius: 3px;
	background: #202321; color: #7c8076; }
#kennelDock QComboBox { min-height: 24px; padding: 1px 6px; border: 1px solid #3a3e3b; border-radius: 3px;
	background: #262927; color: #e6e2d6; }
#kennelDock QComboBox:disabled { color: #6c7068; }
#kennelDock QCheckBox { color: #e6e2d6; spacing: 5px; }
#kennelDock QListWidget { border: 1px solid #2e3230; border-radius: 3px; background: #202321; color: #c9c5b8;
	font-family: "IBM Plex Mono"; font-size: 8pt; }
#kennelDock QToolButton { border: 1px solid #3a3e3b; border-radius: 3px; background: #262927; color: #e6e2d6; padding: 2px 6px; }
)";

static QLabel *eyebrow(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text.toUpper(), parent);
	l->setObjectName("eyebrow");
	return l;
}

Dock::Dock(Engine *engine, QWidget *parent) : QWidget(parent), e_(engine)
{
	setObjectName("kennelDock");
	setStyleSheet(kDockStyle);
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(10, 8, 10, 8);
	v->setSpacing(6);
	// header: the hound, the wordmark, the build
	{
		auto *head = new QHBoxLayout();
		head->setSpacing(8);
		auto *mark = new QLabel(this);
		char *p = obs_module_file("brand/hound_mark.png");
		if (p) {
			QPixmap px(QString::fromUtf8(p));
			bfree(p);
			if (!px.isNull())
				mark->setPixmap(px.scaledToHeight(26, Qt::SmoothTransformation));
		}
		head->addWidget(mark);
		auto *wm = new QLabel("KENNEL.GG WARDOGS", this);
		wm->setObjectName("wordmark");
		head->addWidget(wm);
		head->addStretch(1);
		auto *ver = new QLabel(QString("v%1").arg(PLUGIN_VERSION), this);
		ver->setObjectName("version");
		head->addWidget(ver);
		v->addLayout(head);
	}
	state_ = new QLabel(this);
	state_->setObjectName("statePill");
	state_->setAlignment(Qt::AlignCenter);
	v->addWidget(state_);
	v->addWidget(eyebrow("Squad", this));
	// One button per squad mate who is live with a feed up: press it and their feed takes the main
	// view, press it again and you are back on your own. Rebuilt on every refresh, so a button is
	// there exactly as long as its person is streaming.
	// the two things done most often mid-session, right at the top: add whatever is popped out,
	// and bring the pop-outs back to reach their controls
	auto *quick = new QHBoxLayout();
	auto *addPop = new QPushButton("Add pop-outs - (mute Discord stream before adding)", this);
	addPop->setToolTip(
		"Every popped-out Discord stream becomes a squad mate, named by its username. Same as the "
		"Add on the Squad panel. Right-click each stream in Discord and mute it first: its game sound "
		"would otherwise play in your headphones and go out on your stream through Desktop Audio.");
	showPop_ = new QPushButton("Show pop-outs", this);
	showPop_->setCheckable(true);
	showPop_->setToolTip(
		"Bring the tucked pop-outs back on screen to mute or adjust them; press again to tuck them away.");
	quick->addWidget(addPop);
	quick->addWidget(showPop_);
	quick->addStretch(1);
	v->addLayout(quick);
	connect(addPop, &QPushButton::clicked, this, [this]() {
		QStringList added;
		QString what = e_->addPopouts(&added);
		last_->setText(what);
		for (const QString &name : added)
			for (size_t i = 0; i < e_->cfg.friends.size(); ++i)
				if (QString::fromStdString(e_->cfg.friends[i].name) == name) {
					Friend &f = e_->cfg.friends[i];
					bool ok = false;
					QString v = QInputDialog::getText(
						this, "In-game name",
						"What is " + name +
							" called in the game? (matched against the NEARBY list)",
						QLineEdit::Normal, name, &ok);
					if (ok) {
						v = v.trimmed();
						f.gameName = (v.isEmpty() || v == name) ? "" : v.toStdString();
						e_->cfg.save();
						e_->pushAppConfig();
					}
				}
	});
	connect(showPop_, &QPushButton::clicked, this, [this](bool on) { e_->showPopouts(on); });
	liveRow_ = new QHBoxLayout();
	liveRow_->setSpacing(4);
	v->addLayout(liveRow_);
	// where the live buttons go when the bot cannot fill them in: greyed out, with the way in
	locked_ = new QLabel(this);
	locked_->setObjectName("lockedChip");
	locked_->setTextFormat(Qt::RichText);
	locked_->setWordWrap(true);
	locked_->setOpenExternalLinks(false);
	locked_->hide();
	v->addWidget(locked_);
	sceneWarn_ = new QLabel(this);
	sceneWarn_->setObjectName("lockedChip");
	sceneWarn_->setWordWrap(true);
	sceneWarn_->hide();
	v->addWidget(sceneWarn_);
	std::function<void()> askUser = [this]() {
		bool ok = false;
		QString v = QInputDialog::getText(
			this, "Your Discord username",
			"Your Discord username - the lower-case one under your display name. The Kennel.gg bot "
			"checks it is in the server, and then follows whichever voice channel you are in.",
			QLineEdit::Normal, QString::fromStdString(e_->cfg.myDiscord), &ok);
		if (ok)
			e_->setMyDiscord(v);
	};
	// Detect asks the Discord app; when it is not running, the question falls back to typing
	connect(e_, &Engine::discordUserDetected, this, [askUser](const QString &u, bool byHand) {
		if (byHand && u.isEmpty())
			askUser();
	});
	connect(locked_, &QLabel::linkActivated, this, [this, askUser](const QString &href) {
		if (href == "kennel:detect")
			e_->detectDiscordUser(true);
		else if (href == "kennel:username") {
			bool ok = false;
			QString v = QInputDialog::getText(
				this, "Your Discord username",
				"Your Discord username - the lower-case one under your display name. The Kennel.gg bot "
				"checks it is in the server, and then follows whichever voice channel you are in.",
				QLineEdit::Normal, QString::fromStdString(e_->cfg.myDiscord), &ok);
			if (ok)
				e_->setMyDiscord(v);
		} else
			QDesktopServices::openUrl(QUrl(href));
	});

	detector_ = new QLabel(this);
	detector_->setTextFormat(Qt::RichText);
	v->addWidget(detector_);

	update_ = new QLabel(this);
	update_->setWordWrap(true);
	update_->setTextFormat(Qt::RichText);
	update_->setOpenExternalLinks(true);
	update_->setStyleSheet("color: #c99a3b;");
	update_->hide();
	v->addWidget(update_);
	connect(e_, &Engine::updateChecked, this, &Dock::refresh);

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
	autoSwitch_ = new QCheckBox("Auto switch", this);
	autoSwitch_->setToolTip("Switch to a squad mate by itself when you get downed. Untick to keep your own POV up "
				"no matter what; Show friend's POV still works by hand, and clips keep coming.");
	row->addWidget(autoSwitch_);
	connect(autoSwitch_, &QCheckBox::toggled, this, [this](bool on) {
		if (filling_ || on == e_->cfg.enabled)
			return;
		e_->setEnabled(on);
	});
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
			m.setWindowTitle("Kennel.gg Wardogs");
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
		if (filling_ || i < 0)
			return;
		int f = active_->itemData(i).toInt(); // the list hides squad mates who are not streaming
		if (f >= 0 && f < (int)e_->cfg.friends.size())
			e_->setActive(f);
	});

	auto *btns = new QHBoxLayout();
	show_ = new QPushButton("Show friend's POV", this);
	back_ = new QPushButton("Back to me", this);
	btns->addWidget(show_);
	btns->addWidget(back_);
	v->addLayout(btns);
	connect(show_, &QPushButton::clicked, this, [this]() { e_->applyNow(true, "button"); });
	connect(back_, &QPushButton::clicked, this, [this]() { e_->applyNow(false, "button"); });

	// Dual POV is its own thing: its own person, its own button, nothing to do with the drop-down
	// above, which is who the full-screen swap shows.
	v->addWidget(eyebrow("Dual POV", this));
	auto *dualRow = new QHBoxLayout();
	dualPick_ = new QComboBox(this);
	dualPick_->setToolTip("Who goes in the small Dual POV window. Separate from the squad mate above.");
	dualRow->addWidget(dualPick_, 1);
	dual_ = new QPushButton("Force Dual POV", this);
	dual_->setCheckable(true);
	dual_->setToolTip("Put the person picked here in the small window, now, and keep them there until you press "
			  "this again. The vehicle detector (Dual POV tab) still opens and closes the window by "
			  "itself when this is off.");
	dualRow->addWidget(dual_);
	dualAuto_ = new QCheckBox("Auto", this);
	dualAuto_->setToolTip("Let the vehicle detector open the window by itself when you get in a tank or "
			      "chopper and close it when you get out (needs ClipHound). Untick before a match to "
			      "keep it from happening at all.");
	dualRow->addWidget(dualAuto_);
	v->addLayout(dualRow);
	connect(dualAuto_, &QCheckBox::toggled, this, [this](bool on) {
		if (filling_ || on == e_->cfg.dualAuto)
			return;
		e_->cfg.dualAuto = on;
		e_->cfg.save();
		e_->pushAppConfig(); // ClipHound only watches the vehicle corner while this is on
		e_->log(on ? "Dual POV: auto on - the window opens and closes with the vehicle."
			   : "Dual POV: auto off - only the Force button opens the window.");
	});
	connect(dualPick_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (filling_ || i < 0)
			return;
		int f = dualPick_->itemData(i).toInt();
		if (f < 0 || f >= (int)e_->cfg.friends.size())
			return;
		if (e_->dualOn())
			e_->showInDual(f, "dock"); // the window is up: swap the person inside it
		else {
			e_->cfg.dualFriend = f;
			e_->cfg.save();
		}
	});
	connect(dual_, &QPushButton::clicked, this, [this](bool on) {
		int f = dualPick_->currentIndex() >= 0 ? dualPick_->currentData().toInt() : -1;
		if (on && f >= 0 && f < (int)e_->cfg.friends.size())
			e_->showInDual(f, "dock");
		else
			e_->setDual(on, "dock");
	});
	auto *btns2 = new QHBoxLayout();
	auto *squad = new QPushButton("Squad", this);
	squad->setToolTip("Turn popped-out Discord streams into squad mates, and manage them mid-broadcast.");
	auto *settings = new QPushButton("Settings...", this);
	auto *wiz = new QPushButton("Setup", this);
	auto *logs = new QPushButton("Logs", this);
	auto *disc = new QPushButton("Discord", this);
	disc->setToolTip("Join the Kennel.gg Discord. Squad automation runs through the Kennel Ops bot there and is "
			 "for its members.");
	btns2->addWidget(squad);
	btns2->addWidget(wiz);
	btns2->addWidget(settings);
	btns2->addWidget(logs);
	btns2->addWidget(disc);
	connect(disc, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(e_->discordUrl())); });
	connect(logs, &QPushButton::clicked, this, &Dock::openLogs);
	connect(squad, &QPushButton::clicked, this, &Dock::openSquad);
	connect(wiz, &QPushButton::clicked, this, &Dock::openWizard);
	v->addLayout(btns2);
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
	v->addWidget(eyebrow("Clips", this));
	// the last highlight back on the stream, cut to the action; and the session's compilation
	auto *replayRow = new QHBoxLayout();
	replay_ = new QPushButton("Instant replay", this);
	replay_->setToolTip("Play the last highlight on the stream, cut to the action: a few seconds before the first "
			    "kill to a few seconds after the last (Settings, Clips). Press again to stop.");
	highlights_ = new QPushButton("Play highlights", this);
	highlights_->setToolTip("Play the newest highlights compilation, full screen, under your camera and alerts. "
				"Press again to stop.");
	replayRow->addWidget(replay_);
	replayRow->addWidget(highlights_);
	v->addLayout(replayRow);
	connect(replay_, &QPushButton::clicked, this, [this]() {
		if (e_->replaying())
			e_->stopReplay("dock");
		else
			e_->playReplay("dock");
	});
	connect(highlights_, &QPushButton::clicked, this, [this]() {
		if (e_->replaying())
			e_->stopReplay("dock");
		else
			e_->playCompilation("dock");
	});
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
	saveMenu->addSeparator();
	saveMenu->addAction("Save clip and add a note...", this, [this]() {
		noteNext_ = true; // the clip is saved now; the note comes when the file has landed
		e_->clipNow("manual", {"manual"}, "dock");
	});
	saveMenu->addAction("Clips: titles and tags...", this, [this]() { openClips(); });
	saveBtn_->setMenu(saveMenu);
	connect(&e_->clips, &Clips::saved, this, [this](const Clips::Entry &e) {
		if (!noteNext_ || !e.tags.contains("manual"))
			return;
		noteNext_ = false;
		auto *d = new ClipNoteDialog(e_, e.path, (QWidget *)obs_frontend_get_main_window());
		d->show();
		d->raise();
		d->activateWindow();
	});
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
	v->addWidget(eyebrow("Events", this));
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
	connect(e_, &Engine::languageUnknown, this, &Dock::showLanguageNote, Qt::QueuedConnection);
	connect(&e_->roster, &Roster::changed, this, &Dock::refresh);
	connect(&e_->roster, &Roster::polled, this, &Dock::refresh);
	connect(e_, &Engine::frameUpdated, this, [this]() {
		Match m = e_->lastGame();
		bool down = m.score >= e_->cfg.threshold;
		detector_->setText(QString("Downed state detector: <b style=\"color:%1\">%2</b>")
					   .arg(down ? "#ce6050" : "#8f9c5a", m.score < 0 ? "no template"
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
	// the live buttons at the top: rebuilt only when the set of live squad mates changes
	{
		QStringList liveNames;
		QList<int> liveIdx;
		for (size_t i = 0; i < e_->cfg.friends.size(); ++i)
			if (e_->feedState(e_->cfg.friends[i]) == Engine::Feed::Live &&
			    e_->feedUsable(e_->cfg.friends[i])) {
				liveNames << QString::fromStdString(e_->cfg.friends[i].name);
				liveIdx << (int)i;
			}
		if (liveNames != liveShown_) {
			while (QLayoutItem *it = liveRow_->takeAt(0)) {
				delete it->widget();
				delete it;
			}
			liveButtons_.clear();
			liveNames_.clear();
			for (int k = 0; k < liveNames.size(); k++) {
				auto *b = new QPushButton(liveNames[k], this);
				b->setObjectName("liveChip");
				b->setCheckable(true);
				b->setToolTip("Force " + liveNames[k] +
					      "'s feed into the main view. Press again to come back to yours.");
				int f = liveIdx[k];
				connect(b, &QPushButton::clicked, this, [this, f]() {
					if (f < 0 || f >= (int)e_->cfg.friends.size())
						return;
					if (e_->applied() && e_->cfg.activeFriend == f)
						e_->applyNow(false, "button");
					else {
						e_->setActive(f); // switches on the spot when already showing someone
						if (!e_->applied())
							e_->applyNow(true, "button");
					}
				});
				liveRow_->addWidget(b);
				liveButtons_ << b;
				liveNames_ << liveNames[k];
			}
			liveRow_->addStretch(1);
			liveShown_ = liveNames;
			liveIdx_ = liveIdx;
		}
		for (int k = 0; k < liveButtons_.size(); k++) {
			bool on = e_->applied() && k < liveIdx_.size() && e_->cfg.activeFriend == liveIdx_[k];
			liveButtons_[k]->blockSignals(true);
			liveButtons_[k]->setChecked(on);
			liveButtons_[k]->blockSignals(false);
			liveButtons_[k]->setToolTip(on ? liveNames_[k] +
								    " is on screen. Press to come back to your own POV."
						       : "Force " + liveNames_[k] + "'s feed into the main view.");
		}
	}
	// With the Kennel.gg roster open, the two drop-downs list only the people live in your voice
	// channel right now; everyone else is managed from the Squad panel. Without the roster, only
	// slots known to have no picture are left out.
	bool live = e_->rosterLive();
	QStringList names;
	QList<int> idx;
	for (size_t i = 0; i < e_->cfg.friends.size(); ++i) {
		const Friend &f = e_->cfg.friends[i];
		if (e_->feedState(f) == Engine::Feed::Off)
			continue;
		if (live && e_->feedState(f) != Engine::Feed::Live)
			continue;
		QString label = QString::fromStdString(f.name);
		if (!live && e_->feedState(f) == Engine::Feed::Live)
			label += "  \u25cf"; // a dot for the ones known to be streaming (all of them, when live-only)
		names << label;
		idx << (int)i;
	}
	auto fill = [&](QComboBox *box, int want) {
		QStringList shown;
		for (int i = 0; i < box->count(); i++)
			shown << box->itemText(i);
		if (shown != names) { // rebuilding while the user has the list open would close it
			box->clear();
			for (int i = 0; i < names.size(); i++)
				box->addItem(names[i], idx[i]);
		}
		int row = idx.indexOf(want);
		box->setCurrentIndex(row); // -1 when the one chosen is not streaming: nothing selected
		box->setEnabled(!names.isEmpty());
		box->setPlaceholderText(e_->cfg.friends.empty() ? "no squad mates yet"
					: live                  ? "nobody live in your channel"
								: "nobody streaming");
	};
	filling_ = true;
	fill(active_, e_->cfg.activeFriend);
	if (dualAuto_) {
		dualAuto_->blockSignals(true);
		dualAuto_->setChecked(e_->cfg.dualAuto);
		dualAuto_->blockSignals(false);
	}
	if (dualPick_)
		fill(dualPick_, e_->cfg.dualFriend);
	filling_ = false;
	state_->setText(QString::fromStdString(e_->stateText()));
	{
		const char *mode = e_->applied() ? "showing" : !e_->cfg.enabled ? "off" : "watching";
		if (state_->property("mode").toString() != mode) {
			state_->setProperty("mode", mode);
			state_->style()->unpolish(state_);
			state_->style()->polish(state_);
		}
	}
	if (autoSwitch_) {
		autoSwitch_->blockSignals(true);
		autoSwitch_->setChecked(e_->cfg.enabled);
		autoSwitch_->blockSignals(false);
	}
	if (showPop_) {
		showPop_->blockSignals(true);
		showPop_->setChecked(e_->popoutsShown());
		showPop_->setText(e_->popoutsShown() ? "Tuck pop-outs" : "Show pop-outs");
		showPop_->setVisible(e_->cfg.popoutTuck && e_->cfg.popoutMonitor < 0);
		showPop_->blockSignals(false);
	}
	QString st = e_->appState();
	if (st == "connected") {
		appBtn_->setText("Stop ClipHound");
		appBtn_->setStyleSheet("QPushButton { border-left: 4px solid #8f9c5a; }");
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
	if (update_) {
		bool has = e_->updateAvailable();
		update_->setVisible(has);
		if (has) {
			QString t = "Version " + e_->newVersion().toHtmlEscaped() + " is out (you have " +
				    QString(PLUGIN_VERSION) + ")";
			if (!e_->newVersionUrl().isEmpty())
				t += "  <a style=\"color:#c99a3b\" href=\"" + e_->newVersionUrl().toHtmlEscaped() +
				     "\">download</a>";
			update_->setText(t);
			update_->setToolTip(e_->newVersionNotes());
		}
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
		dual_->setText(e_->dualForced() ? "FORCED Dual POV"
			       : e_->dualOn()   ? "Dual POV (auto)"
						: "Force Dual POV");
		dual_->setStyleSheet(e_->dualForced() ? "QPushButton { border-left: 4px solid #c99a3b; }"
				     : e_->dualOn()   ? "QPushButton { border-left: 4px solid #8f9c5a; }"
						      : "");
	}
	show_->setEnabled(!e_->applied());
	back_->setEnabled(e_->applied());
	if (replay_) {
		bool on = e_->replaying();
		replay_->setText(on ? "Stop replay" : "Instant replay");
		highlights_->setText(on ? "Stop" : e_->highlightsBuilding() ? "Building..." : "Play highlights");
		replay_->setStyleSheet(on ? "QPushButton { border-left: 4px solid #ce6050; }" : "");
	}
	if (sceneWarn_) {
		QString live = e_->sceneMismatch();
		sceneWarn_->setVisible(!live.isEmpty());
		if (!live.isEmpty())
			sceneWarn_->setText(
				"Live scene is '" + live.toHtmlEscaped() + "', but the plugin works in '" +
				QString::fromStdString(e_->cfg.sceneName).toHtmlEscaped() +
				"': nothing it shows is on stream. Switch to that scene, or change it under "
				"Settings, Switch.");
	}
	if (locked_) {
		Engine::Access a = e_->rosterAccess();
		QString url = e_->discordUrl().toHtmlEscaped();
		bool show = !e_->cfg.rosterEnabled || a == Engine::Access::NotMember || a == Engine::Access::NoUsername;
		bool unreadable = e_->cfg.rosterEnabled && e_->roster.running() && !e_->roster.healthy();
		locked_->setVisible(show || unreadable);
		if (unreadable && !show) {
			locked_->setText("Discord voice: " + e_->roster.status().toHtmlEscaped() +
					 ". Until it can be read, the squad shows as it would without the roster.");
		} else if (!e_->cfg.rosterEnabled || a == Engine::Access::NoUsername)
			locked_->setText(
				"<a style=\"color:#c99a3b\" href=\"" + url +
				"\">Join Kennel.gg Discord for more automation</a>: live squad mates appear "
				"here. Already in? <a style=\"color:#c99a3b\" href=\"kennel:detect\">Detect my "
				"Discord username</a> (or <a style=\"color:#c99a3b\" href=\"kennel:username\">type it</a>).");
		else
			locked_->setText(
				"<a style=\"color:#c99a3b\" href=\"" + url +
				"\">Join Kennel.gg Discord for more automation</a>: \"" +
				QString::fromStdString(e_->cfg.myDiscord).toHtmlEscaped() +
				"\" is not in the server. <a style=\"color:#c99a3b\" href=\"kennel:detect\">Detect</a> or "
				"<a style=\"color:#c99a3b\" href=\"kennel:username\">change</a> the username.");
	}
}

void Dock::showSupportNote()
{
	QMessageBox m((QWidget *)obs_frontend_get_main_window());
	m.setWindowTitle("Kennel.gg Wardogs Streaming Tool");
	m.setIcon(QMessageBox::NoIcon);
	m.setTextFormat(Qt::RichText);
	m.setText("<b>Enjoying the plugin?</b>");
	m.setInformativeText("If you are enjoying the plugin and would like to support development, please consider "
			     "supporting us. It is free and always will be; this keeps it moving.<br><br>"
			     "<a href=\"" +
			     QString(Config::supportUrl()) + "\">" + QString(Config::supportUrl()) + "</a>");
	auto *support = m.addButton("Support development", QMessageBox::AcceptRole);
	m.addButton("Maybe later", QMessageBox::RejectRole);
	m.exec();
	if (m.clickedButton() == support)
		QDesktopServices::openUrl(QUrl(Config::supportUrl()));
	e_->supportNoteShown(); // once, whichever button
}

void Dock::openClips(const QString &focusPath)
{
	auto *d = new ClipsDialog(e_, (QWidget *)obs_frontend_get_main_window());
	d->show();
	if (!focusPath.isEmpty())
		d->focusClip(focusPath);
}

void Dock::showLanguageNote()
{
	QMessageBox m((QWidget *)obs_frontend_get_main_window());
	m.setWindowTitle("Kennel.gg Wardogs Streaming Tool");
	m.setIcon(QMessageBox::NoIcon);
	m.setTextFormat(Qt::RichText);
	m.setText("<b>Game language not supported yet?</b>");
	m.setInformativeText(
		"The plugin finds you are downed by the damage-log header on your screen, and it knows the "
		"wording in <b>English, Spanish and French</b>. Something header-like has been on your screen "
		"several times without matching any of them, so your game may be in another language.<br><br>"
		"To add it: while you are downed, press <b>Save a frame</b> (Settings, Detect tab, or the button "
		"below if you are downed right now) and open a ticket in the Kennel.gg Discord with the picture. "
		"Your language goes into the next build.<br><br>"
		"Game already in English, Spanish or French? Then this is a resolution or HUD-scale difference: "
		"cut your own header on the Detect tab instead.");
	auto *save = m.addButton("Save a frame now", QMessageBox::ActionRole);
	auto *discord = m.addButton("Open the Kennel.gg Discord", QMessageBox::AcceptRole);
	m.addButton("Close", QMessageBox::RejectRole);
	m.exec();
	if (m.clickedButton() == save) {
		QString r = e_->saveFrame();
		QMessageBox::information((QWidget *)obs_frontend_get_main_window(), "Kennel.gg Wardogs Streaming Tool",
					 r.startsWith("Could not")
						 ? r
						 : "Saved: " + r +
							   "\n\nAttach this to a ticket in the Kennel.gg Discord.");
	} else if (m.clickedButton() == discord)
		QDesktopServices::openUrl(QUrl(Config::kennelDiscordUrl()));
	e_->languageNoteShown();
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
	d->setWindowTitle("Kennel.gg Wardogs - logs");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	d->resize(820, 600);
	auto *v = new QVBoxLayout(d);
	auto *txt = new QPlainTextEdit(d);
	txt->setReadOnly(true);
	txt->setLineWrapMode(QPlainTextEdit::NoWrap);
	QString appDir = QString::fromStdString(e_->cfg.appPath).isEmpty()
				 ? QString("C:/ProgramData/Kennel.gg/ClipHound")
				 : QFileInfo(QString::fromStdString(e_->cfg.appPath)).absolutePath();
	QString body = QString("=== Kennel.gg Wardogs plugin %1 ===\n").arg(PLUGIN_VERSION);
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

void Dock::openSquad()
{
	if (squad_) {
		squad_->raise();
		squad_->activateWindow();
		return;
	}
	auto *dlg = new SquadPanel(e_, (QWidget *)obs_frontend_get_main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	squad_ = dlg;
	showOnScreen(dlg);
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
