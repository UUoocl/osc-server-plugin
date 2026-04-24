#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <vector>

#include "osc-manager.hpp"

class OscClientRow : public QFrame {
	Q_OBJECT
public:
	OscClientRow(const OscClient &client, QWidget *parent = nullptr);
	OscClient getClient() const;


signals:
	void removed();

private:
	QLineEdit *nameEdit;
	QLineEdit *ipEdit;
	QSpinBox *portOutSpin;

	QPushButton *removeBtn;
};

class OscSettingsDialog : public QDialog {
	Q_OBJECT
public:
	OscSettingsDialog(QWidget *parent = nullptr);
	~OscSettingsDialog();

private slots:
	void onStartStop();
	void onAddClient();
	void onSave();
	void onRefreshStatus();
	void onCheckPorts();

private:
	void loadSettings();
	void saveSettings();
	void setupUi();

	void applyStyles();
	void addLogMessage(const QString &msg);

	QLineEdit *serverIpEdit;
	QSpinBox *serverPortSpin;
	QCheckBox *autoStartCheck;
	QCheckBox *broadcastGeneralCheck;
	QCheckBox *broadcastByDeviceCheck;

	QWidget *logContentWidget;
	QPushButton *toggleLogBtn;



	QPlainTextEdit *logEdit;
	QCheckBox *logCheck;
	QPushButton *startStopBtn;
	QLabel *statusLabel;
	QFrame *statusDot;

	QVBoxLayout *clientsLayout;
	QWidget *clientsContainer;
	std::vector<OscClientRow *> clientRows;

	QTimer *statusTimer;
};
