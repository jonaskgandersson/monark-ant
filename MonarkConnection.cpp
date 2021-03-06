/*
 * Copyright (c) 2015 Erik Botö (erik.boto@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "MonarkConnection.h"

#include <QByteArray>
#include <QDebug>
#include <QtSerialPort/QSerialPortInfo>

MonarkConnection::MonarkConnection() :
    m_serial(0),
    m_pollInterval(1000),
    m_timer(0),
    m_canControlPower(false),
    m_load(0),
    m_loadToWrite(0),
    m_shouldWriteLoad(false)
{
}

void MonarkConnection::setSerialPort(const QString serialPortName)
{
    if (! this->isRunning())
    {
        m_serialPortName = serialPortName;
    } else {
        qWarning() << "MonarkConnection: Cannot set serialPortName while running";
    }
}

void MonarkConnection::setPollInterval(int interval)
{
    if (interval != m_pollInterval)
    {
        m_pollInterval = interval;
        m_timer->setInterval(m_pollInterval);
    }
}

int MonarkConnection::pollInterval()
{
    return m_pollInterval;
}

/**
 * Private function that reads a complete reply and prepares if for
 * processing by replacing \r with \0
 */
QByteArray MonarkConnection::readAnswer(int timeoutMs)
{
    QByteArray data;

    do
    {
        if (m_serial->waitForReadyRead(timeoutMs))
        {
            data.append(m_serial->readAll());
        } else {
            data.append('\r');
        }
    } while (data.indexOf('\r') == -1);

    data.replace("\r", "\0");
    return data;
}

/**
 * QThread::run()
 *
 * Open the serial port and set it up, then starts polling.
 *
 */
void MonarkConnection::run()
{
    // Open and configure serial port
    m_serial = new QSerialPort();

    m_startupTimer = new QTimer();
    m_startupTimer->setInterval(200);
    m_startupTimer->setSingleShot(true);
    m_startupTimer->start();

    m_timer = new QTimer();

    connect(m_startupTimer, SIGNAL(timeout()), this, SLOT(identifySerialPort()), Qt::DirectConnection);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(requestAll()), Qt::DirectConnection);

    qDebug() << "Started Monark Thread";
    exec();
}

void MonarkConnection::requestAll()
{
    // If something else is blocking mutex, don't start another round of requests
    if (! m_mutex.tryLock())
        return;

    requestPower();
    requestPulse();
    requestCadence();

    if ((m_loadToWrite != m_load) && m_canControlPower)
    {
        QString cmd = QString("power %1\r").arg(m_loadToWrite);
        m_serial->write(cmd.toStdString().c_str());
        if (!m_serial->waitForBytesWritten(500))
        {
            // failure to write to device, bail out
            emit connectionStatus(false);
            m_startupTimer->start();
        }
        m_load = m_loadToWrite;
        QByteArray data = m_serial->readAll();
    }

    m_mutex.unlock();
}

void MonarkConnection::requestPower()
{
    // Always empty read buffer first
    m_serial->readAll();

    m_serial->write("power\r");
    if (!m_serial->waitForBytesWritten(500))
    {
        // failure to write to device, bail out
        emit connectionStatus(false);
        m_startupTimer->start();
    }
    QByteArray data = readAnswer(500);
    quint16 p = data.toInt();
    emit power(p);
}

void MonarkConnection::requestPulse()
{
    // Always empty read buffer first
    m_serial->readAll();

    m_serial->write("pulse\r");
    if (!m_serial->waitForBytesWritten(500))
    {
        // failure to write to device, bail out
        emit connectionStatus(false);
        m_startupTimer->start();
    }
    QByteArray data = readAnswer(500);
    quint8 p = data.toInt();
    emit pulse(p);
}

void MonarkConnection::requestCadence()
{
    // Always empty read buffer first
    m_serial->readAll();

    m_serial->write("pedal\r");
    if (!m_serial->waitForBytesWritten(500))
    {
        // failure to write to device, bail out

        m_startupTimer->start();
    }
    QByteArray data = readAnswer(500);
    quint8 c = data.toInt();
    emit cadence(c);
}

void MonarkConnection::identifyModel()
{
    QString servo = "";

    m_serial->write("id\r");
    if (!m_serial->waitForBytesWritten(500))
    {
        // failure to write to device, bail out
        emit connectionStatus(false);
        m_startupTimer->start();
    }
    QByteArray data = readAnswer(500);
    m_id = QString(data);

    if (m_id.toLower().startsWith("novo"))
    {
        m_serial->write("servo\r");
        if (!m_serial->waitForBytesWritten(500))
        {
            // failure to write to device, bail out
            emit connectionStatus(false);
            m_startupTimer->start();
        }
        QByteArray data = readAnswer(500);
        servo = QString(data);
    }


    qDebug() << "Connected to bike: " << m_id;
    qDebug() << "Servo: : " << servo;

    if (m_id.toLower().startsWith("lc"))
    {
        m_canControlPower = true;
        setLoad(100);
    } else if (m_id.toLower().startsWith("novo") && servo != "manual") {
        m_canControlPower = true;
        setLoad(100);
    }
}

void MonarkConnection::setLoad(unsigned int load)
{
    m_loadToWrite = load;
    m_shouldWriteLoad = true;
}

/*
 * Configures a serialport for communicating with a Monark bike.
 */
void MonarkConnection::configurePort(QSerialPort *serialPort)
{
    if (!serialPort)
    {
        qFatal("Trying to configure null port, start debugging.");
    }
    serialPort->setBaudRate(QSerialPort::Baud4800);
    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::SoftwareControl);
    serialPort->setParity(QSerialPort::NoParity);

    // Send empty \r after configuring port, otherwise first command might not
    // be interpreted correctly
    serialPort->write("\r");
}

/**
 * This functions takes a serial port and tries if it can find a Monark bike connected
 * to it.
 */
bool MonarkConnection::discover(QString portName)
{
    bool found = false;
    QSerialPort sp;

    sp.setPortName(portName);

    if (sp.open(QSerialPort::ReadWrite))
    {
        configurePort(&sp);

        // Discard any existing data
        QByteArray data = sp.readAll();

        // Read id from bike
        sp.write("id\r");
        sp.waitForBytesWritten(2000);

        QByteArray id;
        do
        {
            bool readyToRead = sp.waitForReadyRead(1000);
            if (readyToRead)
            {
                id.append(sp.readAll());
            } else {
                id.append('\r');
            }
        } while ((id.indexOf('\r') == -1));

        id.replace("\r", "\0");

        // Should check for all bike ids known to use this protocol
        if (QString(id).toLower().contains("lt") ||
            QString(id).toLower().contains("lc") ||
            QString(id).toLower().contains("novo")) {
            found = true;
        }
    }

    sp.close();

    return found;
}

void MonarkConnection::identifySerialPort()
{
    qDebug() << __func__;

    bool found = false;

    // Make sure the port is closed to start with
    m_serial->close();

    m_timer->stop();

    do {
        qDebug() << "Refreshing list of serial ports...";
        QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
        foreach (QSerialPortInfo port, ports)
        {
#ifdef RASPBERRYPI
            if (port.systemLocation() == "/dev/ttyAMA0")
                continue;
#endif
            qDebug() << "Looking for Monark at " << port.systemLocation();
            if (discover(port.systemLocation()))
            {
                // found monark
                qDebug() << "FOUND!";
                m_serialPortName = port.systemLocation();
                found = true;
                break;
            }
        }
        msleep(500);
    } while (!found);

    m_serial->setPortName(m_serialPortName);

    if (!m_serial->open(QSerialPort::ReadWrite))
    {
        qDebug() << "Error opening serial";
        m_startupTimer->start();
    } else {
        configurePort(m_serial);

        // Discard any existing data
        QByteArray data = m_serial->readAll();
    }

    identifyModel();

    m_timer->setInterval(1000);
    m_timer->start();

    emit connectionStatus(true);
}
