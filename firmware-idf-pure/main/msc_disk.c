/* msc_disk.c — OmniRemote read-only USB MSC virtual drive
 *
 * Backs TinyUSB's MSC class with a FAT12 image baked into flash at build time
 * (see tools/build-msc-image.py). The image holds README.html / README.txt /
 * device-info.json — the bare minimum to make the drive self-documenting.
 *
 * Pattern lifted from slidecue/firmware/main/msc_disk.c (SlideCue M2).
 */

#include <string.h>
#include "esp_log.h"
#include "tusb.h"
#include "class/msc/msc.h"
#include "class/msc/msc_device.h"
#include "msc_disk_image.h"

static const char *TAG = "msc";

void msc_disk_init(void) {
    ESP_LOGI(TAG, "MSC disk: %lu blocks x %u B = %u B (%s)",
             (unsigned long)MSC_DISK_BLOCK_COUNT,
             (unsigned)MSC_DISK_BLOCK_SIZE,
             (unsigned)MSC_DISK_IMAGE_SIZE,
             "read-only OmniRemote docs");
}

/* ---------------- TinyUSB MSC callbacks ---------------- */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    const char vid[] = "OmniRem";
    const char pid[] = "OmniRemote Disk";
    const char rev[] = "1.0";
    memcpy(vendor_id,  vid,  strlen(vid));
    memcpy(product_id, pid,  strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                          uint16_t *block_size) {
    (void)lun;
    *block_count = MSC_DISK_BLOCK_COUNT;
    *block_size  = MSC_DISK_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                           bool start, bool load_eject) {
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
    (void)lun;
    if (lba >= MSC_DISK_BLOCK_COUNT) return -1;
    uint32_t addr = lba * MSC_DISK_BLOCK_SIZE + offset;
    if (addr + bufsize > MSC_DISK_IMAGE_SIZE) {
        bufsize = MSC_DISK_IMAGE_SIZE - addr;
    }
    memcpy(buffer, MSC_DISK_IMAGE + addr, bufsize);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
    (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
    return -1;   /* read-only */
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize) {
    (void)lun; (void)buffer;
    int32_t resplen = 0;
    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            resplen = 0;
            break;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            resplen = -1;
            break;
    }
    if (resplen > bufsize) resplen = bufsize;
    return resplen;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return false;
}
