// butt - broadcast using this tool
//
// Copyright 2007-2018 by Daniel Noethen.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//


#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#include <lame/lame.h>
#include <signal.h>
#include <limits.h>
#include <pthread.h>

#ifdef _WIN32
 #include <time.h>
 #define IDI_ICON 101 
#endif

#if !defined(__APPLE__) && !defined(WIN32)
 #include <FL/Fl_File_Icon.H>
#endif

#if defined(__APPLE__)
 #include <dlfcn.h>
#endif

#include "config.h"

#include "cfg.h"
#include "butt.h"
#include "port_audio.h"
#include "lame_encode.h"
#include "opus_encode.h"
#include "flac_encode.h"
#include "shoutcast.h"
#include "parseconfig.h"
#include "vu_meter.h"
#include "util.h"
#include "flgui.h"
#include "fl_funcs.h"
#include "fl_timer_funcs.h"
#ifdef HAVE_LIBFDK_AAC
 #include "aac_encode.h"
#endif

bool record;
bool recording;
bool connected;
bool streaming;
bool disconnect;
bool try_connect;
bool song_timeout_running;
bool app_timeout_running;

int stream_socket;
double kbytes_sent;
double kbytes_written;
unsigned int record_start_hour;

sec_timer rec_timer;
sec_timer stream_timer;

lame_enc lame_stream;
lame_enc lame_rec;
vorbis_enc vorbis_stream;
vorbis_enc vorbis_rec;
opus_enc opus_stream;
opus_enc opus_rec;
flac_enc flac_stream;
flac_enc flac_rec;
#ifdef HAVE_LIBFDK_AAC
 aac_enc aac_stream;
 aac_enc aac_rec;
#endif


#if defined(__APPLE__) || defined(WIN32)
 aacEncOpenPtr aacEncOpen_butt;
 aacEncoder_SetParamPtr aacEncoder_SetParam_butt;
 aacEncEncodePtr aacEncEncode_butt;
 aacEncInfoPtr aacEncInfo_butt;
 aacEncClosePtr aacEncClose_butt;
#endif


int main(int argc, char *argv[])
{

    char *p;
    char lcd_buf[33];
    char info_buf[256];

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN); //ignore the SIGPIPE signal.
        //(in case the server closes the connection unexpected)
#endif
    
#if ! defined(__APPLE__) && !defined(WIN32)  // LINUX
    Fl_File_Icon::load_system_icons();
    #ifdef HAVE_LIBFDK_AAC
	g_aac_lib_available = 1;
    #endif
#endif

    fl_g = new flgui(); 
    fl_g->window_main->show();

#ifdef _WIN32 
    fl_g->window_main->icon((char *)LoadIcon(fl_display, MAKEINTRESOURCE(IDI_ICON)));
    // The fltk icon code above only loads the default icon. 
    // Here, once the window is shown, we can assign 
    // additional icons, just to make things a bit nicer. 
    HANDLE bigicon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 32, 32, 0); 
    SendMessage(fl_xid(fl_g->window_main), WM_SETICON, ICON_BIG, (LPARAM)bigicon); 
    HANDLE smallicon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, 0); 
    SendMessage(fl_xid(fl_g->window_main), WM_SETICON, ICON_SMALL, (LPARAM)smallicon); 

    // Load aac library
    HMODULE hModule = LoadLibrary(TEXT("libfdk-aac-2.dll"));
    if (hModule != NULL)
    {
        aacEncOpen_butt = (aacEncOpenPtr)GetProcAddress(hModule, "aacEncOpen");
        aacEncoder_SetParam_butt = (aacEncoder_SetParamPtr)GetProcAddress(hModule, "aacEncoder_SetParam");
        aacEncEncode_butt = (aacEncEncodePtr)GetProcAddress(hModule, "aacEncEncode");
        aacEncInfo_butt = (aacEncInfoPtr)GetProcAddress(hModule, "aacEncInfo");
        aacEncClose_butt = (aacEncClosePtr)GetProcAddress(hModule, "aacEncClose");
        g_aac_lib_available = 1;
    }
#endif 

#if defined(__APPLE__)
    void* dylib = dlopen("libfdk-aac.2.dylib", RTLD_LAZY);
    if (dylib != NULL)
    {
        //typedef AACENC_ERROR(WINAPI *aacEncOpenPtr)(HANDLE_AACENCODER*, UINT, UINT);

        aacEncOpen_butt = (aacEncOpenPtr)dlsym(dylib, "aacEncOpen");
        aacEncoder_SetParam_butt = (aacEncoder_SetParamPtr)dlsym(dylib, "aacEncoder_SetParam");
        aacEncEncode_butt = (aacEncEncodePtr)dlsym(dylib, "aacEncEncode");
        aacEncInfo_butt = (aacEncInfoPtr)dlsym(dylib, "aacEncInfo");
        aacEncClose_butt = (aacEncClosePtr)dlsym(dylib, "aacEncClose");

        g_aac_lib_available = 1;
        
    }
#endif


    snprintf(info_buf, sizeof(info_buf), "Starting %s\nWritten by Daniel Nöthen\n"
             "iOS client: https://izicast.de\n"
             "Donate: paypal@danielnoethen.de\n", PACKAGE_STRING);
    print_info(info_buf, 0);

#ifdef _WIN32
    if((argc > 2) && (!strcmp(argv[1], "-c")))
    {
        cfg_path = (char*)malloc((strlen(argv[2])+1) * sizeof(char));
        strcpy(cfg_path, argv[2]);
    }
    else if((p = getenv("APPDATA")) == NULL)
    {
        //If there is no "%APPDATA% we are probably in none-NT Windows
        //So we save the config file to the programm directory
        cfg_path = (char*)malloc(strlen(CONFIG_FILE)+1);
        strcpy(cfg_path, CONFIG_FILE);
    }
    else
    {
        cfg_path = (char*)malloc((PATH_MAX+strlen(CONFIG_FILE)+1) * sizeof(char));
        snprintf(cfg_path, PATH_MAX+strlen(CONFIG_FILE), "%s\\%s", p, CONFIG_FILE);  
    }
#else //UNIX
    p = getenv("HOME");
    if((argc > 2) && (!strcmp(argv[1], "-c")))
    {
        cfg_path = (char*)malloc((strlen(argv[2])+1) * sizeof(char));
        strcpy(cfg_path, argv[2]);
    }
    else if(p == NULL)
    {
        ALERT("No home-directory found");
        return 1;
    }
    else
    {
        cfg_path = (char*)malloc((PATH_MAX+strlen(CONFIG_FILE)+1) * sizeof(char));
        snprintf(cfg_path, PATH_MAX+strlen(CONFIG_FILE), "%s/%s", p, CONFIG_FILE);  
    }
#endif

    lame_stream.gfp = NULL;
    lame_rec.gfp = NULL;
    flac_rec.encoder = NULL;
    flac_stream.encoder = NULL;
#ifdef HAVE_LIBFDK_AAC
    aac_stream.handle = NULL;
    aac_rec.handle = NULL;
#endif
    

    snprintf(info_buf, sizeof(info_buf), "Reading config %s", cfg_path);
    print_info(info_buf, 0);

    if(snd_init() != 0)
    {
        ALERT("PortAudio init failed\nbutt is going to close now");
        return 1;
    }

    if(cfg_set_values(NULL) != 0)        //read config file and initialize config struct
    {
        snprintf(info_buf, sizeof(info_buf), "Could not find config %s", cfg_path);
        print_info(info_buf, 1);

        if(cfg_create_default())
        {
            fl_alert("Could not create config %s\nbutt is going to close now", cfg_path);
            return 1;
        }
        sprintf(info_buf, "butt created a default config at\n%s\n",
                cfg_path );

        print_info(info_buf, 0);
        cfg_set_values(NULL);
    }

    init_main_gui_and_audio();

    snd_open_stream();

    vu_init();

    Fl::add_timeout(0.01, &vu_meter_timer);
    Fl::add_timeout(5, &display_rotate_timer);

    strcpy(lcd_buf, "idle");
    PRINT_LCD(lcd_buf, strlen(lcd_buf), 0, 1);

	if(cfg.main.connect_at_startup)
		button_connect_cb();
    
    if(cfg.rec.rec_after_launch)
        button_record_cb();
    
    GUI_LOOP();

    return 0;
}
