/* TFT demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <time.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "tftspi.h"
#include "tft.h"
#include "spiffs_vfs.h"

#ifdef CONFIG_EXAMPLE_USE_WIFI

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "freertos/event_groups.h"
#include "esp_attr.h"
#include <sys/time.h>
#include <unistd.h>
#include "lwip/err.h"
#include "apps/sntp/sntp.h"
#include "esp_log.h"

#endif


// ==================================================
// Define which spi bus to use VSPI_HOST or HSPI_HOST
#define SPI_BUS VSPI_HOST
// ==================================================


static int _demo_pass = 0;
static uint8_t doprint = 1;
static uint8_t run_gs_demo = 0; // Run gray scale demo if set to 1
static struct tm* tm_info;
static char tmp_buff[64];
static time_t time_now, time_last = 0;
static const char *file_fonts[3] = {"/spiffs/fonts/DotMatrix_M.fon", "/spiffs/fonts/Ubuntu.fon", "/spiffs/fonts/Grotesk24x48.fon"};

#define GDEMO_TIME 1000
#define GDEMO_INFO_TIME 5000

//==================================================================================
#ifdef CONFIG_EXAMPLE_USE_WIFI

static const char tag[] = "[TFT Demo]";

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = 0x00000001;

//------------------------------------------------------------
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

//-------------------------------
static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(tag, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

//-------------------------------
static void initialize_sntp(void)
{
    ESP_LOGI(tag, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

//--------------------------
static int obtain_time(void)
{
	int res = 1;
    initialise_wifi();
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

    initialize_sntp();

    // wait for time to be set
    int retry = 0;
    const int retry_count = 20;

    time(&time_now);
	tm_info = localtime(&time_now);

    while(tm_info->tm_year < (2016 - 1900) && ++retry < retry_count) {
        //ESP_LOGI(tag, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		sprintf(tmp_buff, "Wait %0d/%d", retry, retry_count);
    	TFT_print(tmp_buff, CENTER, LASTY);
		vTaskDelay(500 / portTICK_RATE_MS);
        time(&time_now);
    	tm_info = localtime(&time_now);
    }
    if (tm_info->tm_year < (2016 - 1900)) {
    	ESP_LOGI(tag, "System time NOT set.");
    	res = 0;
    }
    else {
    	ESP_LOGI(tag, "System time is set.");
    }

    ESP_ERROR_CHECK( esp_wifi_stop() );
    return res;
}

#endif  //CONFIG_EXAMPLE_USE_WIFI
//==================================================================================


//----------------------
static void _checkTime()
{
	time(&time_now);
	if (time_now > time_last) {
		color_t last_fg, last_bg;
		time_last = time_now;
		tm_info = localtime(&time_now);
		sprintf(tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

		TFT_saveClipWin();
		TFT_resetclipwin();

		Font curr_font = cfont;
		last_bg = _bg;
		last_fg = _fg;
		_fg = TFT_YELLOW;
		_bg = (color_t){ 64, 64, 64 };
		TFT_setFont(DEFAULT_FONT, NULL);

		TFT_fillRect(1, _height-TFT_getfontheight()-8, _width-3, TFT_getfontheight()+6, _bg);
		TFT_print(tmp_buff, CENTER, _height-TFT_getfontheight()-5);

		cfont = curr_font;
		_fg = last_fg;
		_bg = last_bg;

		TFT_restoreClipWin();
	}
}

/*
//----------------------
static int _checkTouch()
{
	int tx, ty;
	if (TFT_read_touch(&tx, &ty, 0)) {
		while (TFT_read_touch(&tx, &ty, 1)) {
			vTaskDelay(20 / portTICK_RATE_MS);
		}
		return 1;
	}
	return 0;
}
*/

//---------------------
static int Wait(int ms)
{
	uint8_t tm = 1;
	if (ms < 0) {
		tm = 0;
		ms *= -1;
	}
	if (ms <= 50) {
		vTaskDelay(ms / portTICK_RATE_MS);
		//if (_checkTouch()) return 0;
	}
	else {
		for (int n=0; n<ms; n += 50) {
			vTaskDelay(50 / portTICK_RATE_MS);
			if (tm) _checkTime();
			//if (_checkTouch()) return 0;
		}
	}
	return 1;
}

//-------------------------------------------------------------------
static unsigned int rand_interval(unsigned int min, unsigned int max)
{
    int r;
    const unsigned int range = 1 + max - min;
    const unsigned int buckets = RAND_MAX / range;
    const unsigned int limit = buckets * range;

    /* Create equal size buckets all in a row, then fire randomly towards
     * the buckets until you land in one of them. All buckets are equally
     * likely. If you land off the end of the line of buckets, try again. */
    do
    {
        r = rand();
    } while (r >= limit);

    return min + (r / buckets);
}

// Generate random color
//-----------------------------
static color_t random_color() {

	color_t color;
	color.r  = (uint8_t)rand_interval(8,252);
	color.g  = (uint8_t)rand_interval(8,252);
	color.b  = (uint8_t)rand_interval(8,252);
	return color;
}

//---------------------
static void _dispTime()
{
	time(&time_now);
	time_last = time_now;
	tm_info = localtime(&time_now);
	sprintf(tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
	TFT_print(tmp_buff, CENTER, _height-TFT_getfontheight()-5);
}

//---------------------------------
static void disp_header(char *info)
{
	TFT_fillScreen(TFT_BLACK);
	TFT_resetclipwin();

	_fg = TFT_YELLOW;
	_bg = (color_t){ 64, 64, 64 };

	TFT_setFont(DEFAULT_FONT, NULL);
	TFT_fillRect(0, 0, _width-1, TFT_getfontheight()+8, _bg);
	TFT_drawRect(0, 0, _width-1, TFT_getfontheight()+8, TFT_CYAN);

	TFT_fillRect(0, _height-TFT_getfontheight()-9, _width-1, TFT_getfontheight()+8, _bg);
	TFT_drawRect(0, _height-TFT_getfontheight()-9, _width-1, TFT_getfontheight()+8, TFT_CYAN);

	TFT_print(info, CENTER, 4);
	_dispTime();

	_bg = TFT_BLACK;
	TFT_setclipwin(0,TFT_getfontheight()+9, _width-1, _height-TFT_getfontheight()-10);
}

//---------------------------------------------
static void update_header(char *hdr, char *ftr)
{
	color_t last_fg, last_bg;

	TFT_saveClipWin();
	TFT_resetclipwin();

	Font curr_font = cfont;
	last_bg = _bg;
	last_fg = _fg;
	_fg = TFT_YELLOW;
	_bg = (color_t){ 64, 64, 64 };
	TFT_setFont(DEFAULT_FONT, NULL);

	if (hdr) {
		TFT_fillRect(1, 1, _width-3, TFT_getfontheight()+6, _bg);
		TFT_print(hdr, CENTER, 4);
	}

	if (ftr) {
		TFT_fillRect(1, _height-TFT_getfontheight()-8, _width-3, TFT_getfontheight()+6, _bg);
		if (strlen(ftr) == 0) _dispTime();
		else TFT_print(ftr, CENTER, _height-TFT_getfontheight()-5);
	}

	cfont = curr_font;
	_fg = last_fg;
	_bg = last_bg;

	TFT_restoreClipWin();
}

//------------------------
static void test_times() {

	if (doprint) {
	    uint32_t tstart, t1, t2;
		disp_header("TIMINGS");
		// ** Show Fill screen and send_line timings
		tstart = clock();
		TFT_fillWindow(TFT_BLACK);
		t1 = clock() - tstart;
		printf("     Clear screen time: %u ms\r\n", t1);
		TFT_setFont(SMALL_FONT, NULL);
		sprintf(tmp_buff, "Clear screen: %u ms", t1);
		TFT_print(tmp_buff, 0, 140);

		color_t *color_line = heap_caps_malloc((_width*3), MALLOC_CAP_DMA);
		color_t *gsline = NULL;
		if (gray_scale) gsline = malloc(_width*3);
		if (color_line) {
			float hue_inc = (float)((10.0 / (float)(_height-1) * 360.0));
			for (int x=0; x<_width; x++) {
				color_line[x] = HSBtoRGB(hue_inc, 1.0, (float)x / (float)_width);
				if (gsline) gsline[x] = color_line[x];
			}
			disp_select();
			tstart = clock();
			for (int n=0; n<1000; n++) {
				if (gsline) memcpy(color_line, gsline, _width*3);
				send_data(0, 40+(n&63), dispWin.x2-dispWin.x1, 40+(n&63), (uint32_t)(dispWin.x2-dispWin.x1+1), color_line);
				wait_trans_finish(1);
			}
			t2 = clock() - tstart;
			disp_deselect();

			printf("Send color buffer time: %u us (%d pixels)\r\n", t2, dispWin.x2-dispWin.x1+1);
			free(color_line);

			sprintf(tmp_buff, "   Send line: %u us", t2);
			TFT_print(tmp_buff, 0, 144+TFT_getfontheight());
		}
		Wait(GDEMO_INFO_TIME);
    }
}

// Image demo
//-------------------------
static void disp_images() {
    uint32_t tstart;

	disp_header("JPEG IMAGES");

	if (spiffs_is_mounted) {
		// ** Show scaled (1/8, 1/4, 1/2 size) JPG images
		TFT_jpg_image(CENTER, CENTER, 3, SPIFFS_BASE_PATH"/images/test1.jpg", NULL, 0);
		Wait(500);

		TFT_jpg_image(CENTER, CENTER, 2, SPIFFS_BASE_PATH"/images/test2.jpg", NULL, 0);
		Wait(500);

		TFT_jpg_image(CENTER, CENTER, 1, SPIFFS_BASE_PATH"/images/test4.jpg", NULL, 0);
		Wait(500);

		// ** Show full size JPG image
		tstart = clock();
		TFT_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/test3.jpg", NULL, 0);
		tstart = clock() - tstart;
		if (doprint) printf("       JPG Decode time: %u ms\r\n", tstart);
		sprintf(tmp_buff, "Decode time: %u ms", tstart);
		update_header(NULL, tmp_buff);
		Wait(-GDEMO_INFO_TIME);

		// ** Show BMP image
		update_header("BMP IMAGE", "");
		for (int scale=5; scale >= 0; scale--) {
			tstart = clock();
			TFT_bmp_image(CENTER, CENTER, scale, SPIFFS_BASE_PATH"/images/tiger.bmp", NULL, 0);
			tstart = clock() - tstart;
			if (doprint) printf("    BMP time, scale: %d: %u ms\r\n", scale, tstart);
			sprintf(tmp_buff, "Decode time: %u ms", tstart);
			update_header(NULL, tmp_buff);
			Wait(-500);
		}
		Wait(-GDEMO_INFO_TIME);
	}
	else if (doprint) printf("  No file system found.\r\n");
}

//---------------------
static void font_demo()
{
	int x, y, n;
	uint32_t end_time;

	disp_header("FONT DEMO");

	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		y = 4;
		for (int f=DEFAULT_FONT; f<FONT_7SEG; f++) {
			_fg = random_color();
			TFT_setFont(f, NULL);
			TFT_print("Welcome to ESP32", 4, y);
			y += TFT_getfontheight() + 4;
			n++;
		}
	}
	sprintf(tmp_buff, "%d STRINGS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	if (spiffs_is_mounted) {
		disp_header("FONT FROM FILE DEMO");

		text_wrap = 1;
		for (int f=0; f<3; f++) {
			TFT_fillWindow(TFT_BLACK);
			TFT_setFont(DEFAULT_FONT, NULL);
			_fg = TFT_YELLOW;
			TFT_print((char *)file_fonts[f], 0, (dispWin.y2-dispWin.y1)-TFT_getfontheight()-4);
			update_header(NULL, "");

			TFT_setFont(USER_FONT, file_fonts[f]);
			if (f == 0) font_line_space = 4;
			end_time = clock() + GDEMO_TIME;
			n = 0;
			while ((clock() < end_time) && (Wait(0))) {
				_fg = random_color();
				TFT_print("Welcome to ESP32\nThis is user font.", 0, 8);
				n++;
			}
			font_line_space = 0;
			sprintf(tmp_buff, "%d STRINGS", n);
			update_header(NULL, tmp_buff);
			Wait(-GDEMO_INFO_TIME);
		}
		text_wrap = 0;
	}

	disp_header("ROTATED FONT DEMO");

	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		for (int f=DEFAULT_FONT; f<FONT_7SEG; f++) {
			_fg = random_color();
			TFT_setFont(f, NULL);
			x = rand_interval(8, dispWin.x2-8);
			y = rand_interval(0, (dispWin.y2-dispWin.y1)-TFT_getfontheight()-2);
			font_rotate = rand_interval(0, 359);

			TFT_print("Welcome to ESP32", x, y);
			n++;
		}
	}
	font_rotate = 0;
	sprintf(tmp_buff, "%d STRINGS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	disp_header("7-SEG FONT DEMO");

	int ms = 0;
	int last_sec = 0;
	uint32_t ctime = clock();
	end_time = clock() + GDEMO_TIME*2;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		y = 12;
		ms = clock() - ctime;
		time(&time_now);
		tm_info = localtime(&time_now);
		if (tm_info->tm_sec != last_sec) {
			last_sec = tm_info->tm_sec;
			ms = 0;
			ctime = clock();
		}

		_fg = TFT_ORANGE;
		sprintf(tmp_buff, "%02d:%02d:%03d", tm_info->tm_min, tm_info->tm_sec, ms);
		TFT_setFont(FONT_7SEG, NULL);
		set_7seg_font_atrib(12, 2, 1, TFT_DARKGREY);
		//TFT_clearStringRect(12, y, tmp_buff);
		TFT_print(tmp_buff, CENTER, y);
		n++;

		_fg = TFT_GREEN;
		y += TFT_getfontheight() + 12;
		set_7seg_font_atrib(14, 3, 1, TFT_DARKGREY);
		sprintf(tmp_buff, "%02d:%02d", tm_info->tm_sec, ms / 10);
		//TFT_clearStringRect(12, y, tmp_buff);
		TFT_print(tmp_buff, CENTER, y);
		n++;

		_fg = random_color();
		y += TFT_getfontheight() + 8;
		set_7seg_font_atrib(6, 1, 1, TFT_DARKGREY);
		getFontCharacters((uint8_t *)tmp_buff);
		//TFT_clearStringRect(12, y, tmp_buff);
		TFT_print(tmp_buff, CENTER, y);
		n++;
	}
	sprintf(tmp_buff, "%d STRINGS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	disp_header("WINDOW DEMO");

	TFT_saveClipWin();
	TFT_resetclipwin();
	TFT_drawRect(38, 48, (_width*3/4) - 36, (_height*3/4) - 46, TFT_WHITE);
	TFT_setclipwin(40, 50, _width*3/4, _height*3/4);

	TFT_setFont(UBUNTU16_FONT, NULL);
	text_wrap = 1;
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		_fg = random_color();
		TFT_print("This text is printed inside the window.\nLong line can be wrapped to the next line.\nWelcome to ESP32", 0, 0);
		n++;
	}
	text_wrap = 0;
	sprintf(tmp_buff, "%d STRINGS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	TFT_restoreClipWin();
}

//---------------------
static void rect_demo()
{
	int x, y, w, h, n;

	disp_header("RECTANGLE DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(4, dispWin.x2-4);
		y = rand_interval(4, dispWin.y2-2);
		w = rand_interval(2, dispWin.x2-x);
		h = rand_interval(2, dispWin.y2-y);
		TFT_drawRect(x,y,w,h,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d RECTANGLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("FILLED RECTANGLE", "");
	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(4, dispWin.x2-4);
		y = rand_interval(4, dispWin.y2-2);
		w = rand_interval(2, dispWin.x2-x);
		h = rand_interval(2, dispWin.y2-y);
		TFT_fillRect(x,y,w,h,random_color());
		TFT_drawRect(x,y,w,h,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d RECTANGLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//----------------------
static void pixel_demo()
{
	int x, y, n;

	disp_header("DRAW PIXEL DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(0, dispWin.x2);
		y = rand_interval(0, dispWin.y2);
		TFT_drawPixel(x,y,random_color(),1);
		n++;
	}
	sprintf(tmp_buff, "%d PIXELS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//---------------------
static void line_demo()
{
	int x1, y1, x2, y2, n;

	disp_header("LINE DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x1 = rand_interval(0, dispWin.x2);
		y1 = rand_interval(0, dispWin.y2);
		x2 = rand_interval(0, dispWin.x2);
		y2 = rand_interval(0, dispWin.y2);
		TFT_drawLine(x1,y1,x2,y2,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d LINES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//----------------------
static void aline_demo()
{
	int x, y, len, angle, n;

	disp_header("LINE BY ANGLE DEMO");

	x = (dispWin.x2 - dispWin.x1) / 2;
	y = (dispWin.y2 - dispWin.y1) / 2;
	if (x < y) len = x - 8;
	else len = y -8;

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		for (angle=0; angle < 360; angle++) {
			TFT_drawLineByAngle(x,y, 0, len, angle, random_color());
			n++;
		}
	}

	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	while ((clock() < end_time) && (Wait(0))) {
		for (angle=0; angle < 360; angle++) {
			TFT_drawLineByAngle(x, y, len/4, len/4,angle, random_color());
			n++;
		}
		for (angle=0; angle < 360; angle++) {
			TFT_drawLineByAngle(x, y, len*3/4, len/4,angle, random_color());
			n++;
		}
	}
	sprintf(tmp_buff, "%d LINES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//--------------------
static void arc_demo()
{
	uint16_t x, y, r, th, n, i;
	float start, end;
	color_t color, fillcolor;

	disp_header("ARC DEMO");

	x = (dispWin.x2 - dispWin.x1) / 2;
	y = (dispWin.y2 - dispWin.y1) / 2;

	th = 6;
	uint32_t end_time = clock() + GDEMO_TIME;
	i = 0;
	while ((clock() < end_time) && (Wait(0))) {
		if (x < y) r = x - 4;
		else r = y - 4;
		start = 0;
		end = 20;
		n = 1;
		while (r > 10) {
			color = random_color();
			TFT_drawArc(x, y, r, th, start, end, color, color);
			r -= (th+2);
			n++;
			start += 30;
			end = start + (n*20);
			i++;
		}
	}
	sprintf(tmp_buff, "%d ARCS", i);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("OUTLINED ARC", "");
	TFT_fillWindow(TFT_BLACK);
	th = 8;
	end_time = clock() + GDEMO_TIME;
	i = 0;
	while ((clock() < end_time) && (Wait(0))) {
		if (x < y) r = x - 4;
		else r = y - 4;
		start = 0;
		end = 350;
		n = 1;
		while (r > 10) {
			color = random_color();
			fillcolor = random_color();
			TFT_drawArc(x, y, r, th, start, end, color, fillcolor);
			r -= (th+2);
			n++;
			start += 20;
			end -= n*10;
			i++;
		}
	}
	sprintf(tmp_buff, "%d ARCS", i);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//-----------------------
static void circle_demo()
{
	int x, y, r, n;

	disp_header("CIRCLE DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(8, dispWin.x2-8);
		y = rand_interval(8, dispWin.y2-8);
		if (x < y) r = rand_interval(2, x/2);
		else r = rand_interval(2, y/2);
		TFT_drawCircle(x,y,r,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d CIRCLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("FILLED CIRCLE", "");
	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(8, dispWin.x2-8);
		y = rand_interval(8, dispWin.y2-8);
		if (x < y) r = rand_interval(2, x/2);
		else r = rand_interval(2, y/2);
		TFT_fillCircle(x,y,r,random_color());
		TFT_drawCircle(x,y,r,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d CIRCLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//------------------------
static void ellipse_demo()
{
	int x, y, rx, ry, n;

	disp_header("ELLIPSE DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(8, dispWin.x2-8);
		y = rand_interval(8, dispWin.y2-8);
		if (x < y) rx = rand_interval(2, x/4);
		else rx = rand_interval(2, y/4);
		if (x < y) ry = rand_interval(2, x/4);
		else ry = rand_interval(2, y/4);
		TFT_drawEllipse(x,y,rx,ry,random_color(),15);
		n++;
	}
	sprintf(tmp_buff, "%d ELLIPSES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("FILLED ELLIPSE", "");
	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(8, dispWin.x2-8);
		y = rand_interval(8, dispWin.y2-8);
		if (x < y) rx = rand_interval(2, x/4);
		else rx = rand_interval(2, y/4);
		if (x < y) ry = rand_interval(2, x/4);
		else ry = rand_interval(2, y/4);
		TFT_fillEllipse(x,y,rx,ry,random_color(),15);
		TFT_drawEllipse(x,y,rx,ry,random_color(),15);
		n++;
	}
	sprintf(tmp_buff, "%d ELLIPSES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("ELLIPSE SEGMENTS", "");
	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	n = 0;
	int k = 1;
	while ((clock() < end_time) && (Wait(0))) {
		x = rand_interval(8, dispWin.x2-8);
		y = rand_interval(8, dispWin.y2-8);
		if (x < y) rx = rand_interval(2, x/4);
		else rx = rand_interval(2, y/4);
		if (x < y) ry = rand_interval(2, x/4);
		else ry = rand_interval(2, y/4);
		TFT_fillEllipse(x,y,rx,ry,random_color(), (1<<k));
		TFT_drawEllipse(x,y,rx,ry,random_color(), (1<<k));
		k = (k+1) & 3;
		n++;
	}
	sprintf(tmp_buff, "%d SEGMENTS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//-------------------------
static void triangle_demo()
{
	int x1, y1, x2, y2, x3, y3, n;

	disp_header("TRIANGLE DEMO");

	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x1 = rand_interval(4, dispWin.x2-4);
		y1 = rand_interval(4, dispWin.y2-2);
		x2 = rand_interval(4, dispWin.x2-4);
		y2 = rand_interval(4, dispWin.y2-2);
		x3 = rand_interval(4, dispWin.x2-4);
		y3 = rand_interval(4, dispWin.y2-2);
		TFT_drawTriangle(x1,y1,x2,y2,x3,y3,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d TRIANGLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("FILLED TRIANGLE", "");
	TFT_fillWindow(TFT_BLACK);
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		x1 = rand_interval(4, dispWin.x2-4);
		y1 = rand_interval(4, dispWin.y2-2);
		x2 = rand_interval(4, dispWin.x2-4);
		y2 = rand_interval(4, dispWin.y2-2);
		x3 = rand_interval(4, dispWin.x2-4);
		y3 = rand_interval(4, dispWin.y2-2);
		TFT_fillTriangle(x1,y1,x2,y2,x3,y3,random_color());
		TFT_drawTriangle(x1,y1,x2,y2,x3,y3,random_color());
		n++;
	}
	sprintf(tmp_buff, "%d TRIANGLES", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//---------------------
static void poly_demo()
{
	uint16_t x, y, r, rot, oldrot;
	int i, n;
	uint8_t sides[6] = {3, 4, 5, 6, 8, 10};
	color_t color[6] = {TFT_WHITE, TFT_CYAN, TFT_RED,       TFT_BLUE,     TFT_YELLOW,     TFT_ORANGE};
	color_t fill[6]  = {TFT_BLUE,  TFT_NAVY,   TFT_DARKGREEN, TFT_DARKGREY, TFT_LIGHTGREY, TFT_OLIVE};

	disp_header("POLYGON DEMO");

	x = (dispWin.x2 - dispWin.x1) / 2;
	y = (dispWin.y2 - dispWin.y1) / 2;

	rot = 0;
	oldrot = 0;
	uint32_t end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		if (x < y) r = x - 4;
		else r = y - 4;
		for (i=5; i>=0; i--) {
			TFT_drawPolygon(x, y, sides[i], r, TFT_BLACK, TFT_BLACK, oldrot, 1);
			TFT_drawPolygon(x, y, sides[i], r, color[i], color[i], rot, 1);
			r -= 16;
			n += 2;
		}
		Wait(100);
		oldrot = rot;
		rot = (rot + 15) % 360;
	}
	sprintf(tmp_buff, "%d POLYGONS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);

	update_header("FILLED POLYGON", "");
	rot = 0;
	end_time = clock() + GDEMO_TIME;
	n = 0;
	while ((clock() < end_time) && (Wait(0))) {
		if (x < y) r = x - 4;
		else r = y - 4;
		TFT_fillWindow(TFT_BLACK);
		for (i=5; i>=0; i--) {
			TFT_drawPolygon(x, y, sides[i], r, color[i], fill[i], rot, 2);
			r -= 16;
			n += 2;
		}
		Wait(500);
		rot = (rot + 15) % 360;
	}
	sprintf(tmp_buff, "%d POLYGONS", n);
	update_header(NULL, tmp_buff);
	Wait(-GDEMO_INFO_TIME);
}

//----------------------
static void touch_demo()
{
#if USE_TOUCH
	int tx, ty, ltx, lty, doexit = 0;

	disp_header("TOUCH DEMO");
	TFT_setFont(DEFAULT_FONT, NULL);
	_fg = TFT_YELLOW;
	TFT_print("Touch to draw", CENTER, 40);
	TFT_print("Touch footer to clear", CENTER, 60);

	ltx = -9999;
	lty = -999;
	while (1) {
		if (TFT_read_touch(&tx, &ty, 0)) {
			// Touched
			if (((tx >= dispWin.x1) && (tx <= dispWin.x2)) &&
				((ty >= dispWin.y1) && (ty <= dispWin.y2))) {
				if ((doexit > 2) || ((abs(tx-ltx) < 5) && (abs(ty-lty) < 5))) {
					if (((abs(tx-ltx) > 0) || (abs(ty-lty) > 0))) {
						TFT_fillCircle(tx-dispWin.x1, ty-dispWin.y1, 4,random_color());
						sprintf(tmp_buff, "%d,%d", tx, ty);
						update_header(NULL, tmp_buff);
					}
					ltx = tx;
					lty = ty;
				}
				doexit = 0;
			}
			else if (ty > (dispWin.y2+5)) TFT_fillWindow(TFT_BLACK);
			else {
				doexit++;
				if (doexit == 2) update_header(NULL, "---");
				if (doexit > 50) return;
				vTaskDelay(100 / portTICK_RATE_MS);
			}
		}
		else {
			doexit++;
			if (doexit == 2) update_header(NULL, "---");
			if (doexit > 50) return;
			vTaskDelay(100 / portTICK_RATE_MS);
		}
	}
#endif
}


//===============
void tft_demo() {

	font_rotate = 0;
	text_wrap = 0;
	font_transparent = 0;
	font_forceFixed = 0;
	TFT_resetclipwin();

	image_debug = 0;

	uint8_t disp_rot = PORTRAIT;
	_demo_pass = 0;
	gray_scale = 0;
	doprint = 1;

	TFT_setRotation(disp_rot);
	disp_header("ESP32 TFT DEMO");
	TFT_setFont(COMIC24_FONT, NULL);
	int tempy = TFT_getfontheight() + 4;
	_fg = TFT_ORANGE;
	TFT_print("ESP32", CENTER, (dispWin.y2-dispWin.y1)/2 - tempy);
	TFT_setFont(UBUNTU16_FONT, NULL);
	_fg = TFT_CYAN;
	TFT_print("TFT Demo", CENTER, LASTY+tempy);
	tempy = TFT_getfontheight() + 4;
	TFT_setFont(DEFAULT_FONT, NULL);
	_fg = TFT_GREEN;
	sprintf(tmp_buff, "Read speed: %5.2f MHz", (float)max_rdclock/1000000.0);
	TFT_print(tmp_buff, CENTER, LASTY+tempy);

	Wait(4000);

	while (1) {
		if (run_gs_demo) {
			if (_demo_pass == 8) doprint = 0;
			// Change gray scale mode on every 2nd pass
			gray_scale = _demo_pass & 1;
			// change display rotation
			if ((_demo_pass % 2) == 0) {
				_bg = TFT_BLACK;
				TFT_setRotation(disp_rot);
				disp_rot++;
				disp_rot &= 3;
			}
		}
		else {
			if (_demo_pass == 4) doprint = 0;
			// change display rotation
			_bg = TFT_BLACK;
			TFT_setRotation(disp_rot);
			disp_rot++;
			disp_rot &= 3;
		}

		if (doprint) {
			if (disp_rot == 1) sprintf(tmp_buff, "PORTRAIT");
			if (disp_rot == 2) sprintf(tmp_buff, "LANDSCAPE");
			if (disp_rot == 3) sprintf(tmp_buff, "PORTRAIT FLIP");
			if (disp_rot == 0) sprintf(tmp_buff, "LANDSCAPE FLIP");
			printf("\r\n==========================================\r\nDisplay: %s: %s %d,%d %s\r\n\r\n",
					((tft_disp_type == DISP_TYPE_ILI9341) ? "ILI9341" : "ILI9488"), tmp_buff, _width, _height, ((gray_scale) ? "Gray" : "Color"));
		}

		disp_header("Welcome to ESP32");

		test_times();
		font_demo();
		line_demo();
		aline_demo();
		rect_demo();
		circle_demo();
		ellipse_demo();
		arc_demo();
		triangle_demo();
		poly_demo();
		pixel_demo();
		disp_images();
		touch_demo();

		_demo_pass++;
	}
}


//=============
void app_main()
{

    // ========  PREPARE DISPLAY INITIALIZATION  =========

    esp_err_t ret;

    // === SET GLOBAL VARIABLES ==========================

    // ===================================================
    // ==== Set display type                         =====
    tft_disp_type = DEFAULT_DISP_TYPE;
	//tft_disp_type = DISP_TYPE_ILI9341;
	//tft_disp_type = DISP_TYPE_ILI9488;
    // ===================================================

	// ===================================================
	// === Set display resolution if NOT using default ===
	// === DEFAULT_TFT_DISPLAY_WIDTH &                 ===
    // === DEFAULT_TFT_DISPLAY_HEIGHT                  ===
	_width = DEFAULT_TFT_DISPLAY_WIDTH;  // smaller dimension
	_height = DEFAULT_TFT_DISPLAY_HEIGHT; // larger dimension
	// ===================================================

	// ===================================================
	// ==== Set maximum spi clock for display read    ====
	//      operations, function 'find_rd_speed()'    ====
	//      can be used after display initialization  ====
	max_rdclock = 8000000;
	// ===================================================


    // ====  CONFIGURE SPI DEVICES(s)  ====================================================================================

    gpio_set_direction(PIN_NUM_MISO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);

    spi_lobo_device_handle_t spi;
	
    spi_lobo_bus_config_t buscfg={
        .miso_io_num=PIN_NUM_MISO,				// set SPI MISO pin
        .mosi_io_num=PIN_NUM_MOSI,				// set SPI MOSI pin
        .sclk_io_num=PIN_NUM_CLK,				// set SPI CLK pin
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
		.max_transfer_sz = 6*1024,
    };
    spi_lobo_device_interface_config_t devcfg={
        .clock_speed_hz=8000000,                // Initial clock out at 8 MHz
        .mode=0,                                // SPI mode 0
        .spics_io_num=-1,                       // we will use external CS pin
		.spics_ext_io_num=PIN_NUM_CS,           // external CS pin
		.flags=SPI_DEVICE_HALFDUPLEX,           // ALWAYS SET  to HALF DUPLEX MODE!! for display spi
    };

#if USE_TOUCH
    spi_lobo_device_handle_t tsspi = NULL;

    spi_lobo_device_interface_config_t tsdevcfg={
        .clock_speed_hz=2500000,                //Clock out at 2.5 MHz
        .mode=0,                                //SPI mode 2
        .spics_io_num=PIN_NUM_TCS,              //Touch CS pin
		.spics_ext_io_num=-1,                   //Not using the external CS
		.command_bits=8,                        //1 byte command
    };
#endif
    // ====================================================================================================================



	vTaskDelay(500 / portTICK_RATE_MS);
	printf("\r\n==============================\r\n");
    printf("TFT display DEMO, LoBo 09/2017\r\n");
	printf("==============================\r\n\r\n");

	// ==================================================================
	// ==== Initialize the SPI bus and attach the LCD to the SPI bus ====

	ret=spi_lobo_bus_add_device(SPI_BUS, &buscfg, &devcfg, &spi);
    assert(ret==ESP_OK);
	printf("SPI: display device added to spi bus (%d)\r\n", SPI_BUS);
	disp_spi = spi;

	// ==== Test select/deselect ====
	ret = spi_lobo_device_select(spi, 1);
    assert(ret==ESP_OK);
	ret = spi_lobo_device_deselect(spi);
    assert(ret==ESP_OK);

	printf("SPI: attached display device, speed=%u\r\n", spi_lobo_get_speed(spi));
	printf("SPI: bus uses native pins: %s\r\n", spi_lobo_uses_native_pins(spi) ? "true" : "false");

#if USE_TOUCH
	// =====================================================
    // ==== Attach the touch screen to the same SPI bus ====

	ret=spi_lobo_bus_add_device(SPI_BUS, &buscfg, &tsdevcfg, &tsspi);
    assert(ret==ESP_OK);
	printf("SPI: touch screen device added to spi bus (%d)\r\n", SPI_BUS);
	ts_spi = tsspi;

	// ==== Test select/deselect ====
	ret = spi_lobo_device_select(tsspi, 1);
    assert(ret==ESP_OK);
	ret = spi_lobo_device_deselect(tsspi);
    assert(ret==ESP_OK);

	printf("SPI: attached TS device, speed=%u\r\n", spi_lobo_get_speed(tsspi));
#endif

	// ================================
	// ==== Initialize the Display ====

	printf("SPI: display init...\r\n");
	TFT_display_init();
    printf("OK\r\n");
	
	// ==== Set SPI clock used for display operations ====
	spi_lobo_set_speed(spi, DEFAULT_SPI_CLOCK);
	printf("SPI: Changed speed to %u\r\n", spi_lobo_get_speed(spi));

	printf("\r\n---------------------\r\n");
	printf("Graphics demo started\r\n");
	printf("---------------------\r\n");

	font_rotate = 0;
	text_wrap = 0;
	font_transparent = 0;
	font_forceFixed = 0;
	gray_scale = 0;
    TFT_setGammaCurve(DEFAULT_GAMMA_CURVE);
	TFT_setRotation(PORTRAIT);
	TFT_setFont(DEFAULT_FONT, NULL);
	TFT_resetclipwin();

#ifdef CONFIG_EXAMPLE_USE_WIFI

	// ===== Set time zone ======
	setenv("TZ", "CET-1CEST", 0);
	tzset();
	// ==========================

	disp_header("GET NTP TIME");

    time(&time_now);
	tm_info = localtime(&time_now);

	// Is time set? If not, tm_year will be (1970 - 1900).
    if (tm_info->tm_year < (2016 - 1900)) {
        ESP_LOGI(tag, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        _fg = TFT_CYAN;
    	TFT_print("Time is not set yet", CENTER, CENTER);
    	TFT_print("Connecting to WiFi", CENTER, LASTY+TFT_getfontheight()+2);
    	TFT_print("Getting time over NTP", CENTER, LASTY+TFT_getfontheight()+2);
    	_fg = TFT_YELLOW;
    	TFT_print("Wait", CENTER, LASTY+TFT_getfontheight()+2);
        if (obtain_time()) {
        	_fg = TFT_GREEN;
        	TFT_print("System time is set.", CENTER, LASTY);
        }
        else {
        	_fg = TFT_RED;
        	TFT_print("ERROR.", CENTER, LASTY);
        }
        time(&time_now);
    	update_header(NULL, "");
    	Wait(-2000);
    }
#endif

	disp_header("File system INIT");
    _fg = TFT_CYAN;
	TFT_print("Initializing SPIFFS...", CENTER, CENTER);
    // ==== Initialize the file system ====
    printf("\r\n\n");
	vfs_spiffs_register();
    if (!spiffs_is_mounted) {
    	_fg = TFT_RED;
    	TFT_print("SPIFFS not mounted !", CENTER, LASTY+TFT_getfontheight()+2);
    }
    else {
    	_fg = TFT_GREEN;
    	TFT_print("SPIFFS Mounted.", CENTER, LASTY+TFT_getfontheight()+2);
    }
	Wait(-2000);

	// ---- Detect maximum read speed ----
	max_rdclock = find_rd_speed();

	//=========
    // Run demo
    //=========
	tft_demo();
}
