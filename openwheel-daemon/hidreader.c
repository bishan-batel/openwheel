#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <sys/syslog.h>

#include "openwheel.h"
#include "helpers.h"

int main() {
  openlog("openwheel", LOG_PID | LOG_PERROR, LOG_DAEMON);
  int fd = open(HIDRAW_DEVICE, O_RDONLY);
  if (fd < 0) {
    syslog(LOG_ERR, "Failed to open HID device %s", HIDRAW_DEVICE);
    return 1;
  }

  syslog(LOG_DEBUG, "Opened HID Device %s", HIDRAW_DEVICE);

  // Set up D-Bus connection
  DBusConnection* connection = NULL;
  DBusError err;

  dbus_error_init(&err);
  connection = dbus_bus_get(DBUS_BUS_SESSION, &err);

  if (dbus_error_is_set(&err)) {
    syslog(LOG_ERR, "D-Bus connection error (%s)", err.message);
    dbus_error_free(&err);
  }

  if (connection == NULL) {
    return 1;
  }

  // Request the name 'org.asus.dial'
  int ret = dbus_bus_request_name(
    connection,
    "org.asus.dial",
    DBUS_NAME_FLAG_REPLACE_EXISTING,
    &err
  );
  if (dbus_error_is_set(&err)) {
    syslog(
      LOG_ERR,
      "Failed to request D-Bus name 'org.asus.dial' (%s)",
      err.message
    );
    dbus_error_free(&err);
  }
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    syslog(LOG_ERR, "Failed to request D-Bus name 'org.asus.dial'");
    return 1;
  }

  WheelPacket pkt;
  int was_down = 0;

  // send_notification("OpenWheel", "OpenWheel openwheel-daemon started");

  while (1) {
    const size_t bytes_length = read(fd, &pkt, sizeof(pkt));
    if (bytes_length < 0) {
      syslog(LOG_ERR, "Failed to read from HID device");
      break;
    }

    if (bytes_length != 4) {
      syslog(LOG_ERR, "Malformed packet received");
      continue;
    }

    syslog(
      LOG_DEBUG,
      "report_id: %u\nbutton: %u\nrotation_hb: %u\nrotation_lb: %u",
      pkt.report_id,
      pkt.button,
      pkt.rotation_hb,
      pkt.rotation_lb
    );

    if (pkt.rotation_hb == ROTATE_PLUS) {
      syslog(LOG_DEBUG, "Rotate+");
      send_dbus_signal(
        connection,
        DIAL_ROTATE_SIGNAL,
        1
      ); // Send rotation event
      syslog(LOG_DEBUG, "sent rotate plus");
    } else if (pkt.rotation_hb == ROTATE_MINUS) {
      syslog(LOG_DEBUG, "Rotate-");
      send_dbus_signal(connection, DIAL_ROTATE_SIGNAL, -1);
      syslog(LOG_DEBUG, "sent rotate minus");
    } else if (pkt.button == BUTTON_DOWN) {
      syslog(LOG_DEBUG, "Button Down");
      send_dbus_signal(connection, DIAL_PRESS_SIGNAL, 1);
      was_down = 1;
      syslog(LOG_DEBUG, "sent button down");
    } else if (pkt.button == BUTTON_UP && was_down) {
      syslog(LOG_DEBUG, "Button up");
      send_dbus_signal(connection, DIAL_PRESS_SIGNAL, 0);
      was_down = 0;
      syslog(LOG_DEBUG, "sent button up");
    }
  }

  close(fd);
  return 0;
}
