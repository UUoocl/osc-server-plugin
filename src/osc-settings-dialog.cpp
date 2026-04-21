#include "osc-settings-dialog.hpp"
#include <QTimer>
#include <QGroupBox>
#include <QScrollArea>
#include <obs-module.h>
#include <util/platform.h>


OscClientRow::OscClientRow(const OscClient &client, QWidget *parent) : QFrame(parent)
{
	setFrameShape(QFrame::StyledPanel);
	setStyleSheet(
		"OscClientRow { background-color: rgba(255, 255, 255, 0.05); border-radius: 8px; border: 1px solid rgba(255, 255, 255, 0.1); margin-bottom: 4px; padding: 4px; }");

	nameEdit = new QLineEdit(QString::fromStdString(client.name));
	nameEdit->setPlaceholderText("Name");
	nameEdit->setStyleSheet(
		"background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white; padding: 4px;");

	ipEdit = new QLineEdit(QString::fromStdString(client.ip));
	ipEdit->setFixedWidth(120);
	ipEdit->setPlaceholderText("127.0.0.1");
	ipEdit->setStyleSheet(
		"background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white; padding: 4px;");

	portOutSpin = new QSpinBox();
	portOutSpin->setRange(1, 65535);
	portOutSpin->setValue(client.portOut);
	portOutSpin->setMinimumWidth(110);
	portOutSpin->setStyleSheet(
		"background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white;");

	removeBtn = new QPushButton("×");
	removeBtn->setFixedSize(24, 24);
	removeBtn->setCursor(Qt::PointingHandCursor);
	removeBtn->setStyleSheet(
		"QPushButton { background-color: #ff453a; color: white; border-radius: 12px; font-weight: bold; } QPushButton:hover { background-color: #ff3b30; }");

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 8, 8, 8);
	mainLayout->setSpacing(8);

	QHBoxLayout *row1 = new QHBoxLayout();
	row1->addWidget(new QLabel("Name:"));
	row1->addWidget(nameEdit, 1);
	row1->addWidget(new QLabel("IP:"));
	row1->addWidget(ipEdit);
	row1->addWidget(removeBtn);
	mainLayout->addLayout(row1);

	QHBoxLayout *row2 = new QHBoxLayout();

	row2->addWidget(new QLabel("Port:"));
	row2->addWidget(portOutSpin);
	mainLayout->addLayout(row2);

	connect(removeBtn, &QPushButton::clicked, this, &OscClientRow::removed);
}

OscClient OscClientRow::getClient() const
{
	OscClient client;
	client.name = nameEdit->text().toStdString();
	client.ip = ipEdit->text().toStdString();
	client.portOut = portOutSpin->value();
	return client;
}



OscSettingsDialog::OscSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("OSC Server Settings");
	setMinimumWidth(800);
	setMinimumHeight(600);
	setStyleSheet("OscSettingsDialog { background-color: #1e1e2e; color: #cdd6f4; } QLabel { color: #cdd6f4; }");

	setupUi();
	loadSettings();

	statusTimer = new QTimer(this);
	connect(statusTimer, &QTimer::timeout, this, &OscSettingsDialog::onRefreshStatus);
	statusTimer->start(1000);
	onRefreshStatus();
}

OscSettingsDialog::~OscSettingsDialog() {}

void OscSettingsDialog::setupUi()
{
	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	QHBoxLayout *topGrids = new QHBoxLayout();

	// OSC Server Group
	QGroupBox *serverGb = new QGroupBox("OSC Server (UDP)");
	serverGb->setStyleSheet(
		"QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); margin-top: 12px; padding-top: 12px; color: #f5c2e7; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
	QVBoxLayout *serverLay = new QVBoxLayout(serverGb);

	QHBoxLayout *ipLay = new QHBoxLayout();
	serverIpEdit = new QLineEdit("127.0.0.1");
	serverIpEdit->setFixedWidth(120);
	serverIpEdit->setStyleSheet(
		"background: #313244; border: 1px solid #45475a; color: white; padding: 6px; border-radius: 4px;");
	ipLay->addWidget(new QLabel("IP:"));
	ipLay->addWidget(serverIpEdit);
	ipLay->addStretch();
	serverLay->addLayout(ipLay);

	QHBoxLayout *portLay = new QHBoxLayout();
	serverPortSpin = new QSpinBox();
	serverPortSpin->setRange(1024, 65535);
	serverPortSpin->setValue(12346);
	serverPortSpin->setMinimumWidth(110);
	serverPortSpin->setStyleSheet(
		"background: #313244; border: 1px solid #45475a; color: white; padding: 4px; border-radius: 4px;");
	portLay->addWidget(new QLabel("Port:"));
	portLay->addWidget(serverPortSpin);
	portLay->addStretch();
	serverLay->addLayout(portLay);

	autoStartCheck = new QCheckBox("Start server on OBS launch");
	serverLay->addWidget(autoStartCheck);

	QHBoxLayout *oscStatusLay = new QHBoxLayout();
	statusDot = new QFrame();
	statusDot->setFixedSize(10, 10);
	statusDot->setStyleSheet("background-color: #ff6b6b; border-radius: 5px;");
	statusLabel = new QLabel("Stopped");
	startStopBtn = new QPushButton("Start");
	startStopBtn->setStyleSheet(
		"QPushButton { background-color: #a6e3a1; color: #11111b; font-weight: bold; padding: 6px; border-radius: 4px; }");

	oscStatusLay->addWidget(statusDot);
	oscStatusLay->addWidget(statusLabel);
	oscStatusLay->addStretch();
	oscStatusLay->addWidget(startStopBtn);
	serverLay->addLayout(oscStatusLay);

	topGrids->addWidget(serverGb);

	root->addLayout(topGrids);



	// Clients Group
	QGroupBox *clientsGb = new QGroupBox("OSC Devices (Per-Device Routing)");
	clientsGb->setStyleSheet(
		"QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); color: #89b4fa; }");
	QVBoxLayout *clientsMainLay = new QVBoxLayout(clientsGb);
	QScrollArea *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
	clientsContainer = new QWidget();
	clientsLayout = new QVBoxLayout(clientsContainer);
	clientsLayout->setContentsMargins(0, 0, 0, 0);
	clientsLayout->addStretch(1);
	scroll->setWidget(clientsContainer);
	clientsMainLay->addWidget(scroll);
	root->addWidget(clientsGb);

	// Log Group
	QGroupBox *logGb = new QGroupBox("Log Console");
	logGb->setStyleSheet(
		"QGroupBox { color: #89b4fa; font-weight: bold; border: 1px solid rgba(255,255,255,0.1); }");
	QVBoxLayout *logOuterLay = new QVBoxLayout(logGb);

	logContentWidget = new QWidget();
	QVBoxLayout *logLay = new QVBoxLayout(logContentWidget);
	logLay->setContentsMargins(0, 0, 0, 0);

	logCheck = new QCheckBox("Enable Logging");
	logLay->addWidget(logCheck);

	logEdit = new QPlainTextEdit();
	logEdit->setReadOnly(true);
	logEdit->setFixedHeight(150);
	logEdit->setStyleSheet(
		"background: #11111b; color: #a6e3a1; border: 1px solid #45475a; font-family: monospace;");
	logLay->addWidget(logEdit);

	toggleLogBtn = new QPushButton("Collapse Log");
	toggleLogBtn->setStyleSheet(
		"QPushButton { background: rgba(255,255,255,0.05); color: #89b4fa; border: none; padding: 4px; border-radius: 4px; text-align: left; } QPushButton:hover { background: rgba(255,255,255,0.1); }");

	logOuterLay->addWidget(toggleLogBtn);
	logOuterLay->addWidget(logContentWidget);
	root->addWidget(logGb);

	connect(toggleLogBtn, &QPushButton::clicked, [this]() {
		bool visible = !logContentWidget->isVisible();
		logContentWidget->setVisible(visible);
		toggleLogBtn->setText(visible ? "Collapse Log" : "Expand Log");
		GetOscManager().SetLogCollapsed(!visible);
	});

	QHBoxLayout *bottomLay = new QHBoxLayout();
	QPushButton *addClientBtn = new QPushButton("Add Device");
	addClientBtn->setStyleSheet("background-color: #313244; color: white; padding: 10px; border-radius: 4px;");
	QPushButton *saveBtn = new QPushButton("Save & Apply");
	saveBtn->setStyleSheet(
		"background-color: #89b4fa; color: #11111b; font-weight: bold; padding: 10px; border-radius: 4px;");
	bottomLay->addWidget(addClientBtn);
	bottomLay->addWidget(saveBtn);
	root->addLayout(bottomLay);

	connect(startStopBtn, &QPushButton::clicked, this, &OscSettingsDialog::onStartStop);
	connect(addClientBtn, &QPushButton::clicked, this, &OscSettingsDialog::onAddClient);
	connect(saveBtn, &QPushButton::clicked, this, &OscSettingsDialog::onSave);
	connect(logCheck, &QCheckBox::toggled, [](bool checked) { GetOscManager().EnableLogging(checked); });

	GetOscManager().SetLogCallback([this](const std::string &msg) {
		QMetaObject::invokeMethod(this, [this, msg]() { addLogMessage(QString::fromStdString(msg)); });
	});
}

void OscSettingsDialog::onStartStop()
{
	auto &mgr = GetOscManager();
	if (mgr.IsServerRunning()) {
		mgr.StopServer();
	} else {
		mgr.SetServerConfig(serverIpEdit->text().toStdString(), serverPortSpin->value());
		mgr.StartServer();
	}
	onRefreshStatus();
}


void OscSettingsDialog::onRefreshStatus()
{
	auto &mgr = GetOscManager();

	// OSC Status
	bool running = mgr.IsServerRunning();
	statusDot->setStyleSheet(running ? "background-color: #39d353; border-radius: 5px;"
					 : "background-color: #ff6b6b; border-radius: 5px;");
	statusLabel->setText(running ? "Running" : "Stopped");
	startStopBtn->setText(running ? "Stop" : "Start");
	startStopBtn->setStyleSheet(running ? "background-color: #ff453a; color: white;"
					    : "background-color: #a6e3a1; color: #11111b;");

	// Live port availability check for spins if not running
	if (!running) {
		bool avail = OscManager::IsPortAvailable(serverPortSpin->value());
		serverPortSpin->setStyleSheet(avail ? "background: #313244; color: #a6e3a1;"
						    : "background: #313244; color: #ff453a;");
	} else {
		serverPortSpin->setStyleSheet("background: #313244; color: white;");
	}
}

void OscSettingsDialog::onAddClient()
{
	OscClient client;
	client.name = "New Device";
	client.ip = "127.0.0.1";
	client.portOut = 12348;


	OscClientRow *row = new OscClientRow(client, clientsContainer);

	clientsLayout->insertWidget(clientsLayout->count() - 1, row);
	row->show();
	clientRows.push_back(row);
	connect(row, &OscClientRow::removed, [this, row]() {
		clientRows.erase(std::remove(clientRows.begin(), clientRows.end(), row), clientRows.end());
		row->deleteLater();
	});
}

void OscSettingsDialog::addLogMessage(const QString &msg)
{
	if (!logEdit)
		return;
	logEdit->appendPlainText(msg);
	if (logEdit->blockCount() > 100) {
		QTextCursor cursor = logEdit->textCursor();
		cursor.movePosition(QTextCursor::Start);
		cursor.select(QTextCursor::BlockUnderCursor);
		cursor.removeSelectedText();
		cursor.deleteChar();
	}
}

void OscSettingsDialog::onSave()
{
	saveSettings();
}

void OscSettingsDialog::loadSettings()
{
	auto &mgr = GetOscManager();

	serverIpEdit->setText(QString::fromStdString(mgr.GetServerIp()));
	serverPortSpin->setValue(mgr.GetServerPort());

	logCheck->setChecked(mgr.IsLoggingEnabled());
	autoStartCheck->setChecked(mgr.GetAutoStart());

	bool collapsed = mgr.IsLogCollapsed();
	logContentWidget->setVisible(!collapsed);
	toggleLogBtn->setText(collapsed ? "Expand Log" : "Collapse Log");

	for (auto *row : clientRows)
		row->deleteLater();
	clientRows.clear();

	for (const auto &client : mgr.GetClients()) {
		OscClientRow *row = new OscClientRow(client, clientsContainer);

		clientsLayout->insertWidget(clientsLayout->count() - 1, row);
		row->show();
		clientRows.push_back(row);

		connect(row, &OscClientRow::removed, [this, row]() {
			clientRows.erase(std::remove(clientRows.begin(), clientRows.end(), row), clientRows.end());
			row->deleteLater();
		});
	}
}

void OscSettingsDialog::saveSettings()
{
	auto &mgr = GetOscManager();
	mgr.ClearClients();

	for (auto *row : clientRows) {
		mgr.AddClient(row->getClient());
	}

	mgr.SetServerConfig(serverIpEdit->text().toStdString(), serverPortSpin->value());
	mgr.SetAutoStart(autoStartCheck->isChecked());
	mgr.EnableLogging(logCheck->isChecked());

	mgr.SaveConfig();
}



void OscSettingsDialog::onCheckPorts()
{
	onRefreshStatus();
}
