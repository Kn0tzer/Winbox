/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __KNATIVESOCKET_H__
#define __KNATIVESOCKET_H__

#include "ksocketobject.h"
#include <deque>
#include <mutex>
#include <vector>

struct sockaddr_in;

class KNativeSocketObject : public KSocketObject {
public:
    KNativeSocketObject(U32 domain, U32 type, U32 protocol);

    virtual ~KNativeSocketObject();    

    // from KObject
    U32 ioctl(KThread* thread, U32 request) override;
    S64 seek(S64 pos) override;
    S64 length() override;
    S64 getPos() override;
    void setBlocking(bool blocking) override;
    bool isBlocking() override;
    void setAsync(bool isAsync) override;
    bool isAsync() override;
    KFileLock* getLock(KFileLock* lock) override;
    U32 setLock(KFileLock* lock, bool wait) override;
    bool supportsLocks() override;
    bool isOpen() override;
    bool isReadReady() override;
    bool isPriorityReadReady() override;
    bool isWriteReady() override;
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override;
    U32 writeNative(U8* buffer, U32 len) override;
    U32 readNative(U8* buffer, U32 len) override;
    U32 stat(KProcess* process, U32 address, bool is64) override;
    U32 map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) override;
    bool canMap() override;
    BString selfFd() override;

    // from KSocketObject
    U32 accept(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len, U32 flags) override;
    U32 bind(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) override;
    U32 connect(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) override;
    U32 getpeername(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) override;
    U32 getsockname(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) override;
    U32 getsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len_address) override;
    U32 listen(KThread* thread, const KFileDescriptorPtr& fd, U32 backlog) override;
    U32 recvfrom(KThread* thread, const KFileDescriptorPtr& fd, U32 buffer, U32 length, U32 flags, U32 address, U32 address_len) override;
    U32 recvmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) override;
    U32 sendmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) override;
    U32 sendto(KThread* thread, const KFileDescriptorPtr& fd, U32 message, U32 length, U32 flags, U32 dest_addr, U32 dest_len) override;
    U32 setsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len) override;
    U32 shutdown(KThread* thread, const KFileDescriptorPtr& fd, U32 how) override;

    S32 nativeSocket;
    bool connecting;
    bool loopbackDatagram = false;
    bool loopbackStream = false;
    bool loopbackStreamConnected = false;
    bool loopbackStreamPeerEof = false;
    bool loopbackReuseAddress = false;
    U32 loopbackStreamBacklog = 0;
    U32 loopbackAddress = 0;
    U16 loopbackPort = 0;
    U32 loopbackPeerAddress = 0;
    U16 loopbackPeerPort = 0;

    std::weak_ptr<KNativeSocketObject> loopbackStreamPeer;
    std::deque<std::shared_ptr<KNativeSocketObject>> loopbackAcceptQueue;
    std::deque<U8> loopbackStreamBytes;
    std::mutex loopbackStreamMutex;

    struct LoopbackPacket {
        std::vector<U8> bytes;
        U32 sourceAddress = 0;
        U16 sourcePort = 0;
    };
    std::deque<LoopbackPacket> loopbackPackets;
    std::mutex loopbackPacketsMutex;

    U32 bindLoopback(U32 address, U16 port);
    U32 bindLoopbackStream(U32 address, U16 port);
    U32 takeLoopbackStreamConnection(std::shared_ptr<KNativeSocketObject>& accepted, bool nonblocking = false);
    U32 sendLoopbackStream(const U8* bytes, U32 length, U32 flags);
    U32 recvLoopbackStream(U8* bytes, U32 length, U32 flags);
    void closeLoopbackStream();
    U32 ensureLoopbackBound();
    U32 sendLoopbackBytes(KThread* thread, const U8* bytes, U32 length, U32 flags,
                          const struct sockaddr_in* destination, bool hasDestination);
    U32 sendLoopbackTo(KThread* thread, U32 message, U32 length, U32 flags,
                       U32 destinationAddress, U32 destinationLength);
    U32 recvLoopbackFrom(KThread* thread, U32 buffer, U32 length, U32 flags,
                         U32 address, U32 addressLength);

    BOXEDWINE_CONDITION readingCond;
    BOXEDWINE_CONDITION writingCond;
};

bool checkWaitingNativeSockets(int timeout);
FsOpenNode* openHosts(const std::shared_ptr<FsNode>& node, U32 flags, U32 data);
FsOpenNode* openHostname(const std::shared_ptr<FsNode>& node, U32 flags, U32 data);

#endif
