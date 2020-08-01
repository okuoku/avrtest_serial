// NB: Only for Cygwin

#include <stdio.h>
#include <windows.h>
#include <pthread.h>

#define COMPORT "\\\\.\\VIRT0"

// Global comms
pthread_mutex_t comm_mtx;
pthread_cond_t comm_cnd;
int comm_waiting;
int comm_signal;
int comm_char;
int comm_dsr;

static void
proc_char(void){
    printf("CHAR: %02x DSR: %d\n", comm_char, comm_dsr);
}


static void
sendbuf_acquire(void){
    (void)pthread_mutex_lock(&comm_mtx);
    // We don't wait against comm_signal free because 
    // char and dsr will not overwrap
    while(comm_waiting == 0){
        (void)pthread_cond_wait(&comm_cnd, &comm_mtx);
    }
}
static void
sendbuf_release(void){
    comm_signal = 1;
    (void)pthread_cond_broadcast(&comm_cnd);
    (void)pthread_mutex_unlock(&comm_mtx);
}

static void*
thr_combytes(void* h){
    BOOL b;
    HANDLE hCom = (HANDLE)h;
    char c;
    DWORD m;
    for(;;){
        b = ReadFile(hCom, &c, 1, &m, NULL);
        if(!b){
            printf("ERR_READ: %d\n",GetLastError());
            return NULL;
        }
        sendbuf_acquire();
        comm_char = c;
        sendbuf_release();
    }
    return NULL;
}

static void*
thr_comstate(void* h){
    BOOL b;
    DWORD m;
    HANDLE hCom = (HANDLE)h;
    b = GetCommModemStatus(hCom, &m);
    if(!b){
        printf("ERR_INIT: %d\n", GetLastError());
        return NULL;
    }
    // Send initial state
    sendbuf_acquire();
    comm_dsr = (m & MS_DSR_ON) ? 1 : 0;
    sendbuf_release();
    for(;;){
        b = WaitCommEvent(hCom, &m, NULL);

        if(!b){
            printf("ERR_WAIT: %d\n", GetLastError());
            return NULL;
        }
        b = GetCommModemStatus(hCom, &m);
        if(!b){
            printf("ERR_STAT: %d\n", GetLastError());
            return NULL;
        }
        sendbuf_acquire();
        comm_dsr = (m & MS_DSR_ON) ? 1 : 0;
        sendbuf_release();
    }
    return NULL;
}

static void
com_main_loop(HANDLE hCom){
    BOOL b;
    DCB dcb;
    int cur_dsr,dsr;
    char c;
    DWORD m;
    pthread_t thr_bytes;
    pthread_t thr_state;

    (void)pthread_mutex_init(&comm_mtx, NULL);
    (void)pthread_cond_init(&comm_cnd, NULL);
    comm_waiting = 0;
    comm_char = -1;
    comm_dsr = -1;

    memset(&dcb, 0, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    dcb.BaudRate = CBR_9600;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;
    dcb.ByteSize = 8;
    dcb.Parity = 0;
    dcb.StopBits = ONESTOPBIT;

    b = SetCommState(hCom, &dcb);
    if(!b){
        printf("ERR_SETUP_DCB: %d\n", GetLastError());
        return;
    }
    b = SetCommMask(hCom, EV_DSR);
    if(!b){
        printf("ERR_SETUP: %d\n", GetLastError());
        return;
    }

    /* Dummy reset signaling */
    comm_signal = 1;
    comm_char = -1;
    comm_dsr = 0;
    proc_char();
    comm_signal = 0;

    (void)pthread_create(&thr_bytes, NULL, thr_combytes, hCom);
    (void)pthread_create(&thr_state, NULL, thr_comstate, hCom);

    (void)pthread_mutex_lock(&comm_mtx);
    comm_waiting = 1;
    for(;;){
        (void)pthread_cond_broadcast(&comm_cnd);
        for(;;){
            (void)pthread_cond_wait(&comm_cnd, &comm_mtx);
            if(comm_signal){
                break;
            }
        }
        comm_waiting = 0;
        (void)pthread_mutex_unlock(&comm_mtx);
        proc_char();
        (void)pthread_mutex_lock(&comm_mtx);
        comm_char = -1;
        comm_dsr = -1;
        comm_signal = 0;
        comm_waiting = 1;
    }

    (void)pthread_join(thr_bytes, NULL);
    (void)pthread_join(thr_state, NULL);
}

int
main(int ac, char** av){
    HANDLE hCom;

    hCom = CreateFileA(COMPORT,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0, NULL);

    if(hCom == INVALID_HANDLE_VALUE){
        printf("ERR: %d\n", GetLastError());
        return 1;
    }

    com_main_loop(hCom);

    return 0;
}
