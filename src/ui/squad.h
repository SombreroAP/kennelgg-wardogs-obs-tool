#pragma once
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include "engine.h"

/// The Squad panel, one button from the dock: turn popped-out Discord streams into squad mates,
/// see who is on what, make one active, drop one. Built for a hand on the mouse mid-broadcast.
class SquadPanel : public QDialog {
	Q_OBJECT
public:
	explicit SquadPanel(Engine *engine, QWidget *parent = nullptr);
public slots:
	void refresh();

private:
	void addPopouts();
	void makeActive();
	void removeSelected();
	void editGameName();
	void showInDual();
	void askGameName(int idx);
	Engine *e_;
	QLabel *result_, *rosterState_;
	QListWidget *list_;
	QPushButton *add_, *active_, *remove_, *gameName_, *dual_;
	QCheckBox *roster_;
	QLineEdit *me_;
};
