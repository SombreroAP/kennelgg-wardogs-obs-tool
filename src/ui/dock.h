#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QToolButton>
#include <QPointer>
#include <QDialog>
#include "engine.h"

/// The small always-visible panel: state, active squad mate, the two buttons, settings.
class Dock : public QWidget {
	Q_OBJECT
public:
	explicit Dock(Engine *engine, QWidget *parent = nullptr);
public slots:
	void refresh();
	void openSettings();
	void openWizard();
	void openLogs();

private:
	Engine *e_;
	QLabel *state_, *last_, *app_, *clip_;
	QListWidget *events_;
	QLabel *detector_;
	QLabel *near_ = nullptr;
	QComboBox *active_;
	QPushButton *show_, *back_, *pause_, *clipNow_, *appBtn_;
	QToolButton *saveBtn_;
	bool filling_ = false;
	QPointer<QDialog> settings_;
	QPointer<QWidget> wizard_;
};
