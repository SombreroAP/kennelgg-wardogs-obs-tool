#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "engine.h"

/// This stream's clips (and the folder's, from their sidecars), each with a title and tags you
/// can change: the file is renamed to the title, the sidecar carries both.
class ClipsDialog : public QDialog {
	Q_OBJECT
public:
	explicit ClipsDialog(Engine *e, QWidget *parent = nullptr);
	/// Open on one clip, the title field focused: the "save clip with a note" flow.
	void focusClip(const QString &path);

private:
	void reload();
	void pick(int row);
	void apply();
	Engine *e_;
	QTableWidget *table_;
	QLineEdit *title_, *tags_;
	QLabel *spoken_, *file_;
	QPushButton *applyBtn_, *openBtn_, *playBtn_;
	std::vector<Clips::Entry> rows_;
	QString current_;
};

/// The small one: title and tags for the clip just saved. Non-modal, so the game goes on.
class ClipNoteDialog : public QDialog {
	Q_OBJECT
public:
	ClipNoteDialog(Engine *e, const QString &path, QWidget *parent = nullptr);

private:
	Engine *e_;
	QString path_;
	QLineEdit *title_, *tags_;
};
