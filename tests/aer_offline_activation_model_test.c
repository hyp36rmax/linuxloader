#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
 * AER-02D research-only model of Jennifer DVP-0015A's activation gates.
 * It is intentionally not linked into LinuxLoader and cannot drive hardware.
 * ResponseStatus models the validated status produced by
 * CabinetCtrlMIDI_Input(), not an invented physical-board packet.
 */
typedef enum {
    RESPONSE_MISSING,
    RESPONSE_STATUS_0,
    RESPONSE_STATUS_1,
    RESPONSE_OTHER_VALID,
    RESPONSE_INVALID,
    RESPONSE_SPECIAL_EE
} ResponseStatus;

typedef struct {
    int driver_state;
    int check_state;
    int timer;
    int retries;
    int board;
    int boards;
    int init_step;
    int check_entry;
    int calibration_samples;
    bool driver_error;
    bool callback_installed;
    bool callback_ran;
    bool stopped;
} ActivationModel;

static void model_reset(ActivationModel *m, int boards)
{
    memset(m, 0, sizeof(*m));
    m->boards = boards;
}

static bool accepted_probe_response(ResponseStatus response)
{
    return response == RESPONSE_STATUS_0 || response == RESPONSE_STATUS_1 ||
           response == RESPONSE_OTHER_VALID;
}

static void fail_driver(ActivationModel *m)
{
    m->driver_state = 11;
    m->driver_error = true;
}

/* One invocation of the original CabinetCtrl_InitDriver state machine. */
static void native_driver_tick(ActivationModel *m, bool baseboard_available,
                               bool application_error, ResponseStatus response,
                               bool calibration_converged)
{
    if (m->stopped || m->driver_state == 11 || m->driver_state == 12)
        return;

    switch (m->driver_state) {
        case 0:
            if (!baseboard_available)
                return;
            if (application_error) {
                fail_driver(m);
                return;
            }
            m->timer = 90;
            m->driver_state = 1;
            break;
        case 1:
            if (--m->timer <= 0) {
                m->board = 0;
                m->retries = 0;
                m->driver_state = 2;
            }
            break;
        case 2:
            if (++m->retries > 2) {
                fail_driver(m);
                return;
            }
            /* Original sends the 0x7f probe and starts a 16-tick window. */
            m->timer = 16;
            m->driver_state = 3;
            break;
        case 3:
            if (accepted_probe_response(response)) {
                /* Original sends the 0x01/0x30/0x7f request. */
                m->timer = 32;
                m->driver_state = 4;
            } else if (response == RESPONSE_INVALID || response == RESPONSE_SPECIAL_EE) {
                m->driver_state = 2;
            } else if (--m->timer <= 0) {
                fail_driver(m);
            }
            break;
        case 4:
            if (response == RESPONSE_STATUS_1) {
                /* Original sends 0x7c and allows 600 ticks. */
                m->timer = 600;
                m->driver_state = 5;
            } else if (response != RESPONSE_MISSING) {
                /* Original repeats the request until status 1 or timeout. */
            } else if (--m->timer <= 0) {
                m->driver_state = 2;
            }
            break;
        case 5:
            if (response == RESPONSE_STATUS_0) {
                m->init_step = 0;
                m->driver_state = 6;
            } else if (response != RESPONSE_MISSING) {
                /* Original sends the 0x7d idle/neutral request. */
            } else if (--m->timer <= 0) {
                m->driver_state = 2;
            }
            break;
        case 6:
            /* Four evidenced configuration requests: 0x7a, 0x03, 0x06, 0x08. */
            m->timer = 10;
            m->driver_state = 7;
            break;
        case 7:
            if (response == RESPONSE_STATUS_0) {
                if (++m->init_step > 3) {
                    m->timer = 900;
                    m->calibration_samples = 0;
                    m->driver_state = 8;
                } else {
                    m->driver_state = 6;
                }
            } else if (response != RESPONSE_MISSING) {
                m->driver_state = 6;
            } else if (--m->timer < 0) {
                fail_driver(m);
            }
            break;
        case 8:
            ++m->calibration_samples;
            if (calibration_converged)
                m->driver_state = 9;
            else if (--m->timer < 0)
                fail_driver(m);
            break;
        case 9:
            /* Stop the calibration request, restore configured board power. */
            if (++m->board < m->boards) {
                m->retries = 0;
                m->driver_state = 2;
            } else {
                m->driver_state = 10;
            }
            break;
        case 10:
            /* The original drains pending response data before activation. */
            if (response != RESPONSE_MISSING)
                m->driver_state = 12;
            break;
        default:
            fail_driver(m);
            break;
    }
}

/* One invocation of CabinetCtrl_Check(). */
static void native_check_tick(ActivationModel *m)
{
    if (m->stopped || m->driver_state != 12 || m->boards == 0)
        return;
    if (m->check_state == 0) {
        /* Two entries from the native 16x16 check table per invocation. */
        m->check_entry += 2;
        if (m->check_entry >= 256)
            m->check_state = 1;
    } else if (m->check_state == 1) {
        m->callback_installed = true;
        m->check_state = 2;
    }
}

static void event_control_tick(ActivationModel *m, bool paused, bool suspended)
{
    if (!m->stopped && m->callback_installed && !paused && !suspended)
        m->callback_ran = true;
}

static void shutdown_model(ActivationModel *m)
{
    m->stopped = true;
    m->callback_installed = false;
}

static void run_successful_board(ActivationModel *m)
{
    int guard = 0;
    while (m->driver_state != 12 && !m->driver_error && guard++ < 3000) {
        ResponseStatus response = RESPONSE_MISSING;
        bool converged = false;
        if (m->driver_state == 3) response = RESPONSE_STATUS_0;
        if (m->driver_state == 4) response = RESPONSE_STATUS_1;
        if (m->driver_state == 5 || m->driver_state == 7) response = RESPONSE_STATUS_0;
        if (m->driver_state == 8) converged = true;
        if (m->driver_state == 10) response = RESPONSE_STATUS_0;
        native_driver_tick(m, true, false, response, converged);
    }
    assert(guard < 3000);
}

static void test_normal_initialization_and_callback(void)
{
    ActivationModel m;
    model_reset(&m, 1);
    run_successful_board(&m);
    assert(m.driver_state == 12 && !m.driver_error);
    for (int i = 0; i < 128; ++i) native_check_tick(&m);
    assert(m.check_state == 1 && !m.callback_installed);
    native_check_tick(&m);
    assert(m.check_state == 2 && m.callback_installed);
    event_control_tick(&m, false, false);
    assert(m.callback_ran);
}

static void test_missing_hardware_and_invalid_response(void)
{
    ActivationModel missing, invalid;
    model_reset(&missing, 1);
    for (int i = 0; i < 200; ++i)
        native_driver_tick(&missing, false, false, RESPONSE_MISSING, false);
    assert(missing.driver_state == 0 && !missing.driver_error);

    model_reset(&invalid, 1);
    native_driver_tick(&invalid, true, false, RESPONSE_MISSING, false);
    for (int i = 0; i < 90; ++i)
        native_driver_tick(&invalid, true, false, RESPONSE_MISSING, false);
    for (int attempt = 0; attempt < 3 && !invalid.driver_error; ++attempt) {
        native_driver_tick(&invalid, true, false, RESPONSE_MISSING, false);
        native_driver_tick(&invalid, true, false, RESPONSE_INVALID, false);
    }
    assert(invalid.driver_state == 11 && invalid.driver_error);
}

static void test_timeout(void)
{
    ActivationModel m;
    model_reset(&m, 1);
    native_driver_tick(&m, true, false, RESPONSE_MISSING, false);
    for (int i = 0; i < 90; ++i)
        native_driver_tick(&m, true, false, RESPONSE_MISSING, false);
    while (!m.driver_error) {
        native_driver_tick(&m, true, false, RESPONSE_MISSING, false);
        for (int i = 0; i < 17 && !m.driver_error; ++i)
            native_driver_tick(&m, true, false, RESPONSE_MISSING, false);
    }
    assert(m.driver_state == 11);
}

static void test_application_error_and_calibration_timeout(void)
{
    ActivationModel application_error, calibration_timeout;
    model_reset(&application_error, 1);
    native_driver_tick(&application_error, true, true, RESPONSE_MISSING, false);
    assert(application_error.driver_state == 11 && application_error.driver_error);

    model_reset(&calibration_timeout, 1);
    calibration_timeout.driver_state = 8;
    calibration_timeout.timer = 1;
    native_driver_tick(&calibration_timeout, true, false, RESPONSE_MISSING, false);
    native_driver_tick(&calibration_timeout, true, false, RESPONSE_MISSING, false);
    assert(calibration_timeout.driver_state == 11 && calibration_timeout.driver_error);
}

static void test_loader_skip_divergence(void)
{
    ActivationModel m;
    model_reset(&m, 1);
    /* Current loader replacement returns success but writes no native state. */
    bool replacement_returned_one = true;
    (void)replacement_returned_one;
    for (int i = 0; i < 200; ++i) native_check_tick(&m);
    assert(m.driver_state == 0);
    assert(m.check_state == 0);
    assert(!m.callback_installed);
}

static void test_pause_suspend_shutdown(void)
{
    ActivationModel m;
    model_reset(&m, 1);
    m.driver_state = 12;
    m.check_state = 2;
    m.callback_installed = true;
    event_control_tick(&m, true, false);
    event_control_tick(&m, false, true);
    assert(!m.callback_ran);
    event_control_tick(&m, false, false);
    assert(m.callback_ran);
    m.callback_ran = false;
    shutdown_model(&m);
    event_control_tick(&m, false, false);
    assert(!m.callback_ran && !m.callback_installed);
}

static void test_single_and_dual_board(void)
{
    ActivationModel single, dual;
    model_reset(&single, 1);
    model_reset(&dual, 2);
    run_successful_board(&single);
    run_successful_board(&dual);
    assert(single.driver_state == 12 && single.board == 1);
    assert(dual.driver_state == 12 && dual.board == 2);
}

static void test_zero_board_cannot_install_callback(void)
{
    ActivationModel m;
    model_reset(&m, 0);
    m.driver_state = 12;
    for (int i = 0; i < 200; ++i) native_check_tick(&m);
    assert(m.check_state == 0 && !m.callback_installed);
}

int main(void)
{
    test_normal_initialization_and_callback();
    test_missing_hardware_and_invalid_response();
    test_timeout();
    test_application_error_and_calibration_timeout();
    test_loader_skip_divergence();
    test_pause_suspend_shutdown();
    test_single_and_dual_board();
    test_zero_board_cannot_install_callback();
    puts("AER-02D offline activation model: all tests passed");
    return 0;
}
