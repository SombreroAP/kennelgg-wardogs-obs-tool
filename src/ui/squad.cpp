#include "ui/squad.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QComboBox>
#include <QSlider>
#include <algorithm>

SquadPanel::SquadPanel(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs - Squad");
	setMinimumWidth(460);
	auto *v = new QVBoxLayout(this);

	auto *how = new QLabel(
		"In Discord, right-click a squad mate's stream and choose <b>Pop Out</b>. Then right-click the stream again and <b>mute it</b>: its game sound would otherwise play in your headphones and go out on your stream through Desktop Audio the whole time. "
		"Then press Add: every popped-out stream becomes a squad mate, named by their "
		"Discord username and showing that window and no other.",
		this);
	how->setWordWrap(true);
	v->addWidget(how);

	add_ = new QPushButton("Add popped-out Discord streams - (mute Discord stream before adding)", this);
	add_->setMinimumHeight(36);
	add_->setDefault(true);
	v->addWidget(add_);
	connect(add_, &QPushButton::clicked, this, &SquadPanel::addPopouts);
	result_ = new QLabel(this);
	result_->setWordWrap(true);
	result_->setStyleSheet("color: palette(mid);");
	v->addWidget(result_);

	list_ = new QListWidget(this);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	v->addWidget(list_, 1);
	auto *row = new QHBoxLayout();
	active_ = new QPushButton("Make active", this);
	dual_ = new QPushButton("Show in Dual POV", this);
	dual_->setToolTip("Their feed in the small Dual POV window, now, and it stays up until you turn it off.");
	gameName_ = new QPushButton("In-game name...", this);
	remove_ = new QPushButton("Remove", this);
	row->addWidget(active_);
	row->addWidget(dual_);
	row->addWidget(gameName_);
	row->addWidget(remove_);
	row->addStretch(1);
	v->addLayout(row);
	connect(active_, &QPushButton::clicked, this, &SquadPanel::makeActive);
	connect(dual_, &QPushButton::clicked, this, &SquadPanel::showInDual);
	connect(gameName_, &QPushButton::clicked, this, &SquadPanel::editGameName);
	connect(remove_, &QPushButton::clicked, this, &SquadPanel::removeSelected);
	connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { makeActive(); });

	auto *form = new QFormLayout();
	roster_ = new QCheckBox("Also add whoever goes live in Kennel.gg voice, by themselves", this);
	roster_->setChecked(e_->cfg.rosterEnabled);
	roster_->setToolTip("The Kennel.gg Discord bot publishes who is in voice and who is sharing. With this on, a "
			    "slot appears when a squad mate goes live and goes away when they stop.");
	form->addRow(roster_);
	connect(roster_, &QCheckBox::toggled, this, [this](bool on) {
		e_->cfg.rosterEnabled = on;
		e_->cfg.save();
		e_->applyRosterConfig();
		if (!on)
			refresh();
	});
	auto *tuck = new QCheckBox("Keep pop-outs drawing: pin them on top, tucked to the screen edge", this);
	tuck->setChecked(e_->cfg.popoutTuck);
	tuck->setToolTip("Discord stops drawing a window the game completely covers, and its capture goes black. "
			 "Tucked to the edge with a few pixels showing it keeps drawing, and the capture still gets "
			 "the whole window. Needs the game in borderless windowed mode.");
	form->addRow(tuck);
	connect(tuck, &QCheckBox::toggled, this, [this](bool on) {
		e_->cfg.popoutTuck = on;
		e_->cfg.save();
		if (on)
			e_->watchPopouts();
		else
			e_->releaseAllPopouts();
	});
	auto *where = new QComboBox(this);
	where->addItem("Tucked to the edge of their own screen (a sliver showing)", -1);
	{
		int i = 0;
		for (const auto &m : Switcher::monitors())
			where->addItem(QString("Parked on monitor %1 (%2), fully visible")
					       .arg(++i)
					       .arg(QString::fromStdString(m)),
				       i - 1);
	}
	where->setCurrentIndex(std::max(0, where->findData(e_->cfg.popoutMonitor)));
	where->setToolTip("Parking them on a screen the game and OBS are not on keeps every pop-out visible, on top "
			  "and within reach, so Discord's own volume control on each one is a click away.");
	form->addRow("Pop-outs live", where);
	connect(where, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, where](int) {
		e_->cfg.popoutMonitor = where->currentData().toInt();
		e_->cfg.save();
		e_->releaseAllPopouts();
		e_->watchPopouts();
	});
	show_ = new QPushButton(this);
	show_->setCheckable(true);
	show_->setToolTip(
		"Bring the pop-outs back on screen to use their own controls, then press again to tuck them away.");
	form->addRow(show_);
	connect(show_, &QPushButton::clicked, this, [this](bool on) { e_->showPopouts(on); });
	guild_ = new QComboBox(this);
	guild_->setToolTip("Which Discord server's voice channels count. The bot has to be in the server to see it: "
			   "the link below adds it to another one.");
	form->addRow("Discord server", guild_);
	connect(guild_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (fillingGuilds_ || i < 0)
			return;
		e_->cfg.rosterGuild = guild_->currentData().toString().toStdString();
		e_->cfg.save();
		e_->applyRosterConfig();
	});
	invite_ = new QLabel(this);
	invite_->setOpenExternalLinks(true);
	invite_->setWordWrap(true);
	invite_->setStyleSheet("color: palette(mid);");
	form->addRow(invite_);
	rosterState_ = new QLabel(this);
	rosterState_->setStyleSheet("color: palette(mid);");
	form->addRow(rosterState_);
	me_ = new QLineEdit(QString::fromStdString(e_->cfg.myDiscord), this);
	me_->setPlaceholderText("so your own stream is never added, and only your channel counts");
	form->addRow("Your Discord username", me_);
	connect(me_, &QLineEdit::editingFinished, this, [this]() {
		std::string v = me_->text().trimmed().toLower().toStdString();
		if (v == e_->cfg.myDiscord)
			return;
		e_->cfg.myDiscord = v;
		e_->cfg.save();
		e_->syncRoster();
	});
	v->addLayout(form);

	connect(e_, &Engine::stateChanged, this, &SquadPanel::refresh);
	connect(&e_->roster, &Roster::polled, this, &SquadPanel::refresh);
	connect(&e_->roster, &Roster::changed, this, &SquadPanel::refresh);
	refresh();
}

void SquadPanel::refresh()
{
	int sel = list_->currentRow();
	list_->clear();
	for (size_t i = 0; i < e_->cfg.friends.size(); ++i) {
		const Friend &f = e_->cfg.friends[i];
		QString line = QString::fromStdString(f.name);
		if (!f.gameName.empty() && f.gameName != f.name)
			line += "  (in game: " + QString::fromStdString(f.gameName) + ")";
		if ((int)i == e_->cfg.activeFriend)
			line += "   (active)";
		if ((int)i == e_->cfg.dualFriend && e_->dualOn())
			line += "   (in the dual window)";
		QString where;
		switch (f.kind) {
		case FriendKind::Discord:
			where = f.onPopout()            ? "on their pop-out window"
				: f.sharesDiscordCall() ? "on the Discord window (not popped out)"
							: "on a Discord window";
			break;
		case FriendKind::Twitch:
			where = "Twitch";
			break;
		case FriendKind::Kick:
			where = "Kick";
			break;
		case FriendKind::YouTube:
			where = "YouTube";
			break;
		case FriendKind::VdoNinja:
			where = "VDO.Ninja";
			break;
		case FriendKind::ObsSource:
			where = "OBS source";
			break;
		}
		if (f.fromRoster)
			where += ", from Kennel.gg voice";
		if (f.kind == FriendKind::Discord && !f.handle.empty() &&
		    QString::fromStdString(f.handle).compare(QString::fromStdString(f.name), Qt::CaseInsensitive) != 0)
			where += " (" + QString::fromStdString(f.handle) + "'s stream!)";
		QString st = e_->feedStateText(f);
		if (!st.isEmpty())
			where += "  -  " + st;
		list_->addItem(line + "  -  " + where);
	}
	if (list_->count() == 0)
		list_->addItem("(no squad mates yet)");
	if (sel >= 0 && sel < list_->count())
		list_->setCurrentRow(sel);
	bool any = !e_->cfg.friends.empty();
	active_->setEnabled(any);
	remove_->setEnabled(any);
	gameName_->setEnabled(any);
	dual_->setEnabled(any);
	show_->blockSignals(true);
	show_->setChecked(e_->popoutsShown());
	show_->setText(e_->popoutsShown() ? "Tuck pop-outs away again" : "Show pop-outs (to mute or adjust them)");
	show_->blockSignals(false);
	show_->setVisible(e_->cfg.popoutTuck && e_->cfg.popoutMonitor < 0);
	rosterState_->setText(e_->cfg.rosterEnabled ? "Discord voice: " + e_->rosterStatus() : "");
	{
		QStringList gs = e_->roster.guilds();
		QString cur = QString::fromStdString(e_->cfg.rosterGuild);
		if (!cur.isEmpty() && !gs.contains(cur))
			gs << cur;
		QStringList shown;
		for (int i = 0; i < guild_->count(); i++)
			shown << guild_->itemText(i);
		QStringList want = QStringList{"Any server the bot can see"} + gs;
		fillingGuilds_ = true;
		if (shown != want) {
			guild_->clear();
			guild_->addItem("Any server the bot can see", "");
			for (const QString &g : gs)
				guild_->addItem(g, g);
		}
		guild_->setCurrentIndex(std::max(0, guild_->findData(cur)));
		fillingGuilds_ = false;
		QString inv = e_->roster.inviteUrl();
		invite_->setText(
			inv.isEmpty() ? QString()
				      : "Playing on another server? Its admin adds the Kennel Ops bot with <a href=\"" +
						inv + "\">this link</a> and that server's channels appear here.");
		invite_->setVisible(!inv.isEmpty() && e_->cfg.rosterEnabled);
	}
	rosterState_->setVisible(e_->cfg.rosterEnabled);
}

void SquadPanel::addPopouts()
{
	add_->setEnabled(false);
	QStringList added;
	result_->setText(e_->addPopouts(&added));
	QTimer::singleShot(400, this, [this]() { add_->setEnabled(true); });
	refresh();
	// their in-game name is what the NEARBY list is matched against; Discord's username is only a
	// guess at it, so ask while they are being added rather than leave a slot that never matches
	for (const QString &name : added)
		for (size_t i = 0; i < e_->cfg.friends.size(); ++i)
			if (QString::fromStdString(e_->cfg.friends[i].name) == name)
				askGameName((int)i);
}

void SquadPanel::askGameName(int idx)
{
	if (idx < 0 || idx >= (int)e_->cfg.friends.size())
		return;
	Friend &f = e_->cfg.friends[idx];
	bool ok = false;
	QString cur = QString::fromStdString(f.gameName.empty() ? f.name : f.gameName);
	QString v = QInputDialog::getText(
		this, "In-game name",
		"What is " + QString::fromStdString(f.name) +
			" called in the game?\n\nThis is matched against the NEARBY list, so type it "
			"as the game shows it (tags like [WDUK] are fine to leave out).",
		QLineEdit::Normal, cur, &ok);
	if (!ok)
		return;
	v = v.trimmed();
	f.gameName = (v.isEmpty() || v == QString::fromStdString(f.name)) ? "" : v.toStdString();
	e_->cfg.save();
	e_->pushAppConfig(); // ClipHound matches on these names
	refresh();
}

void SquadPanel::editGameName()
{
	askGameName(list_->currentRow());
}

void SquadPanel::showInDual()
{
	int r = list_->currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	if (e_->dualOn() && e_->cfg.dualFriend == r)
		e_->setDual(false, "squad panel");
	else
		e_->showInDual(r);
	refresh();
}

void SquadPanel::makeActive()
{
	int r = list_->currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	e_->setActive(r);
	refresh();
}

void SquadPanel::removeSelected()
{
	int r = list_->currentRow();
	if (r < 0 || r >= (int)e_->cfg.friends.size())
		return;
	Friend f = e_->cfg.friends[r];
	if (QMessageBox::question(this, "Kennel.gg Wardogs",
				  "Remove " + QString::fromStdString(f.name) + " and the sources made for them?") !=
	    QMessageBox::Yes)
		return;
	if (e_->applied() && r == e_->cfg.activeFriend)
		e_->applyNow(false, "squad mate removed");
	e_->releasePopout(f);
	e_->sw.removeFriendSources(e_->cfg, f);
	e_->cfg.friends.erase(e_->cfg.friends.begin() + r);
	if (e_->cfg.activeFriend >= (int)e_->cfg.friends.size())
		e_->cfg.activeFriend = std::max(0, (int)e_->cfg.friends.size() - 1);
	e_->cfg.save();
	e_->armPopoutWatch();
	e_->log("Squad: removed " + QString::fromStdString(f.name) + ".");
	emit e_->stateChanged();
}
