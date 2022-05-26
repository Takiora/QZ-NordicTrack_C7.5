#include "nordictrackifitadbtreadmill.h"
#include "homeform.h"
#include "ios/lockscreen.h"
#include "keepawakehelper.h"
#include "virtualtreadmill.h"
#include <QDateTime>
#include <QFile>
#include <QMetaEnum>
#include <QSettings>
#include <QThread>
#include <chrono>
#include <math.h>

using namespace std::chrono_literals;

nordictrackifitadbtreadmill::nordictrackifitadbtreadmill(bool noWriteResistance, bool noHeartService) {
    QSettings settings;
    m_watt.setType(metric::METRIC_WATT);
    Speed.setType(metric::METRIC_SPEED);
    refresh = new QTimer(this);
    this->noWriteResistance = noWriteResistance;
    this->noHeartService = noHeartService;
    initDone = false;
    connect(refresh, &QTimer::timeout, this, &nordictrackifitadbtreadmill::update);
    refresh->start(200ms);
    QString ip = settings.value("nordictrack_2950_ip", "").toString();
    adbClient = new AdbClient(ip);

    // ******************************************* virtual treadmill init *************************************
    if (!firstStateChanged && !virtualTreadmill && !virtualBike) {
        bool virtual_device_enabled = settings.value("virtual_device_enabled", true).toBool();
        bool virtual_device_force_bike = settings.value("virtual_device_force_bike", false).toBool();
        if (virtual_device_enabled) {
            if (!virtual_device_force_bike) {
                debug("creating virtual treadmill interface...");
                virtualTreadmill = new virtualtreadmill(this, noHeartService);
                connect(virtualTreadmill, &virtualtreadmill::debug, this, &nordictrackifitadbtreadmill::debug);
                connect(virtualTreadmill, &virtualtreadmill::changeInclination, this,
                        &nordictrackifitadbtreadmill::changeInclinationRequested);
            } else {
                debug("creating virtual bike interface...");
                virtualBike = new virtualbike(this);
                connect(virtualBike, &virtualbike::changeInclination, this,
                        &nordictrackifitadbtreadmill::changeInclinationRequested);
            }
            firstStateChanged = 1;
        }
    }
    // ********************************************************************************************************
}

/*
void nordictrackifitadbtreadmill::writeCharacteristic(uint8_t *data, uint8_t data_len, const QString &info, bool
disable_log, bool wait_for_response) { QEventLoop loop; QTimer timeout; if (wait_for_response) {
        connect(gattCommunicationChannelService, &QLowEnergyService::characteristicChanged, &loop, &QEventLoop::quit);
        timeout.singleShot(300ms, &loop, &QEventLoop::quit);
    } else {
        connect(gattCommunicationChannelService, &QLowEnergyService::characteristicWritten, &loop, &QEventLoop::quit);
        timeout.singleShot(300ms, &loop, &QEventLoop::quit);
    }

    gattCommunicationChannelService->writeCharacteristic(gattWriteCharacteristic,
                                                         QByteArray((const char *)data, data_len));

    if (!disable_log) {
        emit debug(QStringLiteral(" >> ") + QByteArray((const char *)data, data_len).toHex(' ') +
                   QStringLiteral(" // ") + info);
    }

    loop.exec();
}
*/
void nordictrackifitadbtreadmill::forceIncline(double incline) {}

void nordictrackifitadbtreadmill::forceSpeed(double speed) {}

void nordictrackifitadbtreadmill::update() {

    QSettings settings;
    QString ip = settings.value("nordictrack_2950_ip", "").toString();
    QString heartRateBeltName =
        settings.value(QStringLiteral("heart_rate_belt_name"), QStringLiteral("Disabled")).toString();
    double weight = settings.value(QStringLiteral("weight"), 75.0).toFloat();

    QString filename = QDate().currentDate().toString("yyyy-MM-dd") + "_logs.txt";
    qDebug() << filename;
    adbClient->doAdbPull("/sdcard.wolflogs/" + filename, homeform::getWritableAppDir() + filename, ip);

    double speed = 0;
    double incline = 0;
    QFile f(filename);
    f.open(QFile::ReadOnly);
    if (f.isOpen()) {
        QTextStream stream(&f);
        for (QString line = stream.readLine(); !line.isNull(); line = stream.readLine()) {
            qDebug() << line;
            if (line.contains(QStringLiteral("Changed KPH"))) {
                QStringList aValues = line.split(" ");
                speed = aValues.last().toDouble();
            } else if (line.contains(QStringLiteral("Changed Grade"))) {
                QStringList aValues = line.split(" ");
                incline = aValues.last().toDouble();
            }
        };
        f.close();
    }

    Inclination = incline;
    Speed = speed;
    if (watts(weight))
        KCal += ((((0.048 * ((double)watts(weight)) + 1.19) * weight * 3.5) / 200.0) /
                 (60000.0 / ((double)lastRefreshCharacteristicChanged.msecsTo(
                                QDateTime::currentDateTime())))); //(( (0.048* Output in watts +1.19) * body weight in
                                                                  // kg * 3.5) / 200 ) / 60
    // KCal = (((uint16_t)((uint8_t)newValue.at(15)) << 8) + (uint16_t)((uint8_t) newValue.at(14)));
    Distance += ((Speed.value() / 3600000.0) *
                 ((double)lastRefreshCharacteristicChanged.msecsTo(QDateTime::currentDateTime())));

    lastRefreshCharacteristicChanged = QDateTime::currentDateTime();

#ifdef Q_OS_ANDROID
    if (settings.value("ant_heart", false).toBool())
        Heart = (uint8_t)KeepAwakeHelper::heart();
    else
#endif
    {
        if (heartRateBeltName.startsWith(QStringLiteral("Disabled"))) {
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
            lockscreen h;
            long appleWatchHeartRate = h.heartRate();
            h.setKcal(KCal.value());
            h.setDistance(Distance.value());
            Heart = appleWatchHeartRate;
            debug("Current Heart from Apple Watch: " + QString::number(appleWatchHeartRate));
#endif
#endif
        }
    }

    emit debug(QStringLiteral("Current Inclination: ") + QString::number(Inclination.value()));
    emit debug(QStringLiteral("Current Speed: ") + QString::number(Speed.value()));
    emit debug(QStringLiteral("Current Calculate Distance: ") + QString::number(Distance.value()));
    // debug("Current Distance: " + QString::number(distance));
    emit debug(QStringLiteral("Current Watt: ") + QString::number(watts(weight)));

    update_metrics(true, watts(settings.value(QStringLiteral("weight"), 75.0).toFloat()));

    // updating the treadmill console every second
    if (sec1Update++ == (500 / refresh->interval())) {
        sec1Update = 0;
        // updateDisplay(elapsed);
    }

    if (requestStart != -1) {
        emit debug(QStringLiteral("starting..."));

        // btinit();

        requestStart = -1;
        emit tapeStarted();
    }
    if (requestStop != -1) {
        emit debug(QStringLiteral("stopping..."));
        // writeCharacteristic(initDataF0C800B8, sizeof(initDataF0C800B8), "stop tape");
        requestStop = -1;
    }
}

void nordictrackifitadbtreadmill::changeInclinationRequested(double grade, double percentage) {
    if (percentage < 0)
        percentage = 0;
    changeInclination(grade, percentage);
}

bool nordictrackifitadbtreadmill::connected() {}

void *nordictrackifitadbtreadmill::VirtualTreadmill() { return virtualTreadmill; }

void *nordictrackifitadbtreadmill::VirtualDevice() { return VirtualTreadmill(); }
