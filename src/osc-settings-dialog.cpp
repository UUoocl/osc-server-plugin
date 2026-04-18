#include "osc-settings-dialog.hpp"
#include <QTimer>
#include <QGroupBox>
#include <QScrollArea>
#include <obs-module.h>

OscClientRow::OscClientRow(const OscClient& client, QWidget* parent) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet("OscClientRow { background-color: rgba(255, 255, 255, 0.05); border-radius: 8px; border: 1px solid rgba(255, 255, 255, 0.1); margin-bottom: 4px; padding: 4px; }");
    
    QHBoxLayout* layout = new QHBoxLayout(this);
    
    nameEdit = new QLineEdit(QString::fromStdString(client.name));
    nameEdit->setPlaceholderText("Name");
    nameEdit->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white; padding: 4px;");
    
    ipEdit = new QLineEdit(QString::fromStdString(client.ip));
    ipEdit->setFixedWidth(100);
    ipEdit->setPlaceholderText("127.0.0.1");
    ipEdit->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white; padding: 4px;");
    
    portOutSpin = new QSpinBox();
    portOutSpin->setRange(1, 65535);
    portOutSpin->setValue(client.portOut);
    portOutSpin->setFixedWidth(80);
    portOutSpin->setToolTip("Device Port (Used for both identification and sending)");
    portOutSpin->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.1); color: white;");
    
    removeBtn = new QPushButton("×");
    removeBtn->setFixedSize(24, 24);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setStyleSheet("QPushButton { background-color: #ff453a; color: white; border-radius: 12px; font-weight: bold; } QPushButton:hover { background-color: #ff3b30; }");
    
    layout->addWidget(new QLabel("Name:"));
    layout->addWidget(nameEdit, 1);
    layout->addWidget(new QLabel("IP:"));
    layout->addWidget(ipEdit);
    layout->addWidget(new QLabel("Port (Out):"));
    layout->addWidget(portOutSpin);
    layout->addWidget(removeBtn);
    layout->setSpacing(8);
    
    connect(removeBtn, &QPushButton::clicked, this, &OscClientRow::removed);
}

OscClient OscClientRow::getClient() const {
    return {
        nameEdit->text().toStdString(),
        ipEdit->text().toStdString(),
        portOutSpin->value()
    };
}

OscSettingsDialog::OscSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("OSC Server Settings");
    setMinimumWidth(650);
    setMinimumHeight(450);
    setStyleSheet("OscSettingsDialog { background-color: #1e1e2e; color: #cdd6f4; } QLabel { color: #cdd6f4; }");
    
    setupUi();
    loadSettings();
    
    statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &OscSettingsDialog::onRefreshStatus);
    statusTimer->start(1000);
    onRefreshStatus();
}

OscSettingsDialog::~OscSettingsDialog() {}

void OscSettingsDialog::setupUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    
    // Server Group
    QGroupBox* serverGb = new QGroupBox("OSC Server Configuration");
    serverGb->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); margin-top: 12px; padding-top: 12px; color: #f5c2e7; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    QVBoxLayout* serverLay = new QVBoxLayout(serverGb);
    
    QHBoxLayout* configLay = new QHBoxLayout();
    serverIpEdit = new QLineEdit("127.0.0.1");
    serverIpEdit->setFixedWidth(100);
    serverIpEdit->setStyleSheet("background: #313244; border: 1px solid #45475a; color: white; padding: 6px; border-radius: 4px;");
    serverPortSpin = new QSpinBox();
    serverPortSpin->setRange(1024, 65535);
    serverPortSpin->setValue(12346);
    serverPortSpin->setFixedWidth(80);
    serverPortSpin->setStyleSheet("background: #313244; border: 1px solid #45475a; color: white; padding: 4px; border-radius: 4px;");
    
    bridgePortSpin = new QSpinBox();
    bridgePortSpin->setRange(1024, 65535);
    bridgePortSpin->setValue(12347);
    bridgePortSpin->setFixedWidth(80);
    bridgePortSpin->setStyleSheet("background: #313244; border: 1px solid #45475a; color: white; padding: 4px; border-radius: 4px;");
    
    configLay->addWidget(new QLabel("Server IP:"));
    configLay->addWidget(serverIpEdit);
    configLay->addSpacing(16);
    configLay->addWidget(new QLabel("Port:"));
    configLay->addWidget(serverPortSpin);
    configLay->addStretch(1);
    serverLay->addLayout(configLay);
    
    QHBoxLayout* controlLay = new QHBoxLayout();
    statusDot = new QFrame();
    statusDot->setFixedSize(10, 10);
    statusDot->setStyleSheet("background-color: #ff6b6b; border-radius: 5px;");
    statusLabel = new QLabel("Stopped");
    startStopBtn = new QPushButton("Start Server");
    startStopBtn->setStyleSheet("QPushButton { background-color: #a6e3a1; color: #11111b; font-weight: bold; padding: 8px; border-radius: 4px; } QPushButton:hover { background-color: #94e2d5; }");
    
    controlLay->addWidget(statusDot);
    controlLay->addWidget(statusLabel);
    controlLay->addStretch(1);
    controlLay->addWidget(startStopBtn);
    serverLay->addLayout(controlLay);
    
    QHBoxLayout* targetLay = new QHBoxLayout();
    targetSourceCombo = new QComboBox();
    targetSourceCombo->setStyleSheet("background: #313244; border: 1px solid #45475a; color: white; padding: 4px; border-radius: 4px;");
    populateSources();
    
    targetLay->addWidget(new QLabel("Target Browser Source:"));
    targetLay->addWidget(targetSourceCombo, 1);
    serverLay->addLayout(targetLay);
    
    root->addWidget(serverGb);
    
    // Clients Group
    QGroupBox* clientsGb = new QGroupBox("OSC Clients (Devices)");
    clientsGb->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid rgba(255,255,255,0.1); margin-top: 12px; padding-top: 12px; color: #89b4fa; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    QVBoxLayout* clientsMainLay = new QVBoxLayout(clientsGb);
    
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    clientsContainer = new QWidget();
    clientsLayout = new QVBoxLayout(clientsContainer);
    clientsLayout->setContentsMargins(0, 0, 0, 0);
    clientsLayout->addStretch(1);
    scroll->setWidget(clientsContainer);
    clientsMainLay->addWidget(scroll);
    
    root->addWidget(clientsGb);
    
    // Message Log Group
    QGroupBox* logGb = new QGroupBox("Message Log");
    logGb->setStyleSheet("QGroupBox { color: #89b4fa; font-weight: bold; }");
    QVBoxLayout* logLay = new QVBoxLayout(logGb);
    
    QHBoxLayout* logTop = new QHBoxLayout();
    logCheck = new QCheckBox("Enable Logging");
    logCheck->setStyleSheet("color: white;");
    QPushButton* clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setFixedWidth(60);
    clearLogBtn->setStyleSheet("background: #45475a; color: white; border-radius: 4px; padding: 4px;");
    logTop->addWidget(logCheck);
    logTop->addStretch();
    logTop->addWidget(clearLogBtn);
    logLay->addLayout(logTop);
    
    logEdit = new QPlainTextEdit();
    logEdit->setReadOnly(true);
    logEdit->setFixedHeight(120);
    logEdit->setStyleSheet("background: #11111b; color: #a6e3a1; border: 1px solid #45475a; font-family: monospace;");
    logLay->addWidget(logEdit);
    
    root->addWidget(logGb);

    QHBoxLayout* bottomLay = new QHBoxLayout();
    QPushButton* addClientBtn = new QPushButton("Add Client");
    addClientBtn->setStyleSheet("background-color: #313244; color: white; padding: 10px; border-radius: 4px;");
    QPushButton* saveBtn = new QPushButton("Save All");
    saveBtn->setStyleSheet("background-color: #89b4fa; color: #11111b; font-weight: bold; padding: 10px; border-radius: 4px;");
    bottomLay->addWidget(addClientBtn);
    bottomLay->addWidget(saveBtn);
    root->addLayout(bottomLay);

    connect(startStopBtn, &QPushButton::clicked, this, &OscSettingsDialog::onStartStop);
    connect(addClientBtn, &QPushButton::clicked, this, &OscSettingsDialog::onAddClient);
    connect(saveBtn, &QPushButton::clicked, this, &OscSettingsDialog::onSave);
    connect(clearLogBtn, &QPushButton::clicked, logEdit, &QPlainTextEdit::clear);
    connect(logCheck, &QCheckBox::toggled, [](bool checked) {
        GetOscManager().EnableLogging(checked);
    });

    GetOscManager().SetLogCallback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            addLogMessage(QString::fromStdString(msg));
        });
    });

    loadSettings();
}

void OscSettingsDialog::onStartStop() {
    auto& mgr = GetOscManager();
    if (mgr.IsServerRunning()) {
        mgr.StopServer();
    } else {
        mgr.SetServerConfig(serverIpEdit->text().toStdString(), serverPortSpin->value());
        mgr.StartServer();
    }
    onRefreshStatus();
}

void OscSettingsDialog::addLogMessage(const QString& msg) {
    if (!logEdit) return;
    logEdit->appendPlainText(msg);
    if (logEdit->blockCount() > 100) {
        QTextCursor cursor = logEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar(); // remove \n
    }
}

void OscSettingsDialog::onAddClient() {
    OscClient client = {"New Device", "127.0.0.1", 12348};
    OscClientRow* row = new OscClientRow(client, clientsContainer);
    clientsLayout->insertWidget(clientsLayout->count() - 1, row);
    clientRows.push_back(row);
    connect(row, &OscClientRow::removed, [this, row]() {
        clientRows.erase(std::remove(clientRows.begin(), clientRows.end(), row), clientRows.end());
        row->deleteLater();
        saveSettings();
    });
    saveSettings();
}

void OscSettingsDialog::onSave() {
    saveSettings();
}

void OscSettingsDialog::onRefreshStatus() {
    auto& mgr = GetOscManager();
    bool running = mgr.IsServerRunning();
    statusDot->setStyleSheet(running ? "background-color: #39d353; border-radius: 5px;" : "background-color: #ff6b6b; border-radius: 5px;");
    statusLabel->setText(running ? QString("Running on %1").arg(mgr.GetServerPort()) : "Stopped");
    startStopBtn->setText(running ? "Stop Server" : "Start Server");
    startStopBtn->setStyleSheet(running ? "QPushButton { background-color: #ff453a; color: white; font-weight: bold; padding: 8px; border-radius: 4px; }" : "QPushButton { background-color: #a6e3a1; color: #11111b; font-weight: bold; padding: 8px; border-radius: 4px; }");
}

void OscSettingsDialog::loadSettings() {
    // Clear existing rows to prevent duplicates
    for (auto* row : clientRows) {
        row->deleteLater();
    }
    clientRows.clear();

    char* configPath = obs_module_config_path("osc-server-settings.json");
    if (!configPath) return;

    obs_data_t* data = obs_data_create_from_json_file(configPath);
    bfree(configPath);

    if (data) {
        const char* ip = obs_data_get_string(data, "server_ip");
        int port = (int)obs_data_get_int(data, "server_port");
        int bridgePort = (int)obs_data_get_int(data, "bridge_port");
        const char* target = obs_data_get_string(data, "target_source");
        
        if (ip && *ip) serverIpEdit->setText(ip);
        if (port > 0) serverPortSpin->setValue(port);
        if (bridgePort > 0) bridgePortSpin->setValue(bridgePort);
        if (target && *target) {
            int index = targetSourceCombo->findText(target);
            if (index != -1) targetSourceCombo->setCurrentIndex(index);
        }

        obs_data_array_t* clientsArray = obs_data_get_array(data, "clients");
        if (clientsArray) {
            size_t count = obs_data_array_count(clientsArray);
            for (size_t i = 0; i < count; i++) {
                obs_data_t* clientData = obs_data_array_item(clientsArray, i);
                OscClient client;
                client.name = obs_data_get_string(clientData, "name");
                client.ip = obs_data_get_string(clientData, "ip");
                client.portOut = (int)obs_data_get_int(clientData, "portOut");
                
                OscClientRow* row = new OscClientRow(client, clientsContainer);
                clientsLayout->insertWidget(clientsLayout->count() - 1, row);
                clientRows.push_back(row);
                connect(row, &OscClientRow::removed, [this, row]() {
                    clientRows.erase(std::remove(clientRows.begin(), clientRows.end(), row), clientRows.end());
                    row->deleteLater();
                    saveSettings();
                });
                
                obs_data_release(clientData);
            }
            obs_data_array_release(clientsArray);
        }
        
        // Push settings to manager
        saveSettings(); 

        obs_data_release(data);
    }
}
#include <util/platform.h>
#include <obs-module.h>

void OscSettingsDialog::saveSettings() {
    auto& mgr = GetOscManager();
    mgr.ClearClients();
    
    obs_data_t* data = obs_data_create();
    obs_data_set_string(data, "server_ip", serverIpEdit->text().toStdString().c_str());
    obs_data_set_int(data, "server_port", serverPortSpin->value());
    obs_data_set_int(data, "bridge_port", bridgePortSpin->value());
    obs_data_set_string(data, "target_source", targetSourceCombo->currentText().toStdString().c_str());

    obs_data_array_t* clientsArray = obs_data_array_create();
    for (auto* row : clientRows) {
        OscClient client = row->getClient();
        mgr.AddClient(client);
        
        obs_data_t* clientData = obs_data_create();
        obs_data_set_string(clientData, "name", client.name.c_str());
        obs_data_set_string(clientData, "ip", client.ip.c_str());
        obs_data_set_int(clientData, "portOut", client.portOut);
        obs_data_array_push_back(clientsArray, clientData);
        obs_data_release(clientData);
    }
    obs_data_set_array(data, "clients", clientsArray);
    obs_data_array_release(clientsArray);

    char* configDir = obs_module_config_path(nullptr);
    if (configDir) {
        os_mkdir(configDir);
        bfree(configDir);
    }

    char* configPath = obs_module_config_path("osc-server-settings.json");
    if (configPath) {
        blog(LOG_INFO, "[OSC Server] Saving settings to: %s", configPath);
        bool success = obs_data_save_json(data, configPath);
        if (!success) {
            blog(LOG_ERROR, "[OSC Server] Failed to save settings to %s", configPath);
        }
        bfree(configPath);
    }

    obs_data_release(data);
    
    // Update manager config
    mgr.SetServerConfig(serverIpEdit->text().toStdString(), serverPortSpin->value());
    mgr.SetTargetSource(targetSourceCombo->currentText().toStdString());
}

void OscSettingsDialog::populateSources() {
    targetSourceCombo->clear();
    targetSourceCombo->addItem("All Browser Sources");
    
    obs_enum_sources([](void* data, obs_source_t* source) {
        QComboBox* combo = (QComboBox*)data;
        const char* id = obs_source_get_id(source);
        if (id && strcmp(id, "browser_source") == 0) {
            const char* name = obs_source_get_name(source);
            combo->addItem(name);
        }
        return true;
    }, targetSourceCombo);

    // Try to set current target from manager
    int index = targetSourceCombo->findText(QString::fromStdString(GetOscManager().GetTargetSource()));
    if (index != -1) {
        targetSourceCombo->setCurrentIndex(index);
    }
}
