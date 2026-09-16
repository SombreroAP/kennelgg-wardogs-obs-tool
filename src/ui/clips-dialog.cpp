#include "ui/clips-dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <obs-frontend-api.h>

static QString tagsText(const QStringList &t)
{
	return t.join(", ");
}

static QStringList tagsFrom(const QString &s)
{
	QStringList out;
	for (const QString &p : s.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts)) {
		QString g = p.trimmed();
		if (!g.isEmpty())
			out << g;
	}
	return out;
}

ClipsDialog::ClipsDialog(Engine *e, QWidget *parent) : QDialog(parent), e_(e)
{
	setWindowTitle("Kennel.gg Wardogs - clips");
	setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
	setAttribute(Qt::WA_DeleteOnClose);
	resize(900, 560);
	auto *v = new QVBoxLayout(this);
	auto *intro =
		new QLabel("Every clip with a title and tags. Change either and press Apply: the file is renamed to "
			   "the title, and both go into the clip's .json for Kennel Cut and the compilation.",
			   this);
	intro->setWordWrap(true);
	v->addWidget(intro);
	table_ = new QTableWidget(this);
	table_->setColumnCount(4);
	table_->setHorizontalHeaderLabels({"When", "Title", "Tags", "File"});
	table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->verticalHeader()->setVisible(false);
	v->addWidget(table_, 1);
	auto *f = new QFormLayout();
	title_ = new QLineEdit(this);
	title_->setPlaceholderText("a few words: what happened");
	tags_ = new QLineEdit(this);
	tags_->setPlaceholderText("comma separated: highlight, funny, fail, ace, ...");
	spoken_ = new QLabel(this);
	spoken_->setWordWrap(true);
	spoken_->setStyleSheet("color:#7c8076");
	file_ = new QLabel(this);
	file_->setWordWrap(true);
	file_->setStyleSheet("color:#7c8076");
	f->addRow("Title", title_);
	f->addRow("Tags", tags_);
	f->addRow("Said", spoken_);
	f->addRow("File", file_);
	v->addLayout(f);
	auto *row = new QHBoxLayout();
	applyBtn_ = new QPushButton("Apply", this);
	applyBtn_->setDefault(true);
	openBtn_ = new QPushButton("Show in folder", this);
	playBtn_ = new QPushButton("Play", this);
	auto *refresh = new QPushButton("Refresh", this);
	auto *close = new QPushButton("Close", this);
	row->addWidget(applyBtn_);
	row->addWidget(playBtn_);
	row->addWidget(openBtn_);
	row->addWidget(refresh);
	row->addStretch(1);
	row->addWidget(close);
	v->addLayout(row);
	connect(table_, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) { pick(r); });
	connect(applyBtn_, &QPushButton::clicked, this, &ClipsDialog::apply);
	connect(title_, &QLineEdit::returnPressed, this, &ClipsDialog::apply);
	connect(tags_, &QLineEdit::returnPressed, this, &ClipsDialog::apply);
	connect(playBtn_, &QPushButton::clicked, this, [this]() {
		if (!current_.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(current_));
	});
	connect(openBtn_, &QPushButton::clicked, this, [this]() {
		if (!current_.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(current_).absolutePath()));
	});
	connect(refresh, &QPushButton::clicked, this, &ClipsDialog::reload);
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	reload();
}

void ClipsDialog::reload()
{
	rows_ = e_->clips.allClips();
	table_->setRowCount((int)rows_.size());
	for (int i = 0; i < (int)rows_.size(); i++) {
		const auto &e = rows_[(size_t)i];
		table_->setItem(i, 0, new QTableWidgetItem(e.when.toString("ddd HH:mm:ss")));
		table_->setItem(i, 1, new QTableWidgetItem(e.title));
		table_->setItem(i, 2, new QTableWidgetItem(tagsText(e.tags)));
		table_->setItem(i, 3, new QTableWidgetItem(QFileInfo(e.path).fileName()));
	}
	table_->resizeColumnToContents(0);
	int keep = -1;
	for (int i = 0; i < (int)rows_.size(); i++)
		if (rows_[(size_t)i].path == current_)
			keep = i;
	if (keep < 0 && !rows_.empty())
		keep = 0;
	if (keep >= 0) {
		table_->setCurrentCell(keep, 1);
		pick(keep);
	}
}

void ClipsDialog::pick(int row)
{
	if (row < 0 || row >= (int)rows_.size()) {
		current_.clear();
		return;
	}
	const auto &e = rows_[(size_t)row];
	current_ = e.path;
	title_->setText(e.title);
	tags_->setText(tagsText(e.tags));
	spoken_->setText(e.info.value("spoken").toString());
	file_->setText(e.path);
}

void ClipsDialog::focusClip(const QString &path)
{
	for (int i = 0; i < (int)rows_.size(); i++)
		if (rows_[(size_t)i].path == path) {
			table_->setCurrentCell(i, 1);
			pick(i);
			break;
		}
	title_->setFocus();
	title_->selectAll();
}

void ClipsDialog::apply()
{
	if (current_.isEmpty())
		return;
	QStringList tags = tagsFrom(tags_->text());
	QString to = e_->clips.relabel(current_, title_->text(), QString(), &tags);
	if (to.isEmpty()) {
		QMessageBox::warning(this, "Kennel.gg Wardogs",
				     "Could not rename that clip. Is it open in a player, or already gone?");
		return;
	}
	current_ = to;
	reload();
	emit e_->stateChanged();
}

// ----- the small one -----

ClipNoteDialog::ClipNoteDialog(Engine *e, const QString &path, QWidget *parent) : QDialog(parent), e_(e), path_(path)
{
	setWindowTitle("Kennel.gg Wardogs - clip saved");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
	auto *v = new QVBoxLayout(this);
	auto *l = new QLabel("Saved: " + QFileInfo(path).fileName() + "\nWhat was it? (Enter to keep going)", this);
	l->setWordWrap(true);
	v->addWidget(l);
	auto *f = new QFormLayout();
	title_ = new QLineEdit(this);
	title_->setPlaceholderText("a few words");
	tags_ = new QLineEdit(this);
	tags_->setPlaceholderText("highlight, funny, fail, ...");
	f->addRow("Title", title_);
	f->addRow("Tags", tags_);
	v->addLayout(f);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	v->addWidget(bb);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(bb, &QDialogButtonBox::accepted, this, [this]() {
		QStringList tags = tagsFrom(tags_->text());
		if (!tags.contains("manual"))
			tags.prepend("manual");
		QString to = e_->clips.relabel(path_, title_->text(), QString(),
					       tags_->text().trimmed().isEmpty() ? nullptr : &tags);
		if (to.isEmpty() && !title_->text().trimmed().isEmpty())
			e_->log("Could not rename " + QFileInfo(path_).fileName() + " - is it open somewhere?");
		emit e_->stateChanged();
		accept();
	});
	title_->setFocus();
	resize(420, sizeHint().height());
}
