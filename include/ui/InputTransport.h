/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _UI_INPUT_TRANSPORT_H
#define _UI_INPUT_TRANSPORT_H

/**
 * Native input transport.
 *
 * The InputChannel provides a mechanism for exchanging InputMessage structures across processes.
 *
 * The InputPublisher and InputConsumer each handle one end-point of an input channel.
 * The InputPublisher is used by the input dispatcher to send events to the application.
 * The InputConsumer is used by the application to receive events from the input dispatcher.
 */

#include <ui/Input.h>
#include <utils/Errors.h>
#include <utils/Timers.h>
#include <utils/RefBase.h>
#include <utils/String8.h>

namespace android {

/*
 * Intermediate representation used to send input events and related signals.
 */
struct InputMessage {
    enum {
        TYPE_KEY = 1,
        TYPE_MOTION = 2,
        TYPE_FINISHED = 3,
    };

    struct Header {
        uint32_t type;
        uint32_t padding; // 8 byte alignment for the body that follows
    } header;

    union Body {
        struct Key {
            uint32_t seq;
            nsecs_t eventTime;
            int32_t deviceId;
            int32_t source;
            int32_t action;
            int32_t flags;
            int32_t keyCode;
            int32_t scanCode;
            int32_t metaState;
            int32_t repeatCount;
            nsecs_t downTime;

            inline size_t size() const {
                return sizeof(Key);
            }
        } key;

        struct Motion {
            uint32_t seq;
            nsecs_t eventTime;
            int32_t deviceId;
            int32_t source;
            int32_t action;
            int32_t flags;
            int32_t metaState;
            int32_t buttonState;
            int32_t edgeFlags;
            nsecs_t downTime;
            float xOffset;
            float yOffset;
            float xPrecision;
            float yPrecision;
            size_t pointerCount;
            struct Pointer {
                PointerProperties properties;
                PointerCoords coords;
            } pointers[MAX_POINTERS];

            inline size_t size() const {
                return sizeof(Motion) - sizeof(Pointer) * MAX_POINTERS
                        + sizeof(Pointer) * pointerCount;
            }
        } motion;

        struct Finished {
            uint32_t seq;
            bool handled;

            inline size_t size() const {
                return sizeof(Finished);
            }
        } finished;
    } body;

    bool isValid(size_t actualSize) const;
    size_t size() const;
};

/*
 * An input channel consists of a local unix domain socket used to send and receive
 * input messages across processes.  Each channel has a descriptive name for debugging purposes.
 *
 * Each endpoint has its own InputChannel object that specifies its file descriptor.
 *
 * The input channel is closed when all references to it are released.
 */
class InputChannel : public RefBase {
protected:
    virtual ~InputChannel();

public:
    InputChannel(const String8& name, int32_t fd);

    /* Creates a pair of input channels.
     *
     * Returns OK on success.
     */
    static status_t openInputChannelPair(const String8& name,
            sp<InputChannel>& outServerChannel, sp<InputChannel>& outClientChannel);

    inline String8 getName() const { return mName; }
    inline int32_t getFd() const { return mFd; }

    /* Sends a message to the other endpoint.
     *
     * If the channel is full then the message is guaranteed not to have been sent at all.
     * Try again after the consumer has sent a finished signal indicating that it has
     * consumed some of the pending messages from the channel.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if the channel is full.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Other errors probably indicate that the channel is broken.
     */
    status_t sendMessage(const InputMessage* msg);

    /* Receives a message sent by the other endpoint.
     *
     * If there is no message present, try again after poll() indicates that the fd
     * is readable.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if there is no message present.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Other errors probably indicate that the channel is broken.
     */
    status_t receiveMessage(InputMessage* msg);

private:
    String8 mName;
    int32_t mFd;
};

/*
 * Publishes input events to an input channel.
 */
class InputPublisher {
public:
    /* Creates a publisher associated with an input channel. */
    explicit InputPublisher(const sp<InputChannel>& channel);

    /* Destroys the publisher and releases its input channel. */
    ~InputPublisher();

    /* Gets the underlying input channel. */
    inline sp<InputChannel> getChannel() { return mChannel; }

    /* Publishes a key event to the input channel.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if the channel is full.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Returns BAD_VALUE if seq is 0.
     * Other errors probably indicate that the channel is broken.
     */
    status_t publishKeyEvent(
            uint32_t seq,
            int32_t deviceId,
            int32_t source,
            int32_t action,
            int32_t flags,
            int32_t keyCode,
            int32_t scanCode,
            int32_t metaState,
            int32_t repeatCount,
            nsecs_t downTime,
            nsecs_t eventTime);

    /* Publishes a motion event to the input channel.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if the channel is full.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Returns BAD_VALUE if seq is 0 or if pointerCount is less than 1 or greater than MAX_POINTERS.
     * Other errors probably indicate that the channel is broken.
     */
    status_t publishMotionEvent(
            uint32_t seq,
            int32_t deviceId,
            int32_t source,
            int32_t action,
            int32_t flags,
            int32_t edgeFlags,
            int32_t metaState,
            int32_t buttonState,
            float xOffset,
            float yOffset,
            float xPrecision,
            float yPrecision,
            nsecs_t downTime,
            nsecs_t eventTime,
            size_t pointerCount,
            const PointerProperties* pointerProperties,
            const PointerCoords* pointerCoords);

    /* Receives the finished signal from the consumer in reply to the original dispatch signal.
     * If a signal was received, returns the message sequence number,
     * and whether the consumer handled the message.
     *
     * The returned sequence number is never 0 unless the operation failed.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if there is no signal present.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Other errors probably indicate that the channel is broken.
     */
    status_t receiveFinishedSignal(uint32_t* outSeq, bool* outHandled);

private:
    sp<InputChannel> mChannel;
};

/*
 * Consumes input events from an input channel.
 */
class InputConsumer {
public:
    /* Creates a consumer associated with an input channel. */
    explicit InputConsumer(const sp<InputChannel>& channel);

    /* Destroys the consumer and releases its input channel. */
    ~InputConsumer();

    /* Gets the underlying input channel. */
    inline sp<InputChannel> getChannel() { return mChannel; }

    /* Consumes an input event from the input channel and copies its contents into
     * an InputEvent object created using the specified factory.
     *
     * Tries to combine a series of move events into larger batches whenever possible.
     *
     * If consumeBatches is false, then defers consuming pending batched events if it
     * is possible for additional samples to be added to them later.  Call hasPendingBatch()
     * to determine whether a pending batch is available to be consumed.
     *
     * If consumeBatches is true, then events are still batched but they are consumed
     * immediately as soon as the input channel is exhausted.
     *
     * The returned sequence number is never 0 unless the operation failed.
     *
     * Returns OK on success.
     * Returns WOULD_BLOCK if there is no event present.
     * Returns DEAD_OBJECT if the channel's peer has been closed.
     * Returns NO_MEMORY if the event could not be created.
     * Other errors probably indicate that the channel is broken.
     */
    status_t consume(InputEventFactoryInterface* factory, bool consumeBatches,
            uint32_t* outSeq, InputEvent** outEvent);

    /* Sends a finished signal to the publisher to inform it that the message
     * with the specified sequence number has finished being process and whether
     * the message was handled by the consumer.
     *
     * Returns OK on success.
     * Returns BAD_VALUE if seq is 0.
     * Other errors probably indicate that the channel is broken.
     */
    status_t sendFinishedSignal(uint32_t seq, bool handled);

    /* Returns true if there is a pending batch. */
    bool hasPendingBatch() const;

private:
    sp<InputChannel> mChannel;

    // The current input message.
    InputMessage mMsg;

    // True if mMsg contains a valid input message that was deferred from the previous
    // call to consume and that still needs to be handled.
    bool mMsgDeferred;

    // Batched motion events per device and source.
    struct Batch {
        uint32_t seq; // sequence number of last input message batched in the event
        MotionEvent event;
    };
    Vector<Batch> mBatches;

    // Chain of batched sequence numbers.  When multiple input messages are combined into
    // a batch, we append a record here that associates the last sequence number in the
    // batch with the previous one.  When the finished signal is sent, we traverse the
    // chain to individually finish all input messages that were part of the batch.
    struct SeqChain {
        uint32_t seq;   // sequence number of batched input message
        uint32_t chain; // sequence number of previous batched input message
    };
    Vector<SeqChain> mSeqChains;

    ssize_t findBatch(int32_t deviceId, int32_t source) const;
    status_t sendUnchainedFinishedSignal(uint32_t seq, bool handled);

    static void initializeKeyEvent(KeyEvent* event, const InputMessage* msg);
    static void initializeMotionEvent(MotionEvent* event, const InputMessage* msg);
    static bool canAppendSamples(const MotionEvent* event, const InputMessage* msg);
    static void appendSamples(MotionEvent* event, const InputMessage* msg);
};

} // namespace android

#endif // _UI_INPUT_TRANSPORT_H
