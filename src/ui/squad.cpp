#include "ui/squad.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QTimer>
#include <algorithm>

SquadPanel::SquadPanel(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg Wardogs - Squad");
	setMinimumWidth(460);
	auto *v = new QVBoxLayout(this);

	auto *how = new QLabel("In Discord, right-click a squad mate's stream and choose <b>Pop Out</b>. "
			       "Then press Add: every popped-out stream becomes a squad mate, named by their "
			       "Discord username and showing that window and no other.",
			       this);
	how->setWordWrap(true);
	v->addWidget(how);

	add_ = new QPushButton("Add popped-out Discord streams", this);
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
	remove_ = new QPushButton("Remove", this);
	row->addWidget(active_);
	row->addWidget(remove_);
	row->addStretch(1);
	v->addLayout(row);
	connect(active_, &QPushButton::clicked, this, &SquadPanel::makeActive);
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
		if ((int)i == e_->cfg.activeFriend)
			line += "   (active)";
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
		case FriendKind::Ndi:
			where = "NDI";
			break;
		}
		if (f.fromRoster)
			where += ", from Kennel.gg voice";
		list_->addItem(line + "  -  " + where);
	}
	if (list_->count() == 0)
		list_->addItem("(no squad mates yet)");
	if (sel >= 0 && sel < list_->count())
		list_->setCurrentRow(sel);
	bool any = !e_->cfg.friends.empty();
	active_->setEnabled(any);
	remove_->setEnabled(any);
	rosterState_->setText(e_->cfg.rosterEnabled ? "Kennel.gg voice: " + e_->rosterStatus() : "");
	rosterState_->setVisible(e_->cfg.rosterEnabled);
}

void SquadPanel::addPopouts()
{
	add_->setEnabled(false);
	result_->setText(e_->addPopouts());
	QTimer::singleShot(400, this, [this]() { add_->setEnabled(true); });
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
	e_->sw.removeFriendSources(e_->cfg, f);
	e_->cfg.friends.erase(e_->cfg.friends.begin() + r);
	if (e_->cfg.activeFriend >= (int)e_->cfg.friends.size())
		e_->cfg.activeFriend = std::max(0, (int)e_->cfg.friends.size() - 1);
	e_->cfg.save();
	e_->armPopoutWatch();
	e_->log("Squad: removed " + QString::fromStdString(f.name) + ".");
	emit e_->stateChanged();
}
