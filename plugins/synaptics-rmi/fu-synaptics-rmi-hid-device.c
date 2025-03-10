/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Synaptics Incorporated.
 * Copyright 2012 Andrew Duggan
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "fu-synaptics-rmi-hid-device.h"
#include "fu-synaptics-rmi-v5-device.h"
#include "fu-synaptics-rmi-v7-device.h"

struct _FuSynapticsRmiHidDevice {
	FuSynapticsRmiDevice parent_instance;
};

G_DEFINE_TYPE(FuSynapticsRmiHidDevice, fu_synaptics_rmi_hid_device, FU_TYPE_SYNAPTICS_RMI_DEVICE)

#define RMI_WRITE_REPORT_ID	   0x9 /* output report */
#define RMI_READ_ADDR_REPORT_ID	   0xa /* output report */
#define RMI_READ_DATA_REPORT_ID	   0xb /* input report */
#define RMI_ATTN_REPORT_ID	   0xc /* input report */
#define RMI_SET_RMI_MODE_REPORT_ID 0xf /* feature report */

#define RMI_DEVICE_DEFAULT_TIMEOUT 2000

#define HID_RMI4_REPORT_ID		0
#define HID_RMI4_READ_INPUT_COUNT	1
#define HID_RMI4_READ_INPUT_DATA	2
#define HID_RMI4_READ_OUTPUT_ADDR	2
#define HID_RMI4_READ_OUTPUT_COUNT	4
#define HID_RMI4_WRITE_OUTPUT_COUNT	1
#define HID_RMI4_WRITE_OUTPUT_ADDR	2
#define HID_RMI4_WRITE_OUTPUT_DATA	4
#define HID_RMI4_FEATURE_MODE		1
#define HID_RMI4_ATTN_INTERRUPT_SOURCES 1
#define HID_RMI4_ATTN_DATA		2

/*
 * This bit disables whatever sleep mode may be selected by the sleep_mode
 * field and forces the device to run at full power without sleeping.
 */
#define RMI_F01_CRTL0_NOSLEEP_BIT (1 << 2)

/*
 * msleep mode controls power management on the device and affects all
 * functions of the device.
 */
#define RMI_F01_CTRL0_SLEEP_MODE_MASK 0x03

#define RMI_SLEEP_MODE_NORMAL	    0x00
#define RMI_SLEEP_MODE_SENSOR_SLEEP 0x01

#define FU_SYNAPTICS_RMI_HID_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static GByteArray *
fu_synaptics_rmi_hid_device_read(FuSynapticsRmiDevice *rmi_device,
				 guint16 addr,
				 gsize req_sz,
				 GError **error)
{
	FuSynapticsRmiHidDevice *self = FU_SYNAPTICS_RMI_HID_DEVICE(rmi_device);
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) req = g_byte_array_new();

	/* maximum size */
	if (req_sz > 0xffff) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "data to read was too long");
		return NULL;
	}

	/* report then old 1 byte read count */
	fu_byte_array_append_uint8(req, RMI_READ_ADDR_REPORT_ID);
	fu_byte_array_append_uint8(req, 0x0);

	/* address */
	fu_byte_array_append_uint16(req, addr, G_LITTLE_ENDIAN);

	/* read output count */
	fu_byte_array_append_uint16(req, req_sz, G_LITTLE_ENDIAN);

	/* request */
	for (guint j = req->len; j < 21; j++)
		fu_byte_array_append_uint8(req, 0x0);
	fu_dump_full(G_LOG_DOMAIN, "ReportWrite", req->data, req->len, 80, FU_DUMP_FLAGS_NONE);
	if (!fu_io_channel_write_byte_array(io_channel,
					    req,
					    RMI_DEVICE_DEFAULT_TIMEOUT,
					    FU_IO_CHANNEL_FLAG_SINGLE_SHOT |
						FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					    error))
		return NULL;

	/* keep reading responses until we get enough data */
	while (buf->len < req_sz) {
		guint8 input_count_sz = 0;
		g_autoptr(GByteArray) res = NULL;
		res = fu_io_channel_read_byte_array(io_channel,
						    req_sz,
						    RMI_DEVICE_DEFAULT_TIMEOUT,
						    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						    error);
		if (res == NULL)
			return NULL;
		if (res->len == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "response zero sized");
			return NULL;
		}
		fu_dump_full(G_LOG_DOMAIN,
			     "ReportRead",
			     res->data,
			     res->len,
			     80,
			     FU_DUMP_FLAGS_NONE);

		/* ignore non data report events */
		if (res->data[HID_RMI4_REPORT_ID] != RMI_READ_DATA_REPORT_ID) {
			g_debug("ignoring report with ID 0x%02x", res->data[HID_RMI4_REPORT_ID]);
			continue;
		}
		if (res->len < HID_RMI4_READ_INPUT_DATA) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "response too small: 0x%02x",
				    res->len);
			return NULL;
		}
		input_count_sz = res->data[HID_RMI4_READ_INPUT_COUNT];
		if (input_count_sz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "input count zero");
			return NULL;
		}
		if (input_count_sz + (guint)HID_RMI4_READ_INPUT_DATA > res->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "underflow 0x%02x from expected 0x%02x",
				    res->len,
				    (guint)input_count_sz + HID_RMI4_READ_INPUT_DATA);
			return NULL;
		}
		g_byte_array_append(buf, res->data + HID_RMI4_READ_INPUT_DATA, input_count_sz);
	}
	fu_dump_full(G_LOG_DOMAIN, "DeviceRead", buf->data, buf->len, 80, FU_DUMP_FLAGS_NONE);

	return g_steal_pointer(&buf);
}

static GByteArray *
fu_synaptics_rmi_hid_device_read_packet_register(FuSynapticsRmiDevice *rmi_device,
						 guint16 addr,
						 gsize req_sz,
						 GError **error)
{
	return fu_synaptics_rmi_hid_device_read(rmi_device, addr, req_sz, error);
}

static gboolean
fu_synaptics_rmi_hid_device_write(FuSynapticsRmiDevice *rmi_device,
				  guint16 addr,
				  GByteArray *req,
				  FuSynapticsRmiDeviceFlags flags,
				  GError **error)
{
	FuSynapticsRmiHidDevice *self = FU_SYNAPTICS_RMI_HID_DEVICE(rmi_device);
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	guint8 len = 0x0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* check size */
	if (req != NULL) {
		if (req->len > 0xff) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "data to write was too long");
			return FALSE;
		}
		len = req->len;
	}

	/* report */
	fu_byte_array_append_uint8(buf, RMI_WRITE_REPORT_ID);

	/* length */
	fu_byte_array_append_uint8(buf, len);

	/* address */
	fu_byte_array_append_uint16(buf, addr, G_LITTLE_ENDIAN);

	/* optional data */
	if (req != NULL)
		g_byte_array_append(buf, req->data, req->len);

	/* pad out to 21 bytes for some reason */
	for (guint i = buf->len; i < 21; i++)
		fu_byte_array_append_uint8(buf, 0x0);
	fu_dump_full(G_LOG_DOMAIN, "DeviceWrite", buf->data, buf->len, 80, FU_DUMP_FLAGS_NONE);

	return fu_io_channel_write_byte_array(io_channel,
					      buf,
					      RMI_DEVICE_DEFAULT_TIMEOUT,
					      FU_IO_CHANNEL_FLAG_SINGLE_SHOT |
						  FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					      error);
}

static gboolean
fu_synaptics_rmi_hid_device_wait_for_attr(FuSynapticsRmiDevice *rmi_device,
					  guint8 source_mask,
					  guint timeout_ms,
					  GError **error)
{
	FuSynapticsRmiHidDevice *self = FU_SYNAPTICS_RMI_HID_DEVICE(rmi_device);
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	g_autoptr(GTimer) timer = g_timer_new();

	/* wait for event from hardware */
	while (g_timer_elapsed(timer, NULL) * 1000.f < timeout_ms) {
		g_autoptr(GByteArray) res = NULL;
		g_autoptr(GError) error_local = NULL;

		/* read from fd */
		res = fu_io_channel_read_byte_array(io_channel,
						    HID_RMI4_ATTN_INTERRUPT_SOURCES + 1,
						    timeout_ms,
						    FU_IO_CHANNEL_FLAG_NONE,
						    &error_local);
		if (res == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
				break;
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_dump_full(G_LOG_DOMAIN,
			     "ReportRead",
			     res->data,
			     res->len,
			     80,
			     FU_DUMP_FLAGS_NONE);
		if (res->len < HID_RMI4_ATTN_INTERRUPT_SOURCES + 1) {
			g_debug("attr: ignoring small read of %u", res->len);
			continue;
		}
		if (res->data[HID_RMI4_REPORT_ID] != RMI_ATTN_REPORT_ID) {
			g_debug("attr: ignoring invalid report ID 0x%x",
				res->data[HID_RMI4_REPORT_ID]);
			continue;
		}

		/* success */
		if (source_mask & res->data[HID_RMI4_ATTN_INTERRUPT_SOURCES])
			return TRUE;

		/* wrong mask */
		g_debug("source mask did not match: 0x%x",
			res->data[HID_RMI4_ATTN_INTERRUPT_SOURCES]);
	}

	/* urgh */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no attr report, timed out");
	return FALSE;
}

typedef enum {
	HID_RMI4_MODE_MOUSE = 0,
	HID_RMI4_MODE_ATTN_REPORTS = 1,
	HID_RMI4_MODE_NO_PACKED_ATTN_REPORTS = 2,
} FuSynapticsRmiHidMode;

static gboolean
fu_synaptics_rmi_hid_device_set_mode(FuSynapticsRmiHidDevice *self,
				     FuSynapticsRmiHidMode mode,
				     GError **error)
{
	const guint8 data[] = {0x0f, mode};
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	fu_dump_raw(G_LOG_DOMAIN, "SetMode", data, sizeof(data));
	return fu_ioctl_execute(ioctl,
				HIDIOCSFEATURE(sizeof(data)), /* nocheck:blocked */
				(guint8 *)data,
				sizeof(data),
				NULL,
				FU_SYNAPTICS_RMI_HID_DEVICE_IOCTL_TIMEOUT,
				FU_IOCTL_FLAG_NONE,
				error);
}

static gboolean
fu_synaptics_rmi_hid_device_open(FuDevice *device, GError **error)
{
	FuSynapticsRmiHidDevice *self = FU_SYNAPTICS_RMI_HID_DEVICE(device);

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_synaptics_rmi_hid_device_parent_class)->open(device, error))
		return FALSE;

	/* set up touchpad so we can query it */
	if (!fu_synaptics_rmi_hid_device_set_mode(self, HID_RMI4_MODE_ATTN_REPORTS, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_hid_device_close(FuDevice *device, GError **error)
{
	FuSynapticsRmiHidDevice *self = FU_SYNAPTICS_RMI_HID_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* turn it back to mouse mode */
	if (!fu_synaptics_rmi_hid_device_set_mode(self, HID_RMI4_MODE_MOUSE, &error_local)) {
		/* if just detached for replug, swallow error */
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring: %s", error_local->message);
	}

	/* FuUdevDevice->close */
	return FU_DEVICE_CLASS(fu_synaptics_rmi_hid_device_parent_class)->close(device, error);
}

static gboolean
fu_synaptics_rmi_hid_device_rebind_driver(FuSynapticsRmiDevice *self, GError **error)
{
	const gchar *hid_id;
	const gchar *driver;
	const gchar *subsystem;
	g_autofree gchar *fn_rebind = NULL;
	g_autofree gchar *fn_unbind = NULL;
	g_autoptr(FuDevice) parent_hid = NULL;
	g_autoptr(FuUdevDevice) parent_phys = NULL;
	g_auto(GStrv) hid_strs = NULL;

	/* get actual HID node */
	parent_hid = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "hid", error);
	if (parent_hid == NULL)
		return FALSE;

	/* build paths */
	parent_phys = FU_UDEV_DEVICE(
	    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "i2c", NULL));
	if (parent_phys == NULL) {
		parent_phys = FU_UDEV_DEVICE(
		    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "usb", NULL));
	}
	if (parent_phys == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no parent device for %s",
			    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(parent_hid)));
		return FALSE;
	}

	/* find the physical ID to use for the rebind */
	hid_strs = g_strsplit(fu_udev_device_get_sysfs_path(parent_phys), "/", -1);
	hid_id = hid_strs[g_strv_length(hid_strs) - 1];
	if (hid_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no HID_PHYS in %s",
			    fu_udev_device_get_sysfs_path(parent_phys));
		return FALSE;
	}

	g_debug("HID_PHYS: %s", hid_id);

	driver = fu_udev_device_get_driver(parent_phys);
	subsystem = fu_udev_device_get_subsystem(parent_phys);
	fn_rebind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "bind", NULL);
	fn_unbind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "unbind", NULL);

	/* unbind hidraw, then bind it again to get a replug */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!fu_synaptics_rmi_device_writeln(fn_unbind, hid_id, error))
		return FALSE;
	if (!fu_synaptics_rmi_device_writeln(fn_rebind, hid_id, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	FuSynapticsRmiFunction *f34;

	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	if (f34->function_version == 0x0 || f34->function_version == 0x1) {
		if (!fu_synaptics_rmi_v5_device_detach(device, progress, error))
			return FALSE;
	} else if (f34->function_version == 0x2) {
		if (!fu_synaptics_rmi_v7_device_detach(device, progress, error))
			return FALSE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "f34 function version 0x%02x unsupported",
			    f34->function_version);
		return FALSE;
	}
	return fu_synaptics_rmi_hid_device_rebind_driver(self, error);
}

static gboolean
fu_synaptics_rmi_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset device */
	if (!fu_synaptics_rmi_device_reset(self, error))
		return FALSE;

	/* rebind to rescan PDT with new firmware running */
	return fu_synaptics_rmi_hid_device_rebind_driver(self, error);
}

static gboolean
fu_synaptics_rmi_hid_device_set_page(FuSynapticsRmiDevice *self, guint8 page, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	fu_byte_array_append_uint8(req, page);
	if (!fu_synaptics_rmi_device_write(self,
					   RMI_DEVICE_PAGE_SELECT_REGISTER,
					   req,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to set RMA page 0x%x: ", page);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_hid_device_disable_sleep(FuSynapticsRmiDevice *rmi_device, GError **error)
{
	FuSynapticsRmiFunction *f01;
	g_autoptr(GByteArray) f01_control0 = NULL;

	f01 = fu_synaptics_rmi_device_get_function(rmi_device, 0x34, error);
	if (f01 == NULL)
		return FALSE;
	f01_control0 = fu_synaptics_rmi_device_read(rmi_device, f01->control_base, 0x1, error);
	if (f01_control0 == NULL) {
		g_prefix_error(error, "failed to write get f01_control0: ");
		return FALSE;
	}
	f01_control0->data[0] |= RMI_F01_CRTL0_NOSLEEP_BIT;
	f01_control0->data[0] =
	    (f01_control0->data[0] & ~RMI_F01_CTRL0_SLEEP_MODE_MASK) | RMI_SLEEP_MODE_NORMAL;
	if (!fu_synaptics_rmi_device_write(rmi_device,
					   f01->control_base,
					   f01_control0,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write f01_control0: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_hid_device_query_status(FuSynapticsRmiDevice *rmi_device, GError **error)
{
	FuSynapticsRmiFunction *f34;
	f34 = fu_synaptics_rmi_device_get_function(rmi_device, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	if (f34->function_version == 0x0 || f34->function_version == 0x1) {
		return fu_synaptics_rmi_v5_device_query_status(rmi_device, error);
	}
	if (f34->function_version == 0x2) {
		return fu_synaptics_rmi_v7_device_query_status(rmi_device, error);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "f34 function version 0x%02x unsupported",
		    f34->function_version);
	return FALSE;
}

static void
fu_synaptics_rmi_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 88, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 7, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_synaptics_rmi_hid_device_init(FuSynapticsRmiHidDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Touchpad");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_synaptics_rmi_device_set_max_page(FU_SYNAPTICS_RMI_DEVICE(self), 0xff);
}

static void
fu_synaptics_rmi_hid_device_class_init(FuSynapticsRmiHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuSynapticsRmiDeviceClass *rmi_class = FU_SYNAPTICS_RMI_DEVICE_CLASS(klass);
	device_class->attach = fu_synaptics_rmi_hid_device_attach;
	device_class->detach = fu_synaptics_rmi_hid_device_detach;
	device_class->open = fu_synaptics_rmi_hid_device_open;
	device_class->close = fu_synaptics_rmi_hid_device_close;
	device_class->set_progress = fu_synaptics_rmi_hid_device_set_progress;
	rmi_class->write = fu_synaptics_rmi_hid_device_write;
	rmi_class->read = fu_synaptics_rmi_hid_device_read;
	rmi_class->wait_for_attr = fu_synaptics_rmi_hid_device_wait_for_attr;
	rmi_class->set_page = fu_synaptics_rmi_hid_device_set_page;
	rmi_class->query_status = fu_synaptics_rmi_hid_device_query_status;
	rmi_class->read_packet_register = fu_synaptics_rmi_hid_device_read_packet_register;
	rmi_class->disable_sleep = fu_synaptics_rmi_hid_device_disable_sleep;
}
