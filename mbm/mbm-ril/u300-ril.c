/* ST-Ericsson U300 RIL
 *
 * Copyright (C) ST-Ericsson AB 2008-2014
 * Copyright 2006, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on reference-ril by The Android Open Source Project.
 *
 * Heavily modified for ST-Ericsson U300 modems.
 * Author: Christian Bejram <christian.bejram@stericsson.com>
 */

#include <telephony/ril.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>
#include <stdbool.h>
#include <cutils/properties.h>

#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"

#include "u300-ril.h"
#include "u300-ril-config.h"
#include "u300-ril-messaging.h"
#include "u300-ril-network.h"
#include "u300-ril-pdp.h"
#include "u300-ril-sim.h"
#include "u300-ril-oem.h"
#include "u300-ril-requestdatahandler.h"
#include "u300-ril-error.h"
#include "u300-ril-stk.h"
#include "u300-ril-device.h"
#include "mbm-ril.h"

#define LOG_TAG "RIL"
#include <utils/Log.h>

#define RIL_VERSION_STRING "MBM u300-ril 4.0-beta"

#define MAX_AT_RESPONSE 0x1000

#define MESSAGE_STORAGE_READY_TIMER 3

#define timespec_cmp(a, b, op)         \
        ((a).tv_sec == (b).tv_sec    \
        ? (a).tv_nsec op (b).tv_nsec \
        : (a).tv_sec op (b).tv_sec)

#define TIMEOUT_SEARCH_FOR_TTY 1 /* Poll every Xs for the port*/
#define TIMEOUT_EMRDY 15 /* Module should respond at least within 15s */
#define TIMEOUT_DEVICE_REMOVED 3
#define MAX_BUF 2048

/*** Global Variables ***/
char* ril_iface;
const struct RIL_Env *s_rilenv;

/*** Declarations ***/
static const char *getVersion(void);
static void signalCloseQueues(void);
static void onRequest(int request, void *data, size_t datalen,
                      RIL_Token t);
static int onSupports(int requestCode);
static void onCancel(RIL_Token t);
extern const char *requestToString(int request);

/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks = {
    RIL_VERSION,
    onRequest,
    getRadioState,
    onSupports,
    onCancel,
    getVersion
};

/*TODO: fix this bad this can dead lock?!?!?*/
static pthread_mutex_t s_screen_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_tid_queueRunner;
static pthread_t s_tid_queueRunnerPrio;

static int s_screenState = true;

typedef struct RILRequest {
    int request;
    void *data;
    size_t datalen;
    RIL_Token token;
    struct RILRequest *next;
} RILRequest;

typedef struct RILEvent {
    void (*eventCallback) (void *param);
    void *param;
    char *name;
    struct timespec abstime;
    struct RILEvent *next;
    struct RILEvent *prev;
} RILEvent;

typedef struct RequestQueue {
    pthread_mutex_t queueMutex;
    pthread_cond_t cond;
    RILRequest *requestList;
    RILEvent *eventList;
    char enabled;
    char closed;
} RequestQueue;

static RequestQueue s_requestQueue = {
    .queueMutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .requestList = NULL,
    .eventList = NULL,
    .enabled = 1,
    .closed = 1
};

static RequestQueue s_requestQueuePrio = {
    .queueMutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .requestList = NULL,
    .eventList = NULL,
    .enabled = 0,
    .closed = 1
};

static RequestQueue *s_requestQueues[] = {
    &s_requestQueue,
    &s_requestQueuePrio
};

static const struct timespec TIMEVAL_0 = { 0, 0 };

/**
 * Enqueue a RILEvent to the request queue. isPrio specifies in what queue
 * the request will end up.
 *
 * 0 = the "normal" queue, 1 = prio queue and 2 = both. If only one queue
 * is present, then the event will be inserted into that queue.
 */
void enqueueRILEventName(int isPrio, void (*callback) (void *param),
                     void *param, const struct timespec *relativeTime, char *name)
{
    int err;
    struct timespec ts;
    char done = 0;
    RequestQueue *q = NULL;

    if (NULL == callback) {
        ALOGE("%s() callback is NULL, event not queued!", __func__);
        return;
    }

    RILEvent *e = malloc(sizeof(RILEvent));
    memset(e, 0, sizeof(RILEvent));

    e->eventCallback = callback;
    e->param = param;
    e->name = name;

    if (relativeTime == NULL) {
        relativeTime = alloca(sizeof(struct timeval));
        memset((struct timeval *) relativeTime, 0, sizeof(struct timeval));
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);

    e->abstime.tv_sec = ts.tv_sec + relativeTime->tv_sec;
    e->abstime.tv_nsec = ts.tv_nsec + relativeTime->tv_nsec;

    if (e->abstime.tv_nsec > 1000000000) {
        e->abstime.tv_sec++;
        e->abstime.tv_nsec -= 1000000000;
    }

    if (!s_requestQueuePrio.enabled ||
        (isPrio == RIL_EVENT_QUEUE_NORMAL || isPrio == RIL_EVENT_QUEUE_ALL)) {
        q = &s_requestQueue;
    } else if (isPrio == RIL_EVENT_QUEUE_PRIO)
        q = &s_requestQueuePrio;

again:
    if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
        ALOGE("%s() failed to take queue mutex: %s!", __func__, strerror(err));

    if (q->eventList == NULL)
        q->eventList = e;
    else {
        if (timespec_cmp(q->eventList->abstime, e->abstime, > )) {
            e->next = q->eventList;
            q->eventList->prev = e;
            q->eventList = e;
        } else {
            RILEvent *tmp = q->eventList;
            do {
                if (timespec_cmp(tmp->abstime, e->abstime, > )) {
                    tmp->prev->next = e;
                    e->prev = tmp->prev;
                    tmp->prev = e;
                    e->next = tmp;
                    break;
                } else if (tmp->next == NULL) {
                    tmp->next = e;
                    e->prev = tmp;
                    break;
                }
                tmp = tmp->next;
            } while (tmp);
        }
    }

    if ((err = pthread_cond_broadcast(&q->cond)) != 0)
        ALOGE("%s() failed to take broadcast queue update: %s!",
            __func__, strerror(err));

    if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
        ALOGE("%s() failed to release queue mutex: %s!",
            __func__, strerror(err));

    if (s_requestQueuePrio.enabled && isPrio == RIL_EVENT_QUEUE_ALL && !done) {
        RILEvent *e2 = malloc(sizeof(RILEvent));
        memcpy(e2, e, sizeof(RILEvent));
        e = e2;
        done = 1;
        q = &s_requestQueuePrio;

        goto again;
    }
}

/**
 * Will LOCK THE MUTEX! MAKE SURE TO RELEASE IT!
 */
void getScreenStateLock(void)
{
    int err;

    /* Just make sure we're not changing anything with regards to screen state. */
    if ((err = pthread_mutex_lock(&s_screen_state_mutex)) != 0)
        ALOGE("%s() failed to take screen state mutex: %s",
            __func__,  strerror(err));
}

int getScreenState(void)
{
    return s_screenState;
}

void releaseScreenStateLock(void)
{
    int err;

    if ((err = pthread_mutex_unlock(&s_screen_state_mutex)) != 0)
        ALOGE("%s() failed to release screen state mutex: %s",
            __func__,  strerror(err));

}

void setScreenState(int screenState)
{
    if (screenState == 1) {
        /* Screen is on - be sure to enable all unsolicited notifications again. */

        /* Configure Network Registration Status.
        *  Configure Packet Domain Network Rregistration Status
        *     n = 0 - disable network registration unsolicited result code.
        *       = 1 - enable network registration unsolicited result code.
        *       = 2 - enable network registration and location information unsolicited
        *             result code.
        */
        at_send_command("AT+CREG=2");
        at_send_command("AT+CGREG=2");

        /* Configure Packet Domain Event Reporting.
        *  mode = 0 - buffer unsolicited result codes in the MT. No codes are
        *             forwarded to the TE.
        *       = 1 - discard unsolicited result codes when MT-TE link is reserved,
        *             for example, in online data mode), otherwise forward them
        *             directly to the TE.
        *   bfr = 0 - MT buffer of unsolicited result codes defined within this
        *             command is cleared when <mode> 1 is entered
        */
        at_send_command("AT+CGEREP=1,0");

        isSimSmsStorageFull(NULL);
        pollSignalStrength((void *)-1);

        /* Configure Mobile Equipment Event Reporting.
        * mode = 0 - buffer unsolicited result codes in the phone. If the phone
        *            result code buffer is full, codes can be buffered elsewhere
        *            orthe oldest ones can be removed to make room for the new ones.
        *      = 3 - forward the unsolicited result codes directly to the terminal
        *            equipment. Phone terminal equipment link-specific in-band
        *            technique used to embed result codes and data when phone is
        *            in on-line data mode.
        * keyp = 0 - no keypad event reporting.
        *      = 2 - enables keypad event reporting using +CKEV: <key>,<press>.
        * disp = 0 - no display event reporting.
        *  ind = 0 - no indicator event reporting
        *        1 - indicator event reporting using +CIEV: <ind>,<value>.
        *            <ind> indicates the indicator order number (as specified for
        *            +CIND) and <value> is the new value of indicator. Only those
        *            indicator events, which are not caused by +CIND shall be
        *            indicated by the TA to the TE.
        *  bfr = 0 - TA buffer of unsolicited result codes defined within this
        *            command is cleared when <mode> 1...3 is entered.
        */
        at_send_command("AT+CMER=3,0,0,1,0");

    } else if (screenState == 0) {
        /* Screen is off - disable all unsolicited notifications. */
        at_send_command("AT+CREG=0");
        at_send_command("AT+CGREG=0");
        at_send_command("AT+CGEREP=0,0");
        at_send_command("AT+CMER=3,0,0,0,0");
    }
}

static void requestScreenState(void *data, size_t datalen, RIL_Token t)
{
    (void) datalen;

    getScreenStateLock();

    if (datalen < sizeof(int *))
        goto error;

    /* No point of enabling unsolicited if radio is off
       or SIM is locked */
    if (RADIO_STATE_SIM_READY != getRadioState())
        goto success;

    s_screenState = ((int *) data)[0];

    if (s_screenState < 2)
        setScreenState(s_screenState);
    else
        goto error;

success:
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    releaseScreenStateLock();
    return;

error:
    ALOGE("ERROR: requestScreenState failed");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    goto finally;
}

/**
 * RIL_REQUEST_GET_CURRENT_CALLS
 *
 * Requests current call list.
 */
void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t)
{
    (void) data; (void) datalen;

    /* Respond SUCCESS just to omit repeated requests (see ril.h) */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static char isPrioRequest(int request)
{
    unsigned int i;
    for (i = 0; i < sizeof(prioRequests) / sizeof(int); i++)
        if (request == prioRequests[i])
            return 1;
    return 0;
}

static void processRequest(int request, void *data, size_t datalen, RIL_Token t)
{
    char prop[PROPERTY_VALUE_MAX];
    ALOGD("%s() %s", __func__, requestToString(request));

    /*
     * These commands won't accept RADIO_NOT_AVAILABLE, so we just return
     * GENERIC_FAILURE if we're not in SIM_STATE_READY.
     */
    RIL_RadioState radio_state = getRadioState();

    if (radio_state != RADIO_STATE_SIM_READY
        && (request == RIL_REQUEST_WRITE_SMS_TO_SIM ||
            request == RIL_REQUEST_DELETE_SMS_ON_SIM)) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }

    /* Ignore all requests except RIL_REQUEST_GET_SIM_STATUS
     * when RADIO_STATE_UNAVAILABLE.
     */
    if (radio_state == RADIO_STATE_UNAVAILABLE
        && request != RIL_REQUEST_GET_SIM_STATUS) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }

    /* Ignore all non-power requests when RADIO_STATE_OFF
     * (except RIL_REQUEST_GET_SIM_STATUS and a few more).
     */
    if ((radio_state == RADIO_STATE_OFF || radio_state == RADIO_STATE_SIM_NOT_READY)
        && !(request == RIL_REQUEST_RADIO_POWER ||
             request == RIL_REQUEST_GET_SIM_STATUS ||
             request == RIL_REQUEST_STK_GET_PROFILE ||
             request == RIL_REQUEST_STK_SET_PROFILE ||
             request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING ||
             request == RIL_REQUEST_GET_IMEISV ||
             request == RIL_REQUEST_GET_IMEI ||
             request == RIL_REQUEST_BASEBAND_VERSION ||
             request == RIL_REQUEST_SCREEN_STATE)) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }

    /* Don't allow radio operations when sim is absent or locked! */
    if (radio_state == RADIO_STATE_SIM_LOCKED_OR_ABSENT
        && !(request == RIL_REQUEST_ENTER_SIM_PIN ||
             request == RIL_REQUEST_ENTER_SIM_PUK ||
             request == RIL_REQUEST_ENTER_SIM_PIN2 ||
             request == RIL_REQUEST_ENTER_SIM_PUK2 ||
             request == RIL_REQUEST_ENTER_DEPERSONALIZATION_CODE ||
             request == RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION ||
             request == RIL_REQUEST_GET_SIM_STATUS ||
             request == RIL_REQUEST_RADIO_POWER ||
             request == RIL_REQUEST_GET_IMEISV ||
             request == RIL_REQUEST_GET_IMEI ||
             request == RIL_REQUEST_BASEBAND_VERSION ||
             request == RIL_REQUEST_DATA_REGISTRATION_STATE ||
             request == RIL_REQUEST_VOICE_REGISTRATION_STATE ||
             request == RIL_REQUEST_OPERATOR ||
             request == RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE ||
             request == RIL_REQUEST_SCREEN_STATE ||
             request == RIL_REQUEST_GET_CURRENT_CALLS)) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }

    switch (request) {

        case RIL_REQUEST_GET_CURRENT_CALLS:
            if (radio_state == RADIO_STATE_SIM_LOCKED_OR_ABSENT)
                RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
            else
                requestGetCurrentCalls(data, datalen, t);
            break;
        case RIL_REQUEST_SCREEN_STATE:
            requestScreenState(data, datalen, t);
            break;

        /* Data Call Requests */
        case RIL_REQUEST_SETUP_DATA_CALL:
            requestSetupDefaultPDP(data, datalen, t);
            break;
        case RIL_REQUEST_DEACTIVATE_DATA_CALL:
            requestDeactivateDefaultPDP(data, datalen, t);
            break;
        case RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE:
            requestLastPDPFailCause(data, datalen, t);
            break;
        case RIL_REQUEST_DATA_CALL_LIST:
            requestPDPContextList(data, datalen, t);
            break;

        /* SMS Requests */
        case RIL_REQUEST_SEND_SMS:
            requestSendSMS(data, datalen, t);
            break;
        case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
            requestSendSMSExpectMore(data, datalen, t);
            break;
        case RIL_REQUEST_WRITE_SMS_TO_SIM:
            requestWriteSmsToSim(data, datalen, t);
            break;
        case RIL_REQUEST_DELETE_SMS_ON_SIM:
            requestDeleteSmsOnSim(data, datalen, t);
            break;
        case RIL_REQUEST_GET_SMSC_ADDRESS:
            requestGetSMSCAddress(data, datalen, t);
            break;
        case RIL_REQUEST_SET_SMSC_ADDRESS:
            requestSetSMSCAddress(data, datalen, t);
            break;
        case RIL_REQUEST_REPORT_SMS_MEMORY_STATUS:
            requestSmsStorageFull(data, datalen, t);
            break;
         case RIL_REQUEST_SMS_ACKNOWLEDGE:
            requestSMSAcknowledge(data, datalen, t);
            break;
        case RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG:
            requestGSMGetBroadcastSMSConfig(data, datalen, t);
            break;
        case RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG:
            requestGSMSetBroadcastSMSConfig(data, datalen, t);
            break;
        case RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION:
            requestGSMSMSBroadcastActivation(data, datalen, t);
            break;

        /* SIM Handling Requests */
        case RIL_REQUEST_SIM_IO:
            requestSIM_IO(data, datalen, t);
            break;
        case RIL_REQUEST_GET_SIM_STATUS:
            requestGetSimStatus(data, datalen, t);
            break;
        case RIL_REQUEST_ENTER_SIM_PIN:
        case RIL_REQUEST_ENTER_SIM_PUK:
        case RIL_REQUEST_ENTER_SIM_PIN2:
        case RIL_REQUEST_ENTER_SIM_PUK2:
            requestEnterSimPin(data, datalen, t, request);
            break;
        case RIL_REQUEST_CHANGE_SIM_PIN:
            requestChangePassword(data, datalen, t, "SC", request);
            break;
        case RIL_REQUEST_CHANGE_SIM_PIN2:
            requestChangePassword(data, datalen, t, "P2", request);
            break;
        case RIL_REQUEST_QUERY_FACILITY_LOCK:
            requestQueryFacilityLock(data, datalen, t);
            break;
        case RIL_REQUEST_SET_FACILITY_LOCK:
            requestSetFacilityLock(data, datalen, t);
            break;

        /* Network Requests */
        case RIL_REQUEST_ENTER_DEPERSONALIZATION_CODE:
            requestEnterSimPin(data, datalen, t, request);
            break;
        case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
            requestQueryNetworkSelectionMode(data, datalen, t);
            break;
        case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
            requestSetNetworkSelectionAutomatic(data, datalen, t);
            break;
        case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
            requestSetNetworkSelectionManual(data, datalen, t);
            break;
        case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
            requestQueryAvailableNetworks(data, datalen, t);
            break;
        case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
            requestSetPreferredNetworkType(data, datalen, t);
            break;
        case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
            requestGetPreferredNetworkType(data, datalen, t);
            break;
        case RIL_REQUEST_VOICE_REGISTRATION_STATE:
            requestRegistrationState(request, data, datalen, t);
            break;
        case RIL_REQUEST_DATA_REGISTRATION_STATE:
            requestGprsRegistrationState(request, data, datalen, t);
            break;
        case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS:
            property_get("mbm.ril.config.nci", prop, "no");
            if (strstr(prop, "no")) {
                RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
                break;
            }
            else {
                requestNeighboringCellIDs(data, datalen, t);
                break;
            }

        /* OEM */
        /* case RIL_REQUEST_OEM_HOOK_RAW:
            requestOEMHookRaw(data, datalen, t);
            break; */
        case RIL_REQUEST_OEM_HOOK_STRINGS:
            requestOEMHookStrings(data, datalen, t);
            break;

        /* Misc */
        case RIL_REQUEST_SIGNAL_STRENGTH:
            requestSignalStrength(data, datalen, t);
            break;
        case RIL_REQUEST_OPERATOR:
            requestOperator(data, datalen, t);
            break;
        case RIL_REQUEST_RADIO_POWER:
            requestRadioPower(data, datalen, t);
            break;
        case RIL_REQUEST_GET_IMSI:
            requestGetIMSI(data, datalen, t);
            break;
        case RIL_REQUEST_GET_IMEI:                  /* Deprecated */
            requestGetIMEI(data, datalen, t);
            break;
        case RIL_REQUEST_GET_IMEISV:                /* Deprecated */
            requestGetIMEISV(data, datalen, t);
            break;
        case RIL_REQUEST_DEVICE_IDENTITY:
            requestDeviceIdentity(data, datalen, t);
            break;
        case RIL_REQUEST_BASEBAND_VERSION:
            requestBasebandVersion(data, datalen, t);
            break;

        /* SIM Application Toolkit */
        case RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE:
            requestStkSendTerminalResponse(data, datalen, t);
            break;
        case RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND:
            requestStkSendEnvelopeCommand(data, datalen, t);
            break;
        case RIL_REQUEST_STK_GET_PROFILE:
            requestStkGetProfile(data, datalen, t);
            break;
        case RIL_REQUEST_STK_SET_PROFILE:
            requestStkSetProfile(data, datalen, t);
            break;
        case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING:
            requestReportStkServiceIsRunning(data, datalen, t);
            getCachedStkMenu();
            break;
        case RIL_REQUEST_ALLOW_DATA:
            //FLINTMAN may need to do something here
            break;
        case RIL_REQUEST_SIM_AUTHENTICATION:
           //FLINTMAN may need to do something here
            break;
        default:
            ALOGW("FIXME: Unsupported request logged: %s",
                 requestToString(request));
            RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
            break;
    }
}

/*** Callback methods from the RIL library to us ***/

/**
 * Call from RIL to us to make a RIL_REQUEST.
 *
 * Must be completed with a call to RIL_onRequestComplete().
 */
static void onRequest(int request, void *data, size_t datalen, RIL_Token t)
{
    RILRequest *r;
    RequestQueue *q = &s_requestQueue;
    int err;

    if (s_requestQueuePrio.enabled && isPrioRequest(request))
        q = &s_requestQueuePrio;

    r = malloc(sizeof(RILRequest));
    memset(r, 0, sizeof(RILRequest));

    /* Formulate a RILRequest and put it in the queue. */
    r->request = request;
    r->data = dupRequestData(request, data, datalen);
    r->datalen = datalen;
    r->token = t;

    if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
        ALOGE("%s() failed to take queue mutex: %s!", __func__, strerror(err));

    /* Queue empty, just throw r on top. */
    if (q->requestList == NULL)
        q->requestList = r;
    else {
        RILRequest *l = q->requestList;
        while (l->next != NULL)
            l = l->next;

        l->next = r;
    }

    if ((err = pthread_cond_broadcast(&q->cond)) != 0)
        ALOGE("%s() failed to broadcast queue update: %s!",
            __func__, strerror(err));

    if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
        ALOGE("%s() failed to release queue mutex: %s!",
            __func__, strerror(err));
}



/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported".
 *
 * Currently just stubbed with the default value of one. This is currently
 * not used by android, and therefore not implemented here. We return
 * RIL_E_REQUEST_NOT_SUPPORTED when we encounter unsupported requests.
 */
static int onSupports(int requestCode)
{
    (void) requestCode;
    ALOGI("onSupports() called!");

    return 1;
}

/**
 * onCancel() is currently stubbed, because android doesn't use it and
 * our implementation will depend on how a cancellation is handled in
 * the upper layers.
 */
static void onCancel(RIL_Token t)
{
    (void) t;
    ALOGI("onCancel() called!");
}

static const char *getVersion(void)
{
    return RIL_VERSION_STRING;
}

static char initializeCommon(void)
{
    int err = 0;

    set_pending_hotswap(0);
    setE2napState(E2NAP_STATE_UNKNOWN);
    setE2napCause(E2NAP_CAUSE_UNKNOWN);

    if (at_handshake() < 0) {
        LOG_FATAL("Handshake failed!");
        return 1;
    }

    /* Configure/set
     *   command echo (E), result code suppression (Q), DCE response format (V)
     *
     *  E0 = DCE does not echo characters during command state and online
     *       command state
     *  V1 = Display verbose result codes
     */
    err = at_send_command("ATE0V1");
    if (err != AT_NOERROR)
        return 1;

   /* Set default character set. */
    err = at_send_command("AT+CSCS=\"UTF-8\"");
    if (err != AT_NOERROR)
        return 1;

    /* Read out device information. Needs to be done prior to enabling
     * unsolicited responses.
     */
    readDeviceInfo();

    /* Enable +CME ERROR: <err> result code and use numeric <err> values. */
    err = at_send_command("AT+CMEE=1");
    if (err != AT_NOERROR)
        return 1;

    err = at_send_command("AT*E2NAP=1");
    /* TODO: this command may return CME error */
    if (err != AT_NOERROR)
        return 1;

    /* Send the current time of the OS to the module */
    sendTime(NULL);

    /* Try to register for hotswap events. Don't care if it fails. */
    err = at_send_command("AT*EESIMSWAP=1");

    /* Try to register for network capability events. Don't care if it fails. */
    err = at_send_command("AT*ERINFO=1");

    /* Disable Service Reporting. */
    err = at_send_command("AT+CR=0");
    if (err != AT_NOERROR)
        return 1;

    /* Configure carrier detect signal - 1 = DCD follows the connection. */
    err = at_send_command("AT&C=1");
    if (err != AT_NOERROR)
        return 1;

    /* Configure DCE response to Data Termnal Ready signal - 0 = ignore. */
    err = at_send_command("AT&D=0");
    if (err != AT_NOERROR)
        return 1;

    /* Configure Bearer Service Type and HSCSD Non-Transparent Call
     *  +CBST
     *     7 = 9600 bps V.32
     *     0 = Asynchronous connection
     *     1 = Non-transparent connection element
     */
    err = at_send_command("AT+CBST=7,0,1");
    if (err != AT_NOERROR)
        return 1;

    /* restore state of STK */
    if (get_stk_service_running()) {
        init_stk_service();
        getCachedStkMenu();
    }

    return 0;
}

/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0.
 */
static char initializeChannel(void)
{
    int err;

    ALOGD("%s()", __func__);

    ResetHotswap();
    setRadioState(RADIO_STATE_OFF);

    /*
     * SIM Application Toolkit Configuration
     *  n = 0 - Disable SAT unsolicited result codes
     *  stkPrfl = - SIM application toolkit profile in hexadecimal format
     *              starting with first byte of the profile.
     *              See 3GPP TS 11.14[1] for details.
     *
     * Terminal profile is currently empty because stkPrfl is currently
     * overriden by the default profile stored in the modem.
     */

    /* Configure Packet Domain Network Registration Status events
     *    2 = Enable network registration and location information
     *        unsolicited result code
     */
    err = at_send_command("AT+CGREG=2");
    if (err != AT_NOERROR)
        return 1;

    /* Set phone functionality.
     *    4 = Disable the phone's transmit and receive RF circuits.
     */
    err = at_send_command("AT+CFUN=4");
    if (err != AT_NOERROR)
        return 1;

    /* Assume radio is off on error. */
    if (isRadioOn() > 0)
        setRadioState(RADIO_STATE_SIM_NOT_READY);

    return 0;
}

/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0.
 */
static char initializePrioChannel(void)
{
    int err;

    ALOGD("%s()", __func__);

    /* Subscribe to Pin code event.
     *   The command requests the MS to report when the PIN code has been
     *   inserted and accepted.
     *      1 = Request for report on inserted PIN code is activated (on)
     */
    err = at_send_command("AT*EPEE=1");
    if (err != AT_NOERROR)
        return 1;

    return 0;
}

/**
 * Called by atchannel when an unsolicited line appears.
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here.
 */
static void onUnsolicited(const char *s, const char *sms_pdu)
{
    /* Ignore unsolicited responses until we're initialized.
       This is OK because the RIL library will poll for initial state. */
    if (getRadioState() == RADIO_STATE_UNAVAILABLE)
        return;

    if (strStartsWith(s, "*ETZV:")) {
        onNetworkTimeReceived(s);
    } else if ((strStartsWith(s, "*EPEV")) || (strStartsWith(s, "+CGEV:"))) {
        /* Pin event, poll SIM State! */
        enqueueRILEvent(RIL_EVENT_QUEUE_PRIO, pollSIMState, (void*) 1, NULL);
    } else if (strStartsWith(s, "*ESIMSR"))
        onSimStateChanged(s);
    else if(strStartsWith(s, "*E2NAP:")) {
        enqueueRILEvent(RIL_EVENT_QUEUE_PRIO, pollSIMState, (void*) 1, NULL);
        onConnectionStateChanged(s);
    } else if(strStartsWith(s, "*ERINFO:"))
        onNetworkCapabilityChanged(s);
    else if(strStartsWith(s, "*E2REG:"))
        onNetworkStatusChanged(s);
    else if (strStartsWith(s, "*EESIMSWAP:"))
        onSimHotswap(s);
    else if (strStartsWith(s, "+CREG:")
            || strStartsWith(s, "+CGREG:"))
        onRegistrationStatusChanged(s);
    else if (strStartsWith(s, "+CMT:"))
        onNewSms(sms_pdu);
    else if (strStartsWith(s, "+CBM:"))
        onNewBroadcastSms(sms_pdu);
    else if (strStartsWith(s, "+CMTI:"))
        onNewSmsOnSIM(s);
    else if (strStartsWith(s, "+CDS:"))
        onNewStatusReport(sms_pdu);
    else if (strStartsWith(s, "+CIEV: 2"))
        onSignalStrengthChanged(s);
    else if (strStartsWith(s, "+CIEV: 7"))
        onNewSmsIndication();
    else if (strStartsWith(s, "*STKEND"))
        RIL_onUnsolicitedResponse(RIL_UNSOL_STK_SESSION_END, NULL, 0);
    else if (strStartsWith(s, "*STKI:"))
        onStkProactiveCommand(s);
    else if (strStartsWith(s, "*STKN:"))
        onStkEventNotify(s);
    else if (strStartsWith(s, "+PACSP0")) { 
        setRadioState(RADIO_STATE_SIM_READY); 
    } 
}

static void signalCloseQueues(void)
{
    unsigned int i;
    for (i = 0; i < (sizeof(s_requestQueues) / sizeof(RequestQueue *)); i++) {
        int err;
        RequestQueue *q = s_requestQueues[i];
        if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
            ALOGE("%s() failed to take queue mutex: %s",
                __func__, strerror(err));

        q->closed = 1;
        if ((err = pthread_cond_signal(&q->cond)) != 0)
            ALOGE("%s() failed to broadcast queue update: %s",
                __func__, strerror(err));

        if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
            ALOGE("%s() failed to take queue mutex: %s", __func__,
                 strerror(err));
    }
}

/* Called on command or reader thread. */
static void onATReaderClosed(void)
{
    ALOGD("%s() Calling at_close()", __func__);

    at_close();

    if (!get_pending_hotswap())
        setRadioState(RADIO_STATE_UNAVAILABLE);
    signalCloseQueues();

}

/* Called on command thread. */
static void onATTimeout(void)
{
    ALOGD("%s() AT channel timeout", __func__);

     /* Last resort, throw escape on the line, close the channel
        and hope for the best. */

    at_send_escape();
    at_close();

    setRadioState(RADIO_STATE_UNAVAILABLE);
    signalCloseQueues();

    /* TODO We may cause a radio reset here. */
}

static void usage(char *s)
{
    fprintf(stderr, "usage: %s [-z] [-p <tcp port>] [-d /dev/tty_device] [-x /dev/tty_device] [-i <network interface>]\n", s);
    exit(-1);
}

struct queueArgs {
    int port;
    char * loophost;
    const char *device_path;
    char isPrio;
    char hasPrio;
};

static int safe_read(int fd, char *buf, int count)
{
    int n;
    int i = 0;

    while (i < count) {
        n = read(fd, buf + i, count - i);
        if (n > 0)
            i += n;
        else if (!(n < 0 && errno == EINTR))
            return -1;
    }

    return count;
}

static void *queueRunner(void *param)
{
    int fd = -1;
    int ret = 0;
    int n;
    fd_set input;
    struct timeval timeout;
    int max_fd = -1;
    char start[MAX_BUF];
    struct queueArgs *queueArgs = (struct queueArgs *) param;
    struct RequestQueue *q = NULL;
    struct stat sb;

    ALOGI("%s() starting!", __func__);

    for (;;) {
        fd = -1;
        max_fd = -1;
        n = 0;
        while (fd < 0) {
            if (queueArgs->port > 0) {
                if (queueArgs->loophost)
                    fd = socket_network_client(queueArgs->loophost, queueArgs->port, SOCK_STREAM);
                else
                    fd = socket_loopback_client(queueArgs->port, SOCK_STREAM);
            } else if (queueArgs->device_path != NULL) {
                /* Program is not controlling terminal -> O_NOCTTY */
                /* Dont care about DCD -> O_NDELAY */
                fd = open(queueArgs->device_path, O_RDWR | O_NOCTTY); /* | O_NDELAY); */
                if (fd >= 0 && !memcmp(queueArgs->device_path, "/dev/ttyA", 9)) {
                    struct termios ios;
                    /* Clear the struct and then call cfmakeraw*/
                    tcflush(fd, TCIOFLUSH);
                    tcgetattr(fd, &ios);
                    memset(&ios, 0, sizeof(struct termios));
                    cfmakeraw(&ios);
                    /* OK now we are in a known state, set what we want*/
                    ios.c_cflag |= CRTSCTS;
                    /* ios.c_cc[VMIN]  = 0; */
                    /* ios.c_cc[VTIME] = 1; */
                    ios.c_cflag |= CS8;
                    tcsetattr(fd, TCSANOW, &ios);
                    tcflush(fd, TCIOFLUSH);
                    tcsetattr(fd, TCSANOW, &ios);
                    tcflush(fd, TCIOFLUSH);
                    tcflush(fd, TCIOFLUSH);
                    cfsetospeed(&ios, B115200);
                    cfsetispeed(&ios, B115200);
                    tcsetattr(fd, TCSANOW, &ios);

                }
            }

            if (fd < 0) {
                if (n == 0) {
                    ALOGE("%s() Failed to open AT channel %s (%s), will silently retry every %ds",
                        __func__, queueArgs->device_path, strerror(errno), TIMEOUT_SEARCH_FOR_TTY);
                    n = 1;
                }
                sleep(TIMEOUT_SEARCH_FOR_TTY);
            }
        }

        /* Reset the blocking mode*/
        fcntl(fd, F_SETFL, 0);
        FD_ZERO(&input);
        FD_SET(fd, &input);
        if (fd >= max_fd)
            max_fd = fd + 1;

        timeout.tv_sec = TIMEOUT_EMRDY;
        timeout.tv_usec = 0;

        ALOGD("%s() Waiting for EMRDY...", __func__);
        n = select(max_fd, &input, NULL, NULL, &timeout);

        if (n < 0) {
            ALOGE("%s() Select error", __func__);
            return NULL;
        } else if (n == 0)
            ALOGE("%s() timeout, go ahead anyway(might work)...", __func__);
        else {
            memset(start, 0, MAX_BUF);
            safe_read(fd, start, MAX_BUF-1);

            if (start == NULL) {
                ALOGD("%s() Oops, empty string", __func__);
                tcflush(fd, TCIOFLUSH);
                FD_CLR(fd, &input);
                close(fd);
                continue;
            }

            if (strstr(start, "EMRDY") == NULL) {
                ALOGD("%s() Oops, this was not EMRDY: %s", __func__, start);
                tcflush(fd, TCIOFLUSH);
                FD_CLR(fd, &input);
                close(fd);
                continue;
            }

            ALOGD("%s() Got EMRDY", __func__);
        }

        ret = at_open(fd, onUnsolicited);

        if (ret < 0) {
            ALOGE("%s() AT error %d on at_open", __func__, ret);
            at_close();
            continue;
        }

        at_set_on_reader_closed(onATReaderClosed);
        at_set_on_timeout(onATTimeout);
        at_set_timeout_msec(1000 * 30);

        q = &s_requestQueue;

        if(initializeCommon()) {
            ALOGE("%s() Failed to initialize channel!", __func__);
            at_close();
            continue;
        }

        if (queueArgs->isPrio == 0) {
            q->closed = 0;
            if (initializeChannel()) {
                ALOGE("%s() Failed to initialize channel!", __func__);
                at_close();
                continue;
            }
            at_make_default_channel();
        } else {
            q = &s_requestQueuePrio;
            q->closed = 0;
            at_set_timeout_msec(1000 * 30);
        }

        if (queueArgs->hasPrio == 0 || queueArgs->isPrio)
            if (initializePrioChannel()) {
                ALOGE("%s() Failed to initialize channel!", __func__);
                at_close();
                continue;
            }

        ALOGE("%s() Looping the requestQueue!", __func__);
        for (;;) {
            RILRequest *r;
            RILEvent *e;
            struct timespec ts;
            int err;

            memset(&ts, 0, sizeof(ts));

            if ((err = pthread_mutex_lock(&q->queueMutex)) != 0)
                ALOGE("%s() failed to take queue mutex: %s!",
                    __func__, strerror(err));

            if (q->closed != 0) {
                ALOGW("%s() AT Channel error, attempting to recover..", __func__);
                if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
                    ALOGE("Failed to release queue mutex: %s!", strerror(err));
                break;
            }

            while (q->closed == 0 && q->requestList == NULL &&
                q->eventList == NULL) {
                if ((err = pthread_cond_wait(&q->cond, &q->queueMutex)) != 0)
                    ALOGE("%s() failed broadcast queue cond: %s!",
                        __func__, strerror(err));
            }

            /* eventList is prioritized, smallest abstime first. */
            if (q->closed == 0 && q->requestList == NULL && q->eventList) {
                int err = 0;
                err = pthread_cond_timedwait_monotonic_np(&q->cond, &q->queueMutex, &q->eventList->abstime);
                if (err && err != ETIMEDOUT)
                    ALOGE("%s() timedwait returned unexpected error: %s",
		        __func__, strerror(err));
            }

            if (q->closed != 0) {
                if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
                    ALOGE("%s(): Failed to release queue mutex: %s!",
                        __func__, strerror(err));
                continue; /* Catch the closed bit at the top of the loop. */
            }

            e = NULL;
            r = NULL;

            clock_gettime(CLOCK_MONOTONIC, &ts);

            if (q->eventList != NULL &&
                timespec_cmp(q->eventList->abstime, ts, < )) {
                e = q->eventList;
                q->eventList = e->next;
            }

            if (q->requestList != NULL) {
                r = q->requestList;
                q->requestList = r->next;
            }

            if ((err = pthread_mutex_unlock(&q->queueMutex)) != 0)
                ALOGE("%s(): Failed to release queue mutex: %s!",
                    __func__, strerror(err));

            if (e) {
                if (NULL != e->name)
                    ALOGD("processEvent(%s)",e->name);
                e->eventCallback(e->param);
                free(e);
            }

            if (r) {
                processRequest(r->request, r->data, r->datalen, r->token);
                freeRequestData(r->request, r->data, r->datalen);
                free(r);
            }
        }

        at_close();

        /* Make sure device is removed before trying to reopen
           otherwise we might end up in a race condition when
           device is being removed from filesystem */

        int i = TIMEOUT_DEVICE_REMOVED;
        sleep(1);
        while((i--) && (stat(queueArgs->device_path, &sb) == 0)) {
            ALOGD("%s() Waiting for %s to be removed (%d)...", __func__,
                queueArgs->device_path, i);
            sleep(1);
        }

        ALOGE("%s() Re-opening after close", __func__);
    }
    return NULL;
}

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc,
                                   char **argv)
{
    int opt;
    int port = -1;
    char *loophost = NULL;
    const char *device_path = NULL;
    const char *priodevice_path = NULL;
    struct queueArgs *queueArgs;
    struct queueArgs *prioQueueArgs;
    pthread_attr_t attr;

    s_rilenv = env;

    ALOGD("%s() entering...", __func__);

    while (-1 != (opt = getopt(argc, argv, "z:i:p:d:s:x:c:"))) {
        switch (opt) {
            case 'z':
                loophost = optarg;
                ALOGD("%s() Using loopback host %s..", __func__, loophost);
                break;

            case 'i':
                ril_iface = optarg;
                ALOGD("%s() Using network interface %s as primary data channel.",
                     __func__, ril_iface);
                break;

            case 'p':
                port = atoi(optarg);
                if (port == 0) {
                    usage(argv[0]);
                    return NULL;
                }
                ALOGD("%s() Opening loopback port %d", __func__, port);
                break;

            case 'd':
                device_path = optarg;
                ALOGD("%s() Opening tty device %s", __func__, device_path);
                break;

            case 'x':
                priodevice_path = optarg;
                ALOGD("%s() Opening priority tty device %s", __func__, priodevice_path);
                break;

            case 'c':
                break;

            default:
                usage(argv[0]);
                return NULL;
        }
    }

    if (ril_iface == NULL) {
        ALOGD("%s() Network interface was not supplied, falling back on usb0!", __func__);
        ril_iface = strdup("usb0\0");
    }

    if (port < 0 && device_path == NULL) {
        usage(argv[0]);
        return NULL;
    }

    queueArgs = malloc(sizeof(struct queueArgs));
    memset(queueArgs, 0, sizeof(struct queueArgs));

    queueArgs->device_path = device_path;
    queueArgs->port = port;
    queueArgs->loophost = loophost;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (priodevice_path != NULL) {
        prioQueueArgs = malloc(sizeof(struct queueArgs));
        memset(prioQueueArgs, 0, sizeof(struct queueArgs));
        prioQueueArgs->device_path = priodevice_path;
        prioQueueArgs->isPrio = 1;
        prioQueueArgs->hasPrio = 1;
        queueArgs->hasPrio = 1;

        s_requestQueuePrio.enabled = 1;

        pthread_create(&s_tid_queueRunnerPrio, &attr, queueRunner, prioQueueArgs);
    }

    pthread_create(&s_tid_queueRunner, &attr, queueRunner, queueArgs);

    return &s_callbacks;
}