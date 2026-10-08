#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * AER-02F synthetic-only virtual drive-board model.
 * No serial, SDL, device, motor, or loader interfaces are present here.
 * Responses are supplied by each test as explicit assumptions rather than
 * being presented as recovered Sega firmware behavior.
 */

enum { MAX_BOARDS = 2, RESPONSE_QUEUE_CAPACITY = 8 };

typedef enum {
    BOARD_DISCONNECTED,
    BOARD_IDLE,
    BOARD_INITIALIZING,
    BOARD_CONFIGURING,
    BOARD_CALIBRATING,
    BOARD_READY,
    BOARD_SHUTDOWN,
    BOARD_FAULT
} BoardLifecycle;

typedef struct {
    uint8_t status;
    unsigned ready_tick;
} QueuedResponse;

typedef struct {
    int board_count;
    bool connected[MAX_BOARDS];
    BoardLifecycle lifecycle[MAX_BOARDS];
    QueuedResponse queue[MAX_BOARDS][RESPONSE_QUEUE_CAPACITY];
    unsigned queue_head[MAX_BOARDS];
    unsigned queue_count[MAX_BOARDS];
    unsigned tick;
    bool physical_transport_accessed;
} VirtualDriveBoard;

typedef struct {
    uint8_t command;
    uint8_t argument1;
    uint8_t argument2;
} LogicalRequest;

static uint8_t encode_status(uint8_t status)
{
    assert(status <= 7);
    return (uint8_t)(status | (status << 4));
}

static int validate_response(uint8_t encoded)
{
    if (encoded == 0xee) return -2;
    if ((encoded & 0x08) != 0) return -1;
    if ((encoded & 0x07) != ((encoded >> 4) & 0x07)) return -1;
    return encoded & 0x07;
}

static size_t frame_requests(const LogicalRequest *requests, int boards,
                             uint8_t *frame, size_t capacity)
{
    if (!requests || !frame || boards < 1 || boards > MAX_BOARDS)
        return 0;
    size_t length = (size_t)(boards * 3 + 1);
    if (capacity < length) return 0;
    uint8_t checksum = 0;
    for (int board = 0; board < boards; ++board) {
        size_t offset = (size_t)board * 3;
        uint8_t command = requests[board].command & 0x7f;
        frame[offset] = board == 0 ? (uint8_t)(command | 0x80) : command;
        frame[offset + 1] = requests[board].argument1;
        frame[offset + 2] = requests[board].argument2;
        checksum ^= command;
        checksum ^= requests[board].argument1;
        checksum ^= requests[board].argument2;
    }
    frame[length - 1] = checksum;
    return length;
}

static int decode_frame(const uint8_t *frame, size_t length, int boards,
                        LogicalRequest *requests)
{
    if (!frame || !requests || boards < 1 || boards > MAX_BOARDS ||
        length != (size_t)(boards * 3 + 1) || (frame[0] & 0x80) == 0)
        return 0;
    uint8_t checksum = 0;
    for (int board = 0; board < boards; ++board) {
        size_t offset = (size_t)board * 3;
        uint8_t command = frame[offset];
        if (board == 0) command &= 0x7f;
        requests[board].command = command;
        requests[board].argument1 = frame[offset + 1];
        requests[board].argument2 = frame[offset + 2];
        checksum ^= command;
        checksum ^= frame[offset + 1];
        checksum ^= frame[offset + 2];
    }
    return checksum == frame[length - 1];
}

static void virtual_board_init(VirtualDriveBoard *board, int board_count)
{
    memset(board, 0, sizeof(*board));
    board->board_count = board_count;
    if (board_count < 1 || board_count > MAX_BOARDS) {
        for (int i = 0; i < MAX_BOARDS; ++i) board->lifecycle[i] = BOARD_FAULT;
        return;
    }
    for (int i = 0; i < board_count; ++i) {
        board->connected[i] = true;
        board->lifecycle[i] = BOARD_IDLE;
    }
}

static void fail_all(VirtualDriveBoard *board)
{
    for (int i = 0; i < MAX_BOARDS; ++i) {
        board->lifecycle[i] = BOARD_FAULT;
        board->queue_count[i] = 0;
    }
}

static bool known_initialization_command(uint8_t command)
{
    switch (command) {
        case 0x7f: case 0x01: case 0x7c: case 0x7d:
        case 0x7a: case 0x03: case 0x06: case 0x08:
        case 0x00: case 0x04:
            return true;
        default:
            return false;
    }
}

static bool virtual_board_accept_frame(VirtualDriveBoard *board,
                                       const uint8_t *frame, size_t length)
{
    LogicalRequest requests[MAX_BOARDS];
    if (board->board_count < 1 || board->board_count > MAX_BOARDS ||
        !decode_frame(frame, length, board->board_count, requests)) {
        fail_all(board);
        return false;
    }
    for (int i = 0; i < board->board_count; ++i) {
        if (!board->connected[i] || !known_initialization_command(requests[i].command)) {
            fail_all(board);
            return false;
        }
        switch (requests[i].command) {
            case 0x7f: case 0x01: case 0x7c: case 0x7d:
                board->lifecycle[i] = BOARD_INITIALIZING;
                break;
            case 0x7a: case 0x03: case 0x06: case 0x08:
                board->lifecycle[i] = BOARD_CONFIGURING;
                break;
            case 0x00: case 0x04:
                board->lifecycle[i] = BOARD_CALIBRATING;
                break;
        }
    }
    return true;
}

static bool virtual_board_queue_assumed_status(VirtualDriveBoard *board, int index,
                                               uint8_t status, unsigned delay_ticks)
{
    if (index < 0 || index >= board->board_count || !board->connected[index] ||
        status > 7 || board->queue_count[index] >= RESPONSE_QUEUE_CAPACITY) {
        fail_all(board);
        return false;
    }
    unsigned slot = (board->queue_head[index] + board->queue_count[index]) % RESPONSE_QUEUE_CAPACITY;
    board->queue[index][slot].status = status;
    board->queue[index][slot].ready_tick = board->tick + delay_ticks;
    ++board->queue_count[index];
    return true;
}

static bool virtual_board_poll(VirtualDriveBoard *board, int index, uint8_t *encoded)
{
    if (index < 0 || index >= board->board_count || !board->connected[index] ||
        board->queue_count[index] == 0)
        return false;
    QueuedResponse *response = &board->queue[index][board->queue_head[index]];
    if (response->ready_tick > board->tick) return false;
    *encoded = encode_status(response->status);
    board->queue_head[index] = (board->queue_head[index] + 1) % RESPONSE_QUEUE_CAPACITY;
    --board->queue_count[index];
    return true;
}

static void virtual_board_disconnect(VirtualDriveBoard *board, int index)
{
    if (index < 0 || index >= board->board_count) return;
    board->connected[index] = false;
    fail_all(board);
}

static void virtual_board_mark_ready(VirtualDriveBoard *board)
{
    for (int i = 0; i < board->board_count; ++i) {
        if (!board->connected[i] || board->lifecycle[i] == BOARD_FAULT) {
            fail_all(board);
            return;
        }
        board->lifecycle[i] = BOARD_READY;
    }
}

static void virtual_board_shutdown(VirtualDriveBoard *board)
{
    for (int i = 0; i < MAX_BOARDS; ++i) {
        board->queue_count[i] = 0;
        board->lifecycle[i] = BOARD_SHUTDOWN;
    }
}

static void test_protocol_framing_and_validation(void)
{
    const LogicalRequest one[] = {{0x7a, 0x00, 0x1f}};
    uint8_t frame[7];
    LogicalRequest decoded[MAX_BOARDS];
    size_t length = frame_requests(one, 1, frame, sizeof(frame));
    assert(length == 4 && frame[0] == 0xfa);
    assert(decode_frame(frame, length, 1, decoded));
    assert(memcmp(one, decoded, sizeof(one)) == 0);
    frame[3] ^= 1;
    assert(!decode_frame(frame, length, 1, decoded));

    const LogicalRequest two[] = {{0x03, 0x32, 0x04}, {0x06, 0x01, 0x02}};
    length = frame_requests(two, 2, frame, sizeof(frame));
    assert(length == 7 && decode_frame(frame, length, 2, decoded));
    assert(memcmp(two, decoded, sizeof(two)) == 0);

    assert(validate_response(0x00) == 0);
    assert(validate_response(0x11) == 1);
    assert(validate_response(0x44) == 4);
    assert(validate_response(0x12) == -1);
    assert(validate_response(0x08) == -1);
    assert(validate_response(0xee) == -2);
}

static void test_valid_synthetic_lifecycle(void)
{
    VirtualDriveBoard board;
    virtual_board_init(&board, 1);
    const uint8_t commands[] = {0x7f, 0x01, 0x7c, 0x7d, 0x7a, 0x03, 0x06, 0x08, 0x00, 0x04};
    for (size_t i = 0; i < sizeof(commands); ++i) {
        LogicalRequest request = {commands[i], 0, 0};
        uint8_t frame[4];
        assert(frame_requests(&request, 1, frame, sizeof(frame)) == 4);
        assert(virtual_board_accept_frame(&board, frame, sizeof(frame)));
        assert(virtual_board_queue_assumed_status(&board, 0, 0, 0));
        uint8_t response = 0xff;
        assert(virtual_board_poll(&board, 0, &response));
        assert(response == 0x00);
    }
    virtual_board_mark_ready(&board);
    assert(board.lifecycle[0] == BOARD_READY);
    assert(!board.physical_transport_accessed);
    virtual_board_shutdown(&board);
    assert(board.lifecycle[0] == BOARD_SHUTDOWN);
}

static void test_delayed_response_and_timeout_model(void)
{
    VirtualDriveBoard board;
    virtual_board_init(&board, 1);
    assert(virtual_board_queue_assumed_status(&board, 0, 1, 3));
    uint8_t response;
    assert(!virtual_board_poll(&board, 0, &response));
    board.tick = 2;
    assert(!virtual_board_poll(&board, 0, &response));
    board.tick = 3;
    assert(virtual_board_poll(&board, 0, &response));
    assert(response == 0x11);
    /* No queued response remains: the game-side model owns timeout policy. */
    assert(!virtual_board_poll(&board, 0, &response));
}

static void test_disconnect_malformed_and_overflow_fail_closed(void)
{
    VirtualDriveBoard disconnected, active_disconnect, malformed, overflow;
    virtual_board_init(&disconnected, 1);
    virtual_board_disconnect(&disconnected, 0);
    assert(disconnected.lifecycle[0] == BOARD_FAULT);

    virtual_board_init(&active_disconnect, 1);
    virtual_board_mark_ready(&active_disconnect);
    assert(active_disconnect.lifecycle[0] == BOARD_READY);
    virtual_board_disconnect(&active_disconnect, 0);
    assert(active_disconnect.lifecycle[0] == BOARD_FAULT);

    virtual_board_init(&malformed, 1);
    LogicalRequest unknown = {0x55, 0, 0};
    uint8_t frame[4];
    assert(frame_requests(&unknown, 1, frame, sizeof(frame)) == 4);
    assert(!virtual_board_accept_frame(&malformed, frame, sizeof(frame)));
    assert(malformed.lifecycle[0] == BOARD_FAULT);

    virtual_board_init(&overflow, 1);
    for (int i = 0; i < RESPONSE_QUEUE_CAPACITY; ++i)
        assert(virtual_board_queue_assumed_status(&overflow, 0, 0, 100));
    assert(!virtual_board_queue_assumed_status(&overflow, 0, 0, 100));
    assert(overflow.lifecycle[0] == BOARD_FAULT && overflow.queue_count[0] == 0);
}

static void test_dual_board_and_repeated_lifecycle(void)
{
    VirtualDriveBoard board;
    for (int cycle = 0; cycle < 3; ++cycle) {
        virtual_board_init(&board, 2);
        LogicalRequest requests[2] = {{0x7f, 0, 0}, {0x7f, 0, 0}};
        uint8_t frame[7];
        assert(frame_requests(requests, 2, frame, sizeof(frame)) == 7);
        assert(virtual_board_accept_frame(&board, frame, sizeof(frame)));
        for (int i = 0; i < 2; ++i)
            assert(virtual_board_queue_assumed_status(&board, i, 0, 0));
        virtual_board_mark_ready(&board);
        assert(board.lifecycle[0] == BOARD_READY && board.lifecycle[1] == BOARD_READY);
        virtual_board_shutdown(&board);
        assert(board.lifecycle[0] == BOARD_SHUTDOWN && board.lifecycle[1] == BOARD_SHUTDOWN);
    }
}

static void test_invalid_board_configuration(void)
{
    VirtualDriveBoard none, excessive;
    virtual_board_init(&none, 0);
    virtual_board_init(&excessive, 3);
    assert(none.lifecycle[0] == BOARD_FAULT);
    assert(excessive.lifecycle[0] == BOARD_FAULT);
}

int main(void)
{
    test_protocol_framing_and_validation();
    test_valid_synthetic_lifecycle();
    test_delayed_response_and_timeout_model();
    test_disconnect_malformed_and_overflow_fail_closed();
    test_dual_board_and_repeated_lifecycle();
    test_invalid_board_configuration();
    puts("AER-02F virtual drive-board model: all tests passed");
    return 0;
}
