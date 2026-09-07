#pragma once
#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include "engine.h"

/// Shows the latest game frame with the found header and the capture box; drag to move the box.
class FramePreview : public QLabel {
	Q_OBJECT
public:
	explicit FramePreview(QWidget *parent = nullptr);
	void setFrame(const QImage &img, const Match &m, double threshold, QRectF box);
signals:
	void boxChanged(QRectF frac);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	QImage img_;
	Match m_;
	double thr_ = 0.85;
	QRectF box_, drag_;
	QPoint start_;
	bool dragging_ = false;
	QRect imageRect() const;
};

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(Engine *engine, QWidget *parent = nullptr);
	~SettingsDialog() override;

private:
	Engine *e_;
	// switch
	QComboBox *scene_, *game_;
	QTableWidget *friends_;
	QListWidget *mute_;
	QCheckBox *keepWarm_, *bringFront_;
	// look
	QCheckBox *lookName_, *lookPlate_, *lookCam_, *lookGrain_, *lookVig_;
	QLineEdit *lookLabel_;
	QSlider *grain_;
	QPushButton *preview_;
	bool previewing_ = false;
	// detect
	FramePreview *frame_;
	QProgressBar *meter_;
	QSlider *thr_;
	QLabel *thrLbl_, *tplLbl_;
	QSpinBox *downFrames_, *upFrames_, *minDown_, *pollMs_;
	QCheckBox *auto_, *revive_;
	QSlider *reviveThr_;
	QLabel *reviveLbl_;

	QWidget *buildSwitchTab();
	QWidget *buildLookTab();
	QWidget *buildDetectTab();
	QWidget *buildAboutTab();
	void fillSources();
	void fillFriends();
	void editFriend(int row);
	void collect(); // UI -> cfg
	void saveAndApply();
};
