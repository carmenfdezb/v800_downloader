#include "v800usb.h"

#include <QFile>
#include <QDateTime>
#include <QStringList>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <stdio.h>

#include "native_usb.h"

V800usb::V800usb(QObject *parent) :
    QObject(parent)
{
    usb = NULL;
}

V800usb::~V800usb()
{
    if(usb != NULL)
    {
        usb->close_usb();
        delete usb;
    }
}

void V800usb::start()
{
    usb = new native_usb();
    if(usb->open_usb(0x0da4, 0x0008) != -1)
    {
        get_all_sessions();
        emit ready();
    }
    else
    {
        emit not_ready();
    }
}

void V800usb::get_all_sessions()
{
    QList<QString> dates, times, files, sessions;
    int dates_iter, times_iter, files_iter;
    bool session_exists = false;

    dates = get_all_dates();
    for(dates_iter = 0; dates_iter < dates.length(); dates_iter++)
    {
        times.clear();
        times = get_all_times(dates[dates_iter]);
        for(times_iter = 0; times_iter < times.length(); times_iter++)
        {
            files.clear();
            files = get_all_files(dates[dates_iter], times[times_iter]);
            for(files_iter = 0; files_iter < files.length(); files_iter++)
            {
                if(QString(files[files_iter]).compare(tr("ROUTE.GZB")) == 0)
                {
                    session_exists = true;
                    break;
                }
            }

            if(session_exists)
            {
                QString date(dates[dates_iter]);
                QString time(times[times_iter]);

                QString combined((QString(tr("%1%2")).arg(date).arg(time)));
                QDateTime session_time = QDateTime::fromString(combined, tr("yyyyMMdd/HHmmss/"));

                sessions.append(session_time.toString(Qt::TextDate));
                session_exists = false;
            }
        }
    }

    emit all_sessions(sessions);
}

void V800usb::get_sessions(QList<QString> sessions, QString save_dir, bool bipolar_output)
{
    QString session;
    QStringList session_split;
    QList<QString> files;
    QDateTime session_time;
    int session_iter, files_iter;

    this->save_dir = save_dir;
    this->bipolar_output = bipolar_output;

    for(session_iter = 0; session_iter < sessions.length(); session_iter++)
    {
        session_time = QDateTime::fromString(sessions.at(session_iter), Qt::TextDate);
        session = session_time.toString(tr("yyyyMMdd/HHmmss"));
        session_split = session.split(tr("/"));

        if(session_split.length() == 2)
        {
            files = get_all_files(session_split[0], session_split[1]);
            for(files_iter = 0; files_iter < files.length(); files_iter++)
                get_file(session_split[0], session_split[1], files[files_iter], SESSION_DATA);
            get_session_info(session_split[0], session_split[1]);

            emit session_done();
        }
    }

    emit sessions_done();
}

void V800usb::get_session_info(QString date, QString time)
{
    get_file(date, time, tr("TSESS.BPB"), SESSION_INFO);
    get_file(date, time, tr("PHYSDATA.BPB"), SESSION_INFO);
}

QList<QString> V800usb::get_all_dates()
{
    QList<QString> dates;
    QByteArray packet, full;
    int cont = 1, usb_state = 0, packet_num = 0, initial_packet = 1;

    while(cont)
    {
        // usb state machine for reading
        switch(usb_state)
        {
        case 0: // send a command to the watch
            packet.clear();
            packet = generate_request(tr("/U/0/"));

            usb->write_usb(packet);

            packet_num = 0;
            usb_state = 1;
            break;
        case 1: // we want to read the buffer now
            packet.clear();
            packet = usb->read_usb();

            // check for end of buffer
            if(is_end(packet))
            {
                full = add_to_full(packet, full, initial_packet, true);
                usb_state = 4;
            }
            else
            {
                full = add_to_full(packet, full, initial_packet, false);
                usb_state = 2;
            }

            // initial packet seems to always have two extra bytes in the front, 0x00 0x00
            if(initial_packet)
                initial_packet = false;
            break;
        case 2: // send an ack packet
            packet.clear();
            packet = generate_ack(packet_num);
            if(packet_num == 0xff)
                packet_num = 0x00;
            else
                packet_num++;

            usb->write_usb(packet);

            usb_state = 1;
            break;
        case 4:
            qDebug("Done getting dates from V800");

            dates = extract_dir_and_files(full);

            cont = 0;
            break;
        }
    }

    return dates;
}

QList<QString> V800usb::get_all_times(QString date)
{
    QList<QString> times;
    QByteArray packet, full;
    int cont = 1, usb_state = 0, packet_num = 0, initial_packet = 1;

    while(cont)
    {
        // usb state machine for reading
        switch(usb_state)
        {
        case 0: // send a command to the watch
            packet.clear();
            packet = generate_request(QString(tr("/U/0/%1/E/")).arg(date));

            usb->write_usb(packet);

            packet_num = 0;
            usb_state = 1;
            break;
        case 1: // we want to read the buffer now
            packet.clear();
            packet = usb->read_usb();

            // check for end of buffer
            if(is_end(packet))
            {
                full = add_to_full(packet, full, initial_packet, true);
                usb_state = 4;
            }
            else
            {
                full = add_to_full(packet, full, initial_packet, false);
                usb_state = 2;
            }

            // initial packet seems to always have two extra bytes in the front, 0x00 0x00
            if(initial_packet)
                initial_packet = false;
            break;
        case 2: // send an ack packet
            packet.clear();
            packet = generate_ack(packet_num);
            if(packet_num == 0xff)
                packet_num = 0x00;
            else
                packet_num++;

            usb->write_usb(packet);

            usb_state = 1;
            break;
        case 4:
            qDebug("Done getting times from V800");

            times = extract_dir_and_files(full);

            cont = 0;
            break;
        }
    }

    return times;
}

QList<QString> V800usb::get_all_files(QString date, QString time)
{
    QList<QString> files;
    QByteArray packet, full;
    int cont = 1, usb_state = 0, packet_num = 0, initial_packet = 1;

    while(cont)
    {
        // usb state machine for reading
        switch(usb_state)
        {
        case 0: // send a command to the watch
            packet.clear();
            packet = generate_request(QString(tr("/U/0/%1/E/%2/00/")).arg(date).arg(time));

            usb->write_usb(packet);

            packet_num = 0;
            usb_state = 1;
            break;
        case 1: // we want to read the buffer now
            packet.clear();
            packet = usb->read_usb();

            // check for end of buffer
            if(is_end(packet))
            {
                full = add_to_full(packet, full, initial_packet, true);
                usb_state = 4;
            }
            else
            {
                full = add_to_full(packet, full, initial_packet, false);
                usb_state = 2;
            }

            // initial packet seems to always have two extra bytes in the front, 0x00 0x00
            if(initial_packet)
                initial_packet = false;
            break;
        case 2: // send an ack packet
            packet.clear();
            packet = generate_ack(packet_num);
            if(packet_num == 0xff)
                packet_num = 0x00;
            else
                packet_num++;

            usb->write_usb(packet);

            usb_state = 1;
            break;
        case 4:
            qDebug("Done getting files from V800");

            files = extract_dir_and_files(full);

            cont = 0;
            break;
        }
    }

    return files;
}

void V800usb::get_file(QString date, QString time, QString file, int type)
{   
    QFile *out_file;
    QByteArray packet, full;
    int cont = 1, usb_state = 0, packet_num = 0;
    bool initial_packet = true;

    //qDebug("Date: %s\nTime: %s\nFile: %s", date.toUtf8(), time.toUtf8(), file.toUtf8());

    while(cont)
    {
        // usb state machine for reading
        switch(usb_state)
        {
        case 0: // send a command to the watch
            packet.clear();
            if(type == SESSION_DATA)
                packet = generate_request(QString(tr("/U/0/%1/E/%2/00/%3")).arg(date).arg(time).arg(file));
            else if(type == SESSION_INFO)
                packet = generate_request(QString(tr("/U/0/%1/E/%2/%3")).arg(date).arg(time).arg(file));

            usb->write_usb(packet);

            packet_num = 0;
            usb_state = 1;
            break;
        case 1: // we want to read the buffer now
            packet.clear();
            packet = usb->read_usb();

            // check for end of buffer
            if(is_end(packet))
            {
                full = add_to_full(packet, full, initial_packet, true);
                usb_state = 4;
            }
            else
            {
                full = add_to_full(packet, full, initial_packet, false);
                usb_state = 2;
            }

            // initial packet seems to always have two extra bytes in the front, 0x00 0x00
            if(initial_packet)
                initial_packet = false;
            break;
        case 2: // send an ack packet
            packet.clear();
            packet = generate_ack(packet_num);
            if(packet_num == 0xff)
                packet_num = 0x00;
            else
                packet_num++;

            usb->write_usb(packet);

            usb_state = 1;
            break;
        case 4:
            QString session_path = QDir::toNativeSeparators(QString(tr("%1/%2/%3")).arg(save_dir).arg(date).arg(time));
            QString session_full_name = QDir::toNativeSeparators(QString(tr("%1/%2/%3/%4")).arg(save_dir).arg(date).arg(time).arg(file));

            QDir session_dir = QDir(session_path);

            if(!session_dir.exists())
                session_dir.mkpath(tr("."));

            out_file = new QFile(session_full_name);
            out_file->open(QIODevice::WriteOnly);
            out_file->write(full);
            out_file->close();

            if(bipolar_output)
            {
                QDir bipolar_dir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QString(tr("/Polar/PolarFlowSync/export")));
                QString tag = QDateTime(QDate::fromString(date, tr("yyyyMMdd")), QTime::fromString(time, tr("HHmmss"))).toString(tr("yyyyMMddhhmmss"));

                QString bipolar_dest(tr(""));
                if(QString(file).compare(tr("TSESS.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-create")).arg(bipolar_dir.absolutePath()).arg(tag));
                else if(QString(file).compare(tr("PHYSDATA.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-physical-information")).arg(bipolar_dir.absolutePath()).arg(tag));
                else if(QString(file).compare(tr("BASE.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-create")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("ALAPS.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-autolaps")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("LAPS.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-laps")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("ROUTE.GZB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-route")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("SAMPLES.GZB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-samples")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("STATS.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-statistics")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("ZONES.BPB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-zones")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));
                else if(QString(file).compare(tr("RR.GZB")) == 0)
                    bipolar_dest = (QString(tr("%1/v2-users-0000000-training-sessions-%2-exercises-%3-rrsamples")).arg(bipolar_dir.absolutePath()).arg(tag).arg(tag));

                qDebug("Path: %s", bipolar_dest.toUtf8().constData());

                if(bipolar_dest != tr(""))
                    QFile::copy(session_full_name, QDir::toNativeSeparators(bipolar_dest));
            }

            qDebug("Done getting file from V800");

            cont = 0;
            break;
        }
    }

}

QList<QString> V800usb::extract_dir_and_files(QByteArray full)
{
    QList<QString> dir_and_files;
    int full_state = 0, size = 0, loc = 0;

    while(loc < full.length())
    {
        switch(full_state)
        {
        case 0: /* look for 0x0A */
            if(full[loc] == (char)0x0A)
            {
                loc++;
                full_state = 1;
            }

            loc++;
            break;
        case 1: /* is this the second 0x0A? */
            if(full[loc] == (char)0x0A)
                full_state = 2;
            else
                full_state = 0;

            loc++;
            break;
        case 2: /* get the size */
            size = full[loc];

            full_state = 3;
            loc++;
            break;
        case 3: /* now get the full string */
            dir_and_files.append(QString(tr(QByteArray(full.constData()+loc, size))));

            full_state = 0;
            loc += size;
            break;
        }
    }

    return dir_and_files;
}

QByteArray V800usb::generate_request(QString request)
{
    QByteArray packet;

    packet[0] = 01;
    packet[1] = (unsigned char)((request.length()+8) << 2);
    packet[2] = 0x00;
    packet[3] = request.length()+4;
    packet[4] = 0x00;
    packet[5] = 0x08;
    packet[6] = 0x00;
    packet[7] = 0x12;
    packet[8] = request.length();
    packet.append(request.toUtf8());

    return packet;
}

QByteArray V800usb::generate_ack(unsigned char packet_num)
{
    QByteArray packet;

    packet[0] = 0x01;
    packet[1] = 0x05;
    packet[2] = packet_num;

    return packet;
}

int V800usb::is_end(QByteArray packet)
{
    if((packet[1] & 0x03) == 1)
        return 0;
    else
        return 1;
}

QByteArray V800usb::add_to_full(QByteArray packet, QByteArray full, bool initial_packet, bool final_packet)
{
    QByteArray new_full = full;
    unsigned int size = (unsigned char)packet[1] >> 2;

    if(initial_packet)
    {
        // final packets have a trailing 0x00 we don't want
        if(final_packet)
            size -= 4;
        else
            size -= 3;

        packet.remove(0, 5);
        new_full.append(packet.constData(), size);
    }
    else
    {
        // final packets have a trailing 0x00 we don't want
        if(final_packet)
            size -= 2;
        else
            size -= 1;

        packet.remove(0, 3);
        new_full.append(packet.constData(), size);
    }

    return new_full;
}