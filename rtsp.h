#ifndef _RTSP_H
#define _RTSP_H

#include "player.h"

typedef struct {
    int fd;
    stream_cfg stream;
    SOCKADDR remote;
} rtsp_conn_info;

void rtsp_serve_loop(rtsp_conn_info *conn);
void rtsp_shutdown_stream(void);

#endif // _RTSP_H

