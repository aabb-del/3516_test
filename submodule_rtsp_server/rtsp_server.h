#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

class RtspServer
{
	RtspServer(char *url,int port);
	void push_frame_264(char *data,int size,int frame_type);
};


void rtsp_server_init(char *name,int port,char* ip);
void rtsp_server_push_frame_264(char *data,int size,int frame_type);

#endif