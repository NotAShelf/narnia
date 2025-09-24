#ifndef CLIPBOARD_H
#define CLIPBOARD_H

int clipboard_init(void);
void clipboard_cleanup(void);
int clipboard_set_text(const char *text);

#endif
