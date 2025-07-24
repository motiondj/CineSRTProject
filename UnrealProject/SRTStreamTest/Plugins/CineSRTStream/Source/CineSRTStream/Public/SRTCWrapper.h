#ifndef SRT_C_WRAPPER_H
#define SRT_C_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int SRTSOCKET_HANDLE;
#define SRT_C_ERROR -1
#define SRT_C_INVALID_SOCK -1

int SRT_C_Initialize(void);
void SRT_C_Cleanup(void);
const char* SRT_C_GetVersion(void);

SRTSOCKET_HANDLE SRT_C_CreateSocket(void);
int SRT_C_CloseSocket(SRTSOCKET_HANDLE sock);
int SRT_C_IsValidSocket(SRTSOCKET_HANDLE sock);

int SRT_C_Bind(SRTSOCKET_HANDLE sock, const char* ip, int port);
int SRT_C_Connect(SRTSOCKET_HANDLE sock, const char* ip, int port);
int SRT_C_Listen(SRTSOCKET_HANDLE sock, int backlog);
SRTSOCKET_HANDLE SRT_C_Accept(SRTSOCKET_HANDLE sock);

int SRT_C_Send(SRTSOCKET_HANDLE sock, const char* data, int len);
int SRT_C_Recv(SRTSOCKET_HANDLE sock, char* data, int len);

int SRT_C_SetIntOption(SRTSOCKET_HANDLE sock, int opt, int value);
int SRT_C_SetStrOption(SRTSOCKET_HANDLE sock, int opt, const char* value);
int SRT_C_GetIntOption(SRTSOCKET_HANDLE sock, int opt, int* value);

int SRT_C_GetLastError(void);
const char* SRT_C_GetLastErrorString(void);

typedef struct {
    double mbpsSendRate;
    double msRTT;
    int pktSndLossTotal;
} SRT_C_Stats;

int SRT_C_GetStats(SRTSOCKET_HANDLE sock, SRT_C_Stats* stats);

#ifdef __cplusplus
}
#endif

#endif // SRT_C_WRAPPER_H 