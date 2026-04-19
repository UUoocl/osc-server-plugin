#include "osc-settings-dialog.hpp"
#include <QTimer>
#include <QGroupBox>
#include <QScrollArea>
#include <obs-module.h>
#include <util/platform.h>

extern void EmitServerDetails();

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

	targetSourceCombo = new QComboBox();
	targetSourceCombo->setStyleSheet(
		"background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white;");
	targetSourceCombo->addItem("All Browser Sources");

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
	row2->addWidget(new QLabel("Target:"));
	row2->addWidget(targetSourceCombo, 1);
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
	client.targetSource = targetSourceCombo->currentText().toStdString();
	return client;
}

void OscClientRow::updateSources(const QStringList &sources)
{
	QString current = targetSourceCombo->currentText();
	targetSourceCombo->clear();
	targetSourceCombo->addItems(sources);
	int index = targetSourceCombo->findText(current);
	if (index != -1)
		targetSourceCombo->setCurrentIndex(index);
}

void OscClientRow::setTarget(const QString &target)
{
	int index = targetSourceCombo->findText(target);
	if (index != -1)
		targetSourceCombo->setCurrentIndex(index);
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

	emitDetailsBtn = new QPushButton("Send OSC details to browsers");
	emitDetailsBtn->setStyleSheet(
		"background-color: #fab387; color: #11111b; font-weight: bold; padding: 6px; border-radius: 4px;");
	serverLay->addWidget(emitDetailsBtn);

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

	// Mongoose Group
	QGroupBox *mongooseGb = new QGroupBox("Mongoose Webserver (TCP/WS)");
	mongooseGb->setStyleSheet(
		"QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); margin-top: 12px; padding-top: 12px; color: #f9e2af; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
	QVBoxLayout *mongooseLay = new QVBoxLayout(mongooseGb);

	QHBoxLayout *mPortLay = new QHBoxLayout();
	mongoosePortSpin = new QSpinBox();
	mongoosePortSpin->setRange(1024, 65535);
	mongoosePortSpin->setValue(12347);
	mongoosePortSpin->setMinimumWidth(110);
	mongoosePortSpin->setStyleSheet(
		"background: #313244; border: 1px solid #45475a; color: white; padding: 4px; border-radius: 4px;");
	mPortLay->addWidget(new QLabel("Port:"));
	mPortLay->addWidget(mongoosePortSpin);

	mongooseLay->addLayout(mPortLay);

	QHBoxLayout *mStatusLay = new QHBoxLayout();
	mongooseStatusDot = new QFrame();
	mongooseStatusDot->setFixedSize(10, 10);
	mongooseStatusDot->setStyleSheet("background-color: #ff6b6b; border-radius: 5px;");
	mongooseStatusLabel = new QLabel("Stopped");
	startStopMongooseBtn = new QPushButton("Start");
	startStopMongooseBtn->setStyleSheet(
		"QPushButton { background-color: #a6e3a1; color: #11111b; font-weight: bold; padding: 6px; border-radius: 4px; }");

	mStatusLay->addWidget(mongooseStatusDot);
	mStatusLay->addWidget(mongooseStatusLabel);
	mStatusLay->addStretch();
	mStatusLay->addWidget(startStopMongooseBtn);
	mongooseLay->addLayout(mStatusLay);

	topGrids->addWidget(mongooseGb);
	root->addLayout(topGrids);

	// Global Routing Group
	QGroupBox *routeGb = new QGroupBox("Global Routing");
	routeGb->setStyleSheet(
		"QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); color: #94e2d5; }");
	QHBoxLayout *routeLay = new QHBoxLayout(routeGb);
	targetSourceCombo = new QComboBox();
	targetSourceCombo->setStyleSheet("background: #313244; color: white;");
	routeLay->addWidget(new QLabel("Default Target:"));
	routeLay->addWidget(targetSourceCombo, 1);
	root->addWidget(routeGb);

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
	connect(startStopMongooseBtn, &QPushButton::clicked, this, &OscSettingsDialog::onStartStopMongoose);
	connect(addClientBtn, &QPushButton::clicked, this, &OscSettingsDialog::onAddClient);
	connect(saveBtn, &QPushButton::clicked, this, &OscSettingsDialog::onSave);
	connect(emitDetailsBtn, &QPushButton::clicked, []() { EmitServerDetails(); });
	connect(logCheck, &QCheckBox::toggled, [](bool checked) { GetOscManager().EnableLogging(checked); });

	GetOscManager().SetLogCallback([this](const std::string &msg) {
		QMetaObject::invokeMethod(this, [this, msg]() { addLogMessage(QString::fromStdString(msg)); });
	});

	populateSources();
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

void OscSettingsDialog::onStartStopMongoose()
{
	auto &mgr = GetOscManager();
	if (mgr.IsMongooseRunning()) {
		mgr.StopMongoose();
	} else {
		mgr.StartMongoose(mongoosePortSpin->value());
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

	// Mongoose Status
	bool mRunning = mgr.IsMongooseRunning();
	mongooseStatusDot->setStyleSheet(mRunning ? "background-color: #39d353; border-radius: 5px;"
						  : "background-color: #ff6b6b; border-radius: 5px;");
	mongooseStatusLabel->setText(mRunning ? "Running" : "Stopped");
	startStopMongooseBtn->setText(mRunning ? "Stop" : "Start");
	startStopMongooseBtn->setStyleSheet(mRunning ? "background-color: #ff453a; color: white;"
						     : "background-color: #a6e3a1; color: #11111b;");

	// Live port availability check for spins if not running
	if (!running) {
		bool avail = OscManager::IsPortAvailable(serverPortSpin->value());
		serverPortSpin->setStyleSheet(avail ? "background: #313244; color: #a6e3a1;"
						    : "background: #313244; color: #ff453a;");
	} else {
		serverPortSpin->setStyleSheet("background: #313244; color: white;");
	}

	if (!mRunning) {
		bool avail = OscManager::IsPortAvailable(mongoosePortSpin->value());
		mongoosePortSpin->setStyleSheet(avail ? "background: #313244; color: #a6e3a1;"
						      : "background: #313244; color: #ff453a;");
	} else {
		mongoosePortSpin->setStyleSheet("background: #313244; color: white;");
	}
}

void OscSettingsDialog::onAddClient()
{
	OscClient client;
	client.name = "New Device";
	client.ip = "127.0.0.1";
	client.portOut = 12348;
	client.targetSource = "All Browser Sources";

	OscClientRow *row = new OscClientRow(client, clientsContainer);

	// Populate sources in new row
	QStringList sources;
	for (int i = 0; i < targetSourceCombo->count(); i++)
		sources << targetSourceCombo->itemText(i);
	row->updateSources(sources);

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
	mongoosePortSpin->setValue(mgr.GetMongoosePort());

	int targetIdx = targetSourceCombo->findText(QString::fromStdString(mgr.GetTargetSource()));
	if (targetIdx != -1)
		targetSourceCombo->setCurrentIndex(targetIdx);

	logCheck->setChecked(mgr.IsLoggingEnabled());
	autoStartCheck->setChecked(mgr.GetAutoStart());

	bool collapsed = mgr.IsLogCollapsed();
	logContentWidget->setVisible(!collapsed);
	toggleLogBtn->setText(collapsed ? "Expand Log" : "Collapse Log");

	for (auto *row : clientRows)
		row->deleteLater();
	clientRows.clear();

	QStringList sources;
	for (int j = 0; j < targetSourceCombo->count(); j++)
		sources << targetSourceCombo->itemText(j);

	for (const auto &client : mgr.GetClients()) {
		OscClientRow *row = new OscClientRow(client, clientsContainer);
		row->updateSources(sources);
		row->setTarget(QString::fromStdString(client.targetSource));

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
	mgr.SetMongoosePort(mongoosePortSpin->value());
	mgr.SetTargetSource(targetSourceCombo->currentText().toStdString());
	mgr.SetAutoStart(autoStartCheck->isChecked());
	mgr.EnableLogging(logCheck->isChecked());

	mgr.SaveConfig();
}

void OscSettingsDialog::populateSources()
{
	QStringList sources;
	sources << "All Browser Sources";

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			QStringList *list = (QStringList *)data;
			const char *id = obs_source_get_id(source);
			if (id && strcmp(id, "browser_source") == 0) {
				list->append(obs_source_get_name(source));
			}
			return true;
		},
		&sources);

	targetSourceCombo->clear();
	targetSourceCombo->addItems(sources);

	for (auto *row : clientRows) {
		row->updateSources(sources);
	}
}

void OscSettingsDialog::onCheckPorts()
{
	onRefreshStatus();
}
