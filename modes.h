#ifndef _MODES_H
#define _MODES_H

typedef struct {
    void (*init)(void);
    void (*run)(void);
    void (*deinit)(void);
} shairport_mode;

shairport_mode *mode_find(int *argc, char ***argv);

#endif

