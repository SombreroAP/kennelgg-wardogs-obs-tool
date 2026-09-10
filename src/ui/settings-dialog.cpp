#include "ui/settings-dialog.h"
#include <QJsonArray>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonObject>
#include <QStandardPaths>
#include <QScrollBar>
#include <QFile>
#include <obs-frontend-api.h>
#include <QFileInfo>
#include <obs-module.h>
#include <plugin-support.h>

// ============================================================ FramePreview

FramePreview::FramePreview(QWidget *parent) : QLabel(parent)
{
	setMinimumHeight(240);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setCursor(Qt::CrossCursor);
}

void FramePreview::setFrame(const QImage &img, const Match &m, double threshold, QRectF box)
{
	img_ = img;
	m_ = m;
	thr_ = threshold;
	box_ = box;
	update();
}

void FramePreview::setBox2(QRectF box)
{
	box2_ = box;
	update();
}

void FramePreview::setPicker(const QString &caption)
{
	picker_ = true;
	caption_ = caption;
	update();
}

QRect FramePreview::imageRect() const
{
	if (img_.isNull())
		return rect();
	double s = std::min((double)width() / img_.width(), (double)height() / img_.height());
	int w = (int)(img_.width() * s), h = (int)(img_.height() * s);
	return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void FramePreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(11, 14, 16));
	if (img_.isNull()) {
		p.setPen(QColor(139, 144, 150));
		p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
			   "No frame yet. Set the game source on the Switch tab and keep this window open.");
		return;
	}
	QRect ir = imageRect();
	p.drawImage(ir, img_);
	auto draw = [&](QRectF f, QColor c, double w, bool dashed) {
		QRectF r(ir.x() + f.x() * ir.width(), ir.y() + f.y() * ir.height(), f.width() * ir.width(),
			 f.height() * ir.height());
		QPen pen(c, w);
		if (dashed)
			pen.setStyle(Qt::DashLine);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
	};
	bool match = m_.score >= thr_;
	if (!picker_ && m_.w > 0)
		draw(QRectF(m_.x, m_.y, m_.w, m_.h), match ? QColor(206, 96, 80) : QColor(139, 144, 150),
		     match ? 3 : 1.5, false);
	draw(box_, QColor(201, 154, 59), picker_ ? 2 : 1, true);
	if (box2_.width() > 0)
		draw(box2_, QColor(96, 176, 206), 2, true);
	if (dragging_ && drag_.width() > 0)
		draw(drag_, QColor(232, 229, 221), 1, true);
	p.setPen(QColor(232, 229, 221));
	QString txt =
		picker_    ? caption_
		: match    ? QString("Damage log header found (%1) - downed").arg(m_.score, 0, 'f', 3)
		: m_.w > 0 ? QString("Best candidate %1 is under the threshold - not downed").arg(m_.score, 0, 'f', 3)
			   : "Looking for the damage log header on the right of the game";
	p.fillRect(QRect(ir.x(), ir.bottom() - 20, ir.width(), 20), QColor(11, 14, 16, 170));
	p.drawText(QRect(ir.x(), ir.bottom() - 20, ir.width(), 20), Qt::AlignCenter, txt);
}

void FramePreview::mousePressEvent(QMouseEvent *ev)
{
	if (img_.isNull() || ev->button() != Qt::LeftButton)
		return;
	dragging_ = true;
	start_ = ev->pos();
	drag_ = QRectF();
}

void FramePreview::mouseMoveEvent(QMouseEvent *ev)
{
	if (!dragging_)
		return;
	QRect ir = imageRect();
	auto frac = [&](QPoint p) {
		return QPointF(std::clamp((p.x() - ir.x()) / (double)ir.width(), 0.0, 1.0),
			       std::clamp((p.y() - ir.y()) / (double)ir.height(), 0.0, 1.0));
	};
	drag_ = QRectF(frac(start_), frac(ev->pos())).normalized();
	update();
}

void FramePreview::mouseReleaseEvent(QMouseEvent *)
{
	if (!dragging_)
		return;
	dragging_ = false;
	if (drag_.width() > 0.01 && drag_.height() > 0.005) {
		box_ = drag_;
		emit boxChanged(box_);
	}
	drag_ = QRectF();
	update();
}

// ============================================================ friend dialog

namespace {
class FriendDialog : public QDialog {
public:
	Friend result;
	FriendDialog(const Friend *existing, const std::vector<std::pair<std::string, std::string>> &sources,
		     QWidget *parent, int defaultKbps)
		: QDialog(parent)
	{
		if (existing)
			result = *existing;
		else
			result.vdoKbps = defaultKbps;
		setWindowTitle(existing ? "Edit squad mate" : "Add a squad mate");
		form_ = new QFormLayout(this);
		name_ = new QLineEdit(QString::fromStdString(result.name), this);
		name_->setPlaceholderText("shown on the POV tag");
		form_->addRow("Name", name_);
		gameName_ = new QLineEdit(QString::fromStdString(result.gameName), this);
		gameName_->setPlaceholderText("same as the name above unless it differs in game");
		form_->addRow("In-game name", gameName_);
		kind_ = new QComboBox(this);
		kind_->addItems({"Twitch stream (~2 s, nothing for them to set up)",
				 "VDO.Ninja / WebRTC (~0.3 s, they open one link)", "OBS source I already have",
				 "Discord Go Live (~0.5-1 s, they Go Live in the call)", "NDI on the LAN (DistroAV)"});
		form_->addRow("Comes in as", kind_);

		// Twitch
		twitch_ = new QLineEdit(this);
		twitch_->setPlaceholderText("channel name, e.g. sombrero");
		form_->addRow("Twitch channel", twitch_);
		// VDO.Ninja
		streamId_ = new QLineEdit(this);
		streamId_->setPlaceholderText("any word you both agree on, e.g. pup-pov");
		form_->addRow("Stream ID", streamId_);
		auto *q = new QHBoxLayout();
		res_ = new QComboBox(this);
		res_->addItems({"720p", "1080p", "1440p"});
		fps_ = new QComboBox(this);
		fps_->addItems({"30 fps", "60 fps"});
		kbps_ = new QSpinBox(this);
		kbps_->setRange(1000, 40000);
		kbps_->setSingleStep(1000);
		kbps_->setSuffix(" kbps max");
		codec_ = new QComboBox(this);
		codec_->addItems({"h264", "vp9", "av1"});
		q->addWidget(res_);
		q->addWidget(fps_);
		q->addWidget(kbps_);
		q->addWidget(codec_);
		qualityRow_ = new QWidget(this);
		qualityRow_->setLayout(q);
		form_->addRow("Quality", qualityRow_);
		auto *linkRow = new QHBoxLayout();
		link_ = new QLineEdit(this);
		link_->setReadOnly(true);
		auto *copy = new QPushButton("Copy", this);
		linkRow->addWidget(link_, 1);
		linkRow->addWidget(copy);
		linkWidget_ = new QWidget(this);
		linkWidget_->setLayout(linkRow);
		form_->addRow("Friend's link", linkWidget_);
		// OBS source
		source_ = new QComboBox(this);
		source_->setEditable(true);
		for (auto &s : sources)
			if (s.first != Config::webSourceName() && s.first != Config::overlaySourceName())
				source_->addItem(QString::fromStdString(s.first));
		source_->setCurrentText(QString::fromStdString(result.source));
		form_->addRow("OBS source", source_);
		// Discord / NDI picker
		pick_ = new QComboBox(this);
		auto *pickRow = new QHBoxLayout();
		pickRow->addWidget(pick_, 1);
		auto *rescan = new QPushButton("Rescan", this);
		pickRow->addWidget(rescan);
		pickWidget_ = new QWidget(this);
		pickWidget_->setLayout(pickRow);
		pickLbl_ = new QLabel("Discord window", this);
		form_->addRow(pickLbl_, pickWidget_);

		hint_ = new QLabel(this);
		hint_->setWordWrap(true);
		form_->addRow(hint_);
		err_ = new QLabel(this);
		err_->setWordWrap(true);
		err_->setStyleSheet("color: #ce6050;");
		form_->addRow(err_);
		auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
		form_->addRow(bb);
		connect(bb, &QDialogButtonBox::accepted, this, [this]() { save(); });
		connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
		connect(copy, &QPushButton::clicked, this,
			[this]() { QApplication::clipboard()->setText(link_->text()); });
		connect(rescan, &QPushButton::clicked, this, [this]() { fillPick(); });
		connect(kind_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refresh(); });
		for (auto *le : {twitch_, streamId_})
			connect(le, &QLineEdit::textChanged, this, [this](const QString &) { updateLink(); });
		for (auto *cb : {res_, fps_, codec_})
			connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this](int) { updateLink(); });
		connect(kbps_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { updateLink(); });

		// load
		if (result.kind == FriendKind::Twitch)
			twitch_->setText(QString::fromStdString(result.channel));
		if (result.kind == FriendKind::VdoNinja)
			streamId_->setText(QString::fromStdString(result.channel));
		res_->setCurrentIndex(result.vdoHeight >= 1440 ? 2 : result.vdoHeight >= 1080 ? 1 : 0);
		fps_->setCurrentIndex(result.vdoFps >= 60 ? 1 : 0);
		kbps_->setValue(result.vdoKbps > 0 ? result.vdoKbps : defaultKbps);
		codec_->setCurrentText(QString::fromStdString(result.vdoCodec.empty() ? "h264" : result.vdoCodec));
		kind_->setCurrentIndex((int)result.kind);
		refresh();
		resize(640, 420);
	}

private:
	QFormLayout *form_;
	QLineEdit *name_, *gameName_, *twitch_, *streamId_, *link_;
	QComboBox *kind_, *source_, *pick_, *res_, *fps_, *codec_;
	QSpinBox *kbps_;
	QWidget *qualityRow_, *linkWidget_, *pickWidget_;
	QLabel *pickLbl_, *hint_, *err_;
	FriendKind kind() const { return (FriendKind)kind_->currentIndex(); }
	Friend draft() const
	{
		Friend f = result;
		f.name = name_->text().trimmed().toStdString();
		f.gameName = gameName_->text().trimmed().toStdString();
		f.kind = kind();
		f.vdoHeight = res_->currentIndex() == 2 ? 1440 : res_->currentIndex() == 1 ? 1080 : 720;
		f.vdoFps = fps_->currentIndex() == 1 ? 60 : 30;
		f.vdoKbps = kbps_->value();
		f.vdoCodec = codec_->currentText().toStdString();
		if (f.kind == FriendKind::Twitch)
			f.channel = twitch_->text().trimmed().toLower().remove('@').toStdString();
		else if (f.kind == FriendKind::VdoNinja)
			f.channel = streamId_->text().trimmed().toStdString();
		else if (f.kind == FriendKind::ObsSource)
			f.source = source_->currentText().trimmed().toStdString();
		else
			f.channel = pick_->currentData().toString().toStdString();
		return f;
	}
	void fillPick()
	{
		pick_->clear();
		FriendKind k = kind();
		if (k == FriendKind::Discord) {
			int firstDiscord = -1;
			for (auto &w : Switcher::listProperty("window_capture", "window")) {
				QString v = QString::fromStdString(w.second), n = QString::fromStdString(w.first);
				bool discord = v.contains("discord", Qt::CaseInsensitive) ||
					       n.contains("discord", Qt::CaseInsensitive);
				if (discord && firstDiscord < 0)
					firstDiscord = pick_->count();
				pick_->addItem((discord ? "Discord: " : "") + n, v);
			}
			// always offer the by-executable match: works before the pop-out exists and follows it when it appears
			pick_->insertItem(0, "Any Discord window (matched by Discord.exe, recommended)",
					  "Discord:Chrome_WidgetWin_1:Discord.exe");
			pick_->setCurrentIndex(firstDiscord >= 0 ? firstDiscord + 1 : 0);
		} else if (k == FriendKind::Ndi) {
			for (auto &n : Switcher::listProperty("ndi_source", "ndi_source_name"))
				pick_->addItem(QString::fromStdString(n.first), QString::fromStdString(n.second));
			if (pick_->count() == 0)
				pick_->addItem(Switcher::kindAvailable("ndi_source")
						       ? "(no NDI sources on the network yet)"
						       : "(DistroAV is not installed)",
					       "");
		}
		if (!result.channel.empty()) {
			int i = pick_->findData(QString::fromStdString(result.channel));
			if (i >= 0)
				pick_->setCurrentIndex(i);
		}
	}
	void refresh()
	{
		FriendKind k = kind();
		form_->setRowVisible(twitch_, k == FriendKind::Twitch);
		form_->setRowVisible(streamId_, k == FriendKind::VdoNinja);
		form_->setRowVisible(qualityRow_, k == FriendKind::VdoNinja);
		form_->setRowVisible(linkWidget_, k == FriendKind::VdoNinja);
		form_->setRowVisible(source_, k == FriendKind::ObsSource);
		form_->setRowVisible(pickWidget_, k == FriendKind::Discord || k == FriendKind::Ndi);
		pickLbl_->setText(k == FriendKind::Ndi ? "NDI source" : "Discord window");
		if (k == FriendKind::Discord || k == FriendKind::Ndi)
			fillPick();
		err_->clear();
		switch (k) {
		case FriendKind::Twitch:
			hint_->setText(
				"A browser source named \"Kennel web\" plays this channel with its audio routed through OBS. They just need to be live; ask them to keep Twitch low-latency mode on. Their stream includes their mic.");
			break;
		case FriendKind::VdoNinja:
			hint_->setText(
				"Send them the link: they open it in Chrome or Edge, pick their game window or screen and tick \"Share system audio\". No mic is sent. Quality here is a ceiling; WebRTC settles lower by itself on a weak link. 1080p60 at 12000 is right for LAN or fibre, 4000-6000 for a weak upload.");
			break;
		case FriendKind::Discord:
			hint_->setText(
				"They press Go Live in the call. Open their stream in Discord and pop it out into its own window. \"Any Discord window\" follows the pop-out automatically; pick a specific window only if you have several. On Save a Window Capture and an Application Audio Capture of Discord are created in your scene. 720p without Nitro.");
			break;
		case FriendKind::Ndi:
			hint_->setText(
				"Lowest latency, on the LAN or over a VPN such as Tailscale. They run OBS with DistroAV's NDI output or NDI Screen Capture; pick their NDI source and it is created in your scene on Save.");
			break;
		default:
			hint_->setText(
				"Any source already in OBS: a capture card, a second PC, an NDI Source you set up yourself. Its audio comes with it.");
		}
		updateLink();
	}
	void updateLink()
	{
		Friend f = draft();
		link_->setText(f.kind == FriendKind::VdoNinja && !f.channel.empty()
				       ? QString::fromStdString(Switcher::vdoPushUrl(f))
				       : QString());
	}
	void save()
	{
		Friend f = draft();
		if (f.kind == FriendKind::ObsSource && f.source.empty()) {
			err_->setText("Pick or type the OBS source name.");
			return;
		}
		if (f.kind == FriendKind::Twitch && f.channel.empty()) {
			err_->setText("Type the Twitch channel name.");
			return;
		}
		if (f.kind == FriendKind::VdoNinja && f.channel.empty()) {
			err_->setText("Type a stream ID (any word you both agree on).");
			return;
		}
		if (f.kind == FriendKind::Ndi && f.channel.empty()) {
			err_->setText(
				"No NDI source picked. They need DistroAV's NDI output or NDI Screen Capture running on the same network.");
			return;
		}
		if (f.kind == FriendKind::Discord && f.channel.empty())
			f.channel = "Discord:Chrome_WidgetWin_1:Discord.exe";
		if (f.name.empty())
			f.name = f.kind == FriendKind::ObsSource ? f.source
				 : f.kind == FriendKind::Discord ? "Discord"
								 : f.channel;
		result = f;
		accept();
	}
};
} // namespace

// ============================================================ SettingsDialog

static const char *kLiveScene = "(the scene that is live)";

SettingsDialog::SettingsDialog(Engine *engine, QWidget *parent) : QDialog(parent), e_(engine)
{
	setWindowTitle("Kennel.gg WARDOGS OBS Tools");
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setSizeGripEnabled(true);
	setMinimumSize(640, 480);
	resize(900, 720);
	auto *v = new QVBoxLayout(this);
	auto *tabs = new QTabWidget(this);
	tabs->addTab(buildSwitchTab(), "Switch");
	tabs->addTab(buildLookTab(), "Look");
	tabs->addTab(buildDetectTab(), "Detect");
	tabs->addTab(buildDualTab(), "Dual POV");
	tabs->addTab(buildClipsTab(), "Clips");
	tabs->addTab(buildAppTab(), "ClipHound");
	tabs->addTab(buildLogsTab(), "Logs");
	tabs->addTab(buildAboutTab(), "Help");
	building_ = false;
	v->addWidget(tabs, 1);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
	v->addWidget(bb);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);
	e_->wantPreview(true);
	connect(e_, &Engine::frameUpdated, this, [this]() {
		Match m = e_->lastGame();
		meter_->setValue(m.score < 0 ? 0 : (int)(m.score * 1000));
		frame_->setFrame(e_->lastFrame(), m, e_->cfg.threshold,
				 QRectF(e_->cfg.boxX, e_->cfg.boxY, e_->cfg.boxW, e_->cfg.boxH));
		frame_->setBox2(QRectF(e_->cfg.nearX, e_->cfg.nearY, e_->cfg.nearW, e_->cfg.nearH));
		if (feedPick_)
			feedPick_->setFrame(e_->lastFrame(), Match(), 1.0,
					    QRectF(e_->cfg.feedX, e_->cfg.feedY, e_->cfg.feedW, e_->cfg.feedH));
	});
	connect(e_, &Engine::stateChanged, this, [this]() { updateAreas(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		tplLbl_->setText(!e_->hasTemplate()     ? "No template"
				 : e_->customTemplate() ? "Custom template"
							: "Built-in template");
	});
}

SettingsDialog::~SettingsDialog()
{
	e_->wantPreview(false);
	if (previewing_)
		e_->previewLook(false);
}

static QLabel *muted(const QString &t, QWidget *p)
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

QWidget *SettingsDialog::buildSwitchTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);

	auto *g1 = new QGroupBox("Your side", w);
	auto *f1 = new QFormLayout(g1);
	game_ = new QComboBox(g1);
	scene_ = new QComboBox(g1);
	auto *refresh = new QPushButton("Refresh", g1);
	auto *mkGame = new QPushButton("Create Game Capture", g1);
	auto *gr = new QHBoxLayout();
	gr->addWidget(game_, 1);
	gr->addWidget(refresh);
	gr->addWidget(mkGame);
	connect(mkGame, &QPushButton::clicked, this, [this]() {
		std::string e = e_->sw.createGameCapture(e_->cfg);
		if (!e.empty()) {
			QMessageBox::warning(this, "Kennel WARDOGS", QString::fromStdString(e));
			return;
		}
		e_->cfg.save();
		fillSources();
		e_->reloadConfig();
	});
	f1->addRow("Your game source", gr);
	f1->addRow("Scene", scene_);
	f1->addRow(muted(
		"The game source is watched for the damage log (rendered on its own, so it can stay under the friend). Squad mates are shown on top of it in this scene. Browser sources for Twitch / VDO.Ninja and the look overlay are created here when first needed.",
		g1));
	v->addWidget(g1);
	connect(refresh, &QPushButton::clicked, this, [this]() { fillSources(); });
	connect(game_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { saveAndApply(); });
	connect(scene_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { saveAndApply(); });

	auto *g2 = new QGroupBox("Squad mates", w);
	auto *h2 = new QHBoxLayout(g2);
	friends_ = new QTableWidget(0, 3, g2);
	friends_->setHorizontalHeaderLabels({"Name", "Comes in as", "Source / channel"});
	friends_->horizontalHeader()->setStretchLastSection(true);
	friends_->setSelectionBehavior(QAbstractItemView::SelectRows);
	friends_->setSelectionMode(QAbstractItemView::SingleSelection);
	friends_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	friends_->verticalHeader()->hide();
	h2->addWidget(friends_, 1);
	auto *fb = new QVBoxLayout();
	auto *add = new QPushButton("Add...", g2);
	auto *edit = new QPushButton("Edit...", g2);
	auto *rem = new QPushButton("Remove", g2);
	auto *act = new QPushButton("Make active", g2);
	for (auto *b : {add, edit, rem, act})
		fb->addWidget(b);
	fb->addStretch(1);
	h2->addLayout(fb);
	v->addWidget(g2, 1);
	connect(add, &QPushButton::clicked, this, [this]() { editFriend(-1); });
	connect(edit, &QPushButton::clicked, this, [this]() { editFriend(friends_->currentRow()); });
	connect(friends_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { editFriend(r); });
	connect(rem, &QPushButton::clicked, this, [this]() {
		int r = friends_->currentRow();
		if (r < 0 || r >= (int)e_->cfg.friends.size())
			return;
		e_->cfg.friends.erase(e_->cfg.friends.begin() + r);
		if (e_->cfg.activeFriend >= (int)e_->cfg.friends.size())
			e_->cfg.activeFriend = std::max(0, (int)e_->cfg.friends.size() - 1);
		e_->cfg.save();
		fillFriends();
		emit e_->stateChanged();
	});
	connect(act, &QPushButton::clicked, this, [this]() {
		int r = friends_->currentRow();
		if (r >= 0) {
			e_->setActive(r);
			fillFriends();
		}
	});

	auto *gc = new QGroupBox("Show whoever is closest", w);
	auto *fc = new QFormLayout(gc);
	nearOn_ = new QCheckBox(
		"When you go down, show the squad mate the game says is nearest (needs ClipHound running)", gc);
	nearOn_->setChecked(e_->cfg.nearEnabled);
	fc->addRow(nearOn_);
	nearFollow_ = new QCheckBox("Keep following the nearest one while you are down", gc);
	nearFollow_->setChecked(e_->cfg.nearFollow);
	fc->addRow(nearFollow_);
	nearMax_ = new QSpinBox(gc);
	nearMax_->setRange(0, 500);
	nearMax_->setSuffix(" m or closer");
	nearMax_->setSpecialValueText("any distance");
	nearMax_->setValue(e_->cfg.nearMaxM);
	fc->addRow("Swap over only for someone", nearMax_);
	connect(nearMax_, &QSpinBox::editingFinished, this, [this]() { saveAndApply(); });
	auto *cdRow = new QHBoxLayout();
	nearCooldown_ = new QSlider(Qt::Horizontal, gc);
	nearCooldown_->setRange(1, 10);
	nearCooldown_->setValue(std::clamp(e_->cfg.nearCooldownS, 1, 10));
	nearCooldown_->setTickPosition(QSlider::TicksBelow);
	nearCooldown_->setTickInterval(1);
	nearCdLbl_ = new QLabel(gc);
	cdRow->addWidget(nearCooldown_, 1);
	cdRow->addWidget(nearCdLbl_);
	fc->addRow("Wait between swaps", cdRow);
	auto showCd = [this]() {
		int v = nearCooldown_->value();
		nearCdLbl_->setText(QString("%1 s").arg(v) + (v <= 2   ? "  (follows them as they move)"
							      : v >= 8 ? "  (settles on one feed and stays)"
								       : ""));
	};
	showCd();
	connect(nearCooldown_, &QSlider::valueChanged, this, [showCd](int) { showCd(); });
	connect(nearCooldown_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	connect(nearCooldown_, &QSlider::actionTriggered, this,
		[this](int a) { // keyboard and click-on-groove changes never send sliderReleased
			if (a != QAbstractSlider::SliderMove)
				QTimer::singleShot(0, this, [this]() { saveAndApply(); });
		});
	fc->addRow(muted(
		"How long the feed stays on one squad mate before it may swap to a closer one, while you are down and they are running to you. 4 s is the default: low values follow whoever is nearest as they move, high values pick one and leave it. The swap the moment you go down never waits.",
		gc));
	nearLbl_ = new QLabel(e_->nearbyStatus(), gc);
	nearLbl_->setWordWrap(true);
	fc->addRow("Nearby now", nearLbl_);
	fc->addRow(muted(
		"The moment you go down, ClipHound reads the NEARBY list in the bottom-right corner of your game and tells the plugin who is how far away, so the POV you cut to is the squad mate who can actually revive you. Nothing is read while you are up, so it costs nothing between fights. While this is on, the squad mate box in the dock follows the closest one by itself; untick it (here or in the dock) to choose the squad mate yourself. It needs ClipHound running, the blue NEARBY box set on the Detect tab, and each squad mate's in-game name filled in (Edit... → In-game name). Without a reading, the squad mate picked in the dock is used as before.",
		gc));
	v->addWidget(gc);
	connect(nearOn_, &QCheckBox::toggled, this, [this](bool on) {
		if (on && !e_->appConnected()) {
			auto r = QMessageBox::question(
				this, "Kennel WARDOGS",
				"Closest needs ClipHound running: it reads the NEARBY list in the corner of your game. It is not running, so Closest stays off.\n\nStart it now? Tick this again once the dock says ClipHound is connected.",
				QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
			nearOn_->blockSignals(true);
			nearOn_->setChecked(false);
			nearOn_->blockSignals(false);
			if (r == QMessageBox::Yes)
				e_->launchApp();
			return;
		}
		saveAndApply();
	});
	connect(nearFollow_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (nearLbl_)
			nearLbl_->setText(e_->nearbyStatus());
		if (nearOn_ && nearOn_->isChecked() != e_->cfg.nearEnabled) {
			nearOn_->blockSignals(true);
			nearOn_->setChecked(e_->cfg.nearEnabled); // ticked from the dock
			nearOn_->blockSignals(false);
		}
	});

	auto *gl = new QGroupBox("Squad on this network", w);
	auto *fl = new QFormLayout(gl);
	playerName_ = new QLineEdit(QString::fromStdString(e_->cfg.playerName), gl);
	playerName_->setPlaceholderText(Lan::hostName());
	fl->addRow("Your name", playerName_);
	auto *lr = new QHBoxLayout();
	lanOn_ = new QCheckBox("Find squad mates on the LAN", gl);
	ndiShare_ = new QCheckBox("Share my game feed over NDI (no mic)", gl);
	autoAdd_ = new QCheckBox("Add them automatically", gl);
	for (auto *c : {lanOn_, ndiShare_, autoAdd_})
		lr->addWidget(c);
	lr->addStretch(1);
	fl->addRow(lr);
	peers_ = new QListWidget(gl);
	peers_->setMaximumHeight(90);
	fl->addRow("Seen", peers_);
	lanStatus_ = muted("", gl);
	fl->addRow(lanStatus_);
	v->addWidget(gl);
	lanOn_->setChecked(e_->cfg.lanEnabled);
	ndiShare_->setChecked(e_->cfg.ndiShare);
	autoAdd_->setChecked(e_->cfg.autoAddPeers);
	for (auto *c : {lanOn_, ndiShare_, autoAdd_})
		connect(c, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(playerName_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	auto fillPeers = [this]() {
		peers_->clear();
		for (auto &kv : e_->lan.peers())
			peers_->addItem(
				kv.second.name + "  ·  " + kv.second.host +
				(kv.second.ndi.isEmpty() ? "  (not sharing NDI)" : "  ·  NDI " + kv.second.ndi));
		if (peers_->count() == 0)
			peers_->addItem(
				e_->cfg.lanEnabled
					? "(no squad mates found yet - they need this plugin running in OBS on the same network)"
					: "(off)");
		bool ndi = Switcher::kindAvailable("ndi_source") && Switcher::outputKindAvailable("ndi_output");
		lanStatus_->setText(
			ndi ? "Everyone on the LAN with this plugin and DistroAV sees each other; their feed appears in the list above and as a squad mate, no typing. Your own feed is sent on audio track 6 with microphones removed from it."
			    : "DistroAV (obs-ndi) is not installed, so NDI sharing is off. Install it from distroav.org on every PC that should share or receive a feed.");
		fillFriends();
	};
	fillPeers();
	connect(&e_->lan, &Lan::peersChanged, this, fillPeers);

	auto *g3 = new QGroupBox("Sound while a squad mate is on screen", w);
	auto *v3 = new QVBoxLayout(g3);
	friendAudio_ = new QCheckBox("Play the squad mate's game sound (their feed is silent otherwise)", g3);
	friendAudio_->setChecked(e_->cfg.friendAudio);
	v3->addWidget(friendAudio_);
	v3->addWidget(muted(
		"Off by default: you go on hearing your own game while your stream shows their POV. Turn it on to hear theirs instead, and tick your own audio below so the two do not play at once.",
		g3));
	auto *h3 = new QHBoxLayout();
	mute_ = new QListWidget(g3);
	h3->addWidget(mute_, 1);
	h3->addWidget(
		muted("Tick anything of YOURS to mute while a squad mate is on screen - usually Desktop Audio, or the game / capture-card source if that carries the sound. Nothing is ticked by default. Do NOT tick your microphone; it keeps going either way. Ticked inputs are put back exactly as they were when you are revived.",
		      g3),
		1);
	v3->addLayout(h3, 1);
	v->addWidget(g3, 1);
	connect(mute_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(friendAudio_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });

	auto *g4 = new QGroupBox("Extras", w);
	auto *v4 = new QVBoxLayout(g4);
	bringFront_ = new QCheckBox("Move the friend source to the top of the scene when shown", g4);
	keepWarm_ = new QCheckBox(
		"Keep the friend feed warm: leave the source on but invisible and muted, so NDI / the player never reconnects (instant switch)",
		g4);
	preload_ = new QCheckBox(
		"Keep every squad mate's feed loaded and playing, hidden and silent, so there is no black screen while it starts (uses their bandwidth for each one)",
		g4);
	preload_->setChecked(e_->cfg.preloadFeeds);
	v4->addWidget(bringFront_);
	v4->addWidget(preload_);
	connect(preload_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	v4->addWidget(keepWarm_);
	v->addWidget(g4);
	connect(bringFront_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(keepWarm_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });

	bringFront_->setChecked(e_->cfg.bringToFront);
	keepWarm_->setChecked(e_->cfg.keepWarm);
	fillSources();
	fillFriends();
	return w;
}

QWidget *SettingsDialog::buildLookTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g = new QGroupBox("Ham it up", w);
	auto *f = new QFormLayout(g);
	lookName_ = new QCheckBox("Name tag  (\"POV · PUP\" bottom-left, Kennel colours)", g);
	auto *nameRow = new QHBoxLayout();
	lookLabel_ = new QLineEdit(QString::fromStdString(e_->cfg.lookLabel), g);
	lookLabel_->setMaximumWidth(120);
	lookPlate_ = new QCheckBox("dark plate", g);
	nameRow->addWidget(new QLabel("prefix", g));
	nameRow->addWidget(lookLabel_);
	nameRow->addWidget(lookPlate_);
	nameRow->addStretch(1);
	f->addRow(lookName_);
	f->addRow("", nameRow);
	lookCam_ = new QCheckBox("Camcorder frame  (viewfinder corners, blinking REC, running timer, battery)", g);
	f->addRow(lookCam_);
	lookGrain_ = new QCheckBox("Film grain", g);
	grain_ = new QSlider(Qt::Horizontal, g);
	grain_->setRange(5, 100);
	grain_->setValue(e_->cfg.grainAmount);
	f->addRow(lookGrain_, grain_);
	lookVig_ = new QCheckBox("Vignette  (darkened edges)", g);
	f->addRow(lookVig_);
	preview_ = new QPushButton("Preview look in OBS", g);
	f->addRow(preview_);
	f->addRow(muted(
		"Drawn by a browser source named \"Kennel look\" that the plugin adds to your scene and shows on top of the friend while you are downed. Nothing touches the friend's feed itself, so it is the same for Twitch, VDO.Ninja and NDI.",
		g));
	v->addWidget(g);
	v->addStretch(1);
	lookName_->setChecked(e_->cfg.lookName);
	lookPlate_->setChecked(e_->cfg.lookPlate);
	lookCam_->setChecked(e_->cfg.lookCam);
	lookGrain_->setChecked(e_->cfg.lookGrain);
	lookVig_->setChecked(e_->cfg.lookVignette);
	auto relook = [this]() {
		saveAndApply();
		if (previewing_ || e_->applied())
			e_->previewLook(true);
	};
	for (auto *c : {lookName_, lookPlate_, lookCam_, lookGrain_, lookVig_})
		connect(c, &QCheckBox::toggled, this, [relook](bool) { relook(); });
	connect(lookLabel_, &QLineEdit::editingFinished, this, relook);
	connect(grain_, &QSlider::sliderReleased, this, relook);
	connect(preview_, &QPushButton::clicked, this, [this]() {
		previewing_ = !previewing_;
		e_->previewLook(previewing_);
		preview_->setText(previewing_ ? "Hide preview" : "Preview look in OBS");
	});
	return w;
}

QWidget *SettingsDialog::buildDetectTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	frame_ = new FramePreview(w);
	v->addWidget(frame_, 1);
	meter_ = new QProgressBar(w);
	meter_->setRange(0, 1000);
	meter_->setFormat("match %v / 1000");
	v->addWidget(meter_);
	connect(frame_, &FramePreview::boxChanged, this, [this](QRectF r) {
		Config &c = e_->cfg;
		if (pickNear_ && pickNear_->isChecked()) {
			c.nearX = r.x();
			c.nearY = r.y();
			c.nearW = r.width();
			c.nearH = r.height();
			c.save();
			e_->pushAppConfig();
		} else {
			c.boxX = r.x();
			c.boxY = r.y();
			c.boxW = r.width();
			c.boxH = r.height();
			c.save();
		}
		updateAreas();
	});

	auto *nr = new QHBoxLayout();
	pickTpl_ = new QRadioButton("the header box (dotted amber)", w);
	pickNear_ = new QRadioButton("the NEARBY list (blue)", w);
	pickTpl_->setChecked(true);
	auto *nearReset = new QPushButton("Reset NEARBY box", w);
	auto *nearTest = new QPushButton("Test read", w);
	nearTest->setToolTip("Read the NEARBY area once, right now, and show what ClipHound sees there.");
	nr->addWidget(new QLabel("Dragging on the picture sets:", w));
	nr->addWidget(pickTpl_);
	nr->addWidget(pickNear_);
	nr->addWidget(nearReset);
	nr->addWidget(nearTest);
	nr->addStretch(1);
	connect(nearTest, &QPushButton::clicked, this, [this]() { showNearbyTest(); });
	v->addLayout(nr);
	nearLbl2_ = muted("", w);
	v->addWidget(nearLbl2_);
	connect(pickTpl_, &QRadioButton::toggled, this, [this](bool) { updateAreas(); });
	connect(nearReset, &QPushButton::clicked, this, [this]() {
		Config &c = e_->cfg;
		c.nearX = 0.80;
		c.nearY = 0.79;
		c.nearW = 0.19;
		c.nearH = 0.14;
		c.save();
		e_->pushAppConfig();
		updateAreas();
	});
	v->addWidget(muted(
		"The blue box is the NEARBY list in the bottom-right corner of your game: the squad mates next to you and how far away they are. It is what \"Show whoever is closest\" on the Switch tab reads, through ClipHound, from the moment you go down until you are back up. Drag round it with a little margin, including room above for a full squad, then press Test read to see what ClipHound makes of it.",
		w));

	auto *row = new QHBoxLayout();
	auto *cap = new QPushButton("Capture my own header template from the box (while downed)", w);
	auto *builtin = new QPushButton("Use built-in template", w);
	tplLbl_ = new QLabel(w);
	row->addWidget(cap);
	row->addWidget(builtin);
	row->addWidget(tplLbl_);
	row->addStretch(1);
	v->addLayout(row);
	connect(cap, &QPushButton::clicked, this, [this]() { e_->captureTemplate(); });
	connect(builtin, &QPushButton::clicked, this, [this]() { e_->useBuiltInTemplate(); });
	tplLbl_->setText(!e_->hasTemplate()     ? "No template"
			 : e_->customTemplate() ? "Custom template"
						: "Built-in template");
	v->addWidget(muted(
		"WARDOGS shows the damage log (\"B  VIEW DAMAGE LOG\" and the body silhouette) the whole time you are downed, map open or not, and hides it when you are revived. the plugin looks for that header anywhere on the right of your game source, at any HUD size, with a template cut from a real frame. Nothing to set up: get downed once and watch the bar go red (~0.9). Only if it never locks on: drag the dotted box tightly around the header while downed and press Capture.",
		w));

	auto *g = new QGroupBox("Tuning", w);
	auto *f = new QFormLayout(g);
	auto *thrRow = new QHBoxLayout();
	thr_ = new QSlider(Qt::Horizontal, g);
	thr_->setRange(50, 99);
	thr_->setValue((int)std::lround(e_->cfg.threshold * 100));
	thrLbl_ = new QLabel(QString::number(e_->cfg.threshold, 'f', 2), g);
	thrRow->addWidget(thr_, 1);
	thrRow->addWidget(thrLbl_);
	thrRow->addWidget(muted("real header ~0.9+, anything else under ~0.75", g));
	f->addRow("Match threshold", thrRow);
	auto *frRow = new QHBoxLayout();
	downFrames_ = new QSpinBox(g);
	downFrames_->setRange(1, 30);
	downFrames_->setValue(e_->cfg.downFrames);
	upFrames_ = new QSpinBox(g);
	upFrames_->setRange(1, 60);
	upFrames_->setValue(e_->cfg.upFrames);
	frRow->addWidget(downFrames_);
	frRow->addWidget(upFrames_);
	frRow->addWidget(muted("polls to confirm down / up (10 per second; 2 / 1 = 0.2 s down, 0.1 s up)", g));
	frRow->addStretch(1);
	f->addRow("Confirm frames", frRow);
	auto *dlRow = new QHBoxLayout();
	downDelay_ = new QSpinBox(g);
	downDelay_->setRange(0, 15000);
	downDelay_->setSingleStep(250);
	downDelay_->setSuffix(" ms");
	downDelay_->setValue(e_->cfg.downDelayMs);
	upDelay_ = new QSpinBox(g);
	upDelay_->setRange(0, 15000);
	upDelay_->setSingleStep(250);
	upDelay_->setSuffix(" ms");
	upDelay_->setValue(e_->cfg.upDelayMs);
	dlRow->addWidget(new QLabel("show squad mate after", g));
	dlRow->addWidget(downDelay_);
	dlRow->addWidget(new QLabel("   back to me after", g));
	dlRow->addWidget(upDelay_);
	dlRow->addWidget(muted("(0 = instant). A quick revive inside the first delay never switches at all.", g));
	dlRow->addStretch(1);
	f->addRow("Switch delays", dlRow);
	connect(downDelay_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(upDelay_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	auto *mdRow = new QHBoxLayout();
	minDown_ = new QSpinBox(g);
	minDown_->setRange(0, 20000);
	minDown_->setSingleStep(250);
	minDown_->setValue(e_->cfg.minDownMs);
	pollMs_ = new QSpinBox(g);
	pollMs_->setRange(100, 1000);
	pollMs_->setSingleStep(50);
	pollMs_->setValue(e_->cfg.pollMs);
	mdRow->addWidget(minDown_);
	mdRow->addWidget(muted("ms minimum on the friend (stops flicker during the revive animation)", g));
	mdRow->addWidget(pollMs_);
	mdRow->addWidget(muted("ms between polls", g));
	mdRow->addStretch(1);
	f->addRow("Timing", mdRow);
	auto_ = new QCheckBox("Switch automatically when the damage log is detected", g);
	auto_->setChecked(e_->cfg.autoDetect);
	f->addRow(auto_);
	revive_ = new QCheckBox(
		"Watch the friend's feed for \"REVIVING\": when they are on you, switch back the instant the damage log goes (no confirm delay, 10 polls / s)",
		g);
	revive_->setChecked(e_->cfg.watchRevive);
	f->addRow(revive_);
	auto *rvRow = new QHBoxLayout();
	reviveThr_ = new QSlider(Qt::Horizontal, g);
	reviveThr_->setRange(50, 99);
	reviveThr_->setValue((int)std::lround(e_->cfg.reviveThreshold * 100));
	reviveLbl_ = new QLabel(QString::number(e_->cfg.reviveThreshold, 'f', 2), g);
	rvRow->addWidget(reviveThr_, 1);
	rvRow->addWidget(reviveLbl_);
	f->addRow("Revive match threshold", rvRow);
	f->addRow(muted(
		"Hotkeys live in OBS Settings → Hotkeys: \"Kennel WARDOGS: show friend's POV / back to me\", \"...capture damage-log template\" and \"...save a clip now\". On a two-PC setup send them from the gaming PC with KeyBridge.",
		g));
	v->addWidget(g);

	connect(thr_, &QSlider::valueChanged, this,
		[this](int val) { thrLbl_->setText(QString::number(val / 100.0, 'f', 2)); });
	connect(thr_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	connect(reviveThr_, &QSlider::valueChanged, this,
		[this](int val) { reviveLbl_->setText(QString::number(val / 100.0, 'f', 2)); });
	connect(reviveThr_, &QSlider::sliderReleased, this, [this]() { saveAndApply(); });
	for (auto *s : {downFrames_, upFrames_, minDown_, pollMs_})
		connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	connect(auto_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(revive_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	return w;
}

QWidget *SettingsDialog::buildClipsTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g1 = new QGroupBox("Replay clips", w);
	auto *f1 = new QFormLayout(g1);
	useReplay_ = new QCheckBox("Save OBS's replay buffer on a clip", g1);
	useReplay_->setChecked(e_->cfg.clipUseReplay);
	f1->addRow(useReplay_);
	autoReplay_ = new QCheckBox("Start OBS's replay buffer automatically (clips need it running)", g1);
	autoReplay_->setChecked(e_->cfg.autoStartReplay);
	f1->addRow(autoReplay_);
	nameTpl_ = new QLineEdit(QString::fromStdString(e_->cfg.clipNameTemplate), g1);
	f1->addRow("File name", nameTpl_);
	f1->addRow(muted(
		"Placeholders: {title} {tags} {date} {time} {source}. Default puts what happened first, e.g. Double kill (2 players)_multikill_2026-09-09_07-36-14.mkv. Every clip is also logged to clips.csv.",
		g1));
	clipDowned_ =
		new QCheckBox("Also save a clip whenever you get downed (the moment before is in the buffer)", g1);
	clipDowned_->setChecked(e_->cfg.clipOnDowned);
	f1->addRow(clipDowned_);
	f1->addRow(muted(
		"Hotkey \"Kennel WARDOGS: save a clip now\" and the dock's Clip now button save one by hand. Replay length is OBS's Settings → Output → Replay Buffer.",
		g1));
	v->addWidget(g1);

	auto *gh = new QGroupBox("Also fire OBS hotkeys on a clip (Aitum Backtrack, anything else)", w);
	auto *vh = new QVBoxLayout(gh);
	hotkeyFilter_ = new QLineEdit(gh);
	hotkeyFilter_->setPlaceholderText("filter, e.g. backtrack");
	vh->addWidget(hotkeyFilter_);
	hotkeyList_ = new QListWidget(gh);
	hotkeyList_->setMaximumHeight(140);
	vh->addWidget(hotkeyList_);
	auto *bfRow = new QHBoxLayout();
	backtrackFolder_ = new QLineEdit(QString::fromStdString(e_->cfg.backtrackFolder), gh);
	backtrackFolder_->setPlaceholderText(
		"Backtrack's output folder (found automatically from its sources when blank)");
	auto *bfBrowse = new QPushButton("Browse...", gh);
	bfRow->addWidget(new QLabel("Name their files too:", gh));
	bfRow->addWidget(backtrackFolder_, 1);
	bfRow->addWidget(bfBrowse);
	vh->addLayout(bfRow);
	connect(bfBrowse, &QPushButton::clicked, this, [this]() {
		QString d =
			QFileDialog::getExistingDirectory(this, "Backtrack output folder", backtrackFolder_->text());
		if (!d.isEmpty()) {
			backtrackFolder_->setText(d);
			saveAndApply();
		}
	});
	connect(backtrackFolder_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	vh->addWidget(muted(
		"Tick the hotkeys OBS should press for you on every clip: with Aitum Backtrack that is its \"Save\" hotkey for the source you want (Backtrack names its own files, so the file-name template above does not apply to those). Untick the replay buffer above to clip with Backtrack alone.",
		gh));
	v->addWidget(gh);
	connect(hotkeyFilter_, &QLineEdit::textChanged, this, [this](const QString &) { fillHotkeys(); });
	connect(hotkeyList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { saveAndApply(); });
	connect(useReplay_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	fillHotkeys();

	auto *g2 = new QGroupBox("Companion app (ClipHound)", w);
	auto *f2 = new QFormLayout(g2);
	auto *br = new QHBoxLayout();
	bridgeOn_ = new QCheckBox("Bridge on, port", g2);
	bridgeOn_->setChecked(e_->cfg.bridgeEnabled);
	bridgePort_ = new QSpinBox(g2);
	bridgePort_->setRange(1024, 65535);
	bridgePort_->setValue(e_->cfg.bridgePort);
	br->addWidget(bridgeOn_);
	br->addWidget(bridgePort_);
	br->addStretch(1);
	f2->addRow(br);
	auto *ap = new QHBoxLayout();
	appPath_ = new QLineEdit(QString::fromStdString(e_->cfg.appPath), g2);
	appPath_->setPlaceholderText("C:\\...\\ClipHound\\ClipHound.bat");
	auto *browse = new QPushButton("Browse...", g2);
	auto *launchNow = new QPushButton("Start now", g2);
	ap->addWidget(appPath_, 1);
	ap->addWidget(browse);
	ap->addWidget(launchNow);
	f2->addRow("App", ap);
	launchApp_ = new QCheckBox("Start it when OBS starts", g2);
	launchApp_->setChecked(e_->cfg.launchApp);
	f2->addRow(launchApp_);
	closeApp_ = new QCheckBox("Close it when OBS closes", g2);
	closeApp_->setChecked(e_->cfg.closeAppWithObs);
	f2->addRow(closeApp_);
	connect(closeApp_, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	f2->addRow(muted(
		"The app reads the kill feed (OCR) and asks the plugin for clips over ws://127.0.0.1:<port>. The plugin sends it native-resolution crops of the game source and POV events; the app sends clip requests with tags. Downed detection stays in the plugin.",
		g2));
	v->addWidget(g2);
	connect(browse, &QPushButton::clicked, this, [this]() {
		QString p = QFileDialog::getOpenFileName(this, "Companion app", appPath_->text(),
							 "Programs (*.exe *.bat *.cmd);;All files (*)");
		if (!p.isEmpty()) {
			appPath_->setText(p);
			saveAndApply();
		}
	});
	connect(launchNow, &QPushButton::clicked, this, [this]() {
		saveAndApply();
		e_->launchApp();
	});

	auto *g3 = new QGroupBox("Recent clips", w);
	auto *v3 = new QVBoxLayout(g3);
	clipList_ = new QListWidget(g3);
	v3->addWidget(clipList_);
	v->addWidget(g3, 1);
	auto fillClips = [this]() {
		clipList_->clear();
		auto &h = e_->clips.history();
		for (auto it = h.rbegin(); it != h.rend(); ++it)
			clipList_->addItem(it->when.toString("HH:mm:ss") + "  " + it->title + "  [" +
					   it->tags.join(", ") + "]  " + QFileInfo(it->path).fileName());
	};
	fillClips();
	connect(e_, &Engine::stateChanged, this, fillClips);

	for (auto *c : {autoReplay_, clipDowned_, bridgeOn_, launchApp_})
		connect(c, &QCheckBox::toggled, this, [this](bool) { saveAndApply(); });
	connect(nameTpl_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(appPath_, &QLineEdit::editingFinished, this, [this]() { saveAndApply(); });
	connect(bridgePort_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { saveAndApply(); });
	return w;
}

QWidget *SettingsDialog::buildAppTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *g = new QGroupBox("ClipHound - kill-feed clips", w);
	auto *f = new QFormLayout(g);
	appName_ = new QLineEdit(QString::fromStdString(e_->cfg.appPlayerName), g);
	appName_->setPlaceholderText("exactly as it appears in the kill feed");
	f->addRow("Your in-game name", appName_);
	auto *libRow = new QHBoxLayout();
	appLibrary_ = new QLineEdit(QString::fromStdString(e_->cfg.appLibrary), g);
	appLibrary_->setPlaceholderText("blank = no index; otherwise index.csv and Resolve metadata are kept here");
	auto *libBrowse = new QPushButton("Browse...", g);
	libRow->addWidget(appLibrary_, 1);
	libRow->addWidget(libBrowse);
	f->addRow("Clip library index", libRow);
	f->addRow(muted(
		"Where the clip files themselves go is the Clips tab's clip folder. The library is an optional index of what happened in each clip.",
		g));
	appEveryKill_ = new QCheckBox(
		"Clip every kill I get (otherwise only notable ones: 120 m+, headshots, vehicles, explosives, multi-kills)",
		g);
	appEveryKill_->setChecked(e_->cfg.appEveryKill);
	f->addRow(appEveryKill_);
	appMulti_ = new QDoubleSpinBox(g);
	appMulti_->setRange(2, 120);
	appMulti_->setDecimals(0);
	appMulti_->setSuffix(" s");
	appMulti_->setValue(e_->cfg.appMultikillWindow > 0 ? e_->cfg.appMultikillWindow : 30);
	f->addRow("Multi-kill window", appMulti_);
	f->addRow(muted(
		"Kills within this many seconds of each other count as one multi-kill (double, triple...). Default 30 s.",
		g));
	v->addWidget(g);

	auto *ga = new QGroupBox("Kill-feed area", w);
	auto *fa = new QVBoxLayout(ga);
	feedPick_ = new FramePreview(ga);
	feedPick_->setMinimumHeight(200);
	feedPick_->setPicker("Drag a box on the picture to set the area");
	fa->addWidget(feedPick_, 1);
	auto *ar = new QHBoxLayout();
	auto *reset = new QPushButton("Reset the kill-feed area", ga);
	ar->addWidget(reset);
	ar->addStretch(1);
	fa->addLayout(ar);
	auto *rr = new QHBoxLayout();
	appFps_ = new QSpinBox(ga);
	appFps_->setRange(3, 15);
	appFps_->setSuffix(" times a second");
	appFps_->setValue(e_->cfg.appFps > 0 ? e_->cfg.appFps : 10);
	rr->addWidget(new QLabel("Read the feed", ga));
	rr->addWidget(appFps_);
	rr->addStretch(1);
	fa->addLayout(rr);
	areaLbl_ = muted("", ga);
	fa->addWidget(areaLbl_);
	fa->addWidget(muted(
		"Drag a box round the kill feed - the list of kills on the left, about half way down - with a bit of margin. The game must be running so you can see where it is. Reading faster gets the clip sooner; a kill is decided about three quarters of a second after it appears whatever the rate, so 10 a second is plenty and 5 costs half the CPU. (The NEARBY box that drives the POV switch is on the Detect tab.)",
		ga));
	v->addWidget(ga, 1);
	auto setArea = [this](QRectF r) {
		Config &c = e_->cfg;
		c.feedX = r.x();
		c.feedY = r.y();
		c.feedW = r.width();
		c.feedH = r.height();
		c.save();
		e_->pushAppConfig();
		updateAreas();
	};
	connect(feedPick_, &FramePreview::boxChanged, this, setArea);
	connect(reset, &QPushButton::clicked, this, [setArea]() { setArea(QRectF(0.0, 0.42, 0.24, 0.16)); });
	connect(appFps_, &QSpinBox::editingFinished, this, [this]() {
		e_->cfg.appFps = appFps_->value();
		e_->cfg.save();
		e_->pushAppConfig();
	});
	updateAreas();

	auto *gt = new QGroupBox("Twitch clips", w);
	auto *ft = new QFormLayout(gt);
	appTwitch_ = new QCheckBox("Create a Twitch clip on notable kills", gt);
	appTwitch_->setChecked(e_->cfg.appTwitchEnabled);
	ft->addRow(appTwitch_);
	appBroadcaster_ = new QLineEdit(QString::fromStdString(e_->cfg.appBroadcaster), gt);
	appBroadcaster_->setPlaceholderText("the channel that is live, e.g. sombrero");
	ft->addRow("Channel to clip", appBroadcaster_);
	auto *tRow = new QHBoxLayout();
	twitchLbl_ = new QLabel(gt);
	twitchLbl_->setWordWrap(true);
	twitchLogin_ = new QPushButton("Log in with Twitch...", gt);
	twitchLogout_ = new QPushButton("Log out", gt);
	tRow->addWidget(twitchLbl_, 1);
	tRow->addWidget(twitchLogin_);
	tRow->addWidget(twitchLogout_);
	ft->addRow("Clipping account", tRow);
	ft->addRow(muted(
		"Log in as the account that should own the clips (a bot account such as InfoKennel works). A code appears and is copied; twitch.tv/activate opens, paste the code, done. Needs ClipHound running.",
		gt));
	v->addWidget(gt);
	v->addStretch(1);

	auto push = [this]() {
		Config &c = e_->cfg;
		c.appPlayerName = appName_->text().trimmed().toStdString();
		c.appLibrary = appLibrary_->text().trimmed().toStdString();
		c.appBroadcaster = appBroadcaster_->text().trimmed().toLower().remove('@').toStdString();
		c.appTwitchEnabled = appTwitch_->isChecked();
		c.appEveryKill = appEveryKill_->isChecked();
		c.appMultikillWindow = appMulti_->value();
		e_->pushAppConfig();
	};
	connect(appEveryKill_, &QCheckBox::toggled, this, [push](bool) { push(); });
	connect(appMulti_, &QDoubleSpinBox::editingFinished, this, push);
	connect(appName_, &QLineEdit::editingFinished, this, push);
	connect(appLibrary_, &QLineEdit::editingFinished, this, push);
	connect(appBroadcaster_, &QLineEdit::editingFinished, this, push);
	connect(appTwitch_, &QCheckBox::toggled, this, [push](bool) { push(); });
	connect(libBrowse, &QPushButton::clicked, this, [this, push]() {
		QString d = QFileDialog::getExistingDirectory(this, "Clip library folder", appLibrary_->text());
		if (!d.isEmpty()) {
			appLibrary_->setText(d);
			push();
		}
	});
	connect(twitchLogin_, &QPushButton::clicked, this, [this]() { e_->twitchLogin(); });
	connect(twitchLogout_, &QPushButton::clicked, this, [this]() { e_->twitchLogout(); });
	connect(e_, &Engine::appConfigReceived, this, [this]() {
		appName_->setText(QString::fromStdString(e_->cfg.appPlayerName));
		appLibrary_->setText(QString::fromStdString(e_->cfg.appLibrary));
		appBroadcaster_->setText(QString::fromStdString(e_->cfg.appBroadcaster));
		appTwitch_->blockSignals(true);
		appTwitch_->setChecked(e_->cfg.appTwitchEnabled);
		appTwitch_->blockSignals(false);
		appEveryKill_->blockSignals(true);
		appEveryKill_->setChecked(e_->cfg.appEveryKill);
		appEveryKill_->blockSignals(false);
		appMulti_->blockSignals(true);
		appMulti_->setValue(e_->cfg.appMultikillWindow > 0 ? e_->cfg.appMultikillWindow : 30);
		appMulti_->blockSignals(false);
	});
	connect(e_, &Engine::twitchStatusChanged, this, [this]() { refreshAppTab(); });
	connect(e_, &Engine::stateChanged, this, [this]() { refreshAppTab(); });
	refreshAppTab();
	return w;
}

/// What ClipHound sees in the NEARBY box: our own crop of it, and the text it read there.
void SettingsDialog::showNearbyTest()
{
	auto *d = new QDialog(this);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle("NEARBY test read");
	d->setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	auto *v = new QVBoxLayout(d);
	auto *pic = new QLabel(d);
	pic->setAlignment(Qt::AlignCenter);
	pic->setStyleSheet("background: #0b0e10;");
	pic->setMinimumHeight(160);
	QImage f = e_->lastFrame();
	const Config &c = e_->cfg;
	if (!f.isNull()) {
		QRect r((int)(c.nearX * f.width()), (int)(c.nearY * f.height()), (int)(c.nearW * f.width()),
			(int)(c.nearH * f.height()));
		r &= QRect(0, 0, f.width(), f.height());
		if (r.width() > 4 && r.height() > 4) {
			QImage crop = f.copy(r);
			pic->setPixmap(QPixmap::fromImage(
				crop.scaledToWidth(std::min(760, crop.width() * 4), Qt::FastTransformation)));
		}
	} else
		pic->setText("No frame from the game source yet.");
	v->addWidget(pic);
	v->addWidget(muted("The blue box, as the plugin sees it. If this is not the NEARBY list, drag the box again "
			   "on the Detect tab while the game is showing.",
			   d));
	auto *out = new QPlainTextEdit(d);
	out->setReadOnly(true);
	out->setMinimumHeight(150);
	out->setPlainText("Asking ClipHound to read it...");
	v->addWidget(out, 1);
	auto *row = new QHBoxLayout();
	auto *again = new QPushButton("Read again", d);
	auto *close = new QPushButton("Close", d);
	row->addStretch(1);
	row->addWidget(again);
	row->addWidget(close);
	v->addLayout(row);
	connect(close, &QPushButton::clicked, d, &QDialog::close);
	connect(again, &QPushButton::clicked, d, [this, out]() {
		out->setPlainText("Asking ClipHound to read it...");
		e_->nearbyTest();
	});
	connect(e_, &Engine::nearbyTested, d, [this, out](const QJsonObject &o) {
		if (o.contains("error")) {
			out->setPlainText(o.value("error").toString() +
					  ".\nStart it from the dock, then press Read again.");
			return;
		}
		auto list = [&o](const char *k) {
			QStringList v;
			for (auto x : o.value(k).toArray())
				v << x.toString();
			return v;
		};
		QStringList names = list("names"), texts = list("texts"), dists = list("dists");
		QStringList found;
		for (auto x : o.value("found").toArray())
			found << x.toObject().value("match").toString() + " " +
					 QString::number(x.toObject().value("dist").toInt()) + " m";
		QString t;
		t += QString("Rows found in the box: %1     distance chips found: %2\n")
			     .arg(o.value("rows").toInt())
			     .arg(o.value("chips").toInt());
		t += "Names read:   " + (texts.isEmpty() ? QString("(nothing)") : texts.join("  |  ")) + "\n";
		t += "Metres read:  " + (dists.isEmpty() ? QString("(nothing)") : dists.join("  |  ")) + "\n";
		t += "Looking for:  " + (names.isEmpty() ? QString("(no in-game names set)") : names.join(", ")) +
		     "\n\n";
		if (!found.isEmpty())
			t += "Matched: " + found.join(", ") + "\nThis is working.\n";
		else if (o.value("rows").toInt() == 0)
			t += "No rows of text were found in the box. It is probably not over the NEARBY list, or the list is empty right now (be in a match, with squad mates near you).\n";
		else if (texts.isEmpty())
			t += "Rows were found but no text came out of them. Try dragging the box a little wider, and make sure it is not covering the map or the score bar.\n";
		else
			t += "Text was read but it does not match any squad mate's in-game name. Set each squad mate's In-game name on the Switch tab to exactly what is shown above.\n";
		if (!o.value("saved").toString().isEmpty())
			t += "\nClipHound saved what it looked at: " + o.value("saved").toString();
		out->setPlainText(t);
	});
	d->resize(820, 560);
	d->show();
	e_->nearbyTest();
}

namespace {
struct DualPreset {
	const char *id, *label;
	double x, y, w;
};
// Measured on 1600x900 frames of the tank (driver and gunner), the Havoc pilot seat and the Havoc
// gunner's CAM view. Top-left sits between the team chat and the kill feed and covers no HUD; the
// CAM view is a framed picture, so the window goes inside the frame, clear of the compass.
const DualPreset kDualPresets[] = {
	{"tank-driver", "Tank - I drive, show my gunner", 0.012, 0.19, 0.26},
	{"tank-gunner", "Tank - I am the gunner, show my driver", 0.012, 0.19, 0.26},
	{"havoc-pilot", "Havoc - I fly, show my gunner", 0.012, 0.19, 0.26},
	{"havoc-gunner", "Havoc - I am the gunner (CAM view), show my pilot", 0.19, 0.075, 0.20},
	{"custom", "Custom - drag the box on the picture", 0, 0, 0},
};
} // namespace

QWidget *SettingsDialog::buildDualTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->addWidget(muted(
		"Two of you in a tank or a Havoc? Show your own POV as usual and put your crew mate's feed in a small window over it, placed where the game draws nothing. Pick who, pick the seat you are in, and the window goes where that seat's HUD leaves room. The window is picture only; the swap when you go down still takes the whole screen and the window steps aside for it. Hotkey: OBS Settings → Hotkeys → \"dual POV window on / off\".",
		w));
	auto *g = new QGroupBox("Dual POV", w);
	auto *f = new QFormLayout(g);
	dualOn_ = new QCheckBox("Show the dual POV window", g);
	f->addRow(dualOn_);
	dualFriend_ = new QComboBox(g);
	f->addRow("Crew mate to show", dualFriend_);
	dualPreset_ = new QComboBox(g);
	for (const auto &p : kDualPresets)
		dualPreset_->addItem(p.label, p.id);
	f->addRow("Vehicle and seat", dualPreset_);
	auto *pos = new QHBoxLayout();
	dualX_ = new QDoubleSpinBox(g);
	dualY_ = new QDoubleSpinBox(g);
	dualW_ = new QDoubleSpinBox(g);
	for (auto *sb : {dualX_, dualY_, dualW_}) {
		sb->setRange(0, 100);
		sb->setDecimals(1);
		sb->setSuffix(" %");
	}
	dualW_->setRange(8, 60);
	pos->addWidget(new QLabel("left", g));
	pos->addWidget(dualX_);
	pos->addWidget(new QLabel("top", g));
	pos->addWidget(dualY_);
	pos->addWidget(new QLabel("width", g));
	pos->addWidget(dualW_);
	pos->addWidget(muted("of the canvas; the height keeps 16:9", g));
	pos->addStretch(1);
	f->addRow("Window", pos);
	auto *op = new QHBoxLayout();
	dualOpacity_ = new QSlider(Qt::Horizontal, g);
	dualOpacity_->setRange(10, 100);
	auto *opLbl = new QLabel(g);
	op->addWidget(dualOpacity_, 1);
	op->addWidget(opLbl);
	f->addRow("Opacity", op);
	dualState_ = new QLabel(g);
	dualState_->setWordWrap(true);
	f->addRow("Now", dualState_);
	v->addWidget(g);
	dualPick_ = new FramePreview(w);
	dualPick_->setMinimumHeight(220);
	dualPick_->setPicker(
		"The dashed box is the dual POV window over your game. Drag on the picture to place it (switches to Custom).");
	v->addWidget(dualPick_, 1);
	v->addWidget(muted(
		"Their feed is whatever you set up for them on the Switch tab: Twitch, VDO.Ninja, Discord or NDI. A Twitch feed runs a couple of seconds behind you; VDO.Ninja or NDI is the one for a tight crew. If they are also the squad mate the POV swap uses, that keeps working.",
		w));

	dualToUi();
	connect(dualOn_, &QCheckBox::toggled, this, [this](bool on) {
		if (building_)
			return;
		dualFromUi(false);
		e_->setDual(on, "Dual POV tab");
	});
	connect(dualFriend_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (!building_)
			dualFromUi(false);
	});
	connect(dualPreset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (!building_)
			dualFromUi(true);
	});
	for (auto *sb : {dualX_, dualY_, dualW_})
		connect(sb, &QDoubleSpinBox::editingFinished, this, [this]() {
			dualPreset_->blockSignals(true);
			dualPreset_->setCurrentIndex(dualPreset_->findData("custom"));
			dualPreset_->blockSignals(false);
			dualFromUi(false);
		});
	connect(dualOpacity_, &QSlider::valueChanged, this, [opLbl](int v) { opLbl->setText(QString("%1 %").arg(v)); });
	connect(dualOpacity_, &QSlider::sliderReleased, this, [this]() { dualFromUi(false); });
	opLbl->setText(QString("%1 %").arg(dualOpacity_->value()));
	connect(dualPick_, &FramePreview::boxChanged, this, [this](QRectF r) {
		Config &c = e_->cfg;
		c.dualX = r.x();
		c.dualY = r.y();
		c.dualW = r.width();
		c.dualPreset = "custom";
		dualToUi();
		dualFromUi(false);
	});
	connect(e_, &Engine::frameUpdated, this, [this]() {
		if (!dualPick_)
			return;
		QImage img = e_->lastFrame();
		double h = img.isNull() ? e_->cfg.dualW * 9 / 16
					: e_->cfg.dualW * 9.0 / 16.0 * img.width() / img.height();
		dualPick_->setFrame(img, Match(), 1.0, QRectF(e_->cfg.dualX, e_->cfg.dualY, e_->cfg.dualW, h));
	});
	connect(e_, &Engine::stateChanged, this, [this]() {
		if (dualState_)
			dualState_->setText(
				e_->dualOn() ? "window is up" +
						       QString(e_->applied() ? " (stepped aside for the POV swap)" : "")
					     : "off");
		if (dualFriend_ && dualFriend_->count() != (int)e_->cfg.friends.size() + 1)
			dualToUi();
	});
	return w;
}

void SettingsDialog::dualToUi()
{
	const Config &c = e_->cfg;
	bool was = building_;
	building_ = true;
	dualOn_->setChecked(e_->dualOn() || c.dualEnabled);
	dualFriend_->clear();
	dualFriend_->addItem("(none)", -1);
	for (size_t i = 0; i < c.friends.size(); i++)
		dualFriend_->addItem(QString::fromStdString(c.friends[i].name), (int)i);
	dualFriend_->setCurrentIndex(std::max(0, dualFriend_->findData(c.dualFriend)));
	int pi = dualPreset_->findData(QString::fromStdString(c.dualPreset));
	dualPreset_->setCurrentIndex(pi < 0 ? 0 : pi);
	dualX_->setValue(c.dualX * 100);
	dualY_->setValue(c.dualY * 100);
	dualW_->setValue(c.dualW * 100);
	dualOpacity_->setValue(c.dualOpacity);
	dualState_->setText(e_->dualOn() ? "window is up" : "off");
	building_ = was;
}

void SettingsDialog::dualFromUi(bool preset)
{
	Config &c = e_->cfg;
	c.dualEnabled = dualOn_->isChecked();
	c.dualFriend = dualFriend_->currentData().toInt();
	c.dualPreset = dualPreset_->currentData().toString().toStdString();
	if (preset) {
		for (const auto &p : kDualPresets)
			if (c.dualPreset == p.id && p.w > 0) {
				c.dualX = p.x;
				c.dualY = p.y;
				c.dualW = p.w;
			}
		dualToUi();
	} else {
		c.dualX = dualX_->value() / 100;
		c.dualY = dualY_->value() / 100;
		c.dualW = dualW_->value() / 100;
	}
	c.dualOpacity = dualOpacity_->value();
	c.save();
	if (e_->dualOn())
		e_->setDual(true, "settings changed");
}

void SettingsDialog::updateAreas()
{
	const Config &c = e_->cfg;
	auto fmt = [](double x, double y, double w, double h) {
		return QString("x %1  y %2  w %3  h %4")
			.arg(x, 0, 'f', 2)
			.arg(y, 0, 'f', 2)
			.arg(w, 0, 'f', 2)
			.arg(h, 0, 'f', 2);
	};
	if (areaLbl_)
		areaLbl_->setText(
			"Kill feed: " + fmt(c.feedX, c.feedY, c.feedW, c.feedH) +
			(e_->appConnected() ? "     (sent to ClipHound)" : "     (ClipHound is not running)"));
	if (nearLbl2_)
		nearLbl2_->setText("NEARBY list: " + fmt(c.nearX, c.nearY, c.nearW, c.nearH) +
				   "     reading: " + e_->nearbyStatus());
}

void SettingsDialog::refreshAppTab()
{
	if (!twitchLbl_)
		return;
	QJsonObject t = e_->twitchStatus();
	QString st = t.value("state").toString();
	bool connected = e_->appConnected();
	if (!connected)
		twitchLbl_->setText("ClipHound is not running (Clips tab → Start now)");
	else if (st == "code")
		twitchLbl_->setText("Go to " + t.value("verification_uri").toString() + " and enter code  " +
				    t.value("user_code").toString());
	else if (st == "ok" && !t.value("login").toString().isEmpty())
		twitchLbl_->setText("Logged in as " + t.value("login").toString());
	else if (st == "error")
		twitchLbl_->setText("Login failed: " + t.value("error").toString());
	else if (t.contains("has_app_id") && !t.value("has_app_id").toBool())
		twitchLbl_->setText("Twitch login is not available in this build yet");
	else
		twitchLbl_->setText("Not logged in");
	twitchLogin_->setEnabled(connected && st != "code");
	twitchLogout_->setEnabled(connected && st == "ok" && !t.value("login").toString().isEmpty());
	if (st == "code") {
		static QString shown;
		QString code = t.value("user_code").toString();
		if (shown != code) {
			shown = code;
			QDesktopServices::openUrl(QUrl(t.value("verification_uri").toString()));
			QApplication::clipboard()->setText(code);
		}
	}
}

static QString tailFile(const QString &path, int lines)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	QStringList all = QString::fromUtf8(f.readAll()).split('\n');
	if (all.size() > lines)
		all = all.mid(all.size() - lines);
	return all.join('\n');
}

QWidget *SettingsDialog::buildLogsTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	logView_ = new QPlainTextEdit(w);
	logView_->setReadOnly(true);
	logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
	v->addWidget(logView_, 1);
	auto *row = new QHBoxLayout();
	auto *copy = new QPushButton("Copy all", w);
	auto *refresh = new QPushButton("Refresh", w);
	auto *obsLogs = new QPushButton("Open OBS log folder", w);
	auto *cfgFolder = new QPushButton("Open plugin config folder", w);
	for (auto *b : {copy, refresh, obsLogs, cfgFolder})
		row->addWidget(b);
	row->addStretch(1);
	v->addLayout(row);
	v->addWidget(muted(
		"Paste this to Sombrero when something misbehaves: it has the plugin's state, its recent log and the tail of ClipHound's log.",
		w));
	connect(copy, &QPushButton::clicked, this, [this, copy]() {
		QApplication::clipboard()->setText(logView_->toPlainText());
		copy->setText("Copied");
	});
	connect(refresh, &QPushButton::clicked, this, [this]() { refreshLogs(); });
	connect(obsLogs, &QPushButton::clicked, this, []() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).section('/', 0, -2) +
			"/obs-studio/logs"));
	});
	connect(cfgFolder, &QPushButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(Config::configDir()))); });
	connect(e_, &Engine::logged, this, [this](const QString &) { refreshLogs(); });
	refreshLogs();
	return w;
}

void SettingsDialog::refreshLogs()
{
	if (!logView_)
		return;
	QString appDir = e_->cfg.appPath.empty() ? QString("C:/ProgramData/Kennel WARDOGS/ClipHound")
						 : QFileInfo(QString::fromStdString(e_->cfg.appPath)).absolutePath();
	QString body = QString("=== Kennel WARDOGS plugin %1 ===\n").arg(PLUGIN_VERSION);
	body += QString("state: %1 | game source: %2 | squad mate: %3 | replay buffer: %4 | ClipHound: %5 | clip hotkeys: %6 | twitch: %7\n\n")
			.arg(QString::fromStdString(e_->stateText()), QString::fromStdString(e_->cfg.gameSource),
			     e_->cfg.active() ? QString::fromStdString(e_->cfg.active()->name) : "(none)",
			     obs_frontend_replay_buffer_active() ? "running" : "NOT running",
			     e_->appConnected() ? "connected" : "not connected",
			     QString::number(e_->cfg.clipHotkeys.size()),
			     e_->twitchStatus().value("state").toString() + " " +
				     e_->twitchStatus().value("login").toString());
	body += e_->recentLog().join('\n');
	body += "\n\n=== ClipHound (" + appDir + "/cliphound.log, last 200 lines) ===\n";
	QString ch = tailFile(appDir + "/cliphound.log", 200);
	body += ch.isEmpty() ? "(no log file - is ClipHound running from that folder?)" : ch;
	bool atEnd = logView_->verticalScrollBar()->value() >= logView_->verticalScrollBar()->maximum() - 4;
	logView_->setPlainText(body);
	if (atEnd)
		logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

QWidget *SettingsDialog::buildAboutTab()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	auto *l = new QLabel(w);
	l->setWordWrap(true);
	l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	l->setText(
		"<h3>Kennel.gg WARDOGS OBS Tools</h3>"
		"<p>Downed in WARDOGS? Your stream shows a squad mate's POV (video and game audio) until you are back up. Your mic is never touched.</p>"
		"<ol>"
		"<li><b>Switch tab:</b> pick your game source, add squad mates, tick the game-audio inputs to mute.</li>"
		"<li><b>Get downed once</b> and watch the Detect tab: the bar goes red when the damage log is found.</li>"
		"<li><b>Look tab:</b> name tag, camcorder frame, grain, vignette. Preview them in OBS.</li>"
		"<li>The <b>Kennel WARDOGS dock</b> (View → Docks) shows the state and has the manual buttons.</li>"
		"</ol>"
		"<p><b>Squad mate feeds.</b> Twitch: nothing for them to do, ~2 s behind with low-latency mode, includes their mic. "
		"VDO.Ninja: they open one link in Chrome/Edge and share their game window with system audio, ~0.3 s, no mic. "
		"NDI: OBS + DistroAV or NDI Screen Capture on the LAN, or over a VPN such as Tailscale. "
		"Discord Go Live (~0.5-1 s, 720p without Nitro): they Go Live in the call, you pop their stream out into its own window, add a Window Capture of it "
		"(Windows 10 method, keep it unminimised) plus an Application Audio Capture of Discord, and add the squad mate as an OBS source pointing at that capture.</p>"
		"<p><b>Timing the switch back.</b> While your friend is on screen, the plugin also watches their feed for the word REVIVING and the progress ring. "
		"When it sees it, the switch back fires the instant the damage log disappears from your own game, with no confirmation delay. "
		"Your own feed is the trigger because it has no latency; the friend's feed only arms it.</p>"
		"<p>Settings and templates: <code>" +
		QString::fromStdString(Config::configDir()) + "</code></p>");
	v->addWidget(l);
	v->addStretch(1);
	return w;
}

void SettingsDialog::fillHotkeys()
{
	if (!hotkeyList_)
		return;
	hotkeyList_->blockSignals(true);
	hotkeyList_->clear();
	QString flt = hotkeyFilter_ ? hotkeyFilter_->text().trimmed().toLower() : QString();
	auto all = Clips::allHotkeys();
	auto rank = [](const QPair<QString, QString> &h) {
		QString t = (h.first + " " + h.second).toLower();
		if (t.contains("backtrack") && t.contains("save"))
			return 0;
		if (t.contains("backtrack"))
			return 1;
		if (t.contains("replay") || t.contains("clip"))
			return 2;
		return 3;
	};
	std::stable_sort(all.begin(), all.end(),
			 [&](const QPair<QString, QString> &x, const QPair<QString, QString> &y) {
				 return rank(x) < rank(y);
			 });
	for (auto &hk : all) {
		if (hk.first.startsWith("kennel.") || hk.first.startsWith("OBSBasic.") ||
		    hk.first.startsWith("libobs."))
			continue;
		bool on = std::find(e_->cfg.clipHotkeys.begin(), e_->cfg.clipHotkeys.end(), hk.first.toStdString()) !=
			  e_->cfg.clipHotkeys.end();
		QString label = hk.second.isEmpty() ? hk.first : hk.second + "   ·  " + hk.first;
		if (!flt.isEmpty() && !label.toLower().contains(flt) && !on)
			continue;
		auto *it = new QListWidgetItem(label, hotkeyList_);
		it->setData(Qt::UserRole, hk.first);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	hotkeyList_->blockSignals(false);
}

void SettingsDialog::fillSources()
{
	auto inputs = Switcher::inputs();
	auto scenes = Switcher::sceneNames();
	game_->blockSignals(true);
	scene_->blockSignals(true);
	mute_->blockSignals(true);
	game_->clear();
	QString chosen = QString::fromStdString(e_->cfg.gameSource);
	if (!chosen.isEmpty())
		game_->addItem(chosen);
	for (auto &i : inputs)
		if (i.first != e_->cfg.gameSource && i.first != Config::webSourceName() &&
		    i.first != Config::overlaySourceName())
			game_->addItem(QString::fromStdString(i.first));
	game_->setCurrentIndex(chosen.isEmpty() ? -1 : 0);
	scene_->clear();
	scene_->addItem(kLiveScene);
	for (auto &s : scenes)
		scene_->addItem(QString::fromStdString(s));
	scene_->setCurrentText(e_->cfg.sceneName.empty() ? kLiveScene : QString::fromStdString(e_->cfg.sceneName));
	mute_->clear();
	for (auto &i : inputs) {
		if (i.first == Config::webSourceName() || i.first == Config::overlaySourceName())
			continue;
		obs_source_t *src = obs_get_source_by_name(i.first.c_str());
		if (!src)
			continue;
		bool audio = (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) != 0;
		obs_source_release(src);
		if (!audio)
			continue;
		QString label = QString::fromStdString(i.first);
		if (i.second.find("input_capture") != std::string::npos)
			label += "   ·  MICROPHONE - leave unticked";
		else if (i.second.find("output_capture") != std::string::npos)
			label += "   ·  desktop audio";
		auto *it = new QListWidgetItem(label, mute_);
		it->setData(Qt::UserRole, QString::fromStdString(i.first));
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		bool on = std::find(e_->cfg.muteWhileDowned.begin(), e_->cfg.muteWhileDowned.end(), i.first) !=
			  e_->cfg.muteWhileDowned.end();
		it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	game_->blockSignals(false);
	scene_->blockSignals(false);
	mute_->blockSignals(false);
}

void SettingsDialog::fillFriends()
{
	friends_->setRowCount(0);
	for (size_t i = 0; i < e_->cfg.friends.size(); i++) {
		auto &f = e_->cfg.friends[i];
		int r = friends_->rowCount();
		friends_->insertRow(r);
		QString name = QString::fromStdString(f.name) + ((int)i == e_->cfg.activeFriend ? "   ●" : "");
		const char *kind = f.kind == FriendKind::Twitch     ? "Twitch stream"
				   : f.kind == FriendKind::VdoNinja ? "VDO.Ninja (WebRTC)"
								    : "OBS source";
		friends_->setItem(r, 0, new QTableWidgetItem(name));
		friends_->setItem(r, 1, new QTableWidgetItem(kind));
		friends_->setItem(r, 2,
				  new QTableWidgetItem(QString::fromStdString(
					  f.kind == FriendKind::ObsSource || f.ownsSources() ? f.source : f.channel)));
	}
}

void SettingsDialog::editFriend(int row)
{
	Friend *existing = row >= 0 && row < (int)e_->cfg.friends.size() ? &e_->cfg.friends[row] : nullptr;
	if (row >= 0 && !existing)
		return;
	FriendDialog dlg(existing, Switcher::inputs(), this, e_->cfg.vdoBitrateKbps);
	if (dlg.exec() != QDialog::Accepted)
		return;
	Friend f = dlg.result;
	if (f.ownsSources()) {
		std::string e = e_->sw.createFriendSources(e_->cfg, f);
		if (!e.empty()) {
			QMessageBox::warning(this, "Kennel WARDOGS",
					     "Could not set up the sources: " + QString::fromStdString(e));
			return;
		}
	}
	if (existing)
		*existing = f;
	else {
		e_->cfg.friends.push_back(f);
		row = (int)e_->cfg.friends.size() - 1;
		if (e_->cfg.friends.size() == 1)
			e_->cfg.activeFriend = 0;
	}
	e_->cfg.save();
	fillFriends();
	friends_->selectRow(row);
	if (row == e_->cfg.activeFriend)
		e_->setActive(row);
	emit e_->stateChanged();
}

void SettingsDialog::collect()
{
	if (building_ || !game_ || !scene_ || !mute_ || !lookName_ || !thr_ || !autoReplay_ || !bridgeOn_ ||
	    !playerName_)
		return;
	Config &c = e_->cfg;
	c.gameSource = game_->currentText().toStdString();
	c.sceneName = scene_->currentText() == kLiveScene ? "" : scene_->currentText().toStdString();
	c.muteWhileDowned.clear();
	for (int i = 0; i < mute_->count(); i++)
		if (mute_->item(i)->checkState() == Qt::Checked)
			c.muteWhileDowned.push_back(mute_->item(i)->data(Qt::UserRole).toString().toStdString());
	c.bringToFront = bringFront_->isChecked();
	c.playerName = playerName_->text().trimmed().toStdString();
	c.lanEnabled = lanOn_->isChecked();
	c.ndiShare = ndiShare_->isChecked();
	c.autoAddPeers = autoAdd_->isChecked();
	c.keepWarm = keepWarm_->isChecked();
	c.preloadFeeds = preload_ ? preload_->isChecked() : c.preloadFeeds;
	c.friendAudio = friendAudio_ ? friendAudio_->isChecked() : c.friendAudio;
	c.lookName = lookName_->isChecked();
	c.lookPlate = lookPlate_->isChecked();
	c.lookCam = lookCam_->isChecked();
	c.lookGrain = lookGrain_->isChecked();
	c.lookVignette = lookVig_->isChecked();
	c.lookLabel = lookLabel_->text().trimmed().isEmpty() ? "POV" : lookLabel_->text().trimmed().toStdString();
	c.grainAmount = grain_->value();
	c.threshold = thr_->value() / 100.0;
	c.reviveThreshold = reviveThr_->value() / 100.0;
	c.downFrames = downFrames_->value();
	c.upFrames = upFrames_->value();
	c.minDownMs = minDown_->value();
	c.downDelayMs = downDelay_ ? downDelay_->value() : c.downDelayMs;
	c.upDelayMs = upDelay_ ? upDelay_->value() : c.upDelayMs;
	c.pollMs = pollMs_->value();
	c.autoDetect = auto_->isChecked();
	c.watchRevive = revive_->isChecked();
	c.autoStartReplay = autoReplay_->isChecked();
	c.clipUseReplay = useReplay_ ? useReplay_->isChecked() : true;
	c.backtrackFolder = backtrackFolder_ ? backtrackFolder_->text().trimmed().toStdString() : c.backtrackFolder;
	if (hotkeyList_) {
		// keep ticked hotkeys that are filtered out of view
		for (int i = 0; i < hotkeyList_->count(); i++) {
			auto *it = hotkeyList_->item(i);
			std::string n = it->data(Qt::UserRole).toString().toStdString();
			bool has = std::find(c.clipHotkeys.begin(), c.clipHotkeys.end(), n) != c.clipHotkeys.end();
			if (it->checkState() == Qt::Checked && !has)
				c.clipHotkeys.push_back(n);
			else if (it->checkState() != Qt::Checked && has)
				c.clipHotkeys.erase(std::remove(c.clipHotkeys.begin(), c.clipHotkeys.end(), n),
						    c.clipHotkeys.end());
		}
	}
	c.clipOnDowned = clipDowned_->isChecked();
	c.clipNameTemplate = nameTpl_->text().trimmed().isEmpty() ? "{title}_{tags}_{date}_{time}"
								  : nameTpl_->text().trimmed().toStdString();
	c.clipFolder = clipFolder_ ? clipFolder_->text().trimmed().toStdString() : c.clipFolder;
	c.bridgeEnabled = bridgeOn_->isChecked();
	c.bridgePort = bridgePort_->value();
	c.appPath = appPath_->text().trimmed().toStdString();
	c.launchApp = launchApp_->isChecked();
	c.closeAppWithObs = closeApp_ ? closeApp_->isChecked() : true;
	c.nearEnabled = nearOn_ ? nearOn_->isChecked() : c.nearEnabled;
	c.nearFollow = nearFollow_ ? nearFollow_->isChecked() : c.nearFollow;
	c.nearCooldownS = nearCooldown_ ? nearCooldown_->value() : c.nearCooldownS;
	c.nearMaxM = nearMax_ ? nearMax_->value() : c.nearMaxM;
	c.appFps = appFps_ ? appFps_->value() : c.appFps;
}

void SettingsDialog::saveAndApply()
{
	if (building_)
		return;
	collect();
	e_->cfg.save();
	e_->reloadConfig();
}
