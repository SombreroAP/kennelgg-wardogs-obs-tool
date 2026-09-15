#pragma once
#include <QWizard>
#include <QWizardPage>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include "engine.h"

/// First-run setup: name → game source → squad mates → clips → go. Everything can be changed later in Settings.
class SetupWizard : public QWizard {
	Q_OBJECT
public:
	explicit SetupWizard(Engine *engine, QWidget *parent = nullptr);
	void accept() override;

private:
	Engine *e_;
	QLineEdit *name_;
	QComboBox *game_;
	QLabel *gameHint_;
	QListWidget *squad_;
	QLineEdit *twitch_;
	QCheckBox *launchApp_, *clipDowned_, *lookName_;
	QLineEdit *me_ = nullptr;
	QCheckBox *rosterOn_ = nullptr;
	QLabel *bot_ = nullptr;
	QLabel *summary_;
	void fillGame();
	void fillSquad();
	void addTwitch();
	QWizardPage *pageWelcome();
	QWizardPage *pageGame();
	QWizardPage *pageSquad();
	QWizardPage *pageClips();
	QWizardPage *pageDone();
};
