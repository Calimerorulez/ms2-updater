#ifndef MS2X_LINUX_TEST_ENTRY_H
#define MS2X_LINUX_TEST_ENTRY_H
/* linux_main.c is linked to the existing wrapped syscall harness, not run live. */
int ms2x_cli_main(int argc, char **argv);
#endif
