#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include "engine.h"

/// The small always-visible panel: state, active squad mate, the two buttons, settings.
class Dock : public QWidget {
	Q_OBJECT
public:
	explicit Dock(Engine *engine, QWidget *parent = nullptr);
public slots:
	void refresh();
	void openSettings();

private:
	Engine *e_;
	QLabel *state_, *last_, *app_, *clip_;
	QProgressBar *meter_;
	QComboBox *active_;
	QPushButton *show_, *back_, *pause_, *clipNow_;
	bool filling_ = false;
};
