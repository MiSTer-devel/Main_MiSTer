#ifndef SCREENSHOT_H
#define SCREENSHOT_H

//void request_screenshot_cmd(char *cmd, int scaled = 0);
void request_screenshot(char *cmd, int scaled = 0);
//void request_screenshot(int scaled);
void screenshot_cb(void);

#endif