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
    OscClientRow(const OscClient& client, QWidget* parent = nullptr);
    OscClient getClient() const;
    void updateSources(const QStringList& sources);
    void setTarget(const QString& target);
    
signals:
    void removed();

private:
    QLineEdit* nameEdit;
    QLineEdit* ipEdit;
    QSpinBox* portOutSpin;
    QComboBox* targetSourceCombo;
    QPushButton* removeBtn;
};

class OscSettingsDialog : public QDialog {
    Q_OBJECT
public:
    OscSettingsDialog(QWidget* parent = nullptr);
    ~OscSettingsDialog();

private slots:
    void onStartStop();
    void onStartStopMongoose();
    void onAddClient();
    void onSave();
    void onRefreshStatus();
    void onCheckPorts();

private:
    void loadSettings();
    void saveSettings();
    void setupUi();
    void populateSources();
    void applyStyles();
    void addLogMessage(const QString& msg);

    QLineEdit* serverIpEdit;
    QSpinBox* serverPortSpin;
    QCheckBox* autoStartCheck;
    QPushButton* emitDetailsBtn;
    
    QWidget* logContentWidget;
    QPushButton* toggleLogBtn;
    
    // Mongoose settings
    QSpinBox* mongoosePortSpin;
    QPushButton* startStopMongooseBtn;
    QLabel* mongooseStatusLabel;
    QFrame* mongooseStatusDot;
    
    QComboBox* targetSourceCombo;
    
    QPlainTextEdit* logEdit;
    QCheckBox* logCheck;
    QPushButton* startStopBtn;
    QLabel* statusLabel;
    QFrame* statusDot;

    QVBoxLayout* clientsLayout;
    QWidget* clientsContainer;
    std::vector<OscClientRow*> clientRows;

    QTimer* statusTimer;
};
