// vu-meter functions for butt
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
//
#include <math.h>
#include "flgui.h"

#include "vu_meter.h"


vu_led_t vu_led[9];

void vu_left_peak_timer(void*);
void vu_right_peak_timer(void*);

void vu_init(void)
{
    int i;

    for(i = 0; i < 9; i++)
    {
        vu_led[0].left.is_peak = 0;
        vu_led[0].right.is_peak = 0;
    }

    vu_led[0].left.widget = fl_g->left_1_light; 
    vu_led[0].right.widget = fl_g->right_1_light; 
    vu_led[0].thld = TRESHOLD_1;

    vu_led[1].left.widget = fl_g->left_2_light; 
    vu_led[1].right.widget = fl_g->right_2_light; 
    vu_led[1].thld = TRESHOLD_2;

    vu_led[2].left.widget = fl_g->left_3_light; 
    vu_led[2].right.widget = fl_g->right_3_light; 
    vu_led[2].thld = TRESHOLD_3;

    vu_led[3].left.widget = fl_g->left_4_light; 
    vu_led[3].right.widget = fl_g->right_4_light; 
    vu_led[3].thld = TRESHOLD_4;

    vu_led[4].left.widget = fl_g->left_5_light; 
    vu_led[4].right.widget = fl_g->right_5_light; 
    vu_led[4].thld = TRESHOLD_5;

    vu_led[5].left.widget = fl_g->left_6_light; 
    vu_led[5].right.widget = fl_g->right_6_light; 
    vu_led[5].thld = TRESHOLD_6;

    vu_led[6].left.widget = fl_g->left_7_light; 
    vu_led[6].right.widget = fl_g->right_7_light; 
    vu_led[6].thld = TRESHOLD_7;

    vu_led[7].right.widget = fl_g->right_8_light; 
    vu_led[7].left.widget = fl_g->left_8_light; 
    vu_led[7].thld = TRESHOLD_8;

    vu_led[8].left.widget = fl_g->left_9_light; 
    vu_led[8].right.widget = fl_g->right_9_light; 
    vu_led[8].thld = TRESHOLD_9;

}

void vu_meter(short left, short right)
{
    int i;
    float left_db;
    float right_db;
    
    // Convert the 16bit integer values into dB values
    left_db = 20*log10(left/32768.0) + VU_OFFSET;
    right_db = 20*log10(right/32768.0) + VU_OFFSET;

    for(i = 0; i < 9; i++)
    {
        if(left_db > vu_led[i].thld)
        {
            vu_led[i].left.widget->show();
            if(i == 8)
            {
                // set new peak
                vu_led[i].left.is_peak = 1;
                
                // Trigger timeout for peak led
                Fl::remove_timeout(&vu_left_peak_timer);
                Fl::add_timeout(1.5/*second*/, &vu_left_peak_timer);
            }
        }
        else if (vu_led[i].left.is_peak == 0)
        {
            vu_led[i].left.widget->hide();
        }
        if(right_db > vu_led[i].thld)
        {
            vu_led[i].right.widget->show();
            if(i == 8)
            {
                vu_led[i].right.is_peak = 1;
                
                Fl::remove_timeout(&vu_right_peak_timer);
                Fl::add_timeout(1.5, &vu_right_peak_timer);
            }
        }
        else if (vu_led[i].right.is_peak == 0)
        {
            vu_led[i].right.widget->hide();
        }
    }
    
}

void vu_left_peak_timer(void*)
{
    // Deactivate the peak led 
    vu_led[8].left.is_peak = 0;
    vu_led[8].left.widget->hide();
}

void vu_right_peak_timer(void*)
{
    vu_led[8].right.is_peak = 0;
    vu_led[8].right.widget->hide();
}
