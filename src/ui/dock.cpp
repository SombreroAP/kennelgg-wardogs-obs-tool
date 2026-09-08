#include "ui/dock.h"
#include "ui/settings-dialog.h"
#include "ui/wizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <obs-frontend-api.h>

Dock::Dock(Engine *engine, QWidget *parent) : QWidget(parent), e_(engine)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(8, 8, 8, 8);
	state_ = new QLabel(this);
	QFont f = state_->font();
	f.setPointSizeF(f.pointSizeF() + 2);
	f.setBold(true);
	state_->setFont(f);
	v->addWidget(state_);

	meter_ = new QProgressBar(this);
	meter_->setRange(0, 1000);
	meter_->setTextVisible(true);
	meter_->setFormat("damage log match %v / 1000");
	v->addWidget(meter_);

	auto *row = new QHBoxLayout();
	row->addWidget(new QLabel("Squad mate", this));
	active_ = new QComboBox(this);
	row->addWidget(active_, 1);
	v->addLayout(row);
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
	pause_ = new QPushButton("Pause", this);
	auto *settings = new QPushButton("Settings...", this);
	auto *wiz = new QPushButton("Setup", this);
	btns2->addWidget(pause_);
	btns2->addWidget(wiz);
	btns2->addWidget(settings);
	connect(wiz, &QPushButton::clicked, this, &Dock::openWizard);
	v->addLayout(btns2);
	connect(pause_, &QPushButton::clicked, this, [this]() { e_->setEnabled(!e_->cfg.enabled); });
	connect(settings, &QPushButton::clicked, this, &Dock::openSettings);

	app_ = new QLabel(this);
	clip_ = new QLabel(this);
	for (auto *l : {app_, clip_}) {
		l->setWordWrap(true);
		l->setStyleSheet("color: palette(mid);");
	}
	auto *clipRow = new QHBoxLayout();
	clipNow_ = new QPushButton("Clip now", this);
	clipRow->addWidget(clipNow_);
	clipRow->addWidget(clip_, 1);
	v->addLayout(clipRow);
	connect(clipNow_, &QPushButton::clicked, this, [this]() { e_->clipNow("manual", {"manual"}, "dock"); });
	v->addWidget(app_);
	last_ = new QLabel(this);
	last_->setWordWrap(true);
	last_->setStyleSheet("color: palette(mid);");
	v->addWidget(last_);
	v->addStretch(1);

	connect(e_, &Engine::stateChanged, this, &Dock::refresh);
	connect(e_, &Engine::frameUpdated, this, [this]() {
		Match m = e_->lastGame();
		meter_->setValue(m.score < 0 ? 0 : (int)(m.score * 1000));
		if (e_->revivingRecent())
			state_->setText(QString::fromStdString(e_->stateText()) +
					QString(" (%1%)").arg((int)(std::max(0.0, e_->reviveProgress()) * 100)));
	});
	connect(e_, &Engine::logged, this, [this](const QString &s) { last_->setText(s); });
	refresh();
}

void Dock::refresh()
{
	filling_ = true;
	active_->clear();
	for (auto &f : e_->cfg.friends)
		active_->addItem(QString::fromStdString(f.name));
	if (!e_->cfg.friends.empty())
		active_->setCurrentIndex(std::clamp(e_->cfg.activeFriend, 0, (int)e_->cfg.friends.size() - 1));
	filling_ = false;
	state_->setText(QString::fromStdString(e_->stateText()));
	state_->setStyleSheet(e_->applied() ? "color: #ce6050;" : "");
	pause_->setText(e_->cfg.enabled ? "Pause" : "Resume");
	app_->setText(e_->appConnected()
			      ? "ClipHound: " + (e_->appStatus().isEmpty() ? QString("connected") : e_->appStatus())
		      : e_->cfg.bridgeEnabled
			      ? QString("ClipHound: not connected (ws://127.0.0.1:%1)").arg(e_->cfg.bridgePort)
			      : "ClipHound: bridge off");
	QString lp = e_->clips.lastPath();
	clip_->setText(lp.isEmpty() ? "no clips yet" : "last: " + QFileInfo(lp).fileName());
	show_->setEnabled(!e_->applied());
	back_->setEnabled(e_->applied());
}

void Dock::openWizard()
{
	auto *w = new SetupWizard(e_, (QWidget *)obs_frontend_get_main_window());
	w->setAttribute(Qt::WA_DeleteOnClose);
	w->show();
}

void Dock::openSettings()
{
	auto *dlg = new SettingsDialog(e_, (QWidget *)obs_frontend_get_main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}
