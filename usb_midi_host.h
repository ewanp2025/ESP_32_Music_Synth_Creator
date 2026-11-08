#ifndef USB_MIDI_HOST_H
#define USB_MIDI_HOST_H

#include <Arduino.h>
#include <usb/usb_host.h>
#include <functional>

class UsbAudioPartner {
public:
    static inline UsbAudioPartner* s_instance = nullptr;
    static inline volatile bool s_enableUsbAudio = false;

    virtual ~UsbAudioPartner() = default;
    virtual bool tryClaimAudioInterface(usb_host_client_handle_t clientHdl,
                                        usb_device_handle_t devHdl,
                                        const usb_config_desc_t* configDesc) = 0;
    virtual void onDeviceDisconnected(usb_host_client_handle_t clientHdl,
                                      usb_device_handle_t devHdl) = 0;
};

class UsbMidiHost {
public:
    using NoteCallback      = std::function<void(int note, int vel, bool on)>;
    using CcCallback        = std::function<void(int cc, int val)>;
    using PitchBendCallback = std::function<void(float bendNorm)>; // -1.0f to +1.0f

    // Backward-compatible begin(): works with 1 argument (main.cpp) or all 3 (Synth SDK)
    void begin(NoteCallback cb, CcCallback ccCb = nullptr, PitchBendCallback pbCb = nullptr) {
        m_cb   = cb;
        m_ccCb = ccCb;
        m_pbCb = pbCb;
        xTaskCreatePinnedToCore(usbHostLibTask, "usb_lib", 4096, this, 5, nullptr, 0);
        xTaskCreatePinnedToCore(usbClientTask,  "usb_cli", 4096, this, 4, nullptr, 0);
    }

    void setCcCallback(CcCallback ccCb) { m_ccCb = ccCb; }
    void setPitchBendCallback(PitchBendCallback pbCb) { m_pbCb = pbCb; }

    bool isConnected() const { return m_connected; }

private:
    NoteCallback      m_cb;
    CcCallback        m_ccCb;
    PitchBendCallback m_pbCb;

    usb_host_client_handle_t m_clientHdl = nullptr;
    usb_device_handle_t m_devHdl = nullptr;
    usb_transfer_t* m_inXfer = nullptr;
    int m_claimedIface = -1;
    volatile bool m_connected = false;
    bool m_audioClaimed = false;

    static bool allowAllEnumFilter(const usb_device_desc_t*, uint8_t* bConfigurationValue) {
        *bConfigurationValue = 1;
        return true;
    }

    static void usbHostLibTask(void* arg) {
        usb_host_config_t cfg = {};
        cfg.skip_phy_setup = false;
        cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
#ifdef CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
        cfg.enum_filter_cb = allowAllEnumFilter;
#endif
        usb_host_install(&cfg);

        while (true) {
            uint32_t eventFlags = 0;
            usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
            if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                usb_host_device_free_all();
            }
        }
    }

    static void onMidiTransfer(usb_transfer_t* xfer) {
        auto* self = static_cast<UsbMidiHost*>(xfer->context);
        if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes >= 4) {
            // Standard 4-byte USB-MIDI Event Packets
            for (int i = 0; i + 3 < xfer->actual_num_bytes; i += 4) {
                uint8_t cin    = xfer->data_buffer[i] & 0x0F;
                uint8_t status = xfer->data_buffer[i + 1] & 0xF0;
                uint8_t data1  = xfer->data_buffer[i + 2] & 0x7F;
                uint8_t data2  = xfer->data_buffer[i + 3] & 0x7F;

                if (cin == 0x09 || cin == 0x08) {
                    // Note On (0x90) / Note Off (0x80)
                    if (status == 0x90 && data2 > 0) {
                        if (self->m_cb) self->m_cb(data1, data2, true);
                    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
                        if (self->m_cb) self->m_cb(data1, 0, false);
                    }
                } else if (cin == 0x0B || status == 0xB0) {
                    // Control Change (CC# = data1, Value 0..127 = data2)
                    if (self->m_ccCb) self->m_ccCb(data1, data2);
                } else if (cin == 0x0E || status == 0xE0) {
                    // 14-bit Pitch Bend Wheel (-1.0f to +1.0f)
                    if (self->m_pbCb) {
                        int bend14 = static_cast<int>(data1) | (static_cast<int>(data2) << 7);
                        float norm = static_cast<float>(bend14 - 8192) * (1.0f / 8192.0f);
                        self->m_pbCb(norm);
                    }
                }
            }
        }
        if (self->m_connected && self->m_devHdl) {
            usb_host_transfer_submit(xfer);
        }
    }

    static void clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
        auto* self = static_cast<UsbMidiHost*>(arg);
        if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
            if (usb_host_device_open(self->m_clientHdl, eventMsg->new_dev.address, &self->m_devHdl) != ESP_OK) return;

            const usb_config_desc_t* configDesc = nullptr;
            if (usb_host_get_active_config_descriptor(self->m_devHdl, &configDesc) != ESP_OK) return;

            // Only attempt USB Audio Out if the user has switched BTN_AUDIO to USB Audio mode
            self->m_audioClaimed = false;
            if (UsbAudioPartner::s_enableUsbAudio && UsbAudioPartner::s_instance) {
                self->m_audioClaimed = UsbAudioPartner::s_instance->tryClaimAudioInterface(
                    self->m_clientHdl, self->m_devHdl, configDesc);
                if (self->m_audioClaimed) return;
            }

            int offset = 0;
            int curIface = -1;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(configDesc);
            while (offset < configDesc->wTotalLength) {
                uint8_t len = p[offset];
                uint8_t type = p[offset + 1];
                if (len == 0) break;

                if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                    auto* ifc = reinterpret_cast<const usb_intf_desc_t*>(&p[offset]);
                    curIface = ifc->bInterfaceNumber;
                } else if (type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && curIface >= 0) {
                    auto* ep = reinterpret_cast<const usb_ep_desc_t*>(&p[offset]);
                    bool isIn = (ep->bEndpointAddress & 0x80) != 0;
                    uint8_t xferType = ep->bmAttributes & 0x03;
                    if (isIn && (xferType == USB_BM_ATTRIBUTES_XFER_BULK || xferType == USB_BM_ATTRIBUTES_XFER_INT)) {
                        if (usb_host_interface_claim(self->m_clientHdl, self->m_devHdl, curIface, 0) == ESP_OK) {
                            self->m_claimedIface = curIface;
                            uint16_t mps = ep->wMaxPacketSize & 0x7FF;
                            usb_host_transfer_alloc(mps, 0, &self->m_inXfer);
                            self->m_inXfer->device_handle = self->m_devHdl;
                            self->m_inXfer->bEndpointAddress = ep->bEndpointAddress;
                            self->m_inXfer->callback = onMidiTransfer;
                            self->m_inXfer->context = self;
                            self->m_inXfer->num_bytes = mps;
                            self->m_connected = true;
                            usb_host_transfer_submit(self->m_inXfer);
                            break;
                        }
                    }
                }
                offset += len;
            }
        } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
            self->m_connected = false;
            if (self->m_inXfer) {
                usb_host_transfer_free(self->m_inXfer);
                self->m_inXfer = nullptr;
            }
            if (self->m_devHdl) {
                if (self->m_audioClaimed && UsbAudioPartner::s_instance) {
                    UsbAudioPartner::s_instance->onDeviceDisconnected(self->m_clientHdl, self->m_devHdl);
                    self->m_audioClaimed = false;
                }
                if (self->m_claimedIface >= 0) {
                    usb_host_interface_release(self->m_clientHdl, self->m_devHdl, self->m_claimedIface);
                    self->m_claimedIface = -1;
                }
                usb_host_device_close(self->m_clientHdl, self->m_devHdl);
                self->m_devHdl = nullptr;
            }
        }
    }

    static void usbClientTask(void* arg) {
        auto* self = static_cast<UsbMidiHost*>(arg);
        vTaskDelay(pdMS_TO_TICKS(250));
        usb_host_client_config_t cliCfg = {};
        cliCfg.is_synchronous = false;
        cliCfg.max_num_event_msg = 5;
        cliCfg.async.client_event_callback = clientEventCallback;
        cliCfg.async.callback_arg = self;
        usb_host_client_register(&cliCfg, &self->m_clientHdl);

        while (true) {
            usb_host_client_handle_events(self->m_clientHdl, portMAX_DELAY);
        }
    }
};

#endif // USB_MIDI_HOST_H