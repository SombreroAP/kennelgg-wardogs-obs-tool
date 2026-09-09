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
	QComboBox *scene_ = nullptr, *game_ = nullptr;
	QTableWidget *friends_;
	QListWidget *mute_ = nullptr;
	QCheckBox *keepWarm_, *bringFront_;
	// look
	QCheckBox *lookName_ = nullptr, *lookPlate_ = nullptr, *lookCam_ = nullptr, *lookGrain_ = nullptr,
		  *lookVig_ = nullptr;
	QLineEdit *lookLabel_;
	QSlider *grain_;
	QPushButton *preview_;
	bool previewing_ = false;
	bool building_ =
		true; // widgets fire changed-signals while being given their saved values; ignore until all tabs exist
	// detect
	FramePreview *frame_;
	QProgressBar *meter_;
	QSlider *thr_ = nullptr;
	QLabel *thrLbl_, *tplLbl_;
	QSpinBox *downFrames_, *upFrames_, *minDown_, *pollMs_;
	QCheckBox *auto_, *revive_;
	QSlider *reviveThr_;
	QLabel *reviveLbl_;

	QWidget *buildSwitchTab();
	QWidget *buildLookTab();
	QWidget *buildDetectTab();
	QWidget *buildAboutTab();
	QWidget *buildClipsTab();
	QLineEdit *playerName_ = nullptr;
	QCheckBox *lanOn_, *ndiShare_, *autoAdd_;
	QListWidget *peers_;
	QLabel *lanStatus_;
	QCheckBox *bridgeOn_ = nullptr, *launchApp_ = nullptr, *autoReplay_ = nullptr, *clipDowned_ = nullptr;
	QSpinBox *bridgePort_;
	QLineEdit *appPath_, *nameTpl_;
	QListWidget *clipList_;
	QCheckBox *useReplay_ = nullptr;
	QListWidget *hotkeyList_ = nullptr;
	QLineEdit *hotkeyFilter_ = nullptr;
	void fillHotkeys();
	void fillSources();
	void fillFriends();
	void editFriend(int row);
	void collect(); // UI -> cfg
	void saveAndApply();
};
