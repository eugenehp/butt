// FLTK GUI related functions
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#ifndef _WIN32
 #include <sys/wait.h>
#endif

#include "config.h"

#include "cfg.h"
#include "butt.h"
#include "util.h"
#include "port_audio.h"
#include "timer.h"
#include "flgui.h"
#include "fl_funcs.h"
#include "shoutcast.h"
#include "icecast.h"
#include "strfuncs.h"
#include "fl_timer_funcs.h"

#if __APPLE__ && __MACH__
 #include "CurrentTrackOSX.h"
#endif

const char* (*current_track_app)(void);


void vu_meter_timer(void*)
{
    if(pa_new_frames)
        snd_update_vu();

    Fl::repeat_timeout(0.01, &vu_meter_timer);
}

void display_info_timer(void*)
{
    char lcd_text_buf[33];

    if(try_to_connect == 1)
    {
        Fl::repeat_timeout(0.1, &display_info_timer);
        return;
    }

    if(display_info == SENT_DATA)
    {
        sprintf(lcd_text_buf, "stream sent\n%0.2lfMB",
                kbytes_sent / 1024);
        print_lcd(lcd_text_buf, strlen(lcd_text_buf), 0, 1);
    }

    if(display_info == STREAM_TIME && timer_is_elapsed(&stream_timer))
    {
        sprintf(lcd_text_buf, "stream time\n%s",
                timer_get_time_str(&stream_timer));
        print_lcd(lcd_text_buf, strlen(lcd_text_buf), 0, 1);
    }

    if(display_info == REC_TIME && timer_is_elapsed(&rec_timer))
    {
        sprintf(lcd_text_buf, "record time\n%s",
                timer_get_time_str(&rec_timer));
        print_lcd(lcd_text_buf, strlen(lcd_text_buf), 0, 1);
    }

    if(display_info == REC_DATA)
    {
        sprintf(lcd_text_buf, "record size\n%0.2lfMB",
                kbytes_written / 1024);
        print_lcd(lcd_text_buf, strlen(lcd_text_buf), 0, 1);
    }

    Fl::repeat_timeout(0.1, &display_info_timer);
}

void display_rotate_timer(void*) 
{

    if(!connected && !recording)
        goto exit;

    if (!cfg.gui.lcd_auto)
        goto exit;

    switch(display_info)
    {
        case STREAM_TIME:
            display_info = SENT_DATA;
            break;
        case SENT_DATA:
            if(recording)
                display_info = REC_TIME;
            else
                display_info = STREAM_TIME;
            break;
        case REC_TIME:
            display_info = REC_DATA;
            break;
        case REC_DATA:
            if(connected)
                display_info = STREAM_TIME;
            else
                display_info = REC_TIME;
            break;
        default:
            break;
    }

exit:
    Fl::repeat_timeout(5, &display_rotate_timer);

}

void is_connected_timer(void*)
{
    if(!connected)
    {
        print_info("ERROR: Connection lost\nreconnecting...", 1);
        if(cfg.srv[cfg.selected_srv]->type == SHOUTCAST)
            sc_disconnect();
        else
            ic_disconnect();

        Fl::remove_timeout(&display_info_timer);
        Fl::remove_timeout(&is_connected_timer);

        //reconnect
        button_connect_cb();

        return;
    }

    Fl::repeat_timeout(0.5, &is_connected_timer);
}

void cfg_win_pos_timer(void*)
{

#ifdef _WIN32
    fl_g->window_cfg->position(fl_g->window_main->x() +
                                fl_g->window_main->w()+15,
                                fl_g->window_main->y());
#else //UNIX
    fl_g->window_cfg->position(fl_g->window_main->x() +
                                fl_g->window_main->w(),
                                fl_g->window_main->y());
#endif

    Fl::repeat_timeout(0.1, &cfg_win_pos_timer);
}

void split_recording_timer(void* mode)
{
    int i;
    int button_clicked = 0;
    static int with_repeat = 0;
    char *insert_pos;
    char *path;
    char *ext;
    char file_num_str[12];
    char *path_for_index_loop;
    struct tm *local_time;
    const time_t t = time(NULL);
    
    if (recording == 0)
        return;
    
    if(*((int*)mode) == 1)
        with_repeat = 1;
    else
        button_clicked = 1;


    // Values < 0 are not allowed
    if(fl_g->input_rec_split_time->value() < 0)
    {
        fl_g->input_rec_split_time->value(0);
        return;
    }

    path = strdup(cfg.rec.path_fmt);
    expand_string(&path);
    ext = util_get_file_extension(cfg.rec.filename);
    if(ext == NULL)
    {
        print_info("Could not find a file extension in current filename\n"
                "Automatic file splitting is deactivated", 0);
        free(path);
        return;
    }


    path_for_index_loop = strdup(path);


    //check if the file already exists
    if((next_fd = fl_fopen(path, "rb")) != NULL)
    {
        fclose(next_fd);

        //increment the index until we find a filename that doesn't exist yet
        for(i = 1; /*inf*/; i++) // index_loop
        {
   
            free(path);
            path = strdup(path_for_index_loop);

            //find beginn of the file extension
            insert_pos = strrstr(path, ext);

            //Put index between end of file name end beginning of extension
            snprintf(file_num_str, sizeof(file_num_str), "-%d", i);
            strinsrt(&path, file_num_str, insert_pos-1);

            if((next_fd = fl_fopen(path, "rb")) == NULL)
                break; // found valid file name 

            fclose(next_fd);

            if (i == 0x7FFFFFFF) // 2^31-1
            {
                free(path);
                free(path_for_index_loop);
                print_info("Could not find a valid filename for next file"
                        "\nbutt keeps recording to current file", 0);
                return;
            }
        }
    }

    free(path_for_index_loop);

    if((next_fd = fl_fopen(path, "wb")) == NULL)
    {
        fl_alert("Could not open:\n%s", path);
        free(path);
        return;
    }

    print_info("Recording to:", 0);
    print_info(path, 0);

    next_file = 1;
    free(path);


    if(with_repeat == 1 && button_clicked == 0)
    {
        local_time = localtime(&t);

        // Make sure that the 60 minutes boundary is not violated in case sync_to_hour == 1
        if((cfg.rec.sync_to_hour == 1) && ((local_time->tm_min + cfg.rec.split_time) > 60))
            Fl::repeat_timeout(60*(60 - local_time->tm_min), &split_recording_timer, &with_repeat);
        else
            Fl::repeat_timeout(60*cfg.rec.split_time, &split_recording_timer, &with_repeat);

    }
}

void stream_silence_timer(void*)
{
    bool repeat_timer = true;
    static bool was_silent_before = false;

    if (silence_detected == true)
    {
        if (was_silent_before == true)
        {
            print_info("Streaming silence threshold has been reached", 0);
            button_disconnect_cb();
            repeat_timer = false;
            was_silent_before = false;
        }
        else
            was_silent_before = true;
    }
    else
        was_silent_before = false;
    
    if (repeat_timer == true)
        Fl::repeat_timeout(cfg.main.silence_threshold, &stream_silence_timer);
}

void record_silence_timer(void*)
{
    bool repeat_timer = true;
    static bool was_silent_before = false;

    if (silence_detected == true)
    {
        if (was_silent_before == true)
        {
            if(recording)
            {
                print_info("Recording silence threshold has been reached", 0);
                stop_recording(false);
                repeat_timer = false;
                was_silent_before = false;
            }
        }
        else
            was_silent_before = true;
    }
    else
        was_silent_before = false;
    
    if (repeat_timer == true)
        Fl::repeat_timeout(cfg.rec.silence_threshold, &record_silence_timer);
}


void songfile_timer(void*)
{
    size_t len;
	int i;
	int num_of_lines;
	int num_of_newlines;
    char song[501];
    char msg[100];
    float repeat_time = 1;

    struct stat s;
    static time_t old_t;
    
    char *last_line = NULL;

    if(cfg.main.song_path == NULL)
        goto exit;

    if(stat(cfg.main.song_path, &s) != 0)
    {

        // File was probably locked by another application
        // retry in 5 seconds
        repeat_time = 5;
        goto exit;
    }

    if(old_t == s.st_mtime) //file hasn't changed
        goto exit;

   if((cfg.main.song_fd = fl_fopen(cfg.main.song_path, "rb")) == NULL)
   {
	   snprintf(msg, sizeof(msg), "Warning\nCould not open: %s.\nWill retry in 5 seconds", 
					   cfg.main.song_path); 

       print_info(msg, 1);
       repeat_time = 5;
       goto exit;
   }

    old_t = s.st_mtime;
    
    if(cfg.main.read_last_line == 1)
    {
        /* Read last line instead of first */
        
        fseek(cfg.main.song_fd, -100, SEEK_END);
        len = fread(song, sizeof(char), 100, cfg.main.song_fd);
        if(len == 0)
        {
            fclose(cfg.main.song_fd);
            goto exit;
        }

		// Count number of lines within the last 100 characters of the file
		// Some programs add a new line to the end of the file and some don't
		// We have to take this into account when counting the number of lines
		num_of_newlines = 0;
		for(i = 0; i < len; i++) 
		{
			if (song[i] == '\n')
				num_of_newlines++;
		}

		if(num_of_newlines == 0) 
			num_of_lines = 1;
		else if (num_of_newlines > 0 && song[len-1] != '\n')
			num_of_lines = num_of_newlines+1;
		else
			num_of_lines = num_of_newlines;
        
        if(num_of_lines > 1) // file has multiple lines
        {
			// Remove newlines at end of file
            if(song[len-2] == '\r') // Windows
                song[len-2] = '\0';
            else if(song[len-1] == '\n') // OSX, Linux
                song[len-1] = '\0';
			else
				song[len] = '\0';

            last_line = strrchr(song, '\n')+1;
        }
        else // file has only one line
        {
		
			// Remove newlines at end of file
            if(song[len-2] == '\r') // Windows
                song[len-2] = '\0';
            else if(song[len-1] == '\n') // OSX, Linux
                song[len-1] = '\0';
			else
				song[len] = '\0';

			last_line = song;
        }
        
        cfg.main.song = (char*) realloc(cfg.main.song, strlen(last_line) +1);
        strcpy(cfg.main.song, last_line);
        update_song();
    }
    else
    {
		// read first line
        if(fgets(song, 500, cfg.main.song_fd) != NULL)
        {
            len = strlen(song);
            
            //remove newline character
            if(song[len-2] == '\r') // Windows
                song[len-2] = '\0';
            else if(song[len-1] == '\n') // OSX, Linux
                song[len-1] = '\0';
            
            cfg.main.song = (char*) realloc(cfg.main.song, strlen(song) +1);
            strcpy(cfg.main.song, song);
            update_song();
        }
    }

   fclose(cfg.main.song_fd);

exit:
    Fl::repeat_timeout(repeat_time, &songfile_timer);
}

void app_timer(void*)
{
    if(!app_timeout_running)
    {
        Fl::remove_timeout(&app_timer);
        return;
    }
    
    if(current_track_app != NULL)
    {
        const char* track = current_track_app();
        if(track != NULL)
        {
            if(cfg.main.song == NULL || strcmp(cfg.main.song, track))
            {
                cfg.main.song = (char*) realloc(cfg.main.song, strlen(track) + 1);
                strcpy(cfg.main.song, track);
                update_song();
            }
            free((void*)track);
        }
        else
        {
            if(cfg.main.song != NULL && strcmp(cfg.main.song, ""))
            {
                cfg.main.song = (char*) realloc(cfg.main.song, 1);
                strcpy(cfg.main.song, "");
                update_song();
            }
        }
    }
    
    Fl::repeat_timeout(1, &app_timer);
}
