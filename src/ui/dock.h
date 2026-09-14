#pragma once
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QToolButton>
#include <QCheckBox>
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
	void openSquad();

private:
	Engine *e_;
	QLabel *state_, *last_, *app_, *clip_;
	QListWidget *events_;
	QLabel *detector_;
	QLabel *near_ = nullptr;
	QLabel *update_ = nullptr;
	QLabel *locked_ = nullptr;
	QCheckBox *closest_ = nullptr;
	QPushButton *dual_ = nullptr;
	QComboBox *active_;
	QComboBox *dualPick_ = nullptr;
	QCheckBox *dualAuto_ = nullptr;
	QPushButton *show_, *back_, *clipNow_, *appBtn_;
	QCheckBox *autoSwitch_ = nullptr;
	QPushButton *showPop_ = nullptr;
	QPushButton *sound_ = nullptr;
	QHBoxLayout *liveRow_ = nullptr;
	QList<QPushButton *> liveButtons_;
	QStringList liveNames_;
	QStringList liveShown_;
	QList<int> liveIdx_;
	QToolButton *saveBtn_;
	bool filling_ = false;
	QPointer<QDialog> settings_;
	QPointer<QWidget> wizard_;
	QPointer<QDialog> squad_;
};
