/* adapter_pref.h - /data/hearbridge/adapter: which Bluetooth controller to use.
 *
 * Words: auto (default), internal, usb, or an explicit ugenX.Y node.
 * The control page writes this; hci_usb_open() reads it on open/reopen. */
#ifndef HEARBRIDGE_ADAPTER_PREF_H
#define HEARBRIDGE_ADAPTER_PREF_H

#define HB_ADAPTER_PATH "/data/hearbridge/adapter"

enum {
    HB_SEL_AUTO = 0,
    HB_SEL_INTERNAL,
    HB_SEL_USB,
    HB_SEL_NODE
};

/* Read the preference file. Missing/blank = auto. On SEL_NODE, node is
 * filled with "/dev/ugenX.Y". */
int hb_adapter_selection(char *node, int cap);

/* Canonical word now in force ("auto", "internal", "usb", or "ugenX.Y"). */
void hb_adapter_pref_word(char *out, int cap);

/* Validate and write the preference. word is auto|internal|usb|ugenX.Y
 * (with or without /dev/). Returns 1 on success. */
int hb_adapter_pref_set(const char *word);

/* Host tests only: redirect the pref file. Empty/NULL clears. */
void hb_adapter_path_override(const char *path);

#endif
