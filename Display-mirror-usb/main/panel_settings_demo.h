#pragma once
// Representative controller reports for UART-disconnected UI development.
static const char *const demo_setting_reports[] = {
    "[SETTINGGROUP:1|0|General]", "[SETTINGGROUP:35|0|Stepper]",
    "[SETTINGGROUP:11|0|Homing]", "[SETTINGGROUP:43|0|Axes]",
    "[SETTINGGROUP:45|43|X axis]", "[SETTINGGROUP:46|43|Y axis]",
    "[SETTINGGROUP:47|43|Z axis]", "[SETTINGGROUP:48|43|A axis]",
    "[SETTINGGROUP:15|0|Networking]", "[SETTINGGROUP:16|15|Wi-Fi]",
    "[SETTING:0|35|Step pulse time|microseconds|6|0.0|2|20|0|0]", "$0=10.0",
    "[SETTING:1|35|Stepper idle delay|milliseconds|5|####0|0|65535|0|0]", "$1=255",
    "[SETTING:10|1|Status report options||1|Position,Buffer,Line,Feed and speed,Pins,WCO,Overrides|0|127|0|0]", "$10=127",
    "[SETTING:13|1|Report inches||0||||0|0]", "$13=0",
    "[SETTING:22|11|Homing options||2|Enable,Single axis,Lock override,Init lock|||0|0]", "$22=3",
    "[SETTING:23|11|Homing direction invert||4|4|0|15|0|0]", "$23=3",
    "[SETTING:24|11|Homing feed rate|mm/min|6|0.0|1|10000|0|0]", "$24=100.0",
    "[SETTING:32|1|Machine mode||3|Normal,Laser,Lathe|||0|0]", "$32=0",
    "[SETTING:39|1|Legacy realtime commands||0||||0|0]", "$39=0",
    "[SETTING:74|16|Wi-Fi network name||7|x(32)|0|32|1|1]", "$74=Workshop",
    "[SETTING:75|16|Wi-Fi password||8|x(64)|0|64|1|1]", "$75=********",
    "[SETTING:302|15|IP address||9||||1|0]", "$302=192.168.0.100",
    "[SETTING:376|43|Rotary axes||1|A-Axis,B-Axis,C-Axis|0|7|0|0]", "$376=0",
    "[SETTINGDESCR:0|Length of each step pulse. Choose a value supported by the motor driver.]",
    "[SETTINGDESCR:22|Enable homing and select optional homing behaviour. Bit zero enables the other options.]",
    "[SETTINGDESCR:23|Choose the axes whose homing direction is inverted.]",
    "[SETTINGDESCR:74|Controller Wi-Fi SSID. This setting belongs to the CNC controller, not the display.]",
    "[SETTINGDESCR:75|Replacement controller Wi-Fi password. The existing password is hidden.]",
    "[SETTINGDESCR:376|Select rotary axes. Rotary positions use degrees. Calibrate steps and rates for the chosen units.]"
};
