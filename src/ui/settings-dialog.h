#pragma once
#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QRadioButton>
#include "engine.h"

/// Shows the latest game frame with the found header and the capture box; drag to move the box.
class FramePreview : public QLabel {
	Q_OBJECT
public:
	explicit FramePreview(QWidget *parent = nullptr);
	void setFrame(const QImage &img, const Match &m, double threshold, QRectF box);
	/// A second box drawn in its own colour (the NEARBY area next to the kill-feed area).
	void setBox2(QRectF box);
	/// Area-picker mode: no detector rectangle, this caption along the bottom.
	void setPicker(const QString &caption);
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
	bool picker_ = false;
	QString caption_;
	QRectF box_, box2_, drag_;
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
	QComboBox *scene_ = nullptr, *game_ = nullptr, *gameDetect_ = nullptr;
	QTableWidget *friends_;
	QPushButton *speedBtn_ = nullptr;
	QLabel *ndiStatus_ = nullptr;
	QComboBox *sceneV_ = nullptr;
	QComboBox *lookPos_ = nullptr, *ndiQuality_ = nullptr;
	QListWidget *onTop_ = nullptr;
	QListWidget *mute_ = nullptr;
	QCheckBox *warmNdi_ = nullptr;
	QCheckBox *keepWarm_, *bringFront_;
	QCheckBox *preload_ = nullptr, *friendAudio_ = nullptr;
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
	QSlider *hold_ = nullptr;
	QLabel *holdLbl_ = nullptr;
	QSpinBox *downFrames_, *upFrames_, *minDown_, *pollMs_;
	QSpinBox *downDelay_ = nullptr, *upDelay_ = nullptr;
	QCheckBox *auto_, *revive_;
	QCheckBox *wide_ = nullptr;
	QSlider *reviveThr_;
	QLabel *reviveLbl_;

	QWidget *buildSwitchTab();
	QWidget *buildLookTab();
	QWidget *buildDetectTab();
	QWidget *buildAboutTab();
	QLabel *updateLbl_ = nullptr;
	QCheckBox *updateAuto_ = nullptr;
	QWidget *buildClipsTab();
	QWidget *buildAppTab();
	QWidget *buildLogsTab();
	QWidget *buildDualTab();
	QComboBox *dualFriend_ = nullptr, *dualPreset_ = nullptr;
	QCheckBox *dualOn_ = nullptr, *dualAuto_ = nullptr, *dualKeep_ = nullptr;
	QRadioButton *dragWin_ = nullptr, *dragKeys_ = nullptr;
	QDoubleSpinBox *dualX_ = nullptr, *dualY_ = nullptr, *dualW_ = nullptr;
	QSlider *dualOpacity_ = nullptr;
	FramePreview *dualPick_ = nullptr;
	QLabel *dualState_ = nullptr;
	void dualToUi();
	void dualFromUi(bool preset);
	QPlainTextEdit *logView_ = nullptr;
	void refreshLogs();
	QLineEdit *appName_ = nullptr, *appLibrary_ = nullptr, *appBroadcaster_ = nullptr, *clipFolder_ = nullptr;
	QCheckBox *appTwitch_ = nullptr, *appEveryKill_ = nullptr;
	QDoubleSpinBox *appMulti_ = nullptr;
	FramePreview *feedPick_ = nullptr;
	QRadioButton *pickTpl_ = nullptr, *pickNear_ = nullptr;
	QLabel *nearLbl2_ = nullptr;
	QLabel *areaLbl_ = nullptr;
	QSpinBox *appFps_ = nullptr;
	void updateAreas();
	void showNearbyTest();
	// closest squad mate (the game's NEARBY list)
	QCheckBox *nearOn_ = nullptr, *nearFollow_ = nullptr;
	QSlider *nearCooldown_ = nullptr;
	QSpinBox *nearMax_ = nullptr;
	QLabel *nearCdLbl_ = nullptr;
	QLabel *nearLbl_ = nullptr;
	QLabel *twitchLbl_ = nullptr;
	QPushButton *twitchLogin_ = nullptr, *twitchLogout_ = nullptr;
	void refreshAppTab();
	QLineEdit *playerName_ = nullptr;
	QCheckBox *lanOn_, *ndiShare_, *autoAdd_;
	QListWidget *peers_;
	QLabel *lanStatus_;
	QCheckBox *bridgeOn_ = nullptr, *launchApp_ = nullptr, *autoReplay_ = nullptr, *clipDowned_ = nullptr;
	QSpinBox *bridgePort_;
	QLineEdit *appPath_, *nameTpl_;
	QListWidget *clipList_;
	QCheckBox *useReplay_ = nullptr;
	QCheckBox *closeApp_ = nullptr;
	QListWidget *hotkeyList_ = nullptr;
	QLineEdit *backtrackFolder_ = nullptr;
	QLineEdit *hotkeyFilter_ = nullptr;
	void fillHotkeys();
	void fillSources();
	void fillFriends();
	void editFriend(int row);
	void collect(); // UI -> cfg
	void saveAndApply();
	void testFeed();
	void testLink();
};
