#include "ui/wizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileInfo>
#include <obs-module.h>

static QLabel *note(const QString &t, QWidget *p)
{
	auto *l = new QLabel(t, p);
	l->setWordWrap(true);
	{
		QFont f = l->font();
		if (f.pointSizeF() > 0)
			f.setPointSizeF(f.pointSizeF() - 0.5);
		else if (f.pixelSize() > 2)
			f.setPixelSize(f.pixelSize() - 1);
		l->setFont(f);
	}
	return l;
}

SetupWizard::SetupWizard(Engine *engine, QWidget *parent) : QWizard(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs OBS Tool - setup");
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage, true);
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setMinimumSize(560, 420);
	resize(720, 540);
	addPage(pageWelcome());
	addPage(pageGame());
	addPage(pageSquad());
	addPage(pageClips());
	addPage(pageDone());
	connect(this, &QWizard::currentIdChanged, this, [this](int id) {
		if (id == 1)
			fillGame();
		if (id == 2)
			fillSquad();
		if (id == 4) {
			QString g = game_->currentText();
			int n = (int)e_->cfg.friends.size();
			summary_->setText(
				QString("<p><b>Name:</b> %1<br><b>Game source:</b> %2<br><b>Squad mates:</b> %3<br><b>Clips:</b> replay buffer on%4</p>"
					"<p>Press Finish, then <b>get downed once</b> with WARDOGS on screen. The dock (View → Docks → Kennel.gg Wardogs) turns red "
					"and your stream shows the squad mate; it comes back the instant you are revived.</p>"
					"<p>Everything here can be changed under Tools → Kennel.gg Wardogs OBS Tool...</p>")
					.arg(name_->text().trimmed().isEmpty() ? Lan::hostName()
									       : name_->text().trimmed(),
					     g.isEmpty() ? "(none yet)" : g)
					.arg(n == 0 ? "none yet - add one from the dock later" : QString::number(n))
					.arg(launchApp_->isChecked() ? ", ClipHound starts with OBS" : ""));
		}
	});
}

QWizardPage *SetupWizard::pageWelcome()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Welcome");
	p->setSubTitle(
		"Downed in WARDOGS? Your stream will show a squad mate's POV until you are back up. Your mic is never touched.");
	auto *f = new QFormLayout(p);
	name_ = new QLineEdit(QString::fromStdString(e_->cfg.playerName), p);
	name_->setPlaceholderText(Lan::hostName());
	f->addRow("Your name", name_);
	f->addRow(note(
		"Shown to squad mates on the same network and on the POV name tag. Leave blank to use this PC's name.",
		p));
	lookName_ = new QCheckBox("Show a \"POV · NAME\" tag over the squad mate's feed", p);
	lookName_->setChecked(e_->cfg.lookName);
	f->addRow(lookName_);
	return p;
}

QWizardPage *SetupWizard::pageGame()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Your game");
	p->setSubTitle(
		"Which OBS source shows WARDOGS? It is watched for the damage log that appears while you are downed.");
	auto *v = new QVBoxLayout(p);
	auto *row = new QHBoxLayout();
	game_ = new QComboBox(p);
	auto *mk = new QPushButton("Create a Game Capture for me", p);
	row->addWidget(game_, 1);
	row->addWidget(mk);
	v->addLayout(row);
	gameHint_ = note("", p);
	v->addWidget(gameHint_);
	v->addStretch(1);
	connect(mk, &QPushButton::clicked, this, [this]() {
		std::string e = e_->sw.createGameCapture(e_->cfg);
		gameHint_->setText(e.empty() ? "Added a Game Capture (any fullscreen game) at the bottom of your scene."
					     : QString::fromStdString(e));
		fillGame();
	});
	return p;
}

void SetupWizard::fillGame()
{
	game_->clear();
	QString chosen = QString::fromStdString(e_->cfg.gameSource);
	int pick = -1;
	for (auto &i : Switcher::inputs()) {
		if (i.first == Config::webSourceName() || i.first == Config::overlaySourceName() ||
		    i.first.rfind("Kennel · ", 0) == 0)
			continue;
		if (i.second.find("audio") != std::string::npos || i.second.find("wasapi") != std::string::npos ||
		    i.second.find("text") != std::string::npos || i.second == "browser_source" ||
		    i.second == "image_source" || i.second == "color_source")
			continue;
		game_->addItem(QString::fromStdString(i.first), QString::fromStdString(i.second));
		int idx = game_->count() - 1;
		if (i.first == chosen.toStdString())
			pick = idx;
		else if (pick < 0 && (i.second == "game_capture" || i.second == "dshow_input"))
			pick = idx;
	}
	if (pick >= 0)
		game_->setCurrentIndex(pick);
	if (game_->count() == 0)
		gameHint_->setText("No video source in OBS yet. Press the button and one is added for you.");
	else if (gameHint_->text().isEmpty())
		gameHint_->setText(
			"Game Capture or a capture card is the usual answer. On a two-PC setup pick the input that carries the gameplay.");
}

QWizardPage *SetupWizard::pageSquad()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Your squad, from Discord");
	p->setSubTitle("Whose POV viewers see while you are down. Squad mates come from the Discord call you are in.");
	auto *v = new QVBoxLayout(p);
	v->addWidget(note(
		"<b>Every session:</b> join your squad's voice channel, watch a squad mate's stream and pop it out "
		"(right-click their stream, <b>Pop Out</b>), then press <b>Add pop-outs</b> on the dock. Their slot is made, "
		"named after them, and their window is tucked to the edge of your screen where Discord keeps drawing it. "
		"Pop out everyone whose POV you might show.<br><br>"
		"<b>Do not minimise a pop-out.</b> A minimised window stops drawing and its feed freezes. Tucked away is "
		"fine; minimised is not. Show pop-outs on the dock brings them back when you need their volume control.",
		p));
	auto *form = new QFormLayout();
	me_ = new QLineEdit(QString::fromStdString(e_->cfg.myDiscord), p);
	me_->setPlaceholderText("your Discord username - the lower-case one under your display name");
	form->addRow("Your Discord username", me_);
	v->addLayout(form);
	rosterOn_ = new QCheckBox("See who is in my channel and who is live, by itself (recommended)", p);
	rosterOn_->setChecked(true);
	rosterOn_->setToolTip(
		"The Kennel Ops Discord bot publishes who is in voice and who is streaming. With this on, "
		"the plugin knows which channel is yours, shows only people who are live, and drops "
		"anyone who leaves.");
	v->addWidget(rosterOn_);
	bot_ = new QLabel(p);
	bot_->setWordWrap(true);
	bot_->setOpenExternalLinks(true);
	v->addWidget(bot_);
	auto refreshBot = [this]() {
		QStringList gs = e_->roster.guilds();
		QString inv = e_->roster.inviteUrl().isEmpty() ? Config::botInviteUrl() : e_->roster.inviteUrl();
		bot_->setText(
			"<b>For the best experience the Kennel Ops bot needs to be in the Discord server you play "
			"on.</b> It only asks to view channels. Servers it can see now: " +
			(gs.isEmpty() ? QString("(checking...)") : gs.join(", ")) +
			". Playing somewhere else? Its admin adds the bot with <a href=\"" + inv +
			"\">this link</a>, and that server appears in the Squad panel.");
	};
	refreshBot();
	connect(&e_->roster, &Roster::polled, this, refreshBot);
	connect(&e_->roster, &Roster::changed, this, refreshBot);
	if (!e_->roster.running())
		e_->roster.configure(QString::fromStdString(e_->cfg.rosterUrl), e_->cfg.rosterPollS, QString(),
				     QString());
	v->addWidget(note("Squad mates on Twitch, Kick, YouTube or VDO.Ninja are added under Settings, Squad, Add. "
			  "A slot for someone on this network sharing over NDI is made by itself.",
			  p));
	squad_ = new QListWidget(p);
	squad_->setMaximumHeight(90);
	v->addWidget(squad_);
	twitch_ = new QLineEdit(p); // kept for the by-hand Twitch add, off the page
	twitch_->hide();
	lanShare_ = new QCheckBox(p);
	lanShare_->setChecked(e_->cfg.ndiShare);
	lanShare_->hide();
	v->addStretch(1);
	connect(&e_->lan, &Lan::peersChanged, this, [this]() { fillSquad(); });
	connect(e_, &Engine::stateChanged, this, [this]() { fillSquad(); });
	return p;
}

void SetupWizard::fillSquad()
{
	squad_->clear();
	for (size_t i = 0; i < e_->cfg.friends.size(); i++) {
		auto &f = e_->cfg.friends[i];
		QString kind = f.kind == FriendKind::Twitch     ? "Twitch"
			       : f.kind == FriendKind::Kick     ? "Kick"
			       : f.kind == FriendKind::YouTube  ? "YouTube"
			       : f.kind == FriendKind::VdoNinja ? "VDO.Ninja"
			       : f.kind == FriendKind::Discord  ? "Discord"
			       : f.kind == FriendKind::Ndi      ? "NDI (LAN)"
								: "OBS source";
		squad_->addItem(QString::fromStdString(f.name) + "  ·  " + kind +
				((int)i == e_->cfg.activeFriend ? "  ·  active" : ""));
	}
	for (auto &kv : e_->lan.peers()) {
		bool known = false;
		for (auto &f : e_->cfg.friends)
			if (f.kind == FriendKind::Ndi &&
			    f.channel ==
				    Switcher::ndiFullName(kv.second.host.toStdString(), kv.second.ndi.toStdString()))
				known = true;
		if (!known)
			squad_->addItem(kv.second.name + "  ·  on the network" +
					(kv.second.ndi.isEmpty() ? " (not sharing)" : ", will be added"));
	}
	if (squad_->count() == 0)
		squad_->addItem("(nobody yet)");
}

void SetupWizard::addTwitch()
{
	QString ch = twitch_->text().trimmed().toLower().remove('@');
	if (ch.isEmpty())
		return;
	Friend f;
	f.name = ch.toStdString();
	f.kind = FriendKind::Twitch;
	f.channel = ch.toStdString();
	e_->cfg.friends.push_back(f);
	if (e_->cfg.friends.size() == 1)
		e_->cfg.activeFriend = 0;
	e_->cfg.save();
	twitch_->clear();
	fillSquad();
}

QWizardPage *SetupWizard::pageClips()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Clips");
	p->setSubTitle("OBS's replay buffer is started for you. Clips are saved and named with tags.");
	auto *v = new QVBoxLayout(p);
	clipDowned_ = new QCheckBox("Save a clip whenever I get downed", p);
	clipDowned_->setChecked(e_->cfg.clipOnDowned);
	v->addWidget(clipDowned_);
	bool appInstalled = QFileInfo::exists("C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe") ||
			    !e_->cfg.appPath.empty();
	launchApp_ = new QCheckBox("Start ClipHound with OBS (reads the kill feed and clips notable kills)", p);
	launchApp_->setChecked(appInstalled && (e_->cfg.launchApp || e_->cfg.appPath.empty()));
	launchApp_->setEnabled(appInstalled);
	v->addWidget(launchApp_);
	v->addWidget(note(
		appInstalled
			? "ClipHound runs silently in the background. Your in-game name, clip folder and the Twitch login are on the ClipHound tab in Settings."
			: "ClipHound was not installed. Run the installer again and tick it if you want kill-feed clips.",
		p));
	v->addWidget(note(
		"Hotkey \"Kennel.gg Wardogs: save a clip now\" and the dock's Clip now button save one by hand. Replay length is OBS's Settings → Output → Replay Buffer.",
		p));
	v->addStretch(1);
	return p;
}

QWizardPage *SetupWizard::pageDone()
{
	auto *p = new QWizardPage(this);
	p->setTitle("Ready");
	auto *v = new QVBoxLayout(p);
	summary_ = new QLabel(p);
	summary_->setWordWrap(true);
	v->addWidget(summary_);
	v->addStretch(1);
	return p;
}

void SetupWizard::accept()
{
	Config &c = e_->cfg;
	c.playerName = name_->text().trimmed().toStdString();
	c.lookName = lookName_->isChecked();
	if (game_->currentIndex() >= 0)
		c.gameSource = game_->currentText().toStdString();
	c.ndiShare = lanShare_->isChecked();
	c.myDiscord = me_->text().trimmed().toLower().remove('@').toStdString();
	c.rosterEnabled = rosterOn_->isChecked();
	c.setupDone = true;
	c.clipOnDowned = clipDowned_->isChecked();
	c.launchApp = launchApp_->isChecked();
	if (c.launchApp && c.appPath.empty())
		c.appPath = "C:/ProgramData/Kennel.gg/ClipHound/ClipHound.exe";
	c.save();
	e_->reloadConfig();
	if (c.launchApp)
		e_->launchApp();
	e_->log("Setup done. Get downed once to see it work.");
	QWizard::accept();
}
