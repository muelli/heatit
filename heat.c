/*
* (c) Tobias Mueller <tobiasmue@gnome.org>
* GPLv3+
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

#define VENDOR_ID      0x32f9
#define PRODUCT_ID     0x0001


const uint8_t BULK_IN_EP = 0x82;
const uint8_t BULK_OUT_EP = 0x02;
const size_t BUFFER_SIZE = 12;


static int hexdmp (uint8_t* buf, size_t size) {
    const size_t hexlen = 2; // hex representation of byte with leading zero
    const size_t outstrlen = size * hexlen;

    char * outstr = malloc(outstrlen + 1);
    if (!outstr) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    // Create a string containing a hex representation of `bytearr`
    char * p = outstr;
    for (size_t i = 0; i < size; i++) {
        p += sprintf(p, "%.2x", buf[i]);
    }

    printf("String variable contains:\n%s\n", outstr);
    free(outstr);

    return 0;
}


static int start_treatment (libusb_device_handle* devh, uint8_t temp, uint8_t time) {
    int rc;
    int bytes_written;

    if (temp > 3) {
        return 1;
    }
    if (time > 2) {
        return 2;
    }

    uint8_t buf[12] = {
        0xff, // Header
        0x08, // Start Treatment
        temp, // Temp
        time, // Time
    };

    fprintf (stderr, "Writing to device\n");
    rc = libusb_bulk_transfer(devh, BULK_OUT_EP, buf, BUFFER_SIZE, &bytes_written, 1000);
    fprintf (stderr, "Written to device: %d %d\n", rc, bytes_written);
    if (rc < 0) {
        printf("Failed to write to device: %d\n", rc);
        libusb_release_interface(devh, 0);
        libusb_close(devh);
        return 1;
    }
    printf("Wrote %d bytes to the device\n", bytes_written);

    return 0;
}

typedef struct {
    uint8_t header;
    uint8_t msgtype;
    uint16_t temp;
    uint8_t internal;
    uint8_t external;
    uint16_t pid;
    uint8_t chksum;
} status;


static int request_status (libusb_device_handle* devh, status* out) {
    int rc;
    int bytes_read, bytes_written;

    char buf[12] = {
          0xff   // HEADER
        , 0x02 // STATUS
        , 0x01 // CHECKSUM (ignored)
    };
    fprintf (stderr, "Writing to device\n");
    rc = libusb_bulk_transfer(devh, BULK_OUT_EP, buf, BUFFER_SIZE, &bytes_written, 1000);
    fprintf (stderr, "Written to device: %d %d\n", rc, bytes_written);
    if (rc < 0) {
        printf("Failed to write to device: %d\n", rc);
        libusb_release_interface(devh, 0);
        libusb_close(devh);
        return 1;
    }
    printf("Wrote %d bytes to the device\n", bytes_written);



    uint8_t in_buffer[BUFFER_SIZE];

    fprintf (stderr, "Reading from device\n");
    rc = libusb_bulk_transfer(devh, BULK_IN_EP, in_buffer, BUFFER_SIZE, &bytes_read, 1500);
    if (rc < 0) {
        printf("Failed to read from device: %d (%s)\n", rc, libusb_error_name(rc));
        libusb_release_interface(devh, 0);
        libusb_close(devh);
        return 2;
    }
    printf("Read %d bytes from the device: %d\n", bytes_read, rc);
    hexdmp (in_buffer, bytes_read);
    out->header = in_buffer[0];
    out->msgtype = in_buffer[1];
    out->temp = ((uint16_t)in_buffer[2] << 8) | in_buffer[3];
    out->internal = in_buffer[4];
    out->external = in_buffer[5];
    out->pid = ((uint16_t)in_buffer[6] << 8) | in_buffer[7];
    out->chksum = in_buffer[8];

    return 0;
}

static libusb_context* acquire_libusb_context () {
    libusb_context *ctx;
    int rc;

    /* Initialize libusb
     */
    rc = libusb_init(&ctx);
    if (rc < 0) {
        fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
        exit(1);
    }

    /* Set debugging output to max level.
     */
    libusb_set_option (ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_OPTION_MAX);

    return ctx;
}

static int get_device (libusb_context* ctx, libusb_device_handle** devh_out) {
    int rc;

    /* Look for a specific device and open it.
     */
    libusb_device_handle* devh = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!devh) {
        fprintf(stderr, "Error finding USB device\n");
        return 1;
    }

    for (int if_num = 0; if_num < 2; if_num++) {
        if (libusb_kernel_driver_active(devh, if_num)) {
            libusb_detach_kernel_driver(devh, if_num);
        }
    }



    {
        int conf;
        rc = libusb_get_configuration(devh, &conf);
        if (rc < 0) {
            printf("Failed to get configuration: %d\n", rc);
            libusb_release_interface(devh, 0);
            libusb_close(devh);
            libusb_exit(ctx);
            return 1;
        }
        fprintf (stderr, "get conf: %d\n", rc);
        if (rc == 0) {
            fprintf (stderr, "Conf: %d\n", conf);

            if (conf != 2) {
                rc = libusb_set_configuration(devh, 2);
                if (rc < 0) {
                    printf("Failed to set configuration: %d\n", rc);
                    libusb_release_interface(devh, 0);
                    libusb_close(devh);
                    libusb_exit(ctx);
                    return 1;
                }
                printf("Successfully selected the second configuration\n");
            }
        }
    }





    for (int if_num = 0; if_num < 2; if_num++) {
        rc = libusb_claim_interface(devh, if_num);
        fprintf (stderr, "Claimed interface %d: %d (%s)\n", if_num, rc, libusb_error_name(rc));
        //break;
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface %d: %s\n",
                    if_num, libusb_error_name(rc));
            // goto out;
        }
    }

    *devh_out = devh;
    return 0;
}

static int print_status (status* s) {
    fprintf (stderr, "Status: \n");
    fprintf (stderr, "Header: %02x\n", s->header);
    fprintf (stderr, "Msg:    %02X\n", s->msgtype);
    fprintf (stderr, "temp:  %u \n", s->temp );
    fprintf (stderr, "int:   %02X\n", s->internal);
    fprintf (stderr, "ext:   %02X\n", s->external);
    fprintf (stderr, "pid:   %u\n", s->pid);
    fprintf (stderr, "check: %02X\n", s->chksum);
}

int main(int argc, char **argv)
{
    int rc;
    int ret;
    libusb_context* ctx = acquire_libusb_context ();
    libusb_device_handle* devh;
    rc = get_device (ctx, &devh);
    if (rc != 0) {
        fprintf (stderr, "Error finding device: %d\n", rc);
        return 1;
    }

    fprintf (stderr, "Args: %d\n", argc);

    if (argc == 1) {
        status sout;
        rc = request_status (devh, &sout);
        print_status (&sout);

        ret = 0;

    } else if (argc == 2) {
        char* time_s = argv[1];
        rc = start_treatment (devh, 1, 1);

        ret = 0;

    } else if (argc == 3) {
        char* time_s = argv[1];
        char* temp_s = argv[2];
        rc = start_treatment (devh, 1, 1);

        ret = 0;

    } else {
        fprintf (stderr, "Expected 0, 1, or 2 arguments. Got %d.\n", argc);

        ret = 1;
    }

    // Release the interface and close the device
    libusb_release_interface(devh, 0);
    libusb_close(devh);
    devh = NULL;
    libusb_exit( NULL );

    return ret;
}
