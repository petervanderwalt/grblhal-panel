#include "panel_hardware.h"
#include <assert.h>
int main() {
    panel::State s;
    assert(panel::status_led_color(s)==0x080808);
    s.online=s.status=true;strcpy(s.mode,"Idle");
    assert(panel::status_led_color(s)==0x00FF00);
    s.busy=true;assert(panel::status_led_color(s)==0x00FF00);s.busy=false;
    strcpy(s.mode,"Run");assert(panel::status_led_color(s)==0xFF8000);
    strcpy(s.mode,"Hold:0");assert(panel::status_led_color(s)==0xFF8000);
    s.alarm=1;assert(panel::status_led_color(s)==0xFF0000);
    s.alarm=0;s.error=20;assert(panel::status_led_color(s)==0xFF0000);
    s.error=0;s.job_complete=true;assert(panel::status_led_color(s)==0x00FF00);
    s.status=false;assert(panel::status_led_color(s)==0x080808);
    puts("PASS RGB policy: offline, Idle, Run, Hold, alarm, error and completion");
}
