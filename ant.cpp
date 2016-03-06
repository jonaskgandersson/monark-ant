#include "ant.h"

#include <QDebug>

#include "antmessage.h"

ANT::ANT() :
    m_usb(0),
    m_state(ST_WAIT_FOR_SYNC)
{

}

void ANT::run()
{

    m_usb = new LibUsb(TYPE_ANT);
    m_pd = new PowerDevice(m_usb,1);

    qDebug() << "Starting ANT thread";
    qDebug() << "Found stick? "  << m_usb->find();
    qDebug() << "Open stick? " << m_usb->open();

    const unsigned char key[8] = { 0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45 };

    // Set ANT+ network key for network 0
    ANTMessage mess(9, ANT_SET_NETWORK, 0, key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
    m_usb->write((char *)mess.data,mess.length);

    msleep(100);

    m_pd->configureChannel();

    while(1)
    {
        // read more bytes from the device
        uint8_t byte;
        if (m_usb->read((char *)&byte, 1) > 0) receiveByte((unsigned char)byte);
        else msleep(5);
    }
}

void ANT::receiveByte(unsigned char byte) {

    switch (m_state) {
    case ST_WAIT_FOR_SYNC:
        if (byte == ANT_SYNC_BYTE) {
            m_state = ST_GET_LENGTH;
            checksum = ANT_SYNC_BYTE;
            rxMessage[0] = byte;
        }
        break;

    case ST_GET_LENGTH:
        if ((byte == 0) || (byte > ANT_MAX_LENGTH)) {
            m_state = ST_WAIT_FOR_SYNC;
        }
        else {
            rxMessage[ANT_OFFSET_LENGTH] = byte;
            checksum ^= byte;
            length = byte;
            bytes = 0;
            m_state = ST_GET_MESSAGE_ID;
        }
        break;

    case ST_GET_MESSAGE_ID:
        rxMessage[ANT_OFFSET_ID] = byte;
        checksum ^= byte;
        m_state = ST_GET_DATA;
        break;

    case ST_GET_DATA:
        rxMessage[ANT_OFFSET_DATA + bytes] = byte;
        checksum ^= byte;
        if (++bytes >= length){
            m_state = ST_VALIDATE_PACKET;
        }
        break;

    case ST_VALIDATE_PACKET:
        if (checksum == byte){
            processMessage();
        }
        m_state = ST_WAIT_FOR_SYNC;
        break;
    }
}

void ANT::processMessage()
{

    fprintf(stderr, "Recv: ");
    for (int i=0; i<=rxMessage[ANT_OFFSET_LENGTH]; ++i)
    {
        fprintf(stderr, "[%02x]", rxMessage[ANT_OFFSET_DATA+i]);
    }

   fprintf(stderr, "\n");

    switch (rxMessage[ANT_OFFSET_ID]) {
    case ANT_NOTIF_STARTUP:
        break;
    case ANT_ACK_DATA:
    case ANT_BROADCAST_DATA:
    case ANT_CHANNEL_STATUS:
    case ANT_CHANNEL_ID:
    case ANT_BURST_DATA:
        handleChannelEvent();
        break;

    case ANT_CHANNEL_EVENT:
        switch (rxMessage[ANT_OFFSET_MESSAGE_CODE]) {
        case EVENT_TRANSFER_TX_FAILED:
            break;
        case EVENT_TRANSFER_TX_COMPLETED:
            // fall through
        default:
            handleChannelEvent();
        }
        break;

    case ANT_VERSION:
        break;

    case ANT_CAPABILITIES:
        break;

    case ANT_SERIAL_NUMBER:
        break;

    default:
        break;
    }
}

//
// Pass inbound message to channel for handling
//
void ANT::handleChannelEvent(void) {
    int channels = 8; // depending on stick, mine is 8 channels
    int channel = rxMessage[ANT_OFFSET_DATA] & 0x7;
    if(channel >= 0 && channel < channels) {

        // handle a channel event here!
        //antChannel[channel]->receiveMessage(rxMessage);
        //qDebug() << "Channel event on channel: " << channel;
        receiveChannelMessage(rxMessage);
    }
}


void ANT::receiveChannelMessage(unsigned char *ant_message)
{
    switch (ant_message[2]) {
    case ANT_CHANNEL_EVENT:
        m_pd->channelEvent(ant_message);
        break;
    case ANT_BROADCAST_DATA:
        //broadcastEvent(ant_message);
        qDebug()<<"Channel broadcast event:";
        break;
    case ANT_ACK_DATA:
        //ackEvent(ant_message);
        qDebug()<<"Channel ack data";
        m_pd->handleAckData(ant_message);

        break;
    case ANT_CHANNEL_ID:
        //channelId(ant_message);
        qDebug()<<"Channel id";
        break;
    case ANT_BURST_DATA:
        //burstData(ant_message);
        qDebug() << "Channel burst data";
        break;
    default:
        //qDebug()<<"dunno?"<<number;
        break; //errors silently ignored for now, would indicate hardware fault.
    }
}

void ANT::setCurrentPower(quint16 power)
{
    if (m_pd)
    {
        m_pd->setCurrentPower(power);
    }
}

void ANT::setCurrentCadence(quint8 cadence)
{
    if (m_pd)
    {
        m_pd->setCurrentCadence(cadence);
    }
}