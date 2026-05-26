#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the OmniRemote MSC virtual drive (logs sizing only — the actual
 * TinyUSB callbacks register themselves via tud_msc_*_cb). */
void msc_disk_init(void);

#ifdef __cplusplus
}
#endif
