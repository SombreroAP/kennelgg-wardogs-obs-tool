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
	setWindowTitle("Kennel.gg WARDOGS OBS Tools - setup");
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
					"<p>Press Finish, then <b>get downed once</b> with WARDOGS on screen. The dock (View → Docks → Kennel WARDOGS) turns red "
					"and your stream shows the squad mate; it comes back the instant you are revived.</p>"
					"<p>Everything here can be changed under Tools → Kennel.gg WARDOGS OBS Tools...</p>")
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
	p->setTitle("Squad mates");
	p->setSubTitle(
		"Whose POV should viewers see while you are down? Squad mates running this plugin on your network are found by themselves.");
	auto *v = new QVBoxLayout(p);
	squad_ = new QListWidget(p);
	squad_->setMaximumHeight(150);
	v->addWidget(squad_);
	auto *row = new QHBoxLayout();
	twitch_ = new QLineEdit(p);
	twitch_->setPlaceholderText("a squad mate's Twitch channel, e.g. pup");
	auto *add = new QPushButton("Add Twitch channel", p);
	row->addWidget(twitch_, 1);
	row->addWidget(add);
	v->addLayout(row);
	lanShare_ = new QCheckBox("Share my own feed over NDI for squad mates on this network (needs DistroAV)", p);
	lanShare_->setChecked(e_->cfg.ndiShare);
	v->addWidget(lanShare_);
	v->addWidget(note(
		"Twitch is the zero-setup option (~2 s behind). Discord Go Live, VDO.Ninja and NDI are under Settings → Switch → Add. You can skip this and add someone later from the dock.",
		p));
	v->addStretch(1);
	connect(add, &QPushButton::clicked, this, &SetupWizard::addTwitch);
	connect(twitch_, &QLineEdit::returnPressed, this, &SetupWizard::addTwitch);
	connect(&e_->lan, &Lan::peersChanged, this, [this]() { fillSquad(); });
	return p;
}

void SetupWizard::fillSquad()
{
	squad_->clear();
	for (size_t i = 0; i < e_->cfg.friends.size(); i++) {
		auto &f = e_->cfg.friends[i];
		QString kind = f.kind == FriendKind::Twitch     ? "Twitch"
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
	bool appInstalled = QFileInfo::exists("C:/ProgramData/Kennel WARDOGS/ClipHound/ClipHound.exe") ||
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
		"Hotkey \"Kennel WARDOGS: save a clip now\" and the dock's Clip now button save one by hand. Replay length is OBS's Settings → Output → Replay Buffer.",
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
	c.clipOnDowned = clipDowned_->isChecked();
	c.launchApp = launchApp_->isChecked();
	if (c.launchApp && c.appPath.empty())
		c.appPath = "C:/ProgramData/Kennel WARDOGS/ClipHound/ClipHound.exe";
	c.save();
	e_->reloadConfig();
	if (c.launchApp)
		e_->launchApp();
	e_->log("Setup done. Get downed once to see it work.");
	QWizard::accept();
}
