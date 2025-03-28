#include "domyostreadmill.h"
#include "virtualtreadmill.h"
#include "keepawakehelper.h"
#include <QFile>
#include <QDateTime>
#include <QMetaEnum>
#include <QSettings>
#include <QBluetoothLocalDevice>
#include "ios/lockscreen.h"

// set speed and incline to 0
uint8_t initData1[] = { 0xf0, 0xc8, 0x01, 0xb9 };
uint8_t initData2[] = { 0xf0, 0xc9, 0xb9 };

uint8_t noOpData[] = { 0xf0, 0xac, 0x9c };

// stop tape
uint8_t initDataF0C800B8[] = { 0xf0, 0xc8, 0x00, 0xb8 };

// main startup sequence
uint8_t initDataStart[] = { 0xf0, 0xa3, 0x93 };
uint8_t initDataStart2[] = { 0xf0, 0xa4, 0x94 };
uint8_t initDataStart3[] = { 0xf0, 0xa5, 0x95 };
uint8_t initDataStart4[] = { 0xf0, 0xab, 0x9b };
uint8_t initDataStart5[] = { 0xf0, 0xc4, 0x03, 0xb7 };
uint8_t initDataStart6[] =
{
        0xf0, 0xad, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0xff
};
uint8_t initDataStart7[] = { 0xff, 0xff, 0x8b }; // power on bt icon
uint8_t initDataStart8[] =
{
        0xf0, 0xcb, 0x02, 0x00, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00
};
uint8_t initDataStart9[] = { 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xb6 }; // power on bt word
uint8_t initDataStart10[] =
{
        0xf0, 0xad, 0xff, 0xff, 0x00, 0x05, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0x01, 0xff
};
uint8_t initDataStart11[] = { 0xff, 0xff, 0x94 }; // start tape
uint8_t initDataStart12[] =
{
        0xf0, 0xcb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x14, 0x01, 0xff, 0xff
};
uint8_t initDataStart13[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbd };

QBluetoothUuid _gattCommunicationChannelServiceId((QString)"49535343-fe7d-4ae5-8fa9-9fafd205e455");
QBluetoothUuid _gattWriteCharacteristicId((QString)"49535343-8841-43f4-a8d4-ecbe34729bb3");
QBluetoothUuid _gattNotifyCharacteristicId((QString)"49535343-1e4d-4bd9-ba61-23c647249616");

domyostreadmill::domyostreadmill(uint32_t pollDeviceTime, bool noConsole, bool noHeartService, double forceInitSpeed, double forceInitInclination)
{
    m_watt.setType(metric::METRIC_WATT);
    this->noConsole = noConsole;
    this->noHeartService = noHeartService;

    if(forceInitSpeed > 0)
        lastSpeed = forceInitSpeed;

    if(forceInitInclination > 0)
        lastInclination = forceInitInclination;

    refresh = new QTimer(this);
    initDone = false;
    connect(refresh, SIGNAL(timeout()), this, SLOT(update()));
    refresh->start(pollDeviceTime);
}

void domyostreadmill::writeCharacteristic(uint8_t* data, uint8_t data_len, QString info, bool disable_log, bool wait_for_response)
{
    QEventLoop loop;
    QTimer timeout;

    if(wait_for_response)
    {
        connect(this, SIGNAL(packetReceived()),
                &loop, SLOT(quit()));
        timeout.singleShot(300, &loop, SLOT(quit()));
    }
    else
    {
        connect(gattCommunicationChannelService, SIGNAL(characteristicWritten(QLowEnergyCharacteristic,QByteArray)),
                &loop, SLOT(quit()));
        timeout.singleShot(300, &loop, SLOT(quit()));
    }

    if(gattCommunicationChannelService->state() != QLowEnergyService::ServiceState::ServiceDiscovered ||
       m_control->state() == QLowEnergyController::UnconnectedState)
    {
        debug("writeCharacteristic error because the connection is closed");
        return;
    }

    gattCommunicationChannelService->writeCharacteristic(gattWriteCharacteristic, QByteArray((const char*)data, data_len));

    if(!disable_log)
        debug(" >> " + QByteArray((const char*)data, data_len).toHex(' ') + " // " + info);

    loop.exec();

    if(timeout.isActive() == false)
        debug(" exit for timeout");
}

void domyostreadmill::updateDisplay(uint16_t elapsed)
{
   uint8_t display[] = {0xf0, 0xcb, 0x03, 0x00, 0x00, 0xff, 0x01, 0x00, 0x00, 0x02,
                        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05, 0x01, 0x01, 0x00,
                        0x0c, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00};

   QSettings settings;
   bool distance = settings.value("domyos_treadmill_distance_display", true).toBool();

   if(elapsed > 5999) // 99:59
   {
       display[3] = ((elapsed / 60) / 60) & 0xFF; // high byte for elapsed time (in seconds)
       display[4] = ((elapsed / 60) % 60) & 0xFF; // low byte for elasped time (in seconds)
   }
   else
   {
       display[3] = (elapsed / 60) & 0xFF; // high byte for elapsed time (in seconds)
       display[4] = (elapsed % 60 & 0xFF); // low byte for elasped time (in seconds)
   }

   if(distance)
   {
       if(odometer() < 10.0)
       {
           display[7] = ((uint8_t)((uint16_t)(odometer() * 100) >> 8)) & 0xFF;
           display[8] = (uint8_t)(odometer() * 100) & 0xFF;
           display[9] = 0x02; // decimal position
       }
       else if(odometer() < 100.0)
       {
           display[7] = ((uint8_t)(odometer() * 10) >> 8) & 0xFF;
           display[8] = (uint8_t)(odometer() * 10) & 0xFF;
           display[9] = 0x01; // decimal position
       }
       else
       {
           display[7] = ((uint8_t)(odometer()) >> 8) & 0xFF;
           display[8] = (uint8_t)(odometer()) & 0xFF;
           display[9] = 0x00; // decimal position
       }
   }
   else
   {
       display[7] = 0x00;
       display[8] = 0x00;
       display[9] = 0x00; // decimal position
   }

   display[12] = (uint8_t)currentHeart().value();

   display[16] = (uint8_t)(currentInclination().value() * 10.0);

   display[20] = (uint8_t)(currentSpeed().value() * 10.0);

   display[23] = ((uint8_t)(calories()) >> 8) & 0xFF;
   display[24] = (uint8_t)(calories()) & 0xFF;

   for(uint8_t i=0; i<sizeof(display)-1; i++)
   {
      display[26] += display[i]; // the last byte is a sort of a checksum
   }

   writeCharacteristic(display, 20, "updateDisplay elapsed=" + QString::number(elapsed), false, false );
   writeCharacteristic(&display[20], sizeof (display) - 20, "updateDisplay elapsed=" + QString::number(elapsed), false, true );
}

void domyostreadmill::forceSpeedOrIncline(double requestSpeed, double requestIncline)
{
   uint8_t writeIncline[] = {0xf0, 0xad, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0x00};

   writeIncline[4] = ((uint16_t)(requestSpeed*10) >> 8) & 0xFF;
   writeIncline[5] = ((uint16_t)(requestSpeed*10) & 0xFF);

   writeIncline[13] = ((uint16_t)(requestIncline*10) >> 8) & 0xFF;
   writeIncline[14] = ((uint16_t)(requestIncline*10) & 0xFF);

   for(uint8_t i=0; i<sizeof(writeIncline)-1; i++)
   {
      //qDebug() << QString::number(writeIncline[i], 16);
      writeIncline[22] += writeIncline[i]; // the last byte is a sort of a checksum
   }

   //qDebug() << "writeIncline crc" << QString::number(writeIncline[26], 16);


   writeCharacteristic(writeIncline, 20, "forceSpeedOrIncline speed=" + QString::number(requestSpeed) + " incline=" + QString::number(requestIncline), false, false);
   writeCharacteristic(&writeIncline[20], sizeof (writeIncline) - 20, "forceSpeedOrIncline speed=" + QString::number(requestSpeed) + " incline=" + QString::number(requestIncline), false, true);
}

bool domyostreadmill::sendChangeFanSpeed(uint8_t speed)
{
   uint8_t fanSpeed[] = {0xf0, 0xca, 0x00, 0x00};

   if(speed > 5) return false;

   fanSpeed[2] = speed;

   for(uint8_t i=0; i<sizeof(fanSpeed)-1; i++)
   {
      fanSpeed[3] += fanSpeed[i]; // the last byte is a sort of a checksum
   }

   writeCharacteristic(fanSpeed, 4, "changeFanSpeed speed=" + QString::number(speed), false, true);

   return true;
}

bool domyostreadmill::changeFanSpeed(uint8_t speed)
{
   requestFanSpeed = speed;

   return true;
}


void domyostreadmill::update()
{
    if(m_control->state() == QLowEnergyController::UnconnectedState)
    {
        emit disconnected();
        return;
    }

    if(initRequest)
    {
        initRequest = false;
        btinit((lastSpeed > 0 ? true : false));
    }
    else if(/*bluetoothDevice.isValid() &&*/
       m_control->state() == QLowEnergyController::DiscoveredState &&
       gattCommunicationChannelService &&
       gattWriteCharacteristic.isValid() &&
       gattNotifyCharacteristic.isValid() &&
       initDone)
    {
        // ******************************************* virtual treadmill init *************************************        
        if(!firstInit && searchStopped && !virtualTreadMill)
        {
            QSettings settings;
            bool virtual_device_enabled = settings.value("virtual_device_enabled", true).toBool();
            if(virtual_device_enabled)
            {
                debug("creating virtual treadmill interface...");
                virtualTreadMill = new virtualtreadmill(this, noHeartService);
                connect(virtualTreadMill,&virtualtreadmill::debug ,this,&domyostreadmill::debug);
                firstInit = 1;
            }
        }
        // ********************************************************************************************************

        //debug("Domyos Treadmill RSSI " + QString::number(bluetoothDevice.rssi()));

        QDateTime current = QDateTime::currentDateTime();
        double deltaTime = (((double)lastTimeUpdate.msecsTo(current)) / ((double)1000.0));
        if(currentSpeed().value() > 0.0 && !firstUpdate && !paused)
        {
            QSettings settings;
           elapsed += deltaTime;
           m_watt = (double)watts(settings.value("weight", 75.0).toFloat());
           m_jouls += (m_watt.value() * deltaTime);
        }
        lastTimeUpdate = current;

        // updating the treadmill console every second
        if(sec1Update++ >= (1000 / refresh->interval()))
        {
            if(incompletePackets == false && noConsole == false)
            {
                sec1Update = 0;
                updateDisplay(elapsed.value());
            }
        }
        else
        {
            if(incompletePackets == false)
                writeCharacteristic(noOpData, sizeof(noOpData), "noOp", false, true);
        }

        // byte 3 - 4 = elapsed time
        // byte 17    = inclination
        if(incompletePackets == false)
        {
            if(requestSpeed != -1)
            {
               if(requestSpeed != currentSpeed().value() && requestSpeed >= 0 && requestSpeed <= 22)
               {
                  debug("writing speed " + QString::number(requestSpeed));
                  double inc = Inclination.value();
                  if(requestInclination != -1)
                  {
                      inc = requestInclination;
                      requestInclination = -1;
                  }
                  forceSpeedOrIncline(requestSpeed, inc);
               }
               requestSpeed = -1;
            }
            if(requestInclination != -1)
            {
               if(requestInclination != currentInclination().value() && requestInclination >= 0 && requestInclination <= 15)
               {
                  debug("writing incline " + QString::number(requestInclination));
                  double speed = currentSpeed().value();
                  if(requestSpeed != -1)
                  {
                      speed = requestSpeed;
                      requestSpeed = -1;
                  }
                  forceSpeedOrIncline(speed, requestInclination);
               }
               requestInclination = -1;
            }
            if(requestStart != -1)
            {
               debug("starting...");
               if(lastSpeed == 0.0)
                   lastSpeed = 0.5;
               btinit(true);
               requestStart = -1;
               emit tapeStarted();
            }
            if(requestStop != -1)
            {
                debug("stopping...");
                writeCharacteristic(initDataF0C800B8, sizeof(initDataF0C800B8), "stop tape", false, true);
                requestStop = -1;
            }
            if(requestFanSpeed != -1)
            {
                debug("changing fan speed...");
                sendChangeFanSpeed(requestFanSpeed);
                requestFanSpeed = -1;
            }
            if(requestIncreaseFan != -1)
            {
                debug("increasing fan speed...");
                sendChangeFanSpeed(FanSpeed + 1);
                requestIncreaseFan = -1;
            }
            else if(requestDecreaseFan != -1)
            {
                debug("decreasing fan speed...");
                sendChangeFanSpeed(FanSpeed - 1);
                requestDecreaseFan = -1;
            }
        }

        elevationAcc += (currentSpeed().value() / 3600.0) * 1000.0 * (currentInclination().value() / 100.0) * deltaTime;
    }

    firstUpdate = false;
}

void domyostreadmill::serviceDiscovered(const QBluetoothUuid &gatt)
{
    debug("serviceDiscovered " + gatt.toString());
}

void domyostreadmill::characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue)
{
    //qDebug() << "characteristicChanged" << characteristic.uuid() << newValue << newValue.length();
    QSettings settings;
    QString heartRateBeltName = settings.value("heart_rate_belt_name", "Disabled").toString();
    bool domyos_treadmill_buttons = settings.value("domyos_treadmill_buttons", false).toBool();
    Q_UNUSED(characteristic);
    QByteArray value = newValue;

    debug(" << " + QString::number(value.length()) + " " + value.toHex(' '));

    // for the init packets, the lenght is always less than 20
    // for the display and status packets, the lenght is always grater then 20 and there are 2 cases:
    // - intense run: it always send more than 20 bytes in one packets, so the lenght will be always != 20
    // - t900: it splits packets with lenght grater than 20 in two distinct packets, so the first one it has lenght of 20,
    //         and the second one with the remained byte
    // so this simply condition will match all the cases, excluding the 20byte packet of the T900.
    if(newValue.length() != 20)
    {
        debug("packetReceived!");
        emit packetReceived();
    }

    QByteArray startBytes;
    startBytes.append(0xf0);
    startBytes.append(0xbc);

    QByteArray startBytes2;
    startBytes2.append(0xf0);
    startBytes2.append(0xdb);

    // on some treadmills, the 26bytes has splitted in 2 packets
    if((lastPacket.length() == 20 && lastPacket.startsWith(startBytes) && value.length() == 6) ||
       (lastPacket.length() == 20 && lastPacket.startsWith(startBytes2) && value.length() == 7))
    {
        incompletePackets = false;
        debug("...final bytes received");
        lastPacket.append(value);
        value = lastPacket;
    }

    lastPacket = value;

    if (value.length() != 26)
    {
        // semaphore for any writing packets (for example, update display)
        if(value.length() == 20 && (value.startsWith(startBytes) || value.startsWith(startBytes2)))
        {
            debug("waiting for other bytes...");
            incompletePackets = true;
        }

        debug("packet ignored");
        return;
    }

    if (value.at(22) == 0x06)
    {
        debug("start button pressed!");
        requestStart = 1;
    }
    else if (value.at(22) == 0x07)
    {
        debug("stop button pressed!");
        requestStop = 1;
    }
    else if (value.at(22) == 0x0b)
    {
        debug("increase speed fan pressed!");
        requestIncreaseFan = 1;
    }
    else if (value.at(22) == 0x0a)
    {
        debug("decrease speed fan pressed!");
        requestDecreaseFan = 1;
    }    
    else if(value.at(22) == 0x08)
    {
        debug("increase speed button on console pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(currentSpeed().value() + 0.2);
    }
    else if(value.at(22) == 0x09)
    {
        debug("decrease speed button on console pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(currentSpeed().value() - 0.2);
    }
    else if(value.at(22) == 0x0c)
    {
        debug("increase inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(currentInclination().value() + 0.5);
    }
    else if(value.at(22) == 0x0d)
    {
        debug("decrease inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(currentInclination().value() - 0.5);
    }
    else if(value.at(22) == 0x11)
    {
        debug("22km/h speed button pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(22.0);
    }
    else if(value.at(22) == 0x10)
    {
        debug("16km/h speed button pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(16.0);
    }
    else if(value.at(22) == 0x0f)
    {
        debug("10km/h speed button pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(10.0);
    }
    else if(value.at(22) == 0x0e)
    {
        debug("5km/h speed button pressed!");
        if(domyos_treadmill_buttons)
            changeSpeed(5.0);
    }
    else if(value.at(22) == 0x15)
    {
        debug("15% inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(15.0);
    }
    else if(value.at(22) == 0x14)
    {
        debug("10% inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(10.0);
    }
    else if(value.at(22) == 0x13)
    {
        debug("5% inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(5.0);
    }
    else if(value.at(22) == 0x12)
    {
        debug("0% inclination button on console pressed!");
        if(domyos_treadmill_buttons)
            changeInclination(0.0);
    }

    /*if ((uint8_t)value.at(1) != 0xbc && value.at(2) != 0x04)  // intense run, these are the bytes for the inclination and speed status
        return;*/

    double speed = GetSpeedFromPacket(value);
    double incline = GetInclinationFromPacket(value);
    double kcal = GetKcalFromPacket(value);
    double distance = GetDistanceFromPacket(value);

#ifdef Q_OS_ANDROID
    if(settings.value("ant_heart", false).toBool())
        Heart = (uint8_t)KeepAwakeHelper::heart();
    else
#endif
    {
        if(heartRateBeltName.startsWith("Disabled"))
        {
            uint8_t heart = ((uint8_t)value.at(18));
            if(heart == 0)
            {
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
            lockscreen h;
        long appleWatchHeartRate = h.heartRate();
        Heart = appleWatchHeartRate;
        debug("Current Heart from Apple Watch: " + QString::number(appleWatchHeartRate));
#endif
#endif
            }
            else
                Heart = heart;
        }
    }
    FanSpeed = value.at(23);

    if(!firstCharacteristicChanged)
        DistanceCalculated += ((speed / 3600.0) / ( 1000.0 / (lastTimeCharacteristicChanged.msecsTo(QDateTime::currentDateTime()))));

    debug("Current speed: " + QString::number(speed));
    debug("Current incline: " + QString::number(incline));
    debug("Current heart: " + QString::number(Heart.value()));
    debug("Current KCal: " + QString::number(kcal));
    debug("Current Distance: " + QString::number(distance));
    debug("Current Distance Calculated: " + QString::number(DistanceCalculated));

    if(m_control->error() != QLowEnergyController::NoError)
        qDebug() << "QLowEnergyController ERROR!!" << m_control->errorString();

    if(Speed.value() != speed)
    {        
        emit speedChanged(speed);
    }
    Speed = speed;
    if(Inclination.value() != incline)
    {        
        emit inclinationChanged(incline);
    }
    Inclination = incline;

    KCal = kcal;
    Distance = distance;    

    if(speed > 0)
    {
        lastSpeed = speed;
        lastInclination = incline;
    }

    lastTimeCharacteristicChanged = QDateTime::currentDateTime();
    firstCharacteristicChanged = false;
}

double domyostreadmill::GetSpeedFromPacket(QByteArray packet)
{
    uint8_t convertedData = (uint8_t)packet.at(7);
    double data = (double)convertedData / 10.0f;
    return data;
}

double domyostreadmill::GetKcalFromPacket(QByteArray packet)
{
    uint16_t convertedData = (packet.at(10) << 8) | ((uint8_t)packet.at(11));
    return (double)convertedData;
}

double domyostreadmill::GetDistanceFromPacket(QByteArray packet)
{
    uint16_t convertedData = (packet.at(12) << 8) | packet.at(13);
    double data = ((double)convertedData) / 10.0f;
    return data;
}

double domyostreadmill::GetInclinationFromPacket(QByteArray packet)
{
    uint16_t convertedData = (packet.at(2) << 8) | packet.at(3);
    double data;

    if(convertedData > 10000)
        data = ((double)convertedData - 65512.0f) / 10.0f;
    else
        data = ((double)convertedData - 1000.0f) / 10.0f;
    if (data < 0) return 0;
    return data;
}

void domyostreadmill::btinit(bool startTape)
{
    writeCharacteristic(initData1, sizeof(initData1), "init", false, true);
    writeCharacteristic(initData2, sizeof(initData2), "init", false, true);
    writeCharacteristic(initDataStart, sizeof(initDataStart), "init", false, true);
    writeCharacteristic(initDataStart2, sizeof(initDataStart2), "init", false, true);
    writeCharacteristic(initDataStart3, sizeof(initDataStart3), "init", false, true);
    writeCharacteristic(initDataStart4, sizeof(initDataStart4), "init", false, true);
    writeCharacteristic(initDataStart5, sizeof(initDataStart5), "init", false, true);
    //writeCharacteristic(initDataStart6, sizeof(initDataStart6), "init", false, false);
    //writeCharacteristic(initDataStart7, sizeof(initDataStart7), "init", false, true);
    forceSpeedOrIncline(lastSpeed, lastInclination);

    writeCharacteristic(initDataStart8, sizeof(initDataStart8), "init", false, false);
    writeCharacteristic(initDataStart9, sizeof(initDataStart9), "init", false, true);    
    if(startTape)
    {
        writeCharacteristic(initDataStart10, sizeof(initDataStart10), "init", false, false);
        writeCharacteristic(initDataStart11, sizeof(initDataStart11), "init", false, true);
        writeCharacteristic(initDataStart12, sizeof(initDataStart12), "init", false, false);
        writeCharacteristic(initDataStart13, sizeof(initDataStart13), "init", false, true);

        forceSpeedOrIncline(lastSpeed, lastInclination);
    }

    initDone = true;
}

void domyostreadmill::stateChanged(QLowEnergyService::ServiceState state)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<QLowEnergyService::ServiceState>();
    debug("BTLE stateChanged " + QString::fromLocal8Bit(metaEnum.valueToKey(state)));
    if(state == QLowEnergyService::ServiceDiscovered)
    {
	    //qDebug() << gattCommunicationChannelService->characteristics();

	    gattWriteCharacteristic = gattCommunicationChannelService->characteristic(_gattWriteCharacteristicId);
	    gattNotifyCharacteristic = gattCommunicationChannelService->characteristic(_gattNotifyCharacteristicId);
        Q_ASSERT(gattWriteCharacteristic.isValid());
        Q_ASSERT(gattNotifyCharacteristic.isValid());

	    // establish hook into notifications
	    connect(gattCommunicationChannelService, SIGNAL(characteristicChanged(QLowEnergyCharacteristic,QByteArray)),
        	    this, SLOT(characteristicChanged(QLowEnergyCharacteristic,QByteArray)));
        connect(gattCommunicationChannelService, SIGNAL(characteristicWritten(const QLowEnergyCharacteristic, const QByteArray)),
                this, SLOT(characteristicWritten(const QLowEnergyCharacteristic, const QByteArray)));
        connect(gattCommunicationChannelService, SIGNAL(error(QLowEnergyService::ServiceError)),
                this, SLOT(errorService(QLowEnergyService::ServiceError)));
        connect(gattCommunicationChannelService, SIGNAL(descriptorWritten(const QLowEnergyDescriptor, const QByteArray)), this,
                SLOT(descriptorWritten(const QLowEnergyDescriptor, const QByteArray)));

	    QByteArray descriptor;
	    descriptor.append((char)0x01);
	    descriptor.append((char)0x00);
	    gattCommunicationChannelService->writeDescriptor(gattNotifyCharacteristic.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration), descriptor);
    }
}

void domyostreadmill::descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue)
{
    debug("descriptorWritten " + descriptor.name() + " " + newValue.toHex(' '));

    initRequest = true;
    emit connectedAndDiscovered();
}

void domyostreadmill::characteristicWritten(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue)
{
    Q_UNUSED(characteristic);
    debug("characteristicWritten " + newValue.toHex(' '));
}

void domyostreadmill::serviceScanDone(void)
{
    debug("serviceScanDone");

    gattCommunicationChannelService = m_control->createServiceObject(_gattCommunicationChannelServiceId);
    connect(gattCommunicationChannelService, SIGNAL(stateChanged(QLowEnergyService::ServiceState)), this, SLOT(stateChanged(QLowEnergyService::ServiceState)));
    gattCommunicationChannelService->discoverDetails();
}

void domyostreadmill::errorService(QLowEnergyService::ServiceError err)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<QLowEnergyService::ServiceError>();
    debug("domyostreadmill::errorService " + QString::fromLocal8Bit(metaEnum.valueToKey(err)) + m_control->errorString());
}

void domyostreadmill::error(QLowEnergyController::Error err)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<QLowEnergyController::Error>();
    debug("domyostreadmill::error " + QString::fromLocal8Bit(metaEnum.valueToKey(err)) + m_control->errorString());
}

void domyostreadmill::deviceDiscovered(const QBluetoothDeviceInfo &device)
{
    {
        bluetoothDevice = device;
        m_control = QLowEnergyController::createCentral(bluetoothDevice, this);
        connect(m_control, SIGNAL(serviceDiscovered(const QBluetoothUuid &)),
                this, SLOT(serviceDiscovered(const QBluetoothUuid &)));
        connect(m_control, SIGNAL(discoveryFinished()),
                this, SLOT(serviceScanDone()));
        connect(m_control, SIGNAL(error(QLowEnergyController::Error)),
                this, SLOT(error(QLowEnergyController::Error)));
        connect(m_control, SIGNAL(stateChanged(QLowEnergyController::ControllerState)), this, SLOT(controllerStateChanged(QLowEnergyController::ControllerState)));

        connect(m_control, static_cast<void (QLowEnergyController::*)(QLowEnergyController::Error)>(&QLowEnergyController::error),
                this, [this](QLowEnergyController::Error error) {
            Q_UNUSED(error);
            Q_UNUSED(this);
            debug("Cannot connect to remote device.");
            searchStopped = false;
            emit disconnected();
        });
        connect(m_control, &QLowEnergyController::connected, this, [this]() {
            Q_UNUSED(this);
            debug("Controller connected. Search services...");
            m_control->discoverServices();
        });
        connect(m_control, &QLowEnergyController::disconnected, this, [this]() {
            Q_UNUSED(this);
            debug("LowEnergy controller disconnected");
            searchStopped = false;
            emit disconnected();
        });

        // Connect
        m_control->connectToDevice();
        return;
    }
}

void domyostreadmill::controllerStateChanged(QLowEnergyController::ControllerState state)
{
    qDebug() << "controllerStateChanged" << state;
    if(state == QLowEnergyController::UnconnectedState && m_control)
    {
        qDebug() << "trying to connect back again...";
        initDone = false;
        m_control->connectToDevice();
    }
}

bool domyostreadmill::connected()
{
    if(!m_control)
        return false;
    return m_control->state() == QLowEnergyController::DiscoveredState;
}

void* domyostreadmill::VirtualTreadMill()
{
    return virtualTreadMill;
}

void* domyostreadmill::VirtualDevice()
{
    return VirtualTreadMill();
}

double domyostreadmill::odometer()
{
    return DistanceCalculated;
}

void domyostreadmill::setLastSpeed(double speed)
{
    lastSpeed = speed;
}

void domyostreadmill::setLastInclination(double inclination)
{
    lastInclination = inclination;
}

void domyostreadmill::searchingStop()
{
    searchStopped = true;
}
