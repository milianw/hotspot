/*
  perfparser.cpp

  This file is part of Hotspot, the Qt GUI for performance analysis.

  Copyright (C) 2016-2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Milian Wolff <milian.wolff@kdab.com>

  Licensees holding valid commercial KDAB Hotspot licenses may use this file in
  accordance with Hotspot Commercial License Agreement provided with the Software.

  Contact info@kdab.com if any conditions of this licensing are not clear to you.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "perfparser.h"

#include <QProcess>
#include <QDebug>
#include <QtEndian>
#include <QBuffer>
#include <QDataStream>
#include <QFileInfo>
#include <QLoggingCategory>

#include <ThreadWeaver/ThreadWeaver>

#include <models/framedata.h>
#include <models/summarydata.h>

#include <util.h>

Q_LOGGING_CATEGORY(LOG_PERFPARSER, "hotspot.perfparser", QtWarningMsg)

namespace {

struct Record
{
    quint32 pid = 0;
    quint32 tid = 0;
    quint64 time = 0;
};

QDataStream& operator>>(QDataStream& stream, Record& record)
{
    return stream >> record.pid >> record.tid >> record.time;
}

QDebug operator<<(QDebug stream, const Record& record)
{
    stream.noquote().nospace() << "Record{"
        << "pid=" << record.pid << ", "
        << "tid=" << record.tid << ", "
        << "time=" << record.time
        << "}";
    return stream;
}

struct StringId
{
    qint32 id = -1;
};

QDataStream& operator>>(QDataStream& stream, StringId& stringId)
{
    return stream >> stringId.id;
}

QDebug operator<<(QDebug stream, const StringId& stringId)
{
    stream.noquote().nospace() << "String{"
        << "id=" << stringId.id
        << "}";
    return stream;
}

struct AttributesDefinition
{
    qint32 id = 0;
    quint32 type = 0;
    quint64 config = 0;
    StringId name;
};

QDataStream& operator>>(QDataStream& stream, AttributesDefinition& attributesDefinition)
{
    return stream >> attributesDefinition.id
                  >> attributesDefinition.type
                  >> attributesDefinition.config
                  >> attributesDefinition.name;
}

QDebug operator<<(QDebug stream, const AttributesDefinition& attributesDefinition)
{
    stream.noquote().nospace() << "AttributesDefinition{"
        << "id=" << attributesDefinition.id << ", "
        << "type=" << attributesDefinition.type << ", "
        << "config=" << attributesDefinition.config << ", "
        << "name=" << attributesDefinition.name
        << "}";
    return stream;
}

struct Command : Record
{
    StringId comm;
};

QDataStream& operator>>(QDataStream& stream, Command& command)
{
    return stream >> static_cast<Record&>(command) >> command.comm;
}

QDebug operator<<(QDebug stream, const Command& command)
{
    stream.noquote().nospace() << "Command{"
        << static_cast<const Record&>(command) << ", "
        << "comm=" << command.comm
        << "}";
    return stream;
}

struct ThreadStart
{
    quint32 childPid = 0;
    quint32 childTid = 0;
    quint64 time = 0;
};

QDataStream& operator>>(QDataStream& stream, ThreadStart& threadStart)
{
    return stream >> threadStart.childPid >> threadStart.childTid >> threadStart.time;
}

QDebug operator<<(QDebug stream, const ThreadStart& threadStart)
{
    stream.noquote().nospace() << "ThreadStart{"
        << "childPid=" << threadStart.childPid << ", "
        << "childTid=" << threadStart.childTid << ", "
        << "time=" << threadStart.time
        << "}";
    return stream;
}

struct ThreadEnd
{
    quint32 childPid = 0;
    quint32 childTid = 0;
    quint64 time = 0;
};

QDataStream& operator>>(QDataStream& stream, ThreadEnd& threadEnd)
{
    return stream >> threadEnd.childPid >> threadEnd.childTid >> threadEnd.time;
}

QDebug operator<<(QDebug stream, const ThreadEnd& threadEnd)
{
    stream.noquote().nospace() << "ThreadEnd{"
        << "childPid=" << threadEnd.childPid << ", "
        << "childTid=" << threadEnd.childTid << ", "
        << "time=" << threadEnd.time
        << "}";
    return stream;
}

struct Location
{
    quint64 address = 0;
    StringId file;
    quint32 pid = 0;
    qint32 line = 0;
    qint32 column = 0;
    qint32 parentLocationId = 0;
};

QDataStream& operator>>(QDataStream& stream, Location& location)
{
    return stream >> location.address >> location.file
        >> location.pid >> location.line
        >> location.column >> location.parentLocationId;
}

QDebug operator<<(QDebug stream, const Location& location)
{
    stream.noquote().nospace() << "Location{"
        << "address=0x" << hex << location.address << dec << ", "
        << "file=" << location.file << ", "
        << "pid=" << location.pid << ", "
        << "line=" << location.line << ", "
        << "column=" << location.column << ", "
        << "parentLocationId=" << location.parentLocationId
        << "}";
    return stream;
}

struct LocationDefinition
{
    qint32 id = 0;
    Location location;
};

QDataStream& operator>>(QDataStream& stream, LocationDefinition& locationDefinition)
{
    return stream >> locationDefinition.id >> locationDefinition.location;
}

QDebug operator<<(QDebug stream, const LocationDefinition& locationDefinition)
{
    stream.noquote().nospace() << "LocationDefinition{"
        << "id=" << locationDefinition.id << ", "
        << "location=" << locationDefinition.location
        << "}";
    return stream;
}

struct Symbol
{
    StringId name;
    StringId binary;
    bool isKernel = false;
};

QDataStream& operator>>(QDataStream& stream, Symbol& symbol)
{
    return stream >> symbol.name >> symbol.binary >> symbol.isKernel;
}

QDebug operator<<(QDebug stream, const Symbol& symbol)
{
    stream.noquote().nospace() << "Symbol{"
        << "name=" << symbol.name << ", "
        << "binary=" << symbol.binary << ", "
        << "isKernel=" << symbol.isKernel
        << "}";
    return stream;
}

struct SymbolDefinition
{
    qint32 id = 0;
    Symbol symbol;
};

QDataStream& operator>>(QDataStream& stream, SymbolDefinition& symbolDefinition)
{
    return stream >> symbolDefinition.id >> symbolDefinition.symbol;
}

QDebug operator<<(QDebug stream, const SymbolDefinition& symbolDefinition)
{
    stream.noquote().nospace() << "SymbolDefinition{"
        << "id=" << symbolDefinition.id << ", "
        << "symbol=" << symbolDefinition.symbol
        << "}";
    return stream;
}

struct Sample : Record
{
    QVector<qint32> frames;
    quint8 guessedFrames = 0;
    qint32 attributeId = 0;
};

QDataStream& operator>>(QDataStream& stream, Sample& sample)
{
    return stream >> static_cast<Record&>(sample)
        >> sample.frames >> sample.guessedFrames >> sample.attributeId;
}

QDebug operator<<(QDebug stream, const Sample& sample)
{
    stream.noquote().nospace() << "Sample{"
        << static_cast<const Record&>(sample) << ", "
        << "frames=" << sample.frames << ", "
        << "guessedFrames=" << sample.guessedFrames << ", "
        << "attributeId=" << sample.attributeId
        << "}";
    return stream;
}

struct StringDefinition
{
    qint32 id = 0;
    QByteArray string;
};

QDataStream& operator>>(QDataStream& stream, StringDefinition& stringDefinition)
{
    return stream >> stringDefinition.id >> stringDefinition.string;
}

QDebug operator<<(QDebug stream, const StringDefinition& stringDefinition)
{
    stream.noquote().nospace() << "StringDefinition{"
        << "id=" << stringDefinition.id << ", "
        << "string=" << stringDefinition.string
        << "}";
    return stream;
}

struct LostDefinition : Record
{
};

QDataStream& operator>>(QDataStream& stream, LostDefinition& lostDefinition)
{
    return stream >> static_cast<Record&>(lostDefinition);
}

QDebug operator<<(QDebug stream, const LostDefinition& lostDefinition)
{
    stream.noquote().nospace() << "LostDefinition{"
        << static_cast<const Record&>(lostDefinition)
        << "}";
    return stream;
}

struct BuildId
{
    quint32 pid = 0;
    QByteArray id;
    QByteArray fileName;
};

QDataStream& operator>>(QDataStream& stream, BuildId& buildId)
{
    return stream >> buildId.pid >> buildId.id >> buildId.fileName;
}

QDebug operator<<(QDebug stream, const BuildId& buildId)
{
    stream.noquote().nospace() << "BuildId{"
        << "pid=" << buildId.pid << ", "
        << "id=" << buildId.id.toHex() << ", "
        << "fileName=" << buildId.fileName
        << "}";
    return stream;
}

struct NumaNode
{
    quint32 nodeId = 0;
    quint64 memTotal = 0;
    quint64 memFree = 0;
    QByteArray topology;
};

QDataStream& operator>>(QDataStream& stream, NumaNode& numaNode)
{
    return stream >> numaNode.nodeId >> numaNode.memTotal
                  >> numaNode.memFree >> numaNode.topology;
}

QDebug operator<<(QDebug stream, const NumaNode& numaNode)
{
    stream.noquote().nospace() << "NumaNode{"
        << "nodeId=" << numaNode.nodeId << ", "
        << "memTotal=" << numaNode.memTotal << ", "
        << "memFree=" << numaNode.memFree << ", "
        << "topology=" << numaNode.topology
        << "}";
    return stream;
}

struct Pmu
{
    quint32 type = 0;
    QByteArray name;
};

QDataStream& operator>>(QDataStream& stream, Pmu& pmu)
{
    return stream >> pmu.type >> pmu.name;
}

QDebug operator<<(QDebug stream, const Pmu& pmu)
{
    stream.noquote().nospace() << "Pmu{"
        << "type=" << pmu.type << ", "
        << "name=" << pmu.name
        << "}";
    return stream;
}

struct GroupDesc
{
    QByteArray name;
    quint32 leaderIndex = 0;
    quint32 numMembers = 0;
};

QDataStream& operator>>(QDataStream& stream, GroupDesc& groupDesc)
{
    return stream >> groupDesc.name >> groupDesc.leaderIndex
                  >> groupDesc.numMembers;
}

QDebug operator<<(QDebug stream, const GroupDesc& groupDesc)
{
    stream.noquote().nospace() << "GroupDesc{"
        << "name=" << groupDesc.name << ", "
        << "leaderIndex=" << groupDesc.leaderIndex << ", "
        << "numMembers=" << groupDesc.numMembers
        << "}";
    return stream;
}


struct FeaturesDefinition
{
    QByteArray hostName;
    QByteArray osRelease;
    QByteArray version;
    QByteArray arch;
    quint32 nrCpusOnline;
    quint32 nrCpusAvailable;
    QByteArray cpuDesc;
    QByteArray cpuId;
    // in kilobytes
    quint64 totalMem;
    QList<QByteArray> cmdline;
    QList<BuildId> buildIds;
    QList<QByteArray> siblingCores;
    QList<QByteArray> siblingThreads;
    QList<NumaNode> numaTopology;
    QList<Pmu> pmuMappings;
    QList<GroupDesc> groupDescs;
};

QDataStream& operator>>(QDataStream& stream, FeaturesDefinition& featuresDefinition)
{
    stream >> featuresDefinition.hostName >> featuresDefinition.osRelease
           >> featuresDefinition.version >> featuresDefinition.arch
           >> featuresDefinition.nrCpusOnline >> featuresDefinition.nrCpusAvailable
           >> featuresDefinition.cpuDesc >> featuresDefinition.cpuId
           >> featuresDefinition.totalMem >> featuresDefinition.cmdline
           >> featuresDefinition.buildIds
           >> featuresDefinition.siblingCores >> featuresDefinition.siblingThreads
           >> featuresDefinition.numaTopology >> featuresDefinition.pmuMappings
           >> featuresDefinition.groupDescs;
    return stream;
}

QDebug operator<<(QDebug stream, const FeaturesDefinition& featuresDefinition)
{
    stream.noquote().nospace() << "FeaturesDefinition{"
        << "hostName=" << featuresDefinition.hostName << ", "
        << "osRelease=" << featuresDefinition.osRelease << ", "
        << "version=" << featuresDefinition.version << ", "
        << "arch=" << featuresDefinition.arch << ", "
        << "nrCpusOnline=" << featuresDefinition.nrCpusOnline << ", "
        << "nrCpusAvailable=" << featuresDefinition.nrCpusAvailable << ", "
        << "cpuDesc=" << featuresDefinition.cpuDesc << ", "
        << "cpuId=" << featuresDefinition.cpuId << ", "
        << "totalMem=" << featuresDefinition.totalMem << ", "
        << "cmdline=" << featuresDefinition.cmdline << ", "
        << "buildIds=" << featuresDefinition.buildIds << ", "
        << "siblingCores=" << featuresDefinition.siblingCores << ", "
        << "siblingThreads=" << featuresDefinition.siblingThreads << ", "
        << "numaTopology=" << featuresDefinition.numaTopology << ", "
        << "pmuMappings=" << featuresDefinition.pmuMappings << ", "
        << "groupDesc=" << featuresDefinition.groupDescs
        << "}";
    return stream;
}

struct LocationData
{
    LocationData(qint32 parentLocationId = -1, const QString& location = {},
                 const QString& address = {})
        : parentLocationId(parentLocationId)
        , location(location)
        , address(address)
    { }

    qint32 parentLocationId = -1;
    QString location;
    QString address;
};

struct SymbolData
{
    QString symbol;
    QString binary;

    bool isValid() const
    {
        return !symbol.isEmpty() || !binary.isEmpty();
    }
};

struct CallerCalleeLocation
{
    QString symbol;
    QString binary;

    bool operator<(const CallerCalleeLocation &location) const
    {
        return std::tie(symbol, binary) < std::tie(location.symbol, location.binary);
    }
};

inline bool operator==(const CallerCalleeLocation &location1, const CallerCalleeLocation &location2)
{
    return location1.symbol == location2.symbol && location1.binary == location2.binary;
}

inline bool operator!=(const CallerCalleeLocation &location1, const CallerCalleeLocation &location2)
{
    return !(location1.symbol == location2.symbol && location1.binary == location2.binary);
}

inline uint qHash(const CallerCalleeLocation &key, uint seed = 0)
{
    Util::HashCombine hash;
    seed = hash(seed, key.symbol);
    seed = hash(seed, key.binary);
    return seed;
}

}

Q_DECLARE_TYPEINFO(AttributesDefinition, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(LocationData, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(SymbolData, Q_MOVABLE_TYPE);

struct PerfParserPrivate
{
    PerfParserPrivate()
    {
        buffer.buffer().reserve(1024);
        buffer.open(QIODevice::ReadOnly);
        stream.setDevice(&buffer);
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    }

    bool tryParse()
    {
        const auto bytesAvailable = process.bytesAvailable();
        switch (state) {
            case HEADER: {
                const auto magic = QByteArrayLiteral("QPERFSTREAM");
                // + 1 to include the trailing \0
                if (bytesAvailable >= magic.size() + 1) {
                    process.read(buffer.buffer().data(), magic.size() + 1);
                    if (buffer.buffer().data() != magic) {
                        state = PARSE_ERROR;
                        qCWarning(LOG_PERFPARSER) << "Failed to read header magic";
                        return false;
                    } else {
                        state = DATA_STREAM_VERSION;
                        return true;
                    }
                }
                break;
            }
            case DATA_STREAM_VERSION: {
                qint32 dataStreamVersion = 0;
                if (bytesAvailable >= static_cast<qint64>(sizeof(dataStreamVersion))) {
                    process.read(buffer.buffer().data(), sizeof(dataStreamVersion));
                    dataStreamVersion = qFromLittleEndian(*reinterpret_cast<qint32*>(buffer.buffer().data()));
                    stream.setVersion(dataStreamVersion);
                    qCDebug(LOG_PERFPARSER) << "data stream version is:" << dataStreamVersion;
                    state = EVENT_HEADER;
                    return true;
                }
                break;
            }
            case EVENT_HEADER:
                if (bytesAvailable >= static_cast<qint64>((eventSize))) {
                    process.read(buffer.buffer().data(), sizeof(eventSize));
                    eventSize = qFromLittleEndian(*reinterpret_cast<quint32*>(buffer.buffer().data()));
                    qCDebug(LOG_PERFPARSER) << "next event size is:" << eventSize;
                    state = EVENT;
                    return true;
                }
                break;
            case EVENT:
                if (bytesAvailable >= static_cast<qint64>(eventSize)) {
                    buffer.buffer().resize(eventSize);
                    process.read(buffer.buffer().data(), eventSize);
                    if (!parseEvent()) {
                        state = PARSE_ERROR;
                        return false;
                    }
                    // await next event
                    state = EVENT_HEADER;
                    return true;
                }
                break;
            case PARSE_ERROR:
                // do nothing
                break;
        }
        return false;
    }

    bool parseEvent()
    {
        Q_ASSERT(buffer.isOpen());
        Q_ASSERT(buffer.isReadable());

        buffer.seek(0);
        Q_ASSERT(buffer.pos() == 0);

        stream.resetStatus();

        qint8 eventType = 0;
        stream >> eventType;
        qCDebug(LOG_PERFPARSER) << "next event is:" << eventType;

        if (eventType < 0 || eventType >= static_cast<qint8>(EventType::InvalidType)) {
            qCWarning(LOG_PERFPARSER) << "invalid event type" << eventType;
            state = PARSE_ERROR;
            return false;
        }

        switch (static_cast<EventType>(eventType)) {
            case EventType::Sample: {
                Sample sample;
                stream >> sample;
                qCDebug(LOG_PERFPARSER) << "parsed:" << sample;
                addSample(sample);
                break;
            }
            case EventType::ThreadStart: {
                ThreadStart threadStart;
                stream >> threadStart;
                qCDebug(LOG_PERFPARSER) << "parsed:" << threadStart;
                break;
            }
            case EventType::ThreadEnd: {
                ThreadEnd threadEnd;
                stream >> threadEnd;
                qCDebug(LOG_PERFPARSER) << "parsed:" << threadEnd;
                break;
            }
            case EventType::Command: {
                Command command;
                stream >> command;
                qCDebug(LOG_PERFPARSER) << "parsed:" << command;
                addCommand(command);
                break;
            }
            case EventType::LocationDefinition: {
                LocationDefinition locationDefinition;
                stream >> locationDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << locationDefinition;
                addLocation(locationDefinition);
                break;
            }
            case EventType::SymbolDefinition: {
                SymbolDefinition symbolDefinition;
                stream >> symbolDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << symbolDefinition;
                addSymbol(symbolDefinition);
                break;
            }
            case EventType::AttributesDefinition: {
                AttributesDefinition attributesDefinition;
                stream >> attributesDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << attributesDefinition;
                addAttributes(attributesDefinition);
                break;
            }
            case EventType::StringDefinition: {
                StringDefinition stringDefinition;
                stream >> stringDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << stringDefinition;
                addString(stringDefinition);
                break;
            }
            case EventType::LostDefinition: {
                LostDefinition lostDefinition;
                stream >> lostDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << lostDefinition;
                addLost(lostDefinition);
                break;
            }
            case EventType::FeaturesDefinition: {
                FeaturesDefinition featuresDefinition;
                stream >> featuresDefinition;
                qCDebug(LOG_PERFPARSER) << "parsed:" << featuresDefinition;
                setFeatures(featuresDefinition);
                break;
            }
            case EventType::InvalidType:
                break;
        }

        if (!stream.atEnd()) {
            qCWarning(LOG_PERFPARSER) << "did not consume all bytes for event of type" << eventType
                                      << buffer.pos() << buffer.size();
            return false;
        }

        return true;
    }

    void finalize()
    {
        FrameData::initializeParents(&bottomUpResult);

        calculateSummary();

        buildTopDownResult();
        buildCallerCalleeResult();
    }

    void addAttributes(const AttributesDefinition& attributesDefinition)
    {
        attributes.push_back(attributesDefinition);
    }

    void addCommand(const Command& command)
    {
        // TODO: keep track of list of commands for filtering later on
        Q_UNUSED(command);
    }

    void addLocation(const LocationDefinition& location)
    {
        Q_ASSERT(locations.size() == location.id);
        Q_ASSERT(symbols.size() == location.id);
        QString locationString;
        if (location.location.file.id != -1) {
            locationString = strings.value(location.location.file.id);
            if (location.location.line != -1) {
                locationString += QLatin1Char(':') + QString::number(location.location.line);
            }
        }
        locations.push_back({
            location.location.parentLocationId,
            locationString,
            QString::number(location.location.address, 16)
        });
        symbols.push_back({});
    }

    void addSymbol(const SymbolDefinition& symbol)
    {
        // TODO: do we need to handle pid/tid here?
        // TODO: store binary, isKernel information
        Q_ASSERT(symbols.size() > symbol.id);
        symbols[symbol.id] = {
            strings.value(symbol.symbol.name.id),
            strings.value(symbol.symbol.binary.id)
        };
    }

    static FrameData* addFrame(FrameData* parent,
                               const QString& symbol, const QString& binary,
                               const QString& location, const QString& address)
    {
        FrameData* ret = nullptr;

        for (auto row = parent->children.data(), end = row + parent->children.size();
             row != end; ++row)
        {
            // TODO: implement aggregation, i.e. to ignore location address
            if (row->symbol == symbol && row->binary == binary
                && row->location == location && row->address == address)
            {
                ret = row;
                break;
            }
        }

        if (!ret) {
            FrameData frame;
            frame.symbol = symbol;
            frame.binary = binary;
            frame.location = location;
            frame.address = address;
            parent->children.append(frame);
            ret = &parent->children.last();
        }

        return ret;
    }

    FrameData* addFrame(FrameData* parent, qint32 id) const
    {
        bool skipNextFrame = false;
        while (id != -1) {
            const auto& location = locations.value(id);
            if (skipNextFrame) {
                id = location.parentLocationId;
                skipNextFrame = false;
                continue;
            }

            auto symbol = symbols.value(id);
            if (!symbol.isValid()) {
                // we get function entry points from the perfparser but
                // those are imo not interesting - skip them
                symbol = symbols.value(location.parentLocationId);
                skipNextFrame = true;
            }

            auto ret = addFrame(parent, symbol.symbol, symbol.binary,
                                location.location, location.address);

            ++ret->inclusiveCost;
            if (parent == &bottomUpResult) {
                ++ret->selfCost;
            }

            parent = ret;
            id = location.parentLocationId;
        }

        return parent;
    }

    void addSample(const Sample& sample)
    {
        addSampleToBottomUp(sample);
        addSampleToSummary(sample);
    }

    void addString(const StringDefinition& string)
    {
        Q_ASSERT(string.id == strings.size());
        strings.push_back(QString::fromUtf8(string.string));
    }

    void addSampleToBottomUp(const Sample& sample)
    {
        ++bottomUpResult.inclusiveCost;
        auto parent = &bottomUpResult;
        for (auto id : sample.frames) {
            parent = addFrame(parent, id);
        }
    }

    static void buildTopDownResult(const QVector<FrameData>& bottomUpData, FrameData* topDownData)
    {
        foreach (const auto& row, bottomUpData) {
            if (row.children.isEmpty()) {
                // leaf node found, bubble up the parent chain to build a top-down tree
                auto node = &row;
                auto stack = topDownData;
                while (node) {
                    auto frame = addFrame(stack,
                                          node->symbol, node->binary,
                                          node->location, node->address);

                    // always use the leaf node's cost and propagate that one up the chain
                    // otherwise we'd count the cost of some nodes multiple times
                    frame->inclusiveCost += row.inclusiveCost;
                    if (node == &row) {
                        frame->selfCost += 1;
                    }
                    stack = frame;
                    node = node->parent;
                }
            } else {
                // recurse to find a leaf
                buildTopDownResult(row.children, topDownData);
            }
        }
    }

    void buildTopDownResult()
    {
        buildTopDownResult(bottomUpResult.children, &topDownResult);
        FrameData::initializeParents(&topDownResult);
    }

    static void buildCallerCalleeResult(const QVector<FrameData>& bottomUpData, FrameData* callerCalleeData)
    {
        for (const FrameData& row : bottomUpData) {
            if (row.children.isEmpty()) {
                // leaf node found, bubble up the parent chain to add cost for all frames
                // to the caller/callee data. this is done top-down since we must not count
                // symbols more than once in the caller-callee data
                QSet<CallerCalleeLocation> recursionGuard;
                auto node = &row;

                while (node) {
                    CallerCalleeLocation needle{node->symbol, node->binary};
                    if (!recursionGuard.contains(needle)) { // aggregate caller-callee data
                        auto it = std::lower_bound(callerCalleeData->children.begin(), callerCalleeData->children.end(), needle,
                            [](const FrameData& frame, const auto needle) { return CallerCalleeLocation{frame.symbol, frame.binary} < needle; });

                        if (it == callerCalleeData->children.end() || CallerCalleeLocation{it->symbol, it->binary} != needle) {
                            it = callerCalleeData->children.insert(it, {node->symbol, node->binary, node->location, node->address, 0, 0, {}, nullptr});
                        }
                        it->inclusiveCost += 1;
                        if (!node->parent) {
                            it->selfCost += 1;
                        }
                        recursionGuard.insert(needle);
                    }
                    node = node->parent;
                }
            } else {
                // recurse to find a leaf
                buildCallerCalleeResult(row.children, callerCalleeData);
            }
        }
    }

    void buildCallerCalleeResult()
    {
        buildCallerCalleeResult(bottomUpResult.children, &callerCalleeResult);
    }

    void addSampleToSummary(const Sample& sample)
    {
        if (sample.time < applicationStartTime || applicationStartTime == 0) {
            applicationStartTime = sample.time;
        }
        else if (sample.time > applicationEndTime || applicationEndTime == 0) {
            applicationEndTime = sample.time;
        }
        uniqueThreads.insert(sample.tid);
        uniqueProcess.insert(sample.pid);
        ++summaryResult.sampleCount;
    }

    void calculateSummary()
    {
        summaryResult.applicationRunningTime = applicationEndTime - applicationStartTime;
        summaryResult.threadCount = uniqueThreads.size();
        summaryResult.processCount = uniqueProcess.size();
    }

    void addLost(const LostDefinition& /*lost*/)
    {
        ++summaryResult.lostChunks;
    }

    void setFeatures(const FeaturesDefinition& features)
    {
        // first entry in cmdline is "perf" which could contain a path
        // we only want to show the name without the path
        auto args = features.cmdline;
        args.removeFirst();
        summaryResult.command = QLatin1String("perf ") + QString::fromUtf8(args.join(' '));

        // TODO: add system info to summary page
    }

    enum State {
        HEADER,
        DATA_STREAM_VERSION,
        EVENT_HEADER,
        EVENT,
        PARSE_ERROR
    };

    enum class EventType {
        Sample,
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        AttributesDefinition,
        StringDefinition,
        LostDefinition,
        FeaturesDefinition,
        InvalidType
    };

    State state = HEADER;
    quint32 eventSize = 0;
    QBuffer buffer;
    QDataStream stream;
    FrameData bottomUpResult;
    FrameData topDownResult;
    QVector<AttributesDefinition> attributes;
    QVector<SymbolData> symbols;
    QVector<LocationData> locations;
    QVector<QString> strings;
    QProcess process;
    SummaryData summaryResult;
    quint64 applicationStartTime = 0;
    quint64 applicationEndTime = 0;
    QSet<quint32> uniqueThreads;
    QSet<quint32> uniqueProcess;
    FrameData callerCalleeResult;
};

PerfParser::PerfParser(QObject* parent)
    : QObject(parent)
{
}

PerfParser::~PerfParser() = default;

void PerfParser::startParseFile(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        emit parsingFailed(tr("File '%1' does not exist.").arg(path));
        return;
    }
    if (!info.isFile()) {
        emit parsingFailed(tr("'%1' is not a file.").arg(path));
        return;
    }
    if (!info.isReadable()) {
        emit parsingFailed(tr("File '%1' is not readable.").arg(path));
        return;
    }

    const auto parserBinary = Util::findLibexecBinary(QStringLiteral("hotspot-perfparser"));
    if (parserBinary.isEmpty()) {
        emit parsingFailed(tr("Failed to find hotspot-perfparser binary."));
        return;
    }

    using namespace ThreadWeaver;
    stream() << make_job([path, parserBinary, this]() {
        PerfParserPrivate d;

        connect(&d.process, &QProcess::readyRead,
                [&d] {
                    while (d.tryParse()) {
                        // just call tryParse until it fails
                    }
                });

        connect(&d.process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [&d, this] (int exitCode, QProcess::ExitStatus exitStatus) {
                    qCDebug(LOG_PERFPARSER) << exitCode << exitStatus;

                    if (exitCode == EXIT_SUCCESS) {
                        d.finalize();
                        emit bottomUpDataAvailable(d.bottomUpResult);
                        emit topDownDataAvailable(d.topDownResult);
                        emit summaryDataAvailable(d.summaryResult);
                        emit callerCalleeDataAvailable(d.callerCalleeResult);
                        emit parsingFinished();
                    } else {
                        emit parsingFailed(tr("The hotspot-perfparser binary exited with code %1.").arg(exitCode));
                    }
                });

        connect(&d.process, &QProcess::errorOccurred,
                [&d, this] (QProcess::ProcessError error) {
                    qCWarning(LOG_PERFPARSER) << error << d.process.errorString();

                    emit parsingFailed(d.process.errorString());
                });

        d.process.start(parserBinary, {QStringLiteral("--input"), path});
        if (!d.process.waitForStarted()) {
            emit parsingFailed(tr("Failed to start the hotspot-perfparser process"));
            return;
        }
        d.process.waitForFinished();
    });
}
