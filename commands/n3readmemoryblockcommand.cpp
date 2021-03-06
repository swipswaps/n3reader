#include "n3readmemoryblockcommand.h"
#include "n3readerhelper.h"
#include <QString>
#include <QByteArray>
#include <QTimer>

const QByteArray N3ReadMemoryBlockCommand::acknowledgementString = QByteArray::fromHex("31 35");

N3ReadMemoryBlockCommand::N3ReadMemoryBlockCommand(quint32 address,
                                                   qint16 length,
                                                   qint16 resultLength,
                                                    QObject *parent = 0) :
    N3ReaderBaseCommand(parent),
    resultPresentationQuantum(resultLength),
    memoryBlockAddress(address),
    memoryBlockLength(length),
    outDataSendTimer(new QTimer(this))
{
    inBuffer.reserve(memoryBlockLength);

    outDataSendTimer->setInterval(N3ReadMemoryBlockCommand::msByteSendingDelay);
    connect(outDataSendTimer, SIGNAL(timeout()), this, SLOT(onOutDataSendTimerShot()));
}

void N3ReadMemoryBlockCommand::execute()
{
    QByteArray packet(9, 0);
    QByteArray ba_memoryBlockAddress = N3ReaderHelper::UIntToBytes(memoryBlockAddress);
    QByteArray ba_memoryBlockLength = N3ReaderHelper::UIntToBytes(memoryBlockLength);
    //length
    packet[0] = 7;
    //cmd code
    packet[1] = 0xA0;
    //memory address
    packet[2] = ba_memoryBlockAddress[0];
    packet[3] = ba_memoryBlockAddress[1];
    packet[4] = ba_memoryBlockAddress[2];
    packet[5] = ba_memoryBlockAddress[3];
    //memory block length
    packet[6] = ba_memoryBlockLength[0];
    packet[7] = ba_memoryBlockLength[1];
    packet[8] = N3ReaderHelper::calculateChecksum(packet);

    outBuffer = reader()->encryptPacket(packet);
    outBufferPosition = 0;
    m_state = CommandState::awaitingAcknowledgementStart;
    inBuffer.clear();
    rawData.clear();
    rawData.reserve(memoryBlockReadQuantum);
    outDataSendTimer->start();
}

inline void appendBufferOnFirstCall(QByteArray &buffer, const QByteArray &data, bool &doAppend)
{
    if (doAppend) {
        buffer.append(data);
        doAppend = false;
    }
}

void N3ReadMemoryBlockCommand::processPacket(const QByteArray &data)
{
    int index = 0;
    bool appendInBuffer = true;

    //first, need to receive acknowledgement
    if (m_state==CommandState::awaitingAcknowledgementStart
        && (index=data.indexOf(acknowledgementString[0]) != -1)) {
            m_state = CommandState::receivingAcknowledgement;
            inBuffer = data.mid(index-1);
            appendInBuffer = false;
    }

    if (m_state==CommandState::receivingAcknowledgement) {
        appendBufferOnFirstCall(inBuffer, data, appendInBuffer);
        if (inBuffer.size() >= acknowledgementString.size()) {
            if (inBuffer.startsWith(acknowledgementString)) {
                m_state = CommandState::receivingData;
                inBuffer = inBuffer.mid(acknowledgementString.size());
            } else {
                m_state = CommandState::badAcknowledgement;
                return;
            }
        }
    }

    //process data after acknowledgement received
    if (m_state==CommandState::receivingData) {
        appendBufferOnFirstCall(inBuffer, data, appendInBuffer);
        if (inBuffer.size()>=memoryBlockReadQuantum) {

            emit sendPacket(QByteArray::fromHex("31"));

            QByteArray dataReceived = reader()->decryptPacket(inBuffer);
            inBuffer.clear();

            if (!bytesReceived) {
                dataReceived = dataReceived.mid(4);
            }

            if (bytesReceived + dataReceived.size() > memoryBlockLength) {
                rawData.append(dataReceived.left(memoryBlockLength-bytesReceived));
                bytesReceived = memoryBlockLength;
            } else {
                bytesReceived += dataReceived.size();
                rawData.append(dataReceived);
            }

            if (bytesReceived >= memoryBlockLength
                    || rawData.size() >= resultPresentationQuantum) {

                while (rawData.size() >= resultPresentationQuantum) {
                    rawDataReady();
                    if (rawData.size() > resultPresentationQuantum) {
                        rawData = rawData.mid(resultPresentationQuantum);
                    } else {
                        rawData.clear();
                    }
                }

                if (bytesReceived >= memoryBlockLength) {
                    emit commandFinished();
                }
            }
        }
    }
}

void N3ReadMemoryBlockCommand::rawDataReady()
{
    emit rawDataAvailable(rawData);
}

void N3ReadMemoryBlockCommand::setMemoryBlockAddress(const quint32 &address)
{
    memoryBlockAddress = address;
}

void N3ReadMemoryBlockCommand::setMemoryBlockLength(const quint32 &length)
{
    memoryBlockLength = length;
}

quint32 N3ReadMemoryBlockCommand::getMemoryBlockAddress() const
{
    return memoryBlockAddress;
}

quint32 N3ReadMemoryBlockCommand::getMemoryBlockLength() const
{
    return memoryBlockLength;
}

void N3ReadMemoryBlockCommand::onOutDataSendTimerShot()
{
    if (outBufferPosition<outBuffer.size()){
        emit sendPacket(outBuffer.mid(outBufferPosition, 1));
        ++outBufferPosition;
    } else {
        outDataSendTimer->stop();
        outBufferPosition=0;
        outBuffer.clear();
    }
}

const QString &N3ReadMemoryBlockCommand::commandName() const
{
    static const QString name("read_memory_block");
    return name;
}

const QString &N3ReadMemoryBlockCommand::description() const
{
    static const QString name("Reads memory block (internal use)");
    return name;
}
