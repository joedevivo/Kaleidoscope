#include "HIDD.h"
#include "HID-Settings.h"

HIDD::HIDD(
  uint8_t _protocol,
  uint8_t _idle,
  uint8_t _itfProtocol,
  uint8_t _inReportLen,
  uint8_t _interval,
  const void *_reportDesc,
  uint16_t _descriptorSize)
  : PluggableUSBModule(1, 1, epType),
    protocol(_protocol),
    idle(_idle),
    itfProtocol(_itfProtocol),
    inReportLen(_inReportLen),
    interval(_interval),
    reportDesc(_reportDesc),
    descriptorSize(_descriptorSize) {

#ifdef ARCH_HAS_CONFIGURABLE_EP_SIZES
  epType[0] = EP_TYPE_INTERRUPT_IN(inReportLen);
#else
  epType[0] = EP_TYPE_INTERRUPT_IN;
#endif
}

/* PluggableUSB implementation: interface, etc. descriptors for config */
int HIDD::getInterface(uint8_t *interfaceCount) {
  uint8_t itfSubClass;
  if (itfProtocol != HID_PROTOCOL_NONE) {
    itfSubClass = HID_SUBCLASS_BOOT;
  } else {
    itfSubClass = HID_SUBCLASS_NONE;
  }

  uint8_t epSize;
#ifdef ARCH_HAS_CONFIGURABLE_EP_SIZES
  epSize = inReportLen;
#else
  epSize = USB_EP_SIZE;
#endif

  HIDDescriptor descSet = {
    D_INTERFACE(pluggedInterface, 1, USB_DEVICE_CLASS_HUMAN_INTERFACE, itfSubClass, itfProtocol),
    D_HIDREPORT(descriptorSize),
    D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT, epSize, interval),
  };
  ++*interfaceCount;
  return USB_SendControl(0, &descSet, sizeof(descSet));
}

/* PluggableUSB implementation: class descriptors */
int HIDD::getDescriptor(USBSetup &setup) {
  // Check if this is a HID Class Descriptor request
  if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) {
    return 0;
  }
  if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) {
    return 0;
  }

  // In a HID Class Descriptor wIndex cointains the interface number
  if (setup.wIndex != pluggedInterface) {
    return 0;
  }

  return USB_SendControl(TRANSFER_PGM, reportDesc, descriptorSize);
}

/* PluggableUSB implementation: control requests */
bool HIDD::setup(USBSetup &setup) {
  if (pluggedInterface != setup.wIndex) {
    return false;
  }

  uint8_t request     = setup.bRequest;
  uint8_t requestType = setup.bmRequestType;

  if (requestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
    if (request == HID_GET_REPORT) {
      // TODO(anyone): HID_GetReport();
      return true;
    }
    if (request == HID_GET_PROTOCOL) {
      if (itfProtocol == HID_PROTOCOL_NONE) {
        return false;
      }
      // AVR optimization; possibly not needed elsewhere
#if defined(__AVR__)
      UEDATX = protocol;
#elif defined(ARDUINO_ARCH_SAM)
      USBDevice.armSend(0, &protocol, 1);
#else
      USB_SendControl(TRANSFER_RELEASE, &protocol, sizeof(protocol));
#endif
      return true;
    }
    if (request == HID_GET_IDLE) {
      // AVR optimization; possibly not needed elsewhere
#if defined(__AVR__)
      UEDATX = idle;
#elif defined(ARDUINO_ARCH_SAM)
      USBDevice.armSend(0, &idle, 1);
#else
      USB_SendControl(TRANSFER_RELEASE, &idle, sizeof(idle));
#endif
      return true;
    }
  }

  if (requestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
    if (request == HID_SET_PROTOCOL) {
      if (itfProtocol == HID_PROTOCOL_NONE) {
        return false;
      }
      protocol = setup.wValueL;
      return true;
    }
    if (request == HID_SET_IDLE) {
      idle = setup.wValueL;
      return true;
    }
    if (request == HID_SET_REPORT) {
      // Check if data has the correct length afterwards
      int length = setup.wLength;

      if (setup.wValueH == HID_REPORT_TYPE_OUTPUT) {
        if (length <= sizeof(outReport)) {
          USB_RecvControl(outReport, length);
          setReportCB(outReport, length);
          return true;
        }
      }
    }
  }

  return false;
}

int HIDD::SendReportNoID(const void *data, int len) {
  return USB_Send(pluggedEndpoint | TRANSFER_RELEASE, data, len);
}

int HIDD::SendReport(uint8_t id, const void *data, int len) {
  if (id == 0) {
    return SendReportNoID(data, len);
  }
  /* On SAMD, we need to send the whole report in one batch; sending the id, and
   * the report itself separately does not work, the report never arrives. Due
   * to this, we merge the two into a single buffer, and send that.
   *
   * While the same would work for other architectures, AVR included, doing so
   * costs RAM, which is something scarce on AVR. So on that platform, we opt to
   * send the id and the report separately instead. */
#ifdef ARDUINO_ARCH_SAMD
  uint8_t p[64];
  p[0] = id;
  memcpy(&p[1], data, len);
  return USB_Send(pluggedEndpoint, p, len + 1);
#else
  auto ret = USB_Send(pluggedEndpoint, &id, 1);
  if (ret < 0) return ret;
  auto ret2 = USB_Send(pluggedEndpoint | TRANSFER_RELEASE, data, len);
  if (ret2 < 0) return ret2;
  return ret + ret2;
#endif
}